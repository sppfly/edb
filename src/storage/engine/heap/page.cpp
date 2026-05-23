// src/storage/engine/heap/page.cpp

#include "storage/engine/heap/page.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>

namespace edb::heap {

namespace {

constexpr auto CHECKSUM_OFFSET = usize{0};
constexpr auto FLAGS_OFFSET = usize{4};
constexpr auto SLOT_COUNT_OFFSET = usize{6};
constexpr auto FREE_START_OFFSET = usize{8};
constexpr auto FREE_END_OFFSET = usize{10};
constexpr auto PAGE_ID_OFFSET = usize{12};
constexpr auto LSN_OFFSET = usize{20};
constexpr auto MAX_U16 = std::uint64_t{0xFFFFU};

[[nodiscard]] auto fits_u16(usize value) -> b8 {
    return b8{value.value <= MAX_U16};
}

[[nodiscard]] auto read_u16(std::span<const std::byte> page, usize offset) -> u16 {
    const auto lo = static_cast<std::uint16_t>(page[offset.value]);
    const auto hi = static_cast<std::uint16_t>(page[offset.value + 1U]);
    return u16{static_cast<std::uint16_t>(lo | static_cast<std::uint16_t>(hi << 8U))};
}

auto write_u16(std::span<std::byte> page, usize offset, u16 value) -> void {
    page[offset.value] = std::byte{static_cast<unsigned char>(value.value & 0xFFU)};
    page[offset.value + 1U] = std::byte{static_cast<unsigned char>((value.value >> 8U) & 0xFFU)};
}

auto write_u32_zero(std::span<std::byte> page, usize offset) -> void {
    for (usize index{0}; index < usize{4}; ++index) {
        page[offset.value + index.value] = std::byte{0};
    }
}

auto write_u64(std::span<std::byte> page, usize offset, u64 value) -> void {
    for (usize index{0}; index < usize{8}; ++index) {
        page[offset.value + index.value] =
            std::byte{static_cast<unsigned char>((value.value >> (index.value * 8U)) & 0xFFU)};
    }
}

[[nodiscard]] auto read_u64(std::span<const std::byte> page, usize offset) -> u64 {
    auto value = std::uint64_t{0};
    for (usize index{0}; index < usize{8}; ++index) {
        value |= static_cast<std::uint64_t>(page[offset.value + index.value]) <<
                 (index.value * 8U);
    }
    return u64{value};
}

[[nodiscard]] auto slot_offset(u16 slot_idx) -> usize {
    return usize{PAGE_HEADER_SIZE.value +
                 (static_cast<std::size_t>(slot_idx.value) * SLOT_SIZE.value)};
}

[[nodiscard]] auto read_slot(std::span<const std::byte> page, u16 slot_idx) -> Result<Slot> {
    auto count = slot_count(page);
    if (!count) {
        return std::unexpected(count.error());
    }
    if (slot_idx >= *count) {
        return std::unexpected(Error::InvalidArgument);
    }
    const auto offset = slot_offset(slot_idx);
    return Slot{.offset = read_u16(page, offset), .len = read_u16(page, offset + usize{2})};
}

auto write_slot(std::span<std::byte> page, u16 slot_idx, Slot slot) -> void {
    const auto offset = slot_offset(slot_idx);
    write_u16(page, offset, slot.offset);
    write_u16(page, offset + usize{2}, slot.len);
}

[[nodiscard]] auto validate_page_size(std::span<const std::byte> page) -> VoidResult {
    if (page.size() < PAGE_HEADER_SIZE.value || page.size() > MAX_U16) {
        return std::unexpected(Error::InvalidArgument);
    }
    return {};
}

}  // namespace

auto initialize_page(std::span<std::byte> page, u64 page_id) -> VoidResult {
    if (auto status = validate_page_size(page); !status) {
        return status;
    }
    std::ranges::fill(page, std::byte{0});
    write_u32_zero(page, CHECKSUM_OFFSET);
    write_u16(page, FLAGS_OFFSET, u16{0});
    write_u16(page, SLOT_COUNT_OFFSET, u16{0});
    write_u16(page, FREE_START_OFFSET, u16{static_cast<std::uint16_t>(PAGE_HEADER_SIZE.value)});
    write_u16(page, FREE_END_OFFSET, u16{static_cast<std::uint16_t>(page.size())});
    write_u64(page, PAGE_ID_OFFSET, page_id);
    write_u64(page, LSN_OFFSET, u64{0});
    return {};
}

auto page_lsn(std::span<const std::byte> page) -> Result<u64> {
    if (auto status = validate_page_size(page); !status) {
        return std::unexpected(status.error());
    }
    return read_u64(page, LSN_OFFSET);
}

auto set_page_lsn(std::span<std::byte> page, u64 lsn) -> VoidResult {
    if (auto status = validate_page_size(page); !status) {
        return status;
    }
    write_u64(page, LSN_OFFSET, lsn);
    return {};
}

auto slot_count(std::span<const std::byte> page) -> Result<u16> {
    if (auto status = validate_page_size(page); !status) {
        return std::unexpected(status.error());
    }
    return read_u16(page, SLOT_COUNT_OFFSET);
}

auto free_space(std::span<const std::byte> page) -> Result<usize> {
    if (auto status = validate_page_size(page); !status) {
        return std::unexpected(status.error());
    }
    const auto free_start = read_u16(page, FREE_START_OFFSET);
    const auto free_end = read_u16(page, FREE_END_OFFSET);
    if (free_end < free_start) {
        return std::unexpected(Error::Corruption);
    }
    return usize{static_cast<std::size_t>(free_end.value - free_start.value)};
}

auto can_insert(std::span<const std::byte> page, usize tuple_size) -> Result<b8> {
    if (!fits_u16(tuple_size).value) {
        return b8{false};
    }
    auto space = free_space(page);
    if (!space) {
        return std::unexpected(space.error());
    }
    return b8{space->value >= (tuple_size + SLOT_SIZE).value};
}

auto insert_tuple(std::span<std::byte> page, std::span<const std::byte> tuple) -> Result<u16> {
    const auto tuple_size = usize{tuple.size()};
    auto can_fit = can_insert(page, tuple_size);
    if (!can_fit) {
        return std::unexpected(can_fit.error());
    }
    if (!can_fit->value) {
        return std::unexpected(Error::OutOfMemory);
    }

    const auto count = read_u16(page, SLOT_COUNT_OFFSET);
    const auto free_end = read_u16(page, FREE_END_OFFSET);
    const auto tuple_offset = u16{static_cast<std::uint16_t>(free_end.value - tuple.size())};
    auto tuple_dst = page.subspan(tuple_offset.value, tuple.size());
    std::ranges::copy(tuple, tuple_dst.begin());

    write_slot(page, count,
               Slot{.offset = tuple_offset, .len = u16{static_cast<std::uint16_t>(tuple.size())}});
    const auto new_count = u16{static_cast<std::uint16_t>(count.value + 1U)};
    write_u16(page, SLOT_COUNT_OFFSET, new_count);
    write_u16(page, FREE_START_OFFSET,
              u16{static_cast<std::uint16_t>(
                  PAGE_HEADER_SIZE.value +
                  (static_cast<std::size_t>(new_count.value) * SLOT_SIZE.value))});
    write_u16(page, FREE_END_OFFSET, tuple_offset);
    return count;
}

auto delete_tuple(std::span<std::byte> page, u16 slot_idx) -> VoidResult {
    auto slot = read_slot(page, slot_idx);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    if (slot->len == u16{0}) {
        return std::unexpected(Error::NotFound);
    }
    write_slot(page, slot_idx, Slot{.offset = u16{0}, .len = u16{0}});
    return {};
}

auto overwrite_tuple(std::span<std::byte> page, u16 slot_idx, std::span<const std::byte> tuple)
    -> VoidResult {
    auto slot = read_slot(page, slot_idx);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    if (slot->len == u16{0}) {
        return std::unexpected(Error::NotFound);
    }
    if (slot->len.value != tuple.size()) {
        return std::unexpected(Error::InvalidArgument);
    }
    if ((static_cast<std::size_t>(slot->offset.value) + static_cast<std::size_t>(slot->len.value)) >
        page.size()) {
        return std::unexpected(Error::Corruption);
    }

    auto dst = page.subspan(slot->offset.value, slot->len.value);
    std::ranges::copy(tuple, dst.begin());
    return {};
}

auto read_tuple(std::span<const std::byte> page, u16 slot_idx)
    -> Result<std::vector<std::byte>> {
    auto slot = read_slot(page, slot_idx);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    if (slot->len == u16{0}) {
        return std::unexpected(Error::NotFound);
    }
    if ((static_cast<std::size_t>(slot->offset.value) + static_cast<std::size_t>(slot->len.value)) >
        page.size()) {
        return std::unexpected(Error::Corruption);
    }
    auto src = page.subspan(slot->offset.value, slot->len.value);
    return std::vector<std::byte>{src.begin(), src.end()};
}

auto is_live_slot(std::span<const std::byte> page, u16 slot_idx) -> Result<b8> {
    auto slot = read_slot(page, slot_idx);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    return b8{slot->len != u16{0}};
}

}  // namespace edb::heap
