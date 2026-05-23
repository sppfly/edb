// tests/unit/query/test_logical_plan.cpp

#include "query/logical_plan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/binder.hpp"
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

auto parse_and_bind(std::string_view sql, Binder& binder) -> BoundStmt {
    Parser parser{sql};
    auto parsed = parser.parse();
    EXPECT_TRUE(parsed.has_value()) << parser.error_message();
    EXPECT_EQ(parsed->size(), std::size_t{1});

    auto bound = binder.bind(parsed->front());
    EXPECT_TRUE(bound.has_value()) << binder.error_message();
    return std::move(*bound);
}

class LogicalPlanTest : public ::testing::Test {
   protected:
    void SetUp() override {
        ASSERT_TRUE(register_builtin_types(registry).has_value());
        ASSERT_TRUE(catalog.open().has_value());
    }

    [[nodiscard]] static auto default_engine_config() -> EngineConfig {
        return EngineConfig{.page_size = usize{512}, .buffer_pool_pages = usize{4}};
    }

    auto create_users_table() -> void {
        const auto int32_type = registry.lookup("int32");
        const auto text_type = registry.lookup("text");
        const auto bool_type = registry.lookup("bool");
        ASSERT_TRUE(int32_type.has_value());
        ASSERT_TRUE(text_type.has_value());
        ASSERT_TRUE(bool_type.has_value());

        auto created = catalog.create_table(CreateTableSpec{
            .name = "users",
            .columns = {
                {.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                {.name = "active", .type_oid = (*bool_type)->oid, .nullable = b8{false}},
            },
        });
        ASSERT_TRUE(created.has_value());
    }

   public:
    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
    Catalog catalog{registry, factory, default_engine_config()};
};

TEST_F(LogicalPlanTest, LowerCreateTableToLogicalCreateTable) {
    Binder binder{catalog, registry};
    LogicalPlanner planner;

    auto bound = parse_and_bind("CREATE TABLE users (id INTEGER)", binder);
    auto plan = planner.build(std::move(bound));
    ASSERT_TRUE(plan.has_value()) << planner.error_message();
    ASSERT_TRUE(std::holds_alternative<LogicalCreateTable>(plan->node));

    const auto& logical = std::get<LogicalCreateTable>(plan->node);
    EXPECT_EQ(logical.stmt.table_name, std::string{"users"});
    ASSERT_EQ(logical.stmt.columns.size(), std::size_t{1});
}

TEST_F(LogicalPlanTest, LowerInsertToLogicalInsert) {
    create_users_table();
    Binder binder{catalog, registry};
    LogicalPlanner planner;

    auto bound = parse_and_bind("INSERT INTO users VALUES (1, 'alice', TRUE)", binder);
    auto plan = planner.build(std::move(bound));
    ASSERT_TRUE(plan.has_value()) << planner.error_message();
    ASSERT_TRUE(std::holds_alternative<LogicalInsert>(plan->node));

    const auto& logical = std::get<LogicalInsert>(plan->node);
    EXPECT_EQ(logical.stmt.table.name, std::string{"users"});
    ASSERT_EQ(logical.stmt.rows.size(), std::size_t{1});
}

TEST_F(LogicalPlanTest, LowerSelectWithoutWhereToProjectOverScan) {
    create_users_table();
    Binder binder{catalog, registry};
    LogicalPlanner planner;

    auto bound = parse_and_bind("SELECT id, name FROM users", binder);
    auto plan = planner.build(std::move(bound));
    ASSERT_TRUE(plan.has_value()) << planner.error_message();
    ASSERT_TRUE(std::holds_alternative<LogicalProject>(plan->node));

    const auto& project = std::get<LogicalProject>(plan->node);
    ASSERT_EQ(project.items.size(), std::size_t{2});
    ASSERT_TRUE(project.input != nullptr);
    ASSERT_TRUE(std::holds_alternative<LogicalScan>(project.input->node));
    const auto& scan = std::get<LogicalScan>(project.input->node);
    EXPECT_EQ(scan.table.name, std::string{"users"});
}

TEST_F(LogicalPlanTest, LowerSelectWithWhereToProjectOverFilterOverScan) {
    create_users_table();
    Binder binder{catalog, registry};
    LogicalPlanner planner;

    auto bound = parse_and_bind("SELECT id FROM users WHERE active = TRUE", binder);
    auto plan = planner.build(std::move(bound));
    ASSERT_TRUE(plan.has_value()) << planner.error_message();
    ASSERT_TRUE(std::holds_alternative<LogicalProject>(plan->node));

    const auto& project = std::get<LogicalProject>(plan->node);
    ASSERT_TRUE(project.input != nullptr);
    ASSERT_TRUE(std::holds_alternative<LogicalFilter>(project.input->node));
    const auto& filter = std::get<LogicalFilter>(project.input->node);
    ASSERT_TRUE(filter.input != nullptr);
    ASSERT_TRUE(std::holds_alternative<LogicalScan>(filter.input->node));
}

}  // namespace