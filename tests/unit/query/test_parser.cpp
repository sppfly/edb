// tests/unit/query/test_parser.cpp

#include <gtest/gtest.h>

#include <variant>

#include "query/ast.hpp"
#include "query/parser.hpp"

using namespace edb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static auto parse_one(std::string_view sql) -> Stmt {
    Parser p{sql};
    auto result = p.parse();
    EXPECT_TRUE(result.has_value()) << p.error_message();
    EXPECT_EQ(result->size(), 1U);
    return std::move(result->front());
}

// ---------------------------------------------------------------------------
// CREATE TABLE
// ---------------------------------------------------------------------------
TEST(Parser, CreateTableSimple) {
    auto stmt = parse_one("CREATE TABLE t (id INTEGER, name TEXT)");
    ASSERT_TRUE(std::holds_alternative<CreateTableStmt>(stmt));
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_EQ(ct.table_name, "t");
    EXPECT_EQ(ct.columns.size(), 2U);
    EXPECT_EQ(ct.columns[0].name, "id");
    EXPECT_EQ(ct.columns[0].type.name, "INTEGER");
    EXPECT_EQ(ct.columns[1].name, "name");
    EXPECT_EQ(ct.columns[1].type.name, "TEXT");
    EXPECT_FALSE(static_cast<bool>(ct.if_not_exists));
}

TEST(Parser, CreateTableIfNotExists) {
    auto stmt = parse_one("CREATE TABLE IF NOT EXISTS users (id INTEGER)");
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_TRUE(static_cast<bool>(ct.if_not_exists));
    EXPECT_EQ(ct.table_name, "users");
}

TEST(Parser, CreateTableNotNull) {
    auto stmt = parse_one("CREATE TABLE t (id INTEGER NOT NULL)");
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_TRUE(static_cast<bool>(ct.columns[0].not_null));
}

TEST(Parser, CreateTablePrimaryKey) {
    auto stmt = parse_one("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_TRUE(static_cast<bool>(ct.columns[0].primary_key));
    EXPECT_TRUE(static_cast<bool>(ct.columns[0].not_null));  // implied
}

TEST(Parser, CreateTableUnique) {
    auto stmt = parse_one("CREATE TABLE t (email TEXT UNIQUE)");
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_TRUE(static_cast<bool>(ct.columns[0].unique_constraint));
}

TEST(Parser, CreateTableVarcharParam) {
    auto stmt = parse_one("CREATE TABLE t (name VARCHAR(100))");
    auto& ct = std::get<CreateTableStmt>(stmt);
    ASSERT_TRUE(ct.columns[0].type.param.has_value());
    EXPECT_EQ(static_cast<uint32_t>(*ct.columns[0].type.param), 100U);
}

TEST(Parser, CreateTableTrailingSemicolon) {
    Parser p{"CREATE TABLE t (id INTEGER);"};
    auto result = p.parse();
    ASSERT_TRUE(result.has_value()) << p.error_message();
    EXPECT_EQ(result->size(), 1U);
}

TEST(Parser, CreateTableCaseInsensitive) {
    auto stmt = parse_one("create table T (id integer)");
    ASSERT_TRUE(std::holds_alternative<CreateTableStmt>(stmt));
    auto& ct = std::get<CreateTableStmt>(stmt);
    EXPECT_EQ(ct.table_name, "T");
}

// ---------------------------------------------------------------------------
// INSERT
// ---------------------------------------------------------------------------
TEST(Parser, InsertPositional) {
    auto stmt = parse_one("INSERT INTO t VALUES (1, 'alice', 99)");
    ASSERT_TRUE(std::holds_alternative<InsertStmt>(stmt));
    auto& ins = std::get<InsertStmt>(stmt);
    EXPECT_EQ(ins.table_name, "t");
    EXPECT_TRUE(ins.column_names.empty());
    ASSERT_EQ(ins.rows.size(), 1U);
    EXPECT_EQ(ins.rows[0].size(), 3U);
}

TEST(Parser, InsertWithColumnList) {
    auto stmt = parse_one("INSERT INTO t (id, name) VALUES (42, 'bob')");
    auto& ins = std::get<InsertStmt>(stmt);
    ASSERT_EQ(ins.column_names.size(), 2U);
    EXPECT_EQ(ins.column_names[0], "id");
    EXPECT_EQ(ins.column_names[1], "name");
}

TEST(Parser, InsertMultipleRows) {
    auto stmt = parse_one("INSERT INTO t VALUES (1), (2), (3)");
    auto& ins = std::get<InsertStmt>(stmt);
    EXPECT_EQ(ins.rows.size(), 3U);
}

TEST(Parser, InsertNullLiteral) {
    auto stmt = parse_one("INSERT INTO t VALUES (NULL)");
    auto& ins = std::get<InsertStmt>(stmt);
    auto& expr = ins.rows[0][0];
    ASSERT_TRUE(std::holds_alternative<Literal>(expr));
    EXPECT_TRUE(std::holds_alternative<NullLiteral>(std::get<Literal>(expr)));
}

TEST(Parser, InsertBoolLiterals) {
    auto stmt = parse_one("INSERT INTO t VALUES (TRUE, FALSE)");
    auto& ins = std::get<InsertStmt>(stmt);
    auto& e0 = ins.rows[0][0];
    auto& e1 = ins.rows[0][1];
    EXPECT_TRUE(std::holds_alternative<BoolLiteral>(std::get<Literal>(e0)));
    EXPECT_TRUE(std::holds_alternative<BoolLiteral>(std::get<Literal>(e1)));
    EXPECT_TRUE(static_cast<bool>(std::get<BoolLiteral>(std::get<Literal>(e0)).value));
    EXPECT_FALSE(static_cast<bool>(std::get<BoolLiteral>(std::get<Literal>(e1)).value));
}

TEST(Parser, InsertStringEscapedQuote) {
    auto stmt = parse_one("INSERT INTO t VALUES ('it''s fine')");
    auto& ins = std::get<InsertStmt>(stmt);
    auto& expr = ins.rows[0][0];
    auto& lit = std::get<StrLiteral>(std::get<Literal>(expr));
    EXPECT_EQ(lit.value, "it's fine");
}

// ---------------------------------------------------------------------------
// SELECT
// ---------------------------------------------------------------------------
TEST(Parser, SelectStar) {
    auto stmt = parse_one("SELECT * FROM users");
    ASSERT_TRUE(std::holds_alternative<SelectStmt>(stmt));
    auto& sel = std::get<SelectStmt>(stmt);
    EXPECT_EQ(sel.table_name, "users");
    ASSERT_EQ(sel.items.size(), 1U);
    EXPECT_TRUE(std::holds_alternative<StarItem>(sel.items[0]));
    EXPECT_FALSE(sel.where.has_value());
}

TEST(Parser, SelectColumns) {
    auto stmt = parse_one("SELECT id, name FROM users");
    auto& sel = std::get<SelectStmt>(stmt);
    ASSERT_EQ(sel.items.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<ExprItem>(sel.items[0]));
    auto& ei = std::get<ExprItem>(sel.items[0]);
    ASSERT_TRUE(std::holds_alternative<ColumnRef>(ei.expr));
    EXPECT_EQ(std::get<ColumnRef>(ei.expr).name, "id");
}

TEST(Parser, SelectAlias) {
    auto stmt = parse_one("SELECT id AS user_id FROM t");
    auto& sel = std::get<SelectStmt>(stmt);
    auto& ei = std::get<ExprItem>(sel.items[0]);
    ASSERT_TRUE(ei.alias.has_value());
    EXPECT_EQ(*ei.alias, "user_id");
}

TEST(Parser, SelectWhereEq) {
    auto stmt = parse_one("SELECT * FROM t WHERE id = 5");
    auto& sel = std::get<SelectStmt>(stmt);
    ASSERT_TRUE(sel.where.has_value());
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<BinaryExpr>>(*sel.where));
    auto& be = *std::get<std::unique_ptr<BinaryExpr>>(*sel.where);
    EXPECT_EQ(be.op, BinaryOp::Eq);
    ASSERT_TRUE(std::holds_alternative<ColumnRef>(be.left));
    EXPECT_EQ(std::get<ColumnRef>(be.left).name, "id");
    ASSERT_TRUE(std::holds_alternative<Literal>(be.right));
    auto& lit = std::get<Literal>(be.right);
    EXPECT_EQ(static_cast<int64_t>(std::get<IntLiteral>(lit).value), 5);
}

