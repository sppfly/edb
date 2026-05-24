// src/wal/recovery.cpp

#include "wal/recovery.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>

#include "storage/engine/heap/page.hpp"

namespace edb {

namespace {

constexpr auto HEAP_INSERT_PREFIX_SIZE = std::size_t{14};
constexpr auto CHECKPOINT_PAYLOAD_SIZE = std::size_t{8};

auto append_u16(std::vector<std::byte>& bytes, u16 value) -> void {
    bytes.push_back(std::byte{static_cast<unsigned char>(value.value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value.value >> 8U) & 0xFFU)});
}

auto append_u32(std::vector<std::byte>& bytes, std::uint32_t value) -> void {
    for (std::size_t index = 0; index < std::size_t{4}; ++index) {
        const auto shift = static_cast<unsigned>(index * 8U);
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

auto append_u64(std::vector<std::byte>& bytes, u64 value) -> void {
    for (std::size_t index = 0; index < std::size_t{8}; ++index) {
        const auto shift = static_cast<unsigned>(index * 8U);
        bytes.push_back(
            std::byte{static_cast<unsigned char>((value.value >> shift) & std::uint64_t{0xFFU})});
    }
}

[[nodiscard]] auto read_u16(std::span<const std::byte> bytes, std::size_t offset) -> u16 {
    const auto lo = static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset]));
    const auto hi = static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1U]));
    return u16{static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8U))};
}

[[nodiscard]] auto read_u32(std::span<const std::byte> bytes, std::size_t offset) -> std::uint32_t {
    auto value = std::uint32_t{0};
    for (std::size_t index = 0; index < std::size_t{4}; ++index) {
        const auto shift = static_cast<unsigned>(index * 8U);
        value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << shift;
    }
    return value;
}

[[nodiscard]] auto read_u64(std::span<const std::byte> bytes, std::size_t offset) -> u64 {
    auto value = std::uint64_t{0};
    for (std::size_t index = 0; index < std::size_t{8}; ++index) {
        const auto shift = static_cast<unsigned>(index * 8U);
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + index]))
                 << shift;
    }
    return u64{value};
}

struct HeapInsertPayload {
    TupleId id;
    std::vector<std::byte> tuple;
};

[[nodiscard]] auto decode_heap_insert_payload(std::span<const std::byte> payload)
    -> Result<HeapInsertPayload> {
    if (payload.size() < HEAP_INSERT_PREFIX_SIZE) {
        return std::unexpected(Error::Corruption);
    }
    const auto tuple_length = read_u32(payload, std::size_t{10});
    if ((payload.size() - HEAP_INSERT_PREFIX_SIZE) != tuple_length) {
        return std::unexpected(Error::Corruption);
    }
    auto tuple = payload.subspan(HEAP_INSERT_PREFIX_SIZE);
    return HeapInsertPayload{
        .id = TupleId{.page_id = read_u64(payload, std::size_t{0}),
                      .slot_idx = read_u16(payload, std::size_t{8})},
        .tuple = std::vector<std::byte>{tuple.begin(), tuple.end()},
    };
}

[[nodiscard]] auto ensure_page_exists(PageStore& store, u64 page_id) -> VoidResult {
    auto count = store.page_count();
    if (!count) {
        return std::unexpected(count.error());
    }
    while (*count <= page_id) {
        auto allocated = store.allocate_page();
        if (!allocated) {
            return std::unexpected(allocated.error());
        }
        count = store.page_count();
        if (!count) {
            return std::unexpected(count.error());
        }
    }
    return {};
}

[[nodiscard]] auto redo_heap_insert(PageStore& store, usize page_size, u64 lsn,
                                    const HeapInsertPayload& insert) -> VoidResult {
    if (auto status = ensure_page_exists(store, insert.id.page_id); !status) {
        return status;
    }

    std::vector<std::byte> page(page_size.value);
    if (auto status = store.read_page(insert.id.page_id, page); !status) {
        return status;
    }
    auto current_lsn = heap::page_lsn(page);
    if (!current_lsn) {
        return std::unexpected(current_lsn.error());
    }
    if (*current_lsn >= lsn) {
        return {};
    }

    auto slots = heap::slot_count(page);
    if (!slots) {
        return std::unexpected(slots.error());
    }
    if (*slots == u16{0} && *current_lsn == u64{0}) {
        if (auto status = heap::initialize_page(page, insert.id.page_id); !status) {
            return status;
        }
    }
    auto slot = heap::insert_tuple(page, insert.tuple);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    if (*slot != insert.id.slot_idx) {
        return std::unexpected(Error::Corruption);
    }
    if (auto status = heap::set_page_lsn(page, lsn); !status) {
        return status;
    }
    return store.write_page(insert.id.page_id, page);
}

