#pragma once

// src/query/parser.hpp
//
// Parser — recursive-descent SQL parser.
//
// Produces a list of Stmt from a SQL source string.
// Errors are returned via EdbResult; the last error message is available
// through error_message() for human-readable diagnostics.
//
// Grammar subset (Phase 5a):
//   stmt      ::= create_table | insert | select
//   create_table ::= CREATE TABLE [IF NOT EXISTS] name ( col_defs )
//   insert    ::= INSERT INTO name [( names )] VALUES ( exprs ) [, ( exprs )]*
//   select    ::= SELECT items FROM name [WHERE expr]
//   items     ::= * | expr [AS name] {, expr [AS name]}
//   expr      ::= or_expr
//   or_expr   ::= and_expr { OR and_expr }
//   and_expr  ::= not_expr { AND not_expr }
//   not_expr  ::= [NOT] cmp_expr
//   cmp_expr  ::= primary [( = | <> | < | <= | > | >= ) primary]
//   primary   ::= literal | column_ref | ( expr )
//
// Thread-safety: not thread-safe; use one Parser per query string.

#include <string>
#include <vector>

#include "query/ast.hpp"
#include "query/lexer.hpp"
#include "query/token.hpp"
#include "utils/error.hpp"

namespace edb {

class Parser {
public:
    explicit Parser(std::string_view src) noexcept;

    // Parse all statements separated by optional semicolons.
    // Returns a non-empty vector on success.
    [[nodiscard]] auto parse() -> Result<std::vector<Stmt>>;

    // Human-readable description of the last parse error.
    [[nodiscard]] auto error_message() const noexcept -> std::string_view;

private:
    Lexer lexer;
    Token cur{};
    std::string last_error;

    // Token control
    auto advance() -> void;
    [[nodiscard]] auto check(TokenKind k) const noexcept -> bool;
    auto match(TokenKind k) -> bool;
    [[nodiscard]] auto expect(TokenKind k) -> Result<Token>;
    [[nodiscard]] auto expect_identifier() -> Result<std::string>;

    // Error helpers
    auto parse_err(std::string msg) -> Error;

    // Statement parsers
    [[nodiscard]] auto parse_stmt() -> Result<Stmt>;
    [[nodiscard]] auto parse_create_table() -> Result<CreateTableStmt>;
    [[nodiscard]] auto parse_insert() -> Result<InsertStmt>;
    [[nodiscard]] auto parse_select() -> Result<SelectStmt>;

    // Sub-parsers
    [[nodiscard]] auto parse_col_def() -> Result<ColumnDef>;
    [[nodiscard]] auto parse_type_name() -> Result<TypeName>;
    [[nodiscard]] auto parse_select_items() -> Result<std::vector<SelectItem>>;
    [[nodiscard]] auto parse_expr_list() -> Result<std::vector<Expr>>;

    // Expression parsers (precedence climbing)
    [[nodiscard]] auto parse_expr() -> Result<Expr>;
    [[nodiscard]] auto parse_or() -> Result<Expr>;
    [[nodiscard]] auto parse_and() -> Result<Expr>;
    [[nodiscard]] auto parse_not() -> Result<Expr>;
    [[nodiscard]] auto parse_cmp() -> Result<Expr>;
    [[nodiscard]] auto parse_primary() -> Result<Expr>;
};

}  // namespace edb
