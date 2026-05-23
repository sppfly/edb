#pragma once

// src/utils/primitives.hpp
//
// Type-safe wrappers for all primitive types used in EDB.
//
// Rules:
//   - Never use raw int/unsigned/long/float/double/bool in EDB code.
//   - Use these wrappers instead: i8, i16, i32, i64, u8, u16, u32, u64,
//     f32, f64, b8, usize, isize.
//   - Cross-type arithmetic is a compile error (e.g. i32 + u32).
//   - Interfacing with OS/C APIs: cast at the call site only, with a comment:
//       // raw-primitive: pread64 requires ssize_t
//
// Thread-safety: wrappers are trivially copyable value types; no shared state.

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace edb {

// ---------------------------------------------------------------------------
// Internal machinery — do not use directly outside this header.
// ---------------------------------------------------------------------------
namespace detail {

template <typename T, typename Tag>
struct Primitive {
    T value{};

    // Construction: explicit only.
    constexpr explicit Primitive(T v) noexcept : value(v) {}

    // Default-constructed to zero.
    constexpr Primitive() noexcept = default;

    // Copy / move — trivial.
    constexpr Primitive(const Primitive&) noexcept = default;
    constexpr Primitive& operator=(const Primitive&) noexcept = default;
    constexpr Primitive(Primitive&&) noexcept = default;
    constexpr Primitive& operator=(Primitive&&) noexcept = default;
    constexpr ~Primitive() noexcept = default;

    // Comparison — full set via spaceship.
    constexpr auto operator<=>(const Primitive&) const noexcept = default;
    constexpr bool operator==(const Primitive&) const noexcept = default;

    // Arithmetic — same type only (cross-type is a compile error).
    constexpr Primitive operator+(Primitive rhs) const noexcept {
        return Primitive{static_cast<T>(value + rhs.value)};
    }
    constexpr Primitive operator-(Primitive rhs) const noexcept {
        return Primitive{static_cast<T>(value - rhs.value)};
    }
    constexpr Primitive operator*(Primitive rhs) const noexcept {
        return Primitive{static_cast<T>(value * rhs.value)};
    }
    constexpr Primitive operator/(Primitive rhs) const noexcept {
        return Primitive{static_cast<T>(value / rhs.value)};
    }
    constexpr Primitive operator%(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value % rhs.value)};
    }

    constexpr Primitive& operator+=(Primitive rhs) noexcept {
        value = static_cast<T>(value + rhs.value);
        return *this;
    }
    constexpr Primitive& operator-=(Primitive rhs) noexcept {
        value = static_cast<T>(value - rhs.value);
        return *this;
    }
    constexpr Primitive& operator*=(Primitive rhs) noexcept {
        value = static_cast<T>(value * rhs.value);
        return *this;
    }
    constexpr Primitive& operator/=(Primitive rhs) noexcept {
        value = static_cast<T>(value / rhs.value);
        return *this;
    }

    constexpr Primitive& operator++() noexcept {
        ++value;
        return *this;
    }
    constexpr Primitive operator++(int) noexcept {
        auto old = *this;
        ++value;
        return old;
    }
    constexpr Primitive& operator--() noexcept {
        --value;
        return *this;
    }
    constexpr Primitive operator--(int) noexcept {
        auto old = *this;
        --value;
        return old;
    }

    // Bitwise — integers only.
    constexpr Primitive operator&(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value & rhs.value)};
    }
    constexpr Primitive operator|(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value | rhs.value)};
    }
    constexpr Primitive operator^(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value ^ rhs.value)};
    }
    constexpr Primitive operator~() const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(~value)};
    }
    constexpr Primitive operator<<(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value << rhs.value)};
    }
    constexpr Primitive operator>>(Primitive rhs) const noexcept
        requires std::integral<T>
    {
        return Primitive{static_cast<T>(value >> rhs.value)};
    }

    // Unary minus — signed / float only.
    constexpr Primitive operator-() const noexcept
        requires(std::signed_integral<T> || std::floating_point<T>)
    {
        return Primitive{static_cast<T>(-value)};
    }

    // Deleted: implicit conversion to underlying type.
    explicit operator T() const noexcept { return value; }
};

}  // namespace detail

// ---------------------------------------------------------------------------
// Public type aliases
// ---------------------------------------------------------------------------

// Signed integers
struct I8Tag {};
struct I16Tag {};
struct I32Tag {};
struct I64Tag {};
using i8 = detail::Primitive<int8_t, I8Tag>;
using i16 = detail::Primitive<int16_t, I16Tag>;
using i32 = detail::Primitive<int32_t, I32Tag>;
using i64 = detail::Primitive<int64_t, I64Tag>;

// Unsigned integers
struct U8Tag {};
struct U16Tag {};
struct U32Tag {};
struct U64Tag {};
using u8 = detail::Primitive<uint8_t, U8Tag>;
using u16 = detail::Primitive<uint16_t, U16Tag>;
using u32 = detail::Primitive<uint32_t, U32Tag>;
using u64 = detail::Primitive<uint64_t, U64Tag>;

// Floating-point
struct F32Tag {};
struct F64Tag {};
using f32 = detail::Primitive<float, F32Tag>;
using f64 = detail::Primitive<double, F64Tag>;

// Boolean
struct B8Tag {};
using b8 = detail::Primitive<bool, B8Tag>;

// Size types
struct UsizeTag {};
struct IsizeTag {};
using usize = detail::Primitive<std::size_t, UsizeTag>;
using isize = detail::Primitive<std::ptrdiff_t, IsizeTag>;

// ---------------------------------------------------------------------------
// User-defined literals  (optional convenience)
//   42_i32,  10_u64,  3.14_f64,  etc.
// ---------------------------------------------------------------------------
namespace literals {

constexpr i32 operator""_i32(unsigned long long v) {
    return i32{static_cast<int32_t>(v)};
}  // raw-primitive: ULL literal
constexpr i64 operator""_i64(unsigned long long v) {
    return i64{static_cast<int64_t>(v)};
}  // raw-primitive: ULL literal
constexpr u32 operator""_u32(unsigned long long v) {
    return u32{static_cast<uint32_t>(v)};
}  // raw-primitive: ULL literal
constexpr u64 operator""_u64(unsigned long long v) {
    return u64{static_cast<uint64_t>(v)};
}  // raw-primitive: ULL literal
constexpr usize operator""_uz(unsigned long long v) {
    return usize{static_cast<std::size_t>(v)};
}  // raw-primitive: ULL literal
constexpr f64 operator""_f64(long double v) {
    return f64{static_cast<double>(v)};
}  // raw-primitive: LD literal

}  // namespace literals

}  // namespace edb

// ---------------------------------------------------------------------------
// std::hash specialisations so wrappers can be used in unordered containers.
// ---------------------------------------------------------------------------
// Specialising std::hash for user-defined types is explicitly permitted by the
// C++ standard ([namespace.std]). NOLINT is required because clang-tidy's
// bugprone-std-namespace-modification check does not distinguish specialisations
// from modifications.
// NOLINTBEGIN(bugprone-std-namespace-modification)
namespace std {

template <typename T, typename Tag>
struct hash<edb::detail::Primitive<T, Tag>> {
    constexpr std::size_t operator()(const edb::detail::Primitive<T, Tag>& p) const noexcept {
        return std::hash<T>{}(p.value);
    }
};

}  // namespace std
// NOLINTEND(bugprone-std-namespace-modification)
