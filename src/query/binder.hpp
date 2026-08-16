#pragma once

// src/query/binder.hpp
//
// Binder -- resolves parser AST nodes against catalog metadata and type names.
//
// Phase 5b starts with CREATE TABLE binding only. The public BoundStmt variant is
// intentionally small so later SELECT/INSERT binding can extend it without
// reshaping the entry point.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "catalog/catalog.hpp"
#include "query/ast.hpp"
#include "utils/error.hpp"

namespace edb {

struct BoundTypeRef {
    u32 oid{0};
    std::string name;
    std::optional<u32> param;
};

struct BoundColumnDef {
    std::string name;
    BoundTypeRef type;
    b8 nullable{b8{true}};
    b8 primary_key{b8{false}};
    b8 unique_constraint{b8{false}};
};

struct BoundLiteral {
    BoundTypeRef type;
    Value value;
};

struct BoundColumnRef {
    u32 relation_oid{0};
    u32 attnum{0};
    std::string name;
    BoundTypeRef type;
    b8 nullable{b8{false}};
};

struct BoundBinaryExpr;

using BoundExpr = std::variant<BoundLiteral, BoundColumnRef, std::unique_ptr<BoundBinaryExpr>>;

struct BoundBinaryExpr {
    BinaryOp op{};
    BoundExpr left;
    BoundExpr right;
    BoundTypeRef type;
};

struct BoundTableRef {
    u32 relation_oid{0};
    std::string name;
    std::vector<BoundColumnRef> columns;
};

struct BoundExprItem {
    BoundExpr expr;
    std::optional<std::string> alias;
};

struct BoundStarItem {};

using BoundSelectItem = std::variant<BoundStarItem, BoundExprItem>;

struct BoundCreateTableStmt {
    std::string table_name;
    std::vector<BoundColumnDef> columns;
    b8 if_not_exists{b8{false}};
};

struct BoundInsertStmt {
    BoundTableRef table;
    std::vector<BoundColumnRef> columns;
    std::vector<std::vector<BoundLiteral>> rows;
};

struct BoundSelectStmt {
    BoundTableRef table;
    std::vector<BoundSelectItem> items;
    std::optional<BoundExpr> where;
};

using BoundStmt = std::variant<BoundCreateTableStmt, BoundInsertStmt, BoundSelectStmt>;

class Binder {
public:
    Binder(Catalog& catalog, const TypeRegistry& types) noexcept;

    [[nodiscard]] auto bind(const Stmt& stmt) -> Result<BoundStmt>;
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

private:
    [[nodiscard]] auto bind_create_table(const CreateTableStmt& stmt)
        -> Result<BoundCreateTableStmt>;
    [[nodiscard]] auto bind_insert(const InsertStmt& stmt) -> Result<BoundInsertStmt>;
    [[nodiscard]] auto bind_select(const SelectStmt& stmt) -> Result<BoundSelectStmt>;
    [[nodiscard]] auto bind_table(std::string_view table_name) -> Result<BoundTableRef>;
    [[nodiscard]] auto bind_column_ref(const ColumnRef& ref, const BoundTableRef& table)
        -> Result<BoundColumnRef>;
    [[nodiscard]] auto infer_literal_type(const Literal& literal) -> Result<BoundTypeRef>;
    [[nodiscard]] auto coerce_literal_to_type(const Literal& literal,
                                              const BoundTypeRef& target_type, b8 target_nullable)
        -> Result<BoundLiteral>;
    [[nodiscard]] auto bind_literal(const Literal& literal, const BoundTypeRef* target_type,
                                    b8 target_nullable) -> Result<BoundLiteral>;
    [[nodiscard]] auto bind_expr(const Expr& expr, const BoundTableRef& table,
                                 const BoundTypeRef* target_type = nullptr) -> Result<BoundExpr>;
    [[nodiscard]] auto bind_binary_expr(const BinaryExpr& expr, const BoundTableRef& table)
        -> Result<BoundExpr>;
    [[nodiscard]] auto lookup_type(std::string_view name) const -> Result<BoundTypeRef>;
    [[nodiscard]] auto lookup_type(u32 oid) const -> Result<BoundTypeRef>;
    auto bind_err(std::string msg) -> void;

    Catalog* catalog{nullptr};
    const TypeRegistry* types{nullptr};
    std::string last_error;
};

}  // namespace edb
