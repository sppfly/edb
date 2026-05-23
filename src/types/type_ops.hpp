#pragma once

// src/types/type_ops.hpp
//
// Runtime type metadata and operations used by row encoding, catalogs, and the
// query layer. Concrete types register byte-level behavior so storage remains
// type-agnostic.

#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

template <typename T>
concept TypeImpl = requires(std::string_view text, std::span<const std::byte> lhs,
                               std::span<const std::byte> rhs) {
    { T::from_text(text) } -> std::same_as<Result<std::vector<std::byte>>>;
    { T::to_text(lhs) } -> std::same_as<std::string>;
    { T::compare(lhs, rhs) } -> std::same_as<std::strong_ordering>;
    { T::hash(lhs) } -> std::same_as<usize>;
    { T::fixed_size() } -> std::same_as<std::optional<usize>>;
};

struct Type {
    u32 oid{0};
    std::string name;
    std::optional<usize> fixed_size;

    std::function<Result<std::vector<std::byte>>(std::string_view)> from_text;
    std::function<std::string(std::span<const std::byte>)> to_text;
    std::function<std::strong_ordering(std::span<const std::byte>, std::span<const std::byte>)>
        compare;
    std::function<usize(std::span<const std::byte>)> hash;
};

}  // namespace edb