TEST(Parser, SelectWhereLt) {
    auto stmt = parse_one("SELECT * FROM t WHERE age < 18");
    auto& sel = std::get<SelectStmt>(stmt);
    auto& be = *std::get<std::unique_ptr<BinaryExpr>>(*sel.where);
    EXPECT_EQ(be.op, BinaryOp::Lt);
}

TEST(Parser, SelectWhereAndOr) {
    auto stmt = parse_one("SELECT * FROM t WHERE a = 1 AND b = 2 OR c = 3");
    auto& sel = std::get<SelectStmt>(stmt);
    ASSERT_TRUE(sel.where.has_value());
    // Top-level should be OR
    auto& top = *std::get<std::unique_ptr<BinaryExpr>>(*sel.where);
    EXPECT_EQ(top.op, BinaryOp::Or);
}

TEST(Parser, SelectWhereStringLiteral) {
    auto stmt = parse_one("SELECT * FROM t WHERE name = 'alice'");
    auto& sel = std::get<SelectStmt>(stmt);
    auto& be = *std::get<std::unique_ptr<BinaryExpr>>(*sel.where);
    auto& lit = std::get<Literal>(be.right);
    EXPECT_EQ(std::get<StrLiteral>(lit).value, "alice");
}

// ---------------------------------------------------------------------------
// Multiple statements
// ---------------------------------------------------------------------------
TEST(Parser, MultipleStatements) {
    Parser p{"SELECT * FROM a; SELECT * FROM b;"};
    auto result = p.parse();
    ASSERT_TRUE(result.has_value()) << p.error_message();
    EXPECT_EQ(result->size(), 2U);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------
TEST(Parser, ErrorMissingTableName) {
    Parser p{"CREATE TABLE"};
    EXPECT_FALSE(p.parse().has_value());
    EXPECT_FALSE(p.error_message().empty());
}

TEST(Parser, ErrorMissingFrom) {
    Parser p{"SELECT * users"};
    EXPECT_FALSE(p.parse().has_value());
}

TEST(Parser, ErrorUnexpectedToken) {
    Parser p{"DELETE FROM t"};
    EXPECT_FALSE(p.parse().has_value());
}

TEST(Parser, ErrorUnclosedParen) {
    Parser p{"SELECT * FROM t WHERE (id = 1"};
    EXPECT_FALSE(p.parse().has_value());
}
