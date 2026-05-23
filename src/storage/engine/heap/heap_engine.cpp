// src/storage/engine/heap/heap_engine.cpp

#include "storage/engine/heap/heap_engine.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "storage/engine/heap/page.hpp"

namespace edb {

namespace {

constexpr auto CURSOR_SLOT_BASE = std::uint64_t{65536};
constexpr auto CURSOR_SCAN_BASE = std::uint64_t{281474976710656ULL};
constexpr auto CURSOR_PAGE_MASK = std::uint64_t{0xFFFFFFFFULL};
constexpr auto HEAP_TUPLE_HEADER_SIZE = std::size_t{18};

auto append_u16(std::vector<std::byte>& bytes, u16 value) -> void {
    bytes.push_back(std::byte{static_cast<unsigned char>(value.value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value.value >> 8U) & 0xFFU)});
}

auto append_u64(std::vector<std::byte>& bytes, u64 value) -> void {
    for (std::size_t index = 0; index < std::size_t{8}; ++index) {
        bytes.push_back(
            std::byte{static_cast<unsigned char>((value.value >> (index * 8U)) & 0xFFU)});
    }
}

[[nodiscard]] auto read_u16(std::span<const std::byte> bytes, std::size_t offset) -> u16 {
    const auto lo = static_cast<std::uint16_t>(bytes[offset]);
    const auto hi = static_cast<std::uint16_t>(bytes[offset + 1U]);
    return u16{static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8U))};
}

[[nodiscard]] auto read_u64(std::span<const std::byte> bytes, std::size_t offset) -> u64 {
    auto value = std::uint64_t{0};
    for (std::size_t index = 0; index < std::size_t{8}; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return u64{value};
}

struct StoredHeapTuple {
    TupleHeader            header;
    std::vector<std::byte> payload;
};

[[nodiscard]] auto encode_heap_tuple(TupleHeader header, std::span<const std::byte> payload)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(HEAP_TUPLE_HEADER_SIZE + payload.size());
    append_u64(bytes, header.xmin.value);
    append_u64(bytes, header.xmax.value);
    append_u16(bytes, header.flags);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

[[nodiscard]] auto decode_heap_tuple(std::span<const std::byte> bytes) -> Result<StoredHeapTuple> {
    if (bytes.size() < HEAP_TUPLE_HEADER_SIZE) {
        return std::unexpected(Error::Corruption);
    }
    auto header = TupleHeader{
        .xmin = TxId{read_u64(bytes, std::size_t{0})},
        .xmax = TxId{read_u64(bytes, std::size_t{8})},
        .flags = read_u16(bytes, std::size_t{16}),
    };
    auto payload = bytes.subspan(HEAP_TUPLE_HEADER_SIZE);
    return StoredHeapTuple{.header = header,
                           .payload = std::vector<std::byte>{payload.begin(), payload.end()}};
}

}  // namespace

auto EdbHeapEngine::insert_impl(const Transaction& tx, std::span<const std::byte> tuple)
    -> Result<TupleId> {
    if (tx.id.value == u64{0}) {
        return std::unexpected(Error::InvalidArgument);
    }
    auto stored = encode_heap_tuple(
        TupleHeader{.xmin = tx.id, .xmax = TxId{u64{0}}, .flags = u16{0}}, tuple);
    return insert_encoded_tuple(stored);
}

auto EdbHeapEngine::delete_tuple_impl(const Transaction& tx, TupleId id) -> VoidResult {
    if (tx.id.value == u64{0}) {
        return std::unexpected(Error::InvalidArgument);
    }
    return mark_deleted(id, tx.id, nullptr);
}

auto EdbHeapEngine::update_tuple_impl(const Transaction& tx, TupleId id, std::span<const std::byte> tuple)
    -> Result<TupleId> {
    auto delete_status = delete_tuple(tx, id);
    if (!delete_status) {
        return std::unexpected(delete_status.error());
    }
    return insert(tx, tuple);
}

auto EdbHeapEngine::delete_tuple(const Transaction& tx, TupleId id,
                                 const TransactionStatusReader& statuses) -> VoidResult {
    if (tx.id.value == u64{0}) {
        return std::unexpected(Error::InvalidArgument);
    }
    return mark_deleted(id, tx.id, &statuses);
}

