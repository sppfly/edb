// tests/unit/types/builtin_types_test.cpp

#include "types/builtin_types.hpp"

#include <gtest/gtest.h>

#include <compare>
#include <string>

using namespace edb;

namespace {

auto lookup_registered_type(EdbTypeRegistry& registry, std::string_view name) -> const EdbType& {
    auto found = registry.lookup(name);
    EXPECT_TRUE(found.has_value());
    return **found;
}

}  // namespace

TEST(BuiltinTypes, RegisterBuiltinTypesAddsAllCoreTypes) {
    EdbTypeRegistry registry;

    ASSERT_TRUE(register_builtin_types(registry).has_value());

    EXPECT_EQ(registry.size().value, usize{5}.value);
    EXPECT_EQ(lookup_registered_type(registry, "int32").oid.value, u32{1}.value);
    EXPECT_EQ(lookup_registered_type(registry, "int64").oid.value, u32{2}.value);
    EXPECT_EQ(lookup_registered_type(registry, "float64").oid.value, u32{3}.value);
    EXPECT_EQ(lookup_registered_type(registry, "bool").oid.value, u32{4}.value);
    EXPECT_EQ(lookup_registered_type(registry, "text").oid.value, u32{5}.value);
}

TEST(BuiltinTypes, IntTypesRoundTripAndCompare) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto& int32_type = lookup_registered_type(registry, "int32");
    const auto& int64_type = lookup_registered_type(registry, "int64");
    auto int32_bytes = int32_type.from_text("-42");
    auto int64_bytes = int64_type.from_text("123456789");
    ASSERT_TRUE(int32_bytes.has_value());
    ASSERT_TRUE(int64_bytes.has_value());
    EXPECT_EQ(int32_type.fixed_size->value, usize{4}.value);
    EXPECT_EQ(int64_type.fixed_size->value, usize{8}.value);
    EXPECT_EQ(int32_type.to_text(*int32_bytes), std::string{"-42"});
    EXPECT_EQ(int64_type.to_text(*int64_bytes), std::string{"123456789"});

    auto higher = int32_type.from_text("7");
    ASSERT_TRUE(higher.has_value());
    EXPECT_EQ(int32_type.compare(*int32_bytes, *higher), std::strong_ordering::less);
}

TEST(BuiltinTypes, FloatBoolAndTextRoundTrip) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto& float_type = lookup_registered_type(registry, "float64");
    const auto& bool_type = lookup_registered_type(registry, "bool");
    const auto& text_type = lookup_registered_type(registry, "text");

    auto float_bytes = float_type.from_text("3.5");
    auto bool_bytes = bool_type.from_text("true");
    auto text_bytes = text_type.from_text("hello");
    ASSERT_TRUE(float_bytes.has_value());
    ASSERT_TRUE(bool_bytes.has_value());
    ASSERT_TRUE(text_bytes.has_value());

    EXPECT_EQ(float_type.fixed_size->value, usize{8}.value);
    EXPECT_EQ(bool_type.fixed_size->value, usize{1}.value);
    EXPECT_EQ(text_type.fixed_size, std::nullopt);
    EXPECT_EQ(bool_type.to_text(*bool_bytes), std::string{"true"});
    EXPECT_EQ(text_type.to_text(*text_bytes), std::string{"hello"});
    EXPECT_EQ(float_type.compare(*float_bytes, *float_bytes), std::strong_ordering::equal);
    EXPECT_GT(float_type.hash(*float_bytes).value, usize{0}.value);
}

TEST(BuiltinTypes, InvalidBuiltinTextInputIsRejected) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto& int32_type = lookup_registered_type(registry, "int32");
    const auto& bool_type = lookup_registered_type(registry, "bool");
    const auto& float_type = lookup_registered_type(registry, "float64");

    auto bad_int = int32_type.from_text("abc");
    auto bad_bool = bool_type.from_text("yes");
    auto bad_float = float_type.from_text("nan");

    ASSERT_FALSE(bad_int.has_value());
    ASSERT_FALSE(bad_bool.has_value());
    ASSERT_FALSE(bad_float.has_value());
    EXPECT_EQ(bad_int.error(), EdbError::InvalidArgument);
    EXPECT_EQ(bad_bool.error(), EdbError::InvalidArgument);
    EXPECT_EQ(bad_float.error(), EdbError::InvalidArgument);
}

TEST(BuiltinTypes, RegisterBuiltinTypesRejectsDuplicates) {
    EdbTypeRegistry registry;

    ASSERT_TRUE(register_builtin_types(registry).has_value());
    auto duplicate = register_builtin_types(registry);

    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), EdbError::AlreadyExists);
}