// tests/unit/catalog/catalog_test.cpp

#include "catalog/catalog.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "types/builtin_types.hpp"

using namespace edb;

namespace {

class SharedMemoryIO final : public StorageIOOps {
   public:
    explicit SharedMemoryIO(std::shared_ptr<std::vector<std::byte>> bytes)
        : storage{std::move(bytes)} {}

   private:
    auto open_impl(const char* /*path*/, const IOConfig& /*cfg*/) -> VoidResult override {
        return {};
    }
    auto close_impl() -> VoidResult override { return {}; }

    auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if (off >= storage->size()) {
            return usize{0};
        }
        const auto available = storage->size() - off;
        const auto count = std::min(available, buf.size());
        auto src = std::span<const std::byte>{*storage}.subspan(off, count);
        std::ranges::copy(src, buf.begin());
        return usize{count};
    }

    auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> override {
        const auto off = offset.value;
        if ((off + buf.size()) > storage->size()) {
            storage->resize(off + buf.size());
        }
        auto dst = std::span<std::byte>{*storage}.subspan(off, buf.size());
        std::ranges::copy(buf, dst.begin());
        return usize{buf.size()};
    }

    auto sync_impl() -> VoidResult override { return {}; }
    auto datasync_impl() -> VoidResult override { return {}; }
    auto truncate_impl(u64 size) -> VoidResult override {
        storage->resize(size.value);
        return {};
    }
    auto file_size_impl() -> Result<u64> override { return u64{storage->size()}; }

    std::shared_ptr<std::vector<std::byte>> storage;
};

class MemoryRelationBackendFactory final : public RelationBackendFactory {
   public:
    auto open_backend(u32 relation_oid, std::string_view /*relation_name*/)
        -> Result<std::unique_ptr<StorageIOOps>> override {
        auto& bytes = relations[relation_oid];
        if (bytes == nullptr) {
            bytes = std::make_shared<std::vector<std::byte>>();
        }
        return std::make_unique<SharedMemoryIO>(bytes);
    }

   private:
    std::unordered_map<u32, std::shared_ptr<std::vector<std::byte>>> relations;
};

auto make_row(TypeRegistry& registry, const Type& int_type, std::string_view int_text,
              const Type& text_type, std::string_view text_text, const Type& bool_type,
              std::string_view bool_text) -> std::vector<Value> {
    auto first = int_type.from_text(int_text);
    auto second = text_type.from_text(text_text);
    auto third = bool_type.from_text(bool_text);
    EXPECT_TRUE(first.has_value());
    EXPECT_TRUE(second.has_value());
    EXPECT_TRUE(third.has_value());
    return std::vector<Value>{{.type_oid = int_type.oid, .bytes = *first, .is_null = b8{false}},
                              {.type_oid = text_type.oid, .bytes = *second, .is_null = b8{false}},
                              {.type_oid = bool_type.oid, .bytes = *third, .is_null = b8{false}}};
}

auto row_value_text(TypeRegistry& registry, const Value& value) -> std::string {
    auto type = registry.lookup(value.type_oid);
    EXPECT_TRUE(type.has_value());
    return (*type)->to_text(value.bytes);
}

}  // namespace

class EdbCatalogTest : public ::testing::Test {
   protected:
    void SetUp() override { ASSERT_TRUE(register_builtin_types(registry).has_value()); }

    [[nodiscard]] static auto default_engine_config() -> EngineConfig {
        return EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}};
    }

    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
};