auto EdbHeapEngine::update_tuple(const Transaction& tx, TupleId id,
                                 std::span<const std::byte> tuple,
                                 const TransactionStatusReader& statuses) -> Result<TupleId> {
    auto delete_status = delete_tuple(tx, id, statuses);
    if (!delete_status) {
        return std::unexpected(delete_status.error());
    }
    return insert(tx, tuple);
}

auto EdbHeapEngine::begin_scan_impl(const VisibilityContext& context,
                                    const TransactionStatusReader& statuses) -> Result<ScanHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    const auto scan_id = next_scan_id;
    ++next_scan_id.value;
    if (next_scan_id == u16{0}) {
        next_scan_id = u16{1};
    }
    auto handle = ScanHandle{.value = encode_cursor(scan_id, u64{0}, u16{0})};
    scan_contexts.emplace(scan_id, ScanContext{.context = context, .statuses = &statuses});
    return handle;
}

auto EdbHeapEngine::open_impl(PageStore& store, const EngineConfig& cfg) -> VoidResult {
    page_store = &store;
    config = cfg;
    auto status =
        buffer_pool.open(store, BufferPoolConfig{.capacity_pages = cfg.buffer_pool_pages,
                                                    .eviction = cfg.buffer_eviction});
    if (!status) {
        page_store = nullptr;
        return status;
    }
    opened = b8{true};
    return {};
}

auto EdbHeapEngine::close_impl() -> VoidResult {
    if (!opened.value) {
        return {};
    }
    auto status = buffer_pool.close();
    page_store = nullptr;
    opened = b8{false};
    return status;
}

auto EdbHeapEngine::insert_impl(std::span<const std::byte> tuple) -> Result<TupleId> {
    auto stored = encode_heap_tuple(
        TupleHeader{.xmin = TxId{u64{1}}, .xmax = TxId{u64{0}}, .flags = u16{0}}, tuple);
    return insert_encoded_tuple(stored);
}

auto EdbHeapEngine::insert_encoded_tuple(std::span<const std::byte> tuple) -> Result<TupleId> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto count = page_store->page_count();
    if (!count) {
        return std::unexpected(count.error());
    }

    for (u64 page_id{0}; page_id < *count; ++page_id) {
        auto inserted = insert_into_existing_page(page_id, tuple);
        if (!inserted) {
            return std::unexpected(inserted.error());
        }
        if (inserted->has_value()) {
            return **inserted;
        }
    }
    return insert_into_new_page(tuple);
}

auto EdbHeapEngine::delete_tuple_impl(TupleId id) -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto handle = buffer_pool.fetch(id.page_id);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    auto status = heap::delete_tuple(handle->data(), id.slot_idx);
    if (!status) {
        auto unpinned = unpin_clean(*handle);
        if (!unpinned) {
            return unpinned;
        }
        return status;
    }
    return buffer_pool.unpin(*handle, b8{true});
}

auto EdbHeapEngine::update_tuple_impl(TupleId id, std::span<const std::byte> tuple)
    -> Result<TupleId> {
    auto delete_status = delete_tuple_impl(id);
    if (!delete_status) {
        return std::unexpected(delete_status.error());
    }
    return insert_impl(tuple);
}

auto EdbHeapEngine::begin_scan_impl() -> Result<ScanHandle> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }
    return ScanHandle{.value = encode_cursor(u64{0}, u16{0})};
}

