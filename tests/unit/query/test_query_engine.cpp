// tests/unit/query/test_query_engine.cpp

#include "query/query_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.hpp"
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

auto value_text(const TypeRegistry& registry, const Value& value) -> std::string {
    auto type = registry.lookup(value.type_oid);
    EXPECT_TRUE(type.has_value());
    return (*type)->to_text(value.bytes);
}

class QueryEngineTest : public ::testing::Test {
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

TEST_F(QueryEngineTest, ExecuteSqlCreateInsertSelect) {
    QueryEngine engine{catalog, registry};

    auto create = engine.execute("CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN)");
    ASSERT_TRUE(create.has_value()) << engine.error_message();
    EXPECT_TRUE(create->rows.empty());

    auto first_insert = engine.execute("INSERT INTO users VALUES (1, 'alice', TRUE)");
    ASSERT_TRUE(first_insert.has_value()) << engine.error_message();
    EXPECT_TRUE(first_insert->rows.empty());
    auto second_insert = engine.execute("INSERT INTO users VALUES (2, 'bob', FALSE)");
    ASSERT_TRUE(second_insert.has_value()) << engine.error_message();
    EXPECT_TRUE(second_insert->rows.empty());

    auto selected = engine.execute("SELECT id, name FROM users WHERE active = TRUE");
    ASSERT_TRUE(selected.has_value()) << engine.error_message();
    ASSERT_EQ(selected->rows.size(), std::size_t{1});
    ASSERT_EQ(selected->rows[0].values.size(), std::size_t{2});
    EXPECT_EQ(selected->rows[0].columns[0].name, std::string{"id"});
    EXPECT_EQ(selected->rows[0].columns[1].name, std::string{"name"});
    EXPECT_EQ(value_text(registry, selected->rows[0].values[0]), std::string{"1"});
    EXPECT_EQ(value_text(registry, selected->rows[0].values[1]), std::string{"alice"});
}

TEST_F(QueryEngineTest, ExecuteRejectsMultipleStatements) {
    QueryEngine engine{catalog, registry};

    auto result = engine.execute("CREATE TABLE users (id INTEGER); SELECT * FROM users");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::InvalidArgument);
    EXPECT_EQ(engine.error_message(), std::string_view{"execute expects exactly one SQL statement"});
}

TEST_F(QueryEngineTest, FailedStatementDoesNotExposePartialWrites) {
    QueryEngine engine{catalog, registry};

    auto create = engine.execute("CREATE TABLE events (id INTEGER, payload TEXT)");
    ASSERT_TRUE(create.has_value()) << engine.error_message();

    const auto oversized_payload = std::string(700, 'x');
    auto failed_insert = engine.execute("INSERT INTO events VALUES (1, 'ok'), (2, '" +
                                        oversized_payload + "')");
    ASSERT_FALSE(failed_insert.has_value());

    auto selected = engine.execute("SELECT * FROM events");
    ASSERT_TRUE(selected.has_value()) << engine.error_message();
    EXPECT_TRUE(selected->rows.empty());
}

TEST_F(QueryEngineTest, TwoThreadAutocommitInsertsAreVisible) {
    QueryEngine engine{catalog, registry};
    auto create = engine.execute("CREATE TABLE events (id INTEGER, payload TEXT)");
    ASSERT_TRUE(create.has_value()) << engine.error_message();

    std::mutex sql_latch;
    auto insert_rows = [&engine, &sql_latch](int base) {
        for (int index = 0; index < 10; ++index) {
            const auto id = base + index;
            std::scoped_lock lock{sql_latch};
            auto inserted = engine.execute("INSERT INTO events VALUES (" + std::to_string(id) +
                                           ", 'event')");
            ASSERT_TRUE(inserted.has_value()) << engine.error_message();
        }
    };

    std::thread first{insert_rows, 0};
    std::thread second{insert_rows, 100};
    first.join();
    second.join();

    auto selected = engine.execute("SELECT * FROM events");
    ASSERT_TRUE(selected.has_value()) << engine.error_message();
    EXPECT_EQ(selected->rows.size(), std::size_t{20});
}

}  // namespace