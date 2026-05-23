// tests/unit/query/test_binder.cpp

#include "query/binder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

    auto create_users_table() -> u32 {
        const auto int32_type = registry.lookup("int32");
        const auto text_type = registry.lookup("text");
        const auto bool_type = registry.lookup("bool");
        EXPECT_TRUE(int32_type.has_value());
        EXPECT_TRUE(text_type.has_value());
        EXPECT_TRUE(bool_type.has_value());

        auto created = catalog.create_table(CreateTableSpec{
            .name = "users",
            .columns = {
                {.name = "id", .type_oid = (*int32_type)->oid, .nullable = b8{false}},
                {.name = "name", .type_oid = (*text_type)->oid, .nullable = b8{false}},
                {.name = "active", .type_oid = (*bool_type)->oid, .nullable = b8{false}},
            },
        });
        EXPECT_TRUE(created.has_value());
        return *created;
    }

   public:
    TypeRegistry registry;
    MemoryRelationBackendFactory factory;
    Catalog catalog{registry, factory, default_engine_config()};
};

TEST_F(BinderTest, BindCreateTableResolvesBuiltinTypesAndNullability) {
    auto stmt = parse_stmt(
        "CREATE TABLE users (id INTEGER NOT NULL, name TEXT, nick VARCHAR(100) UNIQUE)");

    Binder binder{catalog, registry};
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

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::TypeNotFound);
    EXPECT_NE(binder.error_message().find("unknown type"), std::string_view::npos);
}

TEST_F(BinderTest, BindCreateTableRejectsDuplicateColumns) {
    auto stmt = parse_stmt("CREATE TABLE t (id INTEGER, ID TEXT)");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("duplicate column"), std::string_view::npos);
}

TEST_F(BinderTest, BindInsertResolvesTargetColumnsAndLiteralTypes) {
    const auto users_oid = create_users_table();
    const auto int32_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(text_type.has_value());
    ASSERT_TRUE(bool_type.has_value());

    auto stmt = parse_stmt("INSERT INTO users VALUES (1, 'alice', TRUE)");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();
    ASSERT_TRUE(std::holds_alternative<BoundInsertStmt>(*bound));

    const auto& insert = std::get<BoundInsertStmt>(*bound);
    EXPECT_EQ(insert.table.relation_oid.value, users_oid.value);
    ASSERT_EQ(insert.columns.size(), std::size_t{3});
    EXPECT_EQ(insert.columns[0].name, std::string{"id"});
    EXPECT_EQ(insert.columns[1].name, std::string{"name"});
    EXPECT_EQ(insert.columns[2].name, std::string{"active"});
    ASSERT_EQ(insert.rows.size(), std::size_t{1});
    ASSERT_EQ(insert.rows[0].size(), std::size_t{3});
    EXPECT_EQ(insert.rows[0][0].type.oid.value, (*int32_type)->oid.value);
    EXPECT_EQ(insert.rows[0][1].type.oid.value, (*text_type)->oid.value);
    EXPECT_EQ(insert.rows[0][2].type.oid.value, (*bool_type)->oid.value);
    EXPECT_FALSE(static_cast<bool>(insert.rows[0][0].value.is_null));
}

TEST_F(BinderTest, BindInsertColumnListReordersTargets) {
    create_users_table();
    const auto int32_type = registry.lookup("int32");
    const auto text_type = registry.lookup("text");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(text_type.has_value());

    auto stmt = parse_stmt("INSERT INTO users (name, id) VALUES ('alice', 1)");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();

    const auto& insert = std::get<BoundInsertStmt>(*bound);
    ASSERT_EQ(insert.columns.size(), std::size_t{2});
    EXPECT_EQ(insert.columns[0].name, std::string{"name"});
    EXPECT_EQ(insert.columns[1].name, std::string{"id"});
    EXPECT_EQ(insert.rows[0][0].type.oid.value, (*text_type)->oid.value);
    EXPECT_EQ(insert.rows[0][1].type.oid.value, (*int32_type)->oid.value);
}

TEST_F(BinderTest, BindInsertRejectsUnknownColumn) {
    create_users_table();
    auto stmt = parse_stmt("INSERT INTO users (missing) VALUES (1)");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("unknown column"), std::string_view::npos);
}