auto EdbHeapEngine::scan_next_impl(ScanHandle& handle) -> Result<std::optional<Tuple>> {
    if (auto status = check_open(); !status) {
        return std::unexpected(status.error());
    }

    auto count = page_store->page_count();
    if (!count) {
        return std::unexpected(count.error());
    }

    for (auto page_id = cursor_page_id(handle); page_id < *count; ++page_id) {
        auto frame = buffer_pool.fetch(page_id);
        if (!frame) {
            return std::unexpected(frame.error());
        }

        auto slots = heap::slot_count(frame->data());
        if (!slots) {
            if (auto status = unpin_clean(*frame); !status) {
                return std::unexpected(status.error());
            }
            return std::unexpected(slots.error());
        }

        auto slot_idx = page_id == cursor_page_id(handle) ? cursor_slot_idx(handle) : u16{0};
        for (; slot_idx < *slots; ++slot_idx) {
            auto tuple = scan_slot(*frame, handle, page_id, slot_idx);
            if (!tuple) {
                return std::unexpected(tuple.error());
            }
            if (tuple->has_value()) {
                return std::move(*tuple);
            }
        }

        auto status = unpin_clean(*frame);
        if (!status) {
            return std::unexpected(status.error());
        }
        handle.value = encode_cursor(cursor_scan_id(handle), page_id + u64{1}, u16{0});
    }
    return std::optional<Tuple>{};
}

auto EdbHeapEngine::end_scan_impl(ScanHandle& handle) -> VoidResult {
    scan_contexts.erase(cursor_scan_id(handle));
    handle.value = encode_cursor(u64{0}, u16{0});
    return {};
}

auto EdbHeapEngine::page_size_impl() const -> usize {
    return config.page_size;
}

auto EdbHeapEngine::check_open() const -> VoidResult {
    if (!opened.value || page_store == nullptr) {
        return std::unexpected(Error::IoError);
    }
    return {};
}

auto EdbHeapEngine::insert_into_existing_page(u64 page_id, std::span<const std::byte> tuple)
    -> Result<std::optional<TupleId>> {
    auto frame = buffer_pool.fetch(page_id);
    if (!frame) {
        return std::unexpected(frame.error());
    }

    auto can_fit = heap::can_insert(frame->data(), usize{tuple.size()});
    if (!can_fit) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(can_fit.error());
    }
    if (!can_fit->value) {
        auto status = buffer_pool.unpin(*frame, b8{false});
        if (!status) {
            return std::unexpected(status.error());
        }
        return std::optional<TupleId>{};
    }

    auto slot = heap::insert_tuple(frame->data(), tuple);
    if (!slot) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(slot.error());
    }
    auto status = buffer_pool.unpin(*frame, b8{true});
    if (!status) {
        return std::unexpected(status.error());
    }
    return TupleId{.page_id = page_id, .slot_idx = *slot};
}