TEST_F(EdbCatalogTest, BootstrapPersistsSystemCatalogMetadataAcrossReopen) {
    {
        Catalog catalog{registry, factory, default_engine_config()};
        ASSERT_TRUE(catalog.open().has_value());

        auto type = catalog.get_type("text");
        auto klass = catalog.get_class("edb_attribute");
        ASSERT_TRUE(type.has_value());
        ASSERT_TRUE(klass.has_value());
        EXPECT_EQ(type->name, std::string{"text"});
        EXPECT_EQ(klass->relkind, std::string{"system"});
        auto attrs = catalog.get_attributes(klass->oid);
        ASSERT_TRUE(attrs.has_value());
        EXPECT_EQ(attrs->size(), std::size_t{5});
        ASSERT_TRUE(catalog.close().has_value());
    }

    Catalog reopened{registry, factory, default_engine_config()};
    ASSERT_TRUE(reopened.open().has_value());
    auto type = reopened.get_type("int32");
    auto klass = reopened.get_class("edb_class");
    ASSERT_TRUE(type.has_value());
    ASSERT_TRUE(klass.has_value());
    EXPECT_TRUE(type->fixed_size.has_value());
    EXPECT_EQ(type->fixed_size->value, usize{4}.value);
    EXPECT_EQ(klass->name, std::string{"edb_class"});
}

TEST_F(EdbCatalogTest, CreateTablePersistsMetadataAndUserRowsAcrossReopen) {
    u32 users_oid{0};
    {
        Catalog catalog{registry, factory, default_engine_config()};
        ASSERT_TRUE(catalog.open().has_value());

        const auto int_type = registry.lookup("int32");
        const auto text_type = registry.lookup("text");
        const auto bool_type = registry.lookup("bool");
        ASSERT_TRUE(int_type.has_value());
        ASSERT_TRUE(text_type.has_value());
        ASSERT_TRUE(bool_type.has_value());

        auto created = catalog.create_table(CreateTableSpec{
            .name = "users",
            .columns = {{.name = "id", .type_oid = (*int_type)->oid, .nullable = b8{false}},
                        {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                        {.name = "active", .type_oid = (*bool_type)->oid, .nullable = b8{false}}}});
        ASSERT_TRUE(created.has_value());
        users_oid = *created;

        auto klass = catalog.get_class("users");
        ASSERT_TRUE(klass.has_value());
        EXPECT_EQ(klass->oid.value, users_oid.value);
        auto attrs = catalog.get_attributes(users_oid);
        ASSERT_TRUE(attrs.has_value());
        ASSERT_EQ(attrs->size(), std::size_t{3});
        EXPECT_EQ((*attrs)[1].name, std::string{"name"});

        auto table = catalog.open_table("users");
        ASSERT_TRUE(table.has_value());
        ASSERT_TRUE((*table)
                        ->insert(make_row(registry, **int_type, "1", **text_type, "alice",
                                          **bool_type, "true"))
                        .has_value());
        ASSERT_TRUE(catalog.close().has_value());
    }

    Catalog reopened{registry, factory, default_engine_config()};
    ASSERT_TRUE(reopened.open().has_value());
    auto klass = reopened.get_class("users");
    ASSERT_TRUE(klass.has_value());
    EXPECT_EQ(klass->oid.value, users_oid.value);

    auto table = reopened.open_table("users");
    ASSERT_TRUE(table.has_value());
    auto rows = (*table)->scan();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows->size(), std::size_t{1});
    EXPECT_EQ(row_value_text(registry, (*rows)[0][0]), std::string{"1"});
    EXPECT_EQ(row_value_text(registry, (*rows)[0][1]), std::string{"alice"});
    EXPECT_EQ(row_value_text(registry, (*rows)[0][2]), std::string{"true"});
}

TEST_F(EdbCatalogTest, DropTableRemovesMetadataAndHandleLookup) {
    Catalog catalog{registry, factory, default_engine_config()};
    ASSERT_TRUE(catalog.open().has_value());

    const auto int_type = registry.lookup("int32");
    ASSERT_TRUE(int_type.has_value());
    auto created = catalog.create_table(CreateTableSpec{
        .name = "to_drop",
        .columns = {{.name = "id", .type_oid = (*int_type)->oid, .nullable = b8{false}}}});
    ASSERT_TRUE(created.has_value());

    ASSERT_TRUE(catalog.drop_table(*created).has_value());
    auto klass = catalog.get_class("to_drop");
    auto table = catalog.open_table("to_drop");
    ASSERT_FALSE(klass.has_value());
    ASSERT_FALSE(table.has_value());
    EXPECT_EQ(klass.error(), Error::NotFound);
    EXPECT_EQ(table.error(), Error::NotFound);
}