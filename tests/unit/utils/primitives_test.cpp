#include "utils/primitives.hpp"

#include <gtest/gtest.h>
#include <unordered_set>

using namespace edb;
using namespace edb::literals;

// ---------------------------------------------------------------------------
// Construction and equality
// ---------------------------------------------------------------------------

TEST(Primitives, ExplicitConstruction) {
    constexpr i32 a{42};
    constexpr i32 b{42};
    EXPECT_EQ(a, b);
}

TEST(Primitives, DefaultConstructionIsZero) {
    constexpr u64 a{};
    EXPECT_EQ(a, u64{0});
}

TEST(Primitives, UserDefinedLiterals) {
    EXPECT_EQ(42_i32, i32{42});
    EXPECT_EQ(10_u64, u64{10});
    EXPECT_EQ(1_uz,   usize{1});
}

// ---------------------------------------------------------------------------
// Arithmetic — same type
// ---------------------------------------------------------------------------

TEST(Primitives, AdditionSameType) {
    EXPECT_EQ(i32{3} + i32{4}, i32{7});
}

TEST(Primitives, SubtractionSameType) {
    EXPECT_EQ(u32{10} - u32{3}, u32{7});
}

TEST(Primitives, MultiplicationSameType) {
    EXPECT_EQ(i64{6} * i64{7}, i64{42});
}

TEST(Primitives, DivisionSameType) {
    EXPECT_EQ(u64{20} / u64{4}, u64{5});
}

TEST(Primitives, ModuloIntegralOnly) {
    EXPECT_EQ(i32{17} % i32{5}, i32{2});
}

TEST(Primitives, UnaryMinus) {
    EXPECT_EQ(-i32{5}, i32{-5});
    EXPECT_EQ(-f64{1.0}, f64{-1.0});
}

TEST(Primitives, PreIncrement) {
    u32 v{0};
    EXPECT_EQ(++v, u32{1});
    EXPECT_EQ(v, u32{1});
}

TEST(Primitives, PostIncrement) {
    u32 v{0};
    EXPECT_EQ(v++, u32{0});
    EXPECT_EQ(v, u32{1});
}

TEST(Primitives, CompoundAssignment) {
    i32 v{10};
    v += i32{5};
    EXPECT_EQ(v, i32{15});
    v -= i32{3};
    EXPECT_EQ(v, i32{12});
    v *= i32{2};
    EXPECT_EQ(v, i32{24});
    v /= i32{4};
    EXPECT_EQ(v, i32{6});
}

// ---------------------------------------------------------------------------
// Bitwise
// ---------------------------------------------------------------------------

TEST(Primitives, BitwiseAnd) {
    EXPECT_EQ(u32{0b1100} & u32{0b1010}, u32{0b1000});
}

TEST(Primitives, BitwiseOr) {
    EXPECT_EQ(u32{0b1100} | u32{0b0011}, u32{0b1111});
}

TEST(Primitives, BitwiseXor) {
    EXPECT_EQ(u32{0b1111} ^ u32{0b0101}, u32{0b1010});
}

TEST(Primitives, BitwiseNot) {
    EXPECT_EQ(~u8{0}, u8{255});
}

TEST(Primitives, LeftShift) {
    EXPECT_EQ(u32{1} << u32{3}, u32{8});
}

TEST(Primitives, RightShift) {
    EXPECT_EQ(u32{8} >> u32{2}, u32{2});
}

// ---------------------------------------------------------------------------
// Comparison / ordering
// ---------------------------------------------------------------------------

TEST(Primitives, LessThan) {
    EXPECT_LT(i32{1}, i32{2});
}

TEST(Primitives, Spaceship) {
    EXPECT_EQ(i32{3} <=> i32{3}, std::strong_ordering::equal);
    EXPECT_EQ(i32{1} <=> i32{2}, std::strong_ordering::less);
    EXPECT_EQ(i32{2} <=> i32{1}, std::strong_ordering::greater);
}

// ---------------------------------------------------------------------------
// Boolean (b8)
// ---------------------------------------------------------------------------

TEST(Primitives, BooleanEquality) {
    EXPECT_EQ(b8{true}, b8{true});
    EXPECT_NE(b8{true}, b8{false});
}

// ---------------------------------------------------------------------------
// std::hash — usable in unordered containers
// ---------------------------------------------------------------------------

TEST(Primitives, HashUsableInUnorderedSet) {
    std::unordered_set<u64> s;
    s.insert(u64{1});
    s.insert(u64{2});
    s.insert(u64{1});  // duplicate
    EXPECT_EQ(s.size(), static_cast<std::size_t>(2));  // raw-primitive: gtest size_t
}

// ---------------------------------------------------------------------------
// Type safety — cross-type arithmetic must NOT compile.
// The following lines should be uncommented to verify at compile time:
//
//   i32 a{1};  u32 b{1};
//   auto c = a + b;  // error: no matching operator+
//   auto d = i32{1} + 1;  // error: no implicit int→i32 conversion
// ---------------------------------------------------------------------------
