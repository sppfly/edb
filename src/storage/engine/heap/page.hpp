#pragma once

// src/storage/engine/heap/page.hpp
//
// Slotted-page helpers for the heap row-store. Tuple payloads are opaque bytes;
// catalog/type layers interpret them later.

#include <cstddef>
#include <span>
#include <vector>

#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb::heap {

constexpr auto PAGE_HEADER_SIZE = usize{28};
constexpr auto SLOT_SIZE = usize{4};

struct Slot {
    u16 offset;
    u16 len;
};

[[nodiscard]] auto initialize_page(std::span<std::byte> page, u64 page_id) -> VoidResult;
[[nodiscard]] auto slot_count(std::span<const std::byte> page) -> Result<u16>;
[[nodiscard]] auto free_space(std::span<const std::byte> page) -> Result<usize>;
[[nodiscard]] auto can_insert(std::span<const std::byte> page, usize tuple_size) -> Result<b8>;
[[nodiscard]] auto insert_tuple(std::span<std::byte> page, std::span<const std::byte> tuple)
    -> Result<u16>;
[[nodiscard]] auto delete_tuple(std::span<std::byte> page, u16 slot_idx) -> VoidResult;
[[nodiscard]] auto overwrite_tuple(std::span<std::byte> page, u16 slot_idx,
                                   std::span<const std::byte> tuple) -> VoidResult;
[[nodiscard]] auto read_tuple(std::span<const std::byte> page, u16 slot_idx)
    -> Result<std::vector<std::byte>>;
[[nodiscard]] auto is_live_slot(std::span<const std::byte> page, u16 slot_idx) -> Result<b8>;

}  // namespace edb::heap
