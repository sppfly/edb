// tests/unit/types/registry_test.cpp

#include "types/registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <string>
#include <vector>

using namespace edb;

namespace {

struct MockTextType {
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

    static auto hash(std::span<const std::byte> bytes) -> usize { return usize{bytes.size()}; }

    static auto fixed_size() -> std::optional<usize> { return std::nullopt; }
};

struct MockFixedType {
    static auto from_text(std::string_view text) -> Result<std::vector<std::byte>> {
        if (text.size() != 4U) {
            return std::unexpected(Error::InvalidArgument);
        }
        return MockTextType::from_text(text);
    }

    static auto to_text(std::span<const std::byte> bytes) -> std::string {
        return MockTextType::to_text(bytes);
    }

    static auto compare(std::span<const std::byte> lhs, std::span<const std::byte> rhs)
        -> std::strong_ordering {
        return MockTextType::compare(lhs, rhs);
    }

    static auto hash(std::span<const std::byte> bytes) -> usize {
        return MockTextType::hash(bytes);
    }

    static auto fixed_size() -> std::optional<usize> { return usize{4}; }
};

}  // namespace

TEST(EdbTypeRegistry, RegisterAssignsStableOidsAndSupportsLookup) {
    TypeRegistry registry;

    ASSERT_TRUE(registry.register_type<MockTextType>("text").has_value());
    ASSERT_TRUE(registry.register_type<MockFixedType>("code4").has_value());

    auto text = registry.lookup("text");
    auto code4 = registry.lookup("code4");
    ASSERT_TRUE(text.has_value());
    ASSERT_TRUE(code4.has_value());
    EXPECT_EQ((*text)->oid.value, u32{1}.value);
    EXPECT_EQ((*code4)->oid.value, u32{2}.value);
    EXPECT_EQ(registry.size().value, usize{2}.value);

    auto by_oid = registry.lookup(u32{2});
    ASSERT_TRUE(by_oid.has_value());
    EXPECT_EQ((*by_oid)->name, std::string{"code4"});
}

TEST(EdbTypeRegistry, DuplicateNamesAreRejected) {
    TypeRegistry registry;

    ASSERT_TRUE(registry.register_type<MockTextType>("text").has_value());
    auto duplicate = registry.register_type<MockFixedType>("text");

    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), Error::AlreadyExists);
}

TEST(EdbTypeRegistry, LookupOfUnknownTypeReturnsNotFound) {
    TypeRegistry registry;

    auto by_name = registry.lookup("missing");
    auto by_oid = registry.lookup(u32{42});

    ASSERT_FALSE(by_name.has_value());
    ASSERT_FALSE(by_oid.has_value());
    EXPECT_EQ(by_name.error(), Error::NotFound);
    EXPECT_EQ(by_oid.error(), Error::NotFound);
}

TEST(EdbTypeRegistry, RegisteredOpsAreCallableThroughStoredMetadata) {
    TypeRegistry registry;
    ASSERT_TRUE(registry.register_type<MockTextType>("text").has_value());

    auto text = registry.lookup("text");
    ASSERT_TRUE(text.has_value());
    ASSERT_EQ((*text)->fixed_size, std::nullopt);

    auto encoded = (*text)->from_text("abc");
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ((*text)->to_text(*encoded), std::string{"abc"});
    EXPECT_EQ((*text)->compare(*encoded, *encoded), std::strong_ordering::equal);
    EXPECT_EQ((*text)->hash(*encoded).value, usize{3}.value);
}

TEST(EdbTypeRegistry, FixedSizeMetadataPropagatesFromImplementation) {
    TypeRegistry registry;
    ASSERT_TRUE(registry.register_type<MockFixedType>("code4").has_value());

    auto type = registry.lookup("code4");
    ASSERT_TRUE(type.has_value());
    ASSERT_TRUE((*type)->fixed_size.has_value());
    EXPECT_EQ((*type)->fixed_size->value, usize{4}.value);

    auto invalid = (*type)->from_text("abc");
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error(), Error::InvalidArgument);
}