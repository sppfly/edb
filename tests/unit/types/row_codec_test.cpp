// tests/unit/types/row_codec_test.cpp

#include "types/row_codec.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "types/builtin_types.hpp"

using namespace edb;

namespace {

auto make_value(EdbTypeRegistry& registry, const EdbType& type, std::string_view text) -> EdbValue {
    auto bytes = type.from_text(text);
    EXPECT_TRUE(bytes.has_value());
    return EdbValue{.type_oid = type.oid, .bytes = *bytes, .is_null = b8{false}};
}

auto decode_to_text(EdbTypeRegistry& registry, const EdbValue& value) -> std::string {
    auto type = registry.lookup(value.type_oid);
    EXPECT_TRUE(type.has_value());
    return (*type)->to_text(value.bytes);
}

}  // namespace

TEST(EdbRowCodec, EncodesAndDecodesMixedSchemaRoundTrip) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto int32_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(text_type.has_value());
    ASSERT_TRUE(bool_type.has_value());

    EdbRowCodec codec{registry,
                      {{.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                       {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                       {.name = "active", .type_oid = (*bool_type)->oid, .nullable = b8{false}}}};

    const auto encoded = codec.encode(std::vector<EdbValue>{
        make_value(registry, **int32_type, "7"), make_value(registry, **text_type, "alice"),
        make_value(registry, **bool_type, "true")});
    ASSERT_TRUE(encoded.has_value());

    const auto decoded = codec.decode(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 3U);
    EXPECT_EQ(decode_to_text(registry, (*decoded)[0]), std::string{"7"});
    EXPECT_EQ(decode_to_text(registry, (*decoded)[1]), std::string{"alice"});
    EXPECT_EQ(decode_to_text(registry, (*decoded)[2]), std::string{"true"});
}

TEST(EdbRowCodec, SupportsNullableColumns) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto int32_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(text_type.has_value());

    EdbRowCodec codec{registry,
                      {{.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                       {.name = "nickname", .type_oid = (*text_type)->oid, .nullable = b8{true}}}};

    const auto encoded = codec.encode(std::vector<EdbValue>{
        make_value(registry, **int32_type, "9"),
        EdbValue{.type_oid = (*text_type)->oid, .bytes = {}, .is_null = b8{true}}});
    ASSERT_TRUE(encoded.has_value());

    const auto decoded = codec.decode(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2U);
    EXPECT_EQ(decode_to_text(registry, (*decoded)[0]), std::string{"9"});
    EXPECT_TRUE((*decoded)[1].is_null.value);
    EXPECT_TRUE((*decoded)[1].bytes.empty());
}

TEST(EdbRowCodec, RejectsSchemaValueCountMismatch) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto int32_type = registry.lookup("int32");
    ASSERT_TRUE(int32_type.has_value());
    EdbRowCodec codec{registry,
                      {{.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}}}};

    const auto encoded = codec.encode({});

    ASSERT_FALSE(encoded.has_value());
    EXPECT_EQ(encoded.error(), EdbError::InvalidArgument);
}

TEST(EdbRowCodec, RejectsFixedWidthLengthMismatch) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto int32_type = registry.lookup("int32");
    ASSERT_TRUE(int32_type.has_value());
    EdbRowCodec codec{registry,
                      {{.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}}}};

    const auto encoded =
        codec.encode(std::vector<EdbValue>{EdbValue{.type_oid = (*int32_type)->oid,
                                                    .bytes = {std::byte{1}, std::byte{2}},
                                                    .is_null = b8{false}}});

    ASSERT_FALSE(encoded.has_value());
    EXPECT_EQ(encoded.error(), EdbError::InvalidArgument);
}

TEST(EdbRowCodec, RejectsCorruptTuplePayload) {
    EdbTypeRegistry registry;
    ASSERT_TRUE(register_builtin_types(registry).has_value());

    const auto text_type = registry.lookup("text");
    ASSERT_TRUE(text_type.has_value());
    EdbRowCodec codec{registry,
                      {{.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}}}};

    auto encoded = codec.encode(std::vector<EdbValue>{make_value(registry, **text_type, "hello")});
    ASSERT_TRUE(encoded.has_value());
    encoded->pop_back();

    const auto decoded = codec.decode(*encoded);

    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), EdbError::Corruption);
}