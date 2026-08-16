// tests/unit/query/test_exec.cpp

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/binder.hpp"
#include "query/exec.hpp"
#include "query/logical_plan.hpp"
#include "query/parser.hpp"
#include "query/physical_plan.hpp"
#include "storage/io/io_ops.hpp"
#include "types/builtin_types.hpp"

using namespace edb;

namespace {

class SharedMemoryIO final : public StorageIO {
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
        -> Result<std::unique_ptr<StorageIO>> override {
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

auto bind_sql(std::string_view sql, Catalog& catalog, const TypeRegistry& registry) -> BoundStmt {
    Parser parser{sql};
    auto parsed = parser.parse();
    EXPECT_TRUE(parsed.has_value()) << parser.error_message();
    EXPECT_EQ(parsed->size(), std::size_t{1});

    Binder binder{catalog, registry};
    auto bound = binder.bind(parsed->front());
    EXPECT_TRUE(bound.has_value()) << binder.error_message();
    return std::move(*bound);
}

auto build_physical_plan(BoundStmt bound) -> PhysicalPlan {
    LogicalPlanner logical_planner;
    auto logical = logical_planner.build(std::move(bound));
    EXPECT_TRUE(logical.has_value()) << logical_planner.error_message();

    PhysicalPlanner physical_planner;
    auto physical = physical_planner.build(std::move(*logical));
    EXPECT_TRUE(physical.has_value()) << physical_planner.error_message();
    return std::move(*physical);
}

auto build_exec_node(PhysicalPlan physical, Catalog& catalog, const TypeRegistry& registry)
    -> std::unique_ptr<ExecNode> {
    ExecBuilder exec_builder{catalog, registry};
    auto exec = exec_builder.build(std::move(physical));
    EXPECT_TRUE(exec.has_value()) << exec_builder.error_message();
    return std::move(*exec);
}

auto drain_exec_node(ExecNode& exec) -> std::vector<ExecRow> {
    EXPECT_TRUE(exec.open().has_value());
    std::vector<ExecRow> rows;
    while (true) {
        ExecRow row;
        auto next = exec.next(row);
        EXPECT_TRUE(next.has_value());
        if (!static_cast<bool>(*next)) {
            break;
        }
        rows.push_back(std::move(row));
    }
    EXPECT_TRUE(exec.close().has_value());
    return rows;
}

auto run_sql(std::string_view sql, Catalog& catalog, const TypeRegistry& registry)
    -> std::vector<ExecRow> {
    auto bound = bind_sql(sql, catalog, registry);
    auto physical = build_physical_plan(std::move(bound));
    auto exec = build_exec_node(std::move(physical), catalog, registry);
    return drain_exec_node(*exec);
}

class ExecTest : public ::testing::Test {
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

TEST_F(ExecTest, ExecuteCreateTableCreatesCatalogEntry) {
    const auto rows =
        run_sql("CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN)", catalog, registry);
    EXPECT_TRUE(rows.empty());

    auto klass = catalog.get_class("users");
    ASSERT_TRUE(klass.has_value());
    auto attrs = catalog.get_attributes(klass->oid);
    ASSERT_TRUE(attrs.has_value());
    EXPECT_EQ(attrs->size(), std::size_t{3});
}

TEST_F(ExecTest, ExecuteInsertWritesRowsIntoTable) {
    EXPECT_TRUE(
        run_sql("CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN)", catalog, registry)
            .empty());

    const auto rows = run_sql("INSERT INTO users VALUES (1, 'alice', TRUE)", catalog, registry);
    EXPECT_TRUE(rows.empty());

    auto table = catalog.open_table("users");
    ASSERT_TRUE(table.has_value());
    auto stored = (*table)->scan();
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), std::size_t{1});
    EXPECT_EQ(value_text(registry, (*stored)[0][0]), std::string{"1"});
    EXPECT_EQ(value_text(registry, (*stored)[0][1]), std::string{"alice"});
    EXPECT_EQ(value_text(registry, (*stored)[0][2]), std::string{"true"});
}

TEST_F(ExecTest, ExecuteSelectReturnsProjectedRows) {
    EXPECT_TRUE(
        run_sql("CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN)", catalog, registry)
            .empty());
    EXPECT_TRUE(run_sql("INSERT INTO users VALUES (1, 'alice', TRUE)", catalog, registry).empty());
    EXPECT_TRUE(run_sql("INSERT INTO users VALUES (2, 'bob', FALSE)", catalog, registry).empty());

    const auto rows = run_sql("SELECT id, name FROM users WHERE active = TRUE", catalog, registry);
    ASSERT_EQ(rows.size(), std::size_t{1});
    ASSERT_EQ(rows[0].values.size(), std::size_t{2});
    EXPECT_EQ(rows[0].columns[0].name, std::string{"id"});
    EXPECT_EQ(rows[0].columns[1].name, std::string{"name"});
    EXPECT_EQ(value_text(registry, rows[0].values[0]), std::string{"1"});
    EXPECT_EQ(value_text(registry, rows[0].values[1]), std::string{"alice"});
}

TEST_F(ExecTest, ExecuteSelectStarReturnsAllColumns) {
    EXPECT_TRUE(
        run_sql("CREATE TABLE users (id INTEGER, name TEXT, active BOOLEAN)", catalog, registry)
            .empty());
    EXPECT_TRUE(run_sql("INSERT INTO users VALUES (1, 'alice', TRUE)", catalog, registry).empty());

    const auto rows = run_sql("SELECT * FROM users", catalog, registry);
    ASSERT_EQ(rows.size(), std::size_t{1});
    ASSERT_EQ(rows[0].values.size(), std::size_t{3});
    EXPECT_EQ(value_text(registry, rows[0].values[0]), std::string{"1"});
    EXPECT_EQ(value_text(registry, rows[0].values[1]), std::string{"alice"});
    EXPECT_EQ(value_text(registry, rows[0].values[2]), std::string{"true"});
}

}  // namespace