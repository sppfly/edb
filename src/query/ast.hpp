#pragma once

// src/query/ast.hpp
//
// AST node types for the EDB SQL front-end.
//
// All string fields are owning std::string (the AST outlives the source text).
// BinaryExpr is heap-allocated via unique_ptr to break the recursive type cycle.
//
// Stmt is move-only because Expr (via unique_ptr<BinaryExpr>) is move-only.
// Callers should std::move stmts out of the parser result vector.
//
// Thread-safety: AST nodes are value types; no shared state.

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "utils/primitives.hpp"

namespace edb {

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// Literal values
struct IntLiteral {
    i64 value;
};
struct FloatLiteral {
    f64 value;
};
struct StrLiteral {
    std::string value;
};
struct NullLiteral {};
struct BoolLiteral {
    b8 value;
};

using Literal = std::variant<IntLiteral, FloatLiteral, StrLiteral, NullLiteral, BoolLiteral>;

// Column reference (unqualified name; binder resolves to column OID later)
struct ColumnRef {
    std::string name;
};

// Binary operators (comparison + logical)
enum class BinaryOp : uint8_t {  // raw-primitive: enum base
    Eq,                          // =
    Neq,                         // <>
    Lt,                          // <
    Le,                          // <=
    Gt,                          // >
    Ge,                          // >=
    And,                         // AND
    Or,                          // OR
};

// Forward-declared for recursive Expr variant.
struct BinaryExpr;

// Expr is move-only due to unique_ptr<BinaryExpr>.
using Expr = std::variant<Literal, ColumnRef, std::unique_ptr<BinaryExpr>>;

struct BinaryExpr {
    BinaryOp op{};
    Expr left;
    Expr right;
};

// ---------------------------------------------------------------------------
// Type name (in CREATE TABLE column definitions)
//   e.g.  INTEGER  /  VARCHAR(255)  /  DOUBLE PRECISION
// ---------------------------------------------------------------------------
struct TypeName {
    std::string name;          // e.g. "INTEGER", "VARCHAR", "TEXT"
    std::optional<u32> param;  // size/precision where applicable
};

// ---------------------------------------------------------------------------
// Column definition (in CREATE TABLE)
// ---------------------------------------------------------------------------
struct ColumnDef {
    std::string name;
    TypeName type;
    b8 not_null{b8{false}};
    b8 primary_key{b8{false}};
    b8 unique_constraint{b8{false}};
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// CREATE TABLE [IF NOT EXISTS] <name> ( <cols> )
struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
    b8 if_not_exists{b8{false}};
};

// INSERT INTO <name> [( <cols> )] VALUES ( <vals> ) [, ( <vals> ) ]*
struct InsertStmt {
    std::string table_name;
    std::vector<std::string> column_names;  // empty = positional
    std::vector<std::vector<Expr>> rows;
};

// SELECT item: either * or an expression with an optional alias
struct StarItem {};
struct ExprItem {
    Expr expr;
    std::optional<std::string> alias;
};
using SelectItem = std::variant<StarItem, ExprItem>;

// SELECT <items> FROM <table> [WHERE <expr>]
struct SelectStmt {
    std::vector<SelectItem> items;
    std::string table_name;
    std::optional<Expr> where;
};

// Top-level statement union
using Stmt = std::variant<CreateTableStmt, InsertStmt, SelectStmt>;

}  // namespace edb
