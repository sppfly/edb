// src/types/builtin_types.cpp

#include "types/builtin_types.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <sstream>
#include <string>
#include <string_view>

namespace edb {

namespace {

template <typename T, std::size_t N>
auto copy_array_to_vector(const std::array<std::byte, N>& bytes) -> std::vector<std::byte> {
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

template <typename Unsigned, std::size_t N>
auto encode_unsigned_le(Unsigned value) -> std::array<std::byte, N> {
    std::array<std::byte, N> bytes{};
    for (std::size_t index = 0; index < N; ++index) {          // raw-primitive: std::array indexing
        const auto shift = static_cast<unsigned>(index * 8U);  // raw-primitive: bit shift count
        bytes.at(index) = std::byte{static_cast<unsigned char>((value >> shift) & Unsigned{0xFFU})};
    }
    return bytes;
}

template <typename Unsigned>
auto decode_unsigned_le(std::span<const std::byte> bytes) -> Unsigned {
    Unsigned value{0};
    for (std::size_t index = 0; index < bytes.size();
         ++index) {  // raw-primitive: std::array indexing
        value |= static_cast<Unsigned>(std::to_integer<unsigned char>(bytes[index]))
                 << static_cast<unsigned>(index * 8U);
    }
    return value;
}

template <typename Signed, typename Unsigned, std::size_t N>
auto parse_signed_fixed(std::string_view text) -> Result<std::vector<std::byte>> {
    Signed parsed{0};  // raw-primitive: iostream extraction parses primitive scalars
    std::istringstream input{std::string{text}};
    input >> parsed;
    if (input.fail()) {
        return std::unexpected(Error::InvalidArgument);
    }
    input >> std::ws;
    if (!input.eof()) {
        return std::unexpected(Error::InvalidArgument);
    }

    const auto encoded = encode_unsigned_le<Unsigned, N>(std::bit_cast<Unsigned>(parsed));
    return copy_array_to_vector<Signed>(encoded);
}

template <typename Signed, typename Unsigned, std::size_t N>
auto decode_signed_fixed(std::span<const std::byte> bytes) -> Result<Signed> {
    if (bytes.size() != N) {
        return std::unexpected(Error::InvalidArgument);
    }
    return std::bit_cast<Signed>(decode_unsigned_le<Unsigned>(bytes));
}

template <typename Signed, typename Unsigned, std::size_t N>
auto signed_to_text(std::span<const std::byte> bytes) -> std::string {
    const auto decoded = decode_signed_fixed<Signed, Unsigned, N>(bytes);
    if (!decoded) {
        return {};
    }
    return std::to_string(*decoded);
}

template <typename Signed, typename Unsigned, std::size_t N>
auto signed_compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
    -> std::strong_ordering {
    const auto left = decode_signed_fixed<Signed, Unsigned, N>(lhs);
    const auto right = decode_signed_fixed<Signed, Unsigned, N>(rhs);
    if (!left || !right) {
        return std::strong_ordering::equal;
    }
    return *left <=> *right;
}

template <typename Signed, typename Unsigned, std::size_t N>
auto signed_hash(std::span<const std::byte> bytes) -> usize {
    const auto decoded = decode_signed_fixed<Signed, Unsigned, N>(bytes);
    if (!decoded) {
        return usize{0};
    }
    return usize{
        std::hash<Signed>{}(*decoded)};  // raw-primitive: std::hash is defined on primitives
}

struct Int32Type {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        return parse_signed_fixed<std::int32_t, std::uint32_t, 4U>(text);
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        return signed_to_text<std::int32_t, std::uint32_t, 4U>(bytes);
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        return signed_compare<std::int32_t, std::uint32_t, 4U>(lhs, rhs);
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        return signed_hash<std::int32_t, std::uint32_t, 4U>(bytes);
    }

    static auto fixed_size() -> std::optional<usize> { return usize{4}; }
};

struct Int64Type {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        return parse_signed_fixed<std::int64_t, std::uint64_t, 8U>(text);
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        return signed_to_text<std::int64_t, std::uint64_t, 8U>(bytes);
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        return signed_compare<std::int64_t, std::uint64_t, 8U>(lhs, rhs);
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        return signed_hash<std::int64_t, std::uint64_t, 8U>(bytes);
    }