[[nodiscard]] auto rebuild_statuses(std::span<const WalRecord> records)
    -> std::unique_ptr<RecoveredTransactionStatus> {
    auto statuses = std::make_unique<RecoveredTransactionStatus>();
    for (const auto& record : records) {
        if (record.tx_id.value != u64{0} && record.resource_manager == WalResourceManager::Heap) {
            auto existing = statuses->status(record.tx_id);
            if (!existing && existing.error() == Error::NotFound) {
                statuses->set_status(record.tx_id, TxStatus::InProgress);
            }
        }
        if (record.resource_manager == WalResourceManager::Transaction &&
            record.record_type == WalRecordType::Commit) {
            statuses->set_status(record.tx_id, TxStatus::Committed);
        }
        if (record.resource_manager == WalResourceManager::Transaction &&
            record.record_type == WalRecordType::Abort) {
            statuses->set_status(record.tx_id, TxStatus::Aborted);
        }
    }
    return statuses;
}

[[nodiscard]] auto apply_heap_redo(PageStore& store, usize page_size,
                                   std::span<const WalRecord> records,
                                   RecoveredTransactionStatus& statuses) -> VoidResult {
    for (const auto& record : records) {
        if (record.resource_manager != WalResourceManager::Heap ||
            record.record_type != WalRecordType::HeapInsert) {
            continue;
        }
        auto insert = decode_heap_insert_payload(record.payload);
        if (!insert) {
            return std::unexpected(insert.error());
        }
        if (auto status = redo_heap_insert(store, page_size, record.lsn, *insert); !status) {
            return std::unexpected(status.error());
        }
        auto status = statuses.status(record.tx_id);
        if (status && *status == TxStatus::InProgress) {
            statuses.set_status(record.tx_id, TxStatus::Aborted);
        }
    }
    return {};
}

[[nodiscard]] auto decode_checkpoint_payload(std::span<const std::byte> payload) -> Result<u64> {
    if (payload.size() != CHECKPOINT_PAYLOAD_SIZE) {
        return std::unexpected(Error::Corruption);
    }
    return read_u64(payload, std::size_t{0});
}

[[nodiscard]] auto checkpoint_start_index(std::span<const WalRecord> records) -> Result<usize> {
    auto redo_lsn = u64{0};
    for (const auto& record : records) {
        if (record.resource_manager == WalResourceManager::Transaction &&
            record.record_type == WalRecordType::Checkpoint) {
            auto checkpoint_redo = decode_checkpoint_payload(record.payload);
            if (!checkpoint_redo) {
                return std::unexpected(checkpoint_redo.error());
            }
            redo_lsn = *checkpoint_redo;
        }
    }

    for (usize index{0}; index < usize{records.size()}; ++index) {
        if (records[index.value].lsn >= redo_lsn) {
            return index;
        }
    }
    return usize{records.size()};
}

}  // namespace

auto RecoveredTransactionStatus::status(TxId id) const -> Result<TxStatus> {
    const auto found = statuses.find(id);
    if (found == statuses.end()) {
        return std::unexpected(Error::NotFound);
    }
    return found->second;
}

auto RecoveredTransactionStatus::set_status(TxId id, TxStatus status) -> void {
    statuses[id] = status;
}

auto make_heap_insert_payload(TupleId id, std::span<const std::byte> tuple)
    -> Result<std::vector<std::byte>> {
    if (tuple.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error::Overflow);
    }

    std::vector<std::byte> payload;
    payload.reserve(HEAP_INSERT_PREFIX_SIZE + tuple.size());
    append_u64(payload, id.page_id);
    append_u16(payload, id.slot_idx);
    append_u32(payload, static_cast<std::uint32_t>(tuple.size()));
    payload.insert(payload.end(), tuple.begin(), tuple.end());
    return payload;
}

auto make_checkpoint_payload(u64 redo_lsn) -> std::vector<std::byte> {
    std::vector<std::byte> payload;
    payload.reserve(CHECKPOINT_PAYLOAD_SIZE);
    append_u64(payload, redo_lsn);
    return payload;
}

auto recover_heap(PageStore& store, usize page_size, const WalManager& wal)
    -> Result<HeapRecoveryResult> {
    auto records = wal.read_all();
    if (!records) {
        return std::unexpected(records.error());
    }

    auto start_index = checkpoint_start_index(*records);
    if (!start_index) {
        return std::unexpected(start_index.error());
    }
    auto recovery_records = std::span<const WalRecord>{*records}.subspan(start_index->value);

    auto statuses = rebuild_statuses(recovery_records);
    if (auto status = apply_heap_redo(store, page_size, recovery_records, *statuses); !status) {
        return std::unexpected(status.error());
    }

    return HeapRecoveryResult{.statuses = std::move(statuses)};
}

}  // namespace edb