TEST_F(BinderTest, BindInsertRejectsValueCountMismatch) {
    create_users_table();
    auto stmt = parse_stmt("INSERT INTO users VALUES (1, 'alice')");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("values"), std::string_view::npos);
}

TEST_F(BinderTest, BindInsertRejectsLiteralTypeMismatch) {
    create_users_table();
    auto stmt = parse_stmt("INSERT INTO users VALUES ('oops', 'alice', TRUE)");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("cannot coerce literal"), std::string_view::npos);
}

TEST_F(BinderTest, BindSelectResolvesProjectionAliasAndWhere) {
    create_users_table();
    const auto int32_type = registry.lookup("int32");
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(int32_type.has_value());
    ASSERT_TRUE(bool_type.has_value());

    auto stmt = parse_stmt("SELECT id, name AS user_name FROM users WHERE id = 5");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();
    ASSERT_TRUE(std::holds_alternative<BoundSelectStmt>(*bound));

    const auto& select = std::get<BoundSelectStmt>(*bound);
    EXPECT_EQ(select.table.name, std::string{"users"});
    ASSERT_EQ(select.table.columns.size(), std::size_t{3});
    ASSERT_EQ(select.items.size(), std::size_t{2});

    const auto& first_item = std::get<BoundExprItem>(select.items[0]);
    ASSERT_TRUE(std::holds_alternative<BoundColumnRef>(first_item.expr));
    EXPECT_EQ(std::get<BoundColumnRef>(first_item.expr).attnum.value, u32{1}.value);

    const auto& second_item = std::get<BoundExprItem>(select.items[1]);
    ASSERT_TRUE(second_item.alias.has_value());
    EXPECT_EQ(*second_item.alias, std::string{"user_name"});

    ASSERT_TRUE(select.where.has_value());
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundBinaryExpr>>(*select.where));
    const auto& where_expr = *std::get<std::unique_ptr<BoundBinaryExpr>>(*select.where);
    EXPECT_EQ(where_expr.type.oid.value, (*bool_type)->oid.value);
    ASSERT_TRUE(std::holds_alternative<BoundLiteral>(where_expr.right));
    EXPECT_EQ(std::get<BoundLiteral>(where_expr.right).type.oid.value, (*int32_type)->oid.value);
}

TEST_F(BinderTest, BindSelectStarResolvesTableSchema) {
    create_users_table();
    auto stmt = parse_stmt("SELECT * FROM users");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();

    const auto& select = std::get<BoundSelectStmt>(*bound);
    ASSERT_EQ(select.items.size(), std::size_t{1});
    EXPECT_TRUE(std::holds_alternative<BoundStarItem>(select.items[0]));
    EXPECT_EQ(select.table.columns.size(), std::size_t{3});
}

TEST_F(BinderTest, BindSelectRejectsUnknownTable) {
    auto stmt = parse_stmt("SELECT * FROM missing");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("unknown table"), std::string_view::npos);
}

TEST_F(BinderTest, BindSelectRejectsUnknownColumn) {
    create_users_table();
    auto stmt = parse_stmt("SELECT missing FROM users");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("unknown column"), std::string_view::npos);
}

TEST_F(BinderTest, BindSelectRejectsTypeMismatchInWhere) {
    create_users_table();
    auto stmt = parse_stmt("SELECT * FROM users WHERE active = 1");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_FALSE(bound.has_value());
    EXPECT_EQ(bound.error(), Error::AnalyzerError);
    EXPECT_NE(binder.error_message().find("cannot coerce literal"), std::string_view::npos);
}

TEST_F(BinderTest, BindSelectBindsBooleanConjunctions) {
    create_users_table();
    const auto bool_type = registry.lookup("bool");
    ASSERT_TRUE(bool_type.has_value());
    auto stmt = parse_stmt("SELECT * FROM users WHERE id = 1 AND active = TRUE");

    Binder binder{catalog, registry};
    auto bound = binder.bind(stmt);
    ASSERT_TRUE(bound.has_value()) << binder.error_message();

    const auto& select = std::get<BoundSelectStmt>(*bound);
    ASSERT_TRUE(select.where.has_value());
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BoundBinaryExpr>>(*select.where));
    const auto& top = *std::get<std::unique_ptr<BoundBinaryExpr>>(*select.where);
    EXPECT_EQ(top.op, BinaryOp::And);
    EXPECT_EQ(top.type.oid.value, (*bool_type)->oid.value);
}

}  // namespace