    static auto fixed_size() -> std::optional<usize> { return usize{8}; }
};

struct Float64Type {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        std::string owned{text};
        char* parse_end = nullptr;  // raw-primitive: strtod uses C pointer API
        const double parsed = std::strtod(owned.c_str(), &parse_end);  // raw-primitive: C API
        if (parse_end == nullptr || *parse_end != '\0' || !std::isfinite(parsed)) {
            return std::unexpected(Error::InvalidArgument);
        }

        const auto bits = std::bit_cast<std::uint64_t>(parsed);
        return copy_array_to_vector<double>(encode_unsigned_le<std::uint64_t, 8U>(bits));
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        if (bytes.size() != 8U) {
            return {};
        }
        const auto bits = decode_unsigned_le<std::uint64_t>(bytes);
        return std::to_string(std::bit_cast<double>(bits));
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        if (lhs.size() != 8U || rhs.size() != 8U) {
            return std::strong_ordering::equal;
        }
        const auto left = std::bit_cast<double>(decode_unsigned_le<std::uint64_t>(lhs));
        const auto right = std::bit_cast<double>(decode_unsigned_le<std::uint64_t>(rhs));
        if (left < right) {
            return std::strong_ordering::less;
        }
        if (left > right) {
            return std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        if (bytes.size() != 8U) {
            return usize{0};
        }
        const auto value = std::bit_cast<double>(decode_unsigned_le<std::uint64_t>(bytes));
        return usize{
            std::hash<double>{}(value)};  // raw-primitive: std::hash is defined on primitives
    }

    static auto fixed_size() -> std::optional<usize> { return usize{8}; }
};

struct BoolType {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        if (text == "true") {
            return std::vector<std::byte>{std::byte{1}};
        }
        if (text == "false") {
            return std::vector<std::byte>{std::byte{0}};
        }
        return std::unexpected(Error::InvalidArgument);
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        if (bytes.size() != 1U) {
            return {};
        }
        return bytes[0] == std::byte{0} ? "false" : "true";
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        if (lhs.size() != 1U || rhs.size() != 1U) {
            return std::strong_ordering::equal;
        }
        return std::to_integer<unsigned char>(lhs[0]) <=> std::to_integer<unsigned char>(rhs[0]);
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        if (bytes.size() != 1U) {
            return usize{0};
        }
        return usize{std::to_integer<unsigned char>(bytes[0])};
    }

    static auto fixed_size() -> std::optional<usize> { return usize{1}; }
};

struct TextType {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        std::vector<std::byte> bytes{};
        bytes.reserve(text.size());
        std::ranges::transform(text, std::back_inserter(bytes), [](char ch) {
            return std::byte{static_cast<unsigned char>(ch)};  // raw-primitive: byte conversion
        });
        return bytes;
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        std::string text{};
        text.reserve(bytes.size());
        std::ranges::transform(bytes, std::back_inserter(text), [](std::byte ch) {
            return static_cast<char>(ch);  // raw-primitive: string stores char bytes
        });
        return text;
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        return to_text(lhs) <=> to_text(rhs);
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        return usize{std::hash<std::string>{}(to_text(bytes))};
    }

    static auto fixed_size() -> std::optional<usize> { return std::nullopt; }
};

}  // namespace

auto register_builtin_types(TypeRegistry& registry) -> VoidResult {
    if (auto status = registry.register_type<Int32Type>("int32"); !status) {
        return status;
    }
    if (auto status = registry.register_type<Int64Type>("int64"); !status) {
        return status;
    }
    if (auto status = registry.register_type<Float64Type>("float64"); !status) {
        return status;
    }
    if (auto status = registry.register_type<BoolType>("bool"); !status) {
        return status;
    }
    return registry.register_type<TextType>("text");
}

}  // namespace edb