auto EdbHeapEngine::insert_into_new_page(std::span<const std::byte> tuple)
    -> Result<TupleId> {
    auto allocated = page_store->allocate_page();
    if (!allocated) {
        return std::unexpected(allocated.error());
    }

    auto frame = buffer_pool.fetch_new(*allocated);
    if (!frame) {
        return std::unexpected(frame.error());
    }
    if (auto status = heap::initialize_page(frame->data(), *allocated); !status) {
        if (auto unpinned = unpin_clean(*frame); !unpinned) {
            return std::unexpected(unpinned.error());
        }
        return std::unexpected(status.error());
    }
    auto slot = heap::insert_tuple(frame->data(), tuple);
    if (!slot) {
        if (auto status = unpin_clean(*frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(slot.error());
    }
    auto status = buffer_pool.unpin(*frame, b8{true});
    if (!status) {
        return std::unexpected(status.error());
    }
    return TupleId{.page_id = *allocated, .slot_idx = *slot};
}

auto EdbHeapEngine::scan_slot(FrameHandle& frame, ScanHandle& handle, u64 page_id, u16 slot_idx)
    -> Result<std::optional<Tuple>> {
    auto live = heap::is_live_slot(frame.data(), slot_idx);
    if (!live) {
        if (auto status = unpin_clean(frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(live.error());
    }
    if (!live->value) {
        return std::optional<Tuple>{};
    }

    auto tuple = heap::read_tuple(frame.data(), slot_idx);
    if (!tuple) {
        if (auto status = unpin_clean(frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(tuple.error());
    }
    auto stored = decode_heap_tuple(*tuple);
    if (!stored) {
        if (auto status = unpin_clean(frame); !status) {
            return std::unexpected(status.error());
        }
        return std::unexpected(stored.error());
    }

    if (const auto context = scan_contexts.find(cursor_scan_id(handle));
        context != scan_contexts.end()) {
        auto visible = Visibility::is_visible(stored->header, context->second.context,
                                              *context->second.statuses);
        if (!visible) {
            if (auto status = unpin_clean(frame); !status) {
                return std::unexpected(status.error());
            }
            return std::unexpected(visible.error());
        }
        if (!visible->value) {
            return std::optional<Tuple>{};
        }
    }

    handle.value = encode_cursor(cursor_scan_id(handle), page_id,
                                 u16{static_cast<std::uint16_t>(slot_idx.value + 1U)});
    const auto tuple_id = TupleId{.page_id = page_id, .slot_idx = slot_idx};
    auto data = std::move(stored->payload);
    auto status = unpin_clean(frame);
    if (!status) {
        return std::unexpected(status.error());
    }
    return Tuple{.id = tuple_id, .data = std::move(data)};
}

auto EdbHeapEngine::mark_deleted(TupleId id, TxId xmax,
                                 const TransactionStatusReader* statuses) -> VoidResult {
    if (auto status = check_open(); !status) {
        return status;
    }

    auto handle = buffer_pool.fetch(id.page_id);
    if (!handle) {
        return std::unexpected(handle.error());
    }

    auto tuple = heap::read_tuple(handle->data(), id.slot_idx);
    if (!tuple) {
        if (auto status = unpin_clean(*handle); !status) {
            return status;
        }
        return std::unexpected(tuple.error());
    }
    auto stored = decode_heap_tuple(*tuple);
    if (!stored) {
        if (auto status = unpin_clean(*handle); !status) {
            return status;
        }
        return std::unexpected(stored.error());
    }
    if (auto status = check_delete_conflict(stored->header.xmax, statuses); !status) {
        if (auto unpinned = unpin_clean(*handle); !unpinned) {
            return unpinned;
        }
        return status;
    }

    auto overwritten = heap::overwrite_tuple(
        handle->data(), id.slot_idx,
        encode_heap_tuple(TupleHeader{.xmin = stored->header.xmin, .xmax = xmax,
                                      .flags = stored->header.flags},
                          stored->payload));
    if (!overwritten) {
        if (auto status = unpin_clean(*handle); !status) {
            return status;
        }
        return overwritten;
    }
    return buffer_pool.unpin(*handle, b8{true});
}

auto EdbHeapEngine::check_delete_conflict(TxId existing_xmax,
                                          const TransactionStatusReader* statuses) -> VoidResult {
    if (existing_xmax.value == u64{0}) {
        return {};
    }
    if (statuses == nullptr) {
        return std::unexpected(Error::TransactionAborted);
    }

    auto status = statuses->status(existing_xmax);
    if (!status) {
        return std::unexpected(status.error());
    }
    if (*status == TxStatus::Aborted) {
        return {};
    }
    return std::unexpected(Error::TransactionAborted);
}

auto EdbHeapEngine::unpin_clean(FrameHandle& handle) -> VoidResult {
    return buffer_pool.unpin(handle, b8{false});
}

auto EdbHeapEngine::encode_cursor(u64 page_id, u16 slot_idx) -> u64 {
    return encode_cursor(u16{0}, page_id, slot_idx);
}

auto EdbHeapEngine::encode_cursor(u16 scan_id, u64 page_id, u16 slot_idx) -> u64 {
    return u64{(static_cast<std::uint64_t>(scan_id.value) * CURSOR_SCAN_BASE) +
               ((page_id.value & CURSOR_PAGE_MASK) * CURSOR_SLOT_BASE) + slot_idx.value};
}

auto EdbHeapEngine::cursor_scan_id(ScanHandle handle) -> u16 {
    return u16{static_cast<std::uint16_t>(handle.value.value / CURSOR_SCAN_BASE)};
}

auto EdbHeapEngine::cursor_page_id(ScanHandle handle) -> u64 {
    return u64{(handle.value.value / CURSOR_SLOT_BASE) & CURSOR_PAGE_MASK};
}

auto EdbHeapEngine::cursor_slot_idx(ScanHandle handle) -> u16 {
    return u16{static_cast<std::uint16_t>(handle.value.value % CURSOR_SLOT_BASE)};
}

}  // namespace edb
