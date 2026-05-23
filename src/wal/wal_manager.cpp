// src/wal/wal_manager.cpp

#include "wal/wal_manager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <mutex>

namespace edb {

namespace {

constexpr auto WAL_MAGIC = std::uint32_t{0x314C5745U};
constexpr auto WAL_HEADER_SIZE = std::size_t{40};
constexpr auto WAL_CRC_OFFSET = std::size_t{36};

auto append_u16(std::vector<std::byte>& bytes, std::uint16_t value) -> void {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
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

[[nodiscard]] auto read_u16(std::span<const std::byte> bytes, std::size_t offset)
    -> std::uint16_t {
    const auto lo = static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset]));
    const auto hi = static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1U]));
    return static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8U));
}

[[nodiscard]] auto read_u32(std::span<const std::byte> bytes, std::size_t offset)
    -> std::uint32_t {
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

[[nodiscard]] auto crc32(std::span<const std::byte> bytes) -> std::uint32_t {
    auto crc = std::uint32_t{0xFFFFFFFFU};
    for (const auto byte : bytes) {
        crc ^= std::to_integer<std::uint32_t>(byte);
        for (std::uint8_t bit = 0; bit < 8U; ++bit) {
            const auto mask = std::uint32_t{0U} - (crc & 1U);
            crc = (crc >> 1U) ^ (std::uint32_t{0xEDB88320U} & mask);
        }
    }
    return ~crc;
}

auto write_crc(std::vector<std::byte>& bytes, std::uint32_t crc) -> void {
    for (std::size_t index = 0; index < std::size_t{4}; ++index) {
        const auto shift = static_cast<unsigned>(index * 8U);
        bytes[WAL_CRC_OFFSET + index] =
            std::byte{static_cast<unsigned char>((crc >> shift) & 0xFFU)};
    }
}

[[nodiscard]] auto encode_record(u64 lsn, const WalAppendRecord& record)
    -> Result<std::vector<std::byte>> {
    if (record.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(Error::Overflow);
    }

    std::vector<std::byte> bytes;
    bytes.reserve(WAL_HEADER_SIZE + record.payload.size());
    append_u32(bytes, WAL_MAGIC);
    append_u64(bytes, lsn);
    append_u64(bytes, record.prev_lsn);
    append_u64(bytes, record.tx_id.value);
    append_u16(bytes, static_cast<std::uint16_t>(record.resource_manager));
    append_u16(bytes, static_cast<std::uint16_t>(record.record_type));
    append_u32(bytes, static_cast<std::uint32_t>(record.payload.size()));
    append_u32(bytes, std::uint32_t{0});
    bytes.insert(bytes.end(), record.payload.begin(), record.payload.end());

    write_crc(bytes, crc32(bytes));
    return bytes;
}

[[nodiscard]] auto read_exact(StorageIOOps& io, u64 offset, std::span<std::byte> bytes)
    -> VoidResult {
    auto read = io.read(offset, bytes);
    if (!read) {
        return std::unexpected(read.error());
    }
    if (read->value != bytes.size()) {
        return std::unexpected(Error::Corruption);
    }
    return {};
}

[[nodiscard]] auto decode_record(std::span<const std::byte> header,
                                 std::vector<std::byte> payload) -> Result<WalRecord> {
    if (read_u32(header, std::size_t{0}) != WAL_MAGIC) {
        return std::unexpected(Error::Corruption);
    }

    std::vector<std::byte> crc_bytes;
    crc_bytes.reserve(WAL_HEADER_SIZE + payload.size());
    crc_bytes.insert(crc_bytes.end(), header.begin(), header.end());
    crc_bytes.insert(crc_bytes.end(), payload.begin(), payload.end());
    const auto expected_crc = read_u32(crc_bytes, WAL_CRC_OFFSET);
    write_crc(crc_bytes, std::uint32_t{0});
    if (crc32(crc_bytes) != expected_crc) {
        return std::unexpected(Error::Corruption);
    }

    return WalRecord{.lsn = read_u64(header, std::size_t{4}),
                     .prev_lsn = read_u64(header, std::size_t{12}),
                     .tx_id = TxId{read_u64(header, std::size_t{20})},
                     .resource_manager = static_cast<WalResourceManager>(
                         read_u16(header, std::size_t{28})),
                     .record_type = static_cast<WalRecordType>(
                         read_u16(header, std::size_t{30})),
                     .payload = std::move(payload)};
}

}  // namespace

WalManager::WalManager(StorageIOOps& io) noexcept : io{&io} {}

auto WalManager::open() -> VoidResult {
    if (io == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }
    auto size = io->file_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    std::scoped_lock lock{mutex};
    next_offset = *size;
    last_lsn = u64{0};
    durable_lsn = u64{0};
    return {};
}

auto WalManager::append(const WalAppendRecord& record) -> Result<u64> {
    if (io == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    std::scoped_lock lock{mutex};
    const auto lsn = u64{next_offset.value + 1U};
    auto bytes = encode_record(lsn, record);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    auto written = io->write(next_offset, *bytes);
    if (!written) {
        return std::unexpected(written.error());
    }
    if (written->value != bytes->size()) {
        return std::unexpected(Error::IoError);
    }
    next_offset = u64{next_offset.value + bytes->size()};
    last_lsn = lsn;
    return lsn;
}

auto WalManager::read_all() const -> Result<std::vector<WalRecord>> {
    if (io == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }

    std::scoped_lock lock{mutex};
    auto size = io->file_size();
    if (!size) {
        return std::unexpected(size.error());
    }

    std::vector<WalRecord> records;
    for (u64 offset{0}; offset < *size;) {
        if ((size->value - offset.value) < WAL_HEADER_SIZE) {
            return std::unexpected(Error::Corruption);
        }

        std::array<std::byte, WAL_HEADER_SIZE> header{};
        if (auto status = read_exact(*io, offset, header); !status) {
            return std::unexpected(status.error());
        }

        const auto payload_length = read_u32(header, std::size_t{32});
        if ((size->value - offset.value - WAL_HEADER_SIZE) < payload_length) {
            return std::unexpected(Error::Corruption);
        }
        std::vector<std::byte> payload(payload_length);
        if (!payload.empty()) {
            if (auto status = read_exact(*io, u64{offset.value + WAL_HEADER_SIZE}, payload);
                !status) {
                return std::unexpected(status.error());
            }
        }

        auto record = decode_record(header, std::move(payload));
        if (!record) {
            return std::unexpected(record.error());
        }
        records.push_back(std::move(*record));
        offset = u64{offset.value + WAL_HEADER_SIZE + payload_length};
    }
    return records;
}

auto WalManager::flush(u64 lsn) -> VoidResult {
    if (io == nullptr) {
        return std::unexpected(Error::InvalidArgument);
    }
    auto status = io->datasync();
    if (!status) {
        return status;
    }
    std::scoped_lock lock{mutex};
    if (durable_lsn < lsn) {
        durable_lsn = lsn;
    }
    return {};
}

auto WalManager::flush_through(u64 lsn) -> VoidResult {
    return flush(lsn);
}

auto WalManager::appended_lsn() const -> u64 {
    std::scoped_lock lock{mutex};
    return last_lsn;
}

auto WalManager::flushed_lsn() const -> u64 {
    std::scoped_lock lock{mutex};
    return durable_lsn;
}

}  // namespace edb