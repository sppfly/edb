// tests/unit/query/test_binder.cpp

#include "query/binder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/parser.hpp"
#include "storage/io/io_ops.hpp"
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

auto parse_stmt(std::string_view sql) -> Stmt {
    Parser parser{sql};
    auto parsed = parser.parse();
    EXPECT_TRUE(parsed.has_value()) << parser.error_message();
    EXPECT_EQ(parsed->size(), std::size_t{1});
    return std::move(parsed->front());
}

class BinderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_TRUE(register_builtin_types(registry).has_value());
        ASSERT_TRUE(catalog.open().has_value());
    }

    [[nodiscard]] static auto default_engine_config() -> EngineConfig {
        return EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}};
    }

   public:
    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
    Catalog catalog{registry, factory, default_engine_config()};
};

TEST_F(BinderTest, BindCreateTableResolvesBuiltinTypesAndNullability) {
    auto stmt = parse_stmt(
        "CREATE TABLE users (id INTEGER NOT NULL, name TEXT, nick VARCHAR(100) UNIQUE)");

    Binder binder{catalog};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();
    ASSERT_TRUE(std::holds_alternative<BoundCreateTableStmt>(*bound));

    const auto& create = std::get<BoundCreateTableStmt>(*bound);
    const auto int32_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(text_type.has_value());

    EXPECT_EQ(create.table_name, std::string{"users"});
    ASSERT_EQ(create.columns.size(), std::size_t{3});
    EXPECT_EQ(create.columns[0].name, std::string{"id"});
    EXPECT_EQ(create.columns[0].type.oid.value, (*int32_type)->oid.value);
    EXPECT_FALSE(static_cast<bool>(create.columns[0].nullable));
    EXPECT_EQ(create.columns[1].type.oid.value, (*text_type)->oid.value);
    EXPECT_TRUE(static_cast<bool>(create.columns[1].nullable));
    EXPECT_EQ(create.columns[2].type.oid.value, (*text_type)->oid.value);
    ASSERT_TRUE(create.columns[2].type.param.has_value());
    EXPECT_EQ(create.columns[2].type.param->value, u32{100}.value);
    EXPECT_TRUE(static_cast<bool>(create.columns[2].unique_constraint));
}

TEST_F(BinderTest, BindCreateTableRejectsUnknownType) {
    auto stmt = parse_stmt("CREATE TABLE t (id UUID)");

    Binder binder{catalog};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::TypeNotFound);
    EXPECT_NE(binder.error_message().find("unknown type"), std::string_view::npos);
}

TEST_F(BinderTest, BindCreateTableRejectsDuplicateColumns) {
    auto stmt = parse_stmt("CREATE TABLE t (id INTEGER, ID TEXT)");

    Binder binder{catalog};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("duplicate column"), std::string_view::npos);
}

}  // namespace
