// src/query/parser.cpp

#include "query/parser.hpp"

#include <charconv>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace edb {

// ---------------------------------------------------------------------------
// Constructor / token control
// ---------------------------------------------------------------------------

Parser::Parser(std::string_view src) noexcept : lexer{src} {
    advance();  // prime cur with the first token
}

auto Parser::advance() -> void { cur = lexer.next(); }

auto Parser::check(TokenKind k) const noexcept -> bool { return cur.kind == k; }

auto Parser::match(TokenKind k) -> bool {
    if (check(k)) {
        advance();
        return true;
    }
    return false;
}

auto Parser::expect(TokenKind k) -> Result<Token> {
    if (!check(k)) {
        parse_err(std::format("expected {} but got '{}' at line {}:{}",
                              token_kind_name(k), cur.text,
                              static_cast<uint32_t>(cur.line),   // raw-primitive: format arg
                              static_cast<uint32_t>(cur.col)));  // raw-primitive: format arg
        return std::unexpected(Error::ParseError);
    }
    Token t = cur;
    advance();
    return t;
}

auto Parser::expect_identifier() -> Result<std::string> {
    if (cur.kind != TokenKind::Identifier) {
        parse_err(std::format("expected identifier but got '{}' at line {}:{}",
                              cur.text,
                              static_cast<uint32_t>(cur.line),   // raw-primitive
                              static_cast<uint32_t>(cur.col)));  // raw-primitive
        return std::unexpected(Error::ParseError);
    }
    std::string name{cur.text};
    advance();
    return name;
}

auto Parser::parse_err(std::string msg) -> Error {
    last_error = std::move(msg);
    return Error::ParseError;
}

auto Parser::error_message() const noexcept -> std::string_view { return last_error; }

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

auto Parser::parse() -> Result<std::vector<Stmt>> {
    std::vector<Stmt> stmts;
    while (!check(TokenKind::Eof)) {
        match(TokenKind::Semicolon);  // allow leading/separating semicolons
        if (check(TokenKind::Eof)) {
            break;
        }
        auto stmt = parse_stmt();
        if (!stmt) {
            return std::unexpected(stmt.error());
        }
        stmts.push_back(std::move(*stmt));
        match(TokenKind::Semicolon);  // optional trailing semicolon
    }
    return stmts;
}

auto Parser::parse_stmt() -> Result<Stmt> {
    if (check(TokenKind::KwCreate)) {
        advance();
        auto s = parse_create_table();
        if (!s) {
            return std::unexpected(s.error());
        }
        return Stmt{std::move(*s)};
    }
    if (check(TokenKind::KwInsert)) {
        advance();
        auto s = parse_insert();
        if (!s) {
            return std::unexpected(s.error());
        }
        return Stmt{std::move(*s)};
    }
    if (check(TokenKind::KwSelect)) {
        advance();
        auto s = parse_select();
        if (!s) {
            return std::unexpected(s.error());
        }
        return Stmt{std::move(*s)};
    }
    parse_err(std::format(
        "unexpected token '{}' at line {}:{} \xe2\x80\x94 expected CREATE, INSERT, or SELECT",
        cur.text,
        static_cast<uint32_t>(cur.line),   // raw-primitive
        static_cast<uint32_t>(cur.col)));  // raw-primitive
    return std::unexpected(Error::ParseError);
}

// ---------------------------------------------------------------------------
// CREATE TABLE [IF NOT EXISTS] name ( col_def {, col_def} )
// ---------------------------------------------------------------------------

auto Parser::parse_create_table() -> Result<CreateTableStmt> {
    auto res = expect(TokenKind::KwTable);
    if (!res) {
        return std::unexpected(res.error());
    }

    b8 if_not_exists{false};
    if (match(TokenKind::KwIf)) {
        auto r1 = expect(TokenKind::KwNot);
        if (!r1) {
            return std::unexpected(r1.error());
        }
        auto r2 = expect(TokenKind::KwExists);
        if (!r2) {
            return std::unexpected(r2.error());
        }
        if_not_exists = b8{true};
    }

    auto name = expect_identifier();
    if (!name) {
        return std::unexpected(name.error());
    }

    auto lp = expect(TokenKind::LParen);
    if (!lp) {
        return std::unexpected(lp.error());
    }

    std::vector<ColumnDef> cols;
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        auto col = parse_col_def();
        if (!col) {
            return std::unexpected(col.error());
        }
        cols.push_back(std::move(*col));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }

    auto rp = expect(TokenKind::RParen);
    if (!rp) {
        return std::unexpected(rp.error());
    }

    return CreateTableStmt{
        .table_name    = std::move(*name),
        .columns       = std::move(cols),
        .if_not_exists = if_not_exists,
    };
}

auto Parser::parse_col_def() -> Result<ColumnDef> {
    auto col_name = expect_identifier();
    if (!col_name) {
        return std::unexpected(col_name.error());
    }

    auto type = parse_type_name();
    if (!type) {
        return std::unexpected(type.error());
    }

    ColumnDef col;
    col.name = std::move(*col_name);
    col.type = std::move(*type);

    bool parsing_constraints = true;
    while (parsing_constraints) {
        if (match(TokenKind::KwNot)) {
            auto r = expect(TokenKind::KwNull);
            if (!r) {
                return std::unexpected(r.error());
            }
            col.not_null = b8{true};
        } else if (match(TokenKind::KwNull)) {
            col.not_null = b8{false};
        } else if (match(TokenKind::KwPrimary)) {
            auto r = expect(TokenKind::KwKey);
            if (!r) {
                return std::unexpected(r.error());
            }
            col.primary_key = b8{true};
            col.not_null    = b8{true};  // PRIMARY KEY implies NOT NULL
        } else if (match(TokenKind::KwUnique)) {
            col.unique_constraint = b8{true};
        } else {
            parsing_constraints = false;
        }
    }

    return col;
}

auto Parser::parse_type_name() -> Result<TypeName> {
    auto name = expect_identifier();
    if (!name) {
        return std::unexpected(name.error());
    }

    // Handle two-word types: DOUBLE PRECISION (case-insensitive second word)
    if (*name == "double" || *name == "DOUBLE") {
        if (check(TokenKind::Identifier) &&
            (cur.text == "precision" || cur.text == "PRECISION")) {
            advance();
            return TypeName{.name = "double precision", .param = std::nullopt};
        }
    }

    std::optional<u32> param;
    if (match(TokenKind::LParen)) {
        if (!check(TokenKind::LitInteger)) {
            parse_err("expected integer parameter in type name");
            return std::unexpected(Error::ParseError);
        }
        std::string_view num_text = cur.text;
        u32 val{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) — from_chars requires raw pointer range
        auto [ptr, ec] = std::from_chars(num_text.data(), num_text.data() + num_text.size(),
                                         val.value);
        if (ec != std::errc{}) {
            parse_err(std::format("invalid type parameter '{}'", num_text));
            return std::unexpected(Error::ParseError);
        }
        param = val;
        advance();
        auto rp = expect(TokenKind::RParen);
        if (!rp) {
            return std::unexpected(rp.error());
        }
    }

    return TypeName{.name = std::move(*name), .param = param};
}

// ---------------------------------------------------------------------------
// INSERT INTO name [( col_names )] VALUES ( exprs ) [, ( exprs )]*
// ---------------------------------------------------------------------------

auto Parser::parse_insert() -> Result<InsertStmt> {
    auto r = expect(TokenKind::KwInto);
    if (!r) {
        return std::unexpected(r.error());
    }

    auto name = expect_identifier();
    if (!name) {
        return std::unexpected(name.error());
    }

    // Optional column list
    std::vector<std::string> col_names;
    if (match(TokenKind::LParen)) {
        while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
            auto col = expect_identifier();
            if (!col) {
                return std::unexpected(col.error());
            }
            col_names.push_back(std::move(*col));
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        auto rp = expect(TokenKind::RParen);
        if (!rp) {
            return std::unexpected(rp.error());
        }
    }

    auto rv = expect(TokenKind::KwValues);
    if (!rv) {
        return std::unexpected(rv.error());
    }

    std::vector<std::vector<Expr>> rows;
    while (true) {
        auto lp = expect(TokenKind::LParen);
        if (!lp) {
            return std::unexpected(lp.error());
        }
        auto exprs = parse_expr_list();
        if (!exprs) {
            return std::unexpected(exprs.error());
        }
        auto rp = expect(TokenKind::RParen);
        if (!rp) {
            return std::unexpected(rp.error());
        }
        rows.push_back(std::move(*exprs));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }

    return InsertStmt{
        .table_name   = std::move(*name),
        .column_names = std::move(col_names),
        .rows         = std::move(rows),
    };
}

// ---------------------------------------------------------------------------
// SELECT items FROM name [WHERE expr]
// ---------------------------------------------------------------------------

auto Parser::parse_select() -> Result<SelectStmt> {
    auto items = parse_select_items();
    if (!items) {
        return std::unexpected(items.error());
    }

    auto rf = expect(TokenKind::KwFrom);
    if (!rf) {
        return std::unexpected(rf.error());
    }

    auto table = expect_identifier();
    if (!table) {
        return std::unexpected(table.error());
    }

    std::optional<Expr> where;
    if (match(TokenKind::KwWhere)) {
        auto e = parse_expr();
        if (!e) {
            return std::unexpected(e.error());
        }
        where = std::move(*e);
    }

    return SelectStmt{
        .items      = std::move(*items),
        .table_name = std::move(*table),
        .where      = std::move(where),
    };
}

auto Parser::parse_select_items() -> Result<std::vector<SelectItem>> {
    std::vector<SelectItem> items;
    while (true) {
        if (match(TokenKind::Star)) {
            items.emplace_back(StarItem{});
        } else {
            auto e = parse_expr();
            if (!e) {
                return std::unexpected(e.error());
            }
            std::optional<std::string> alias;
            if (match(TokenKind::KwAs)) {
                auto a = expect_identifier();
                if (!a) {
                    return std::unexpected(a.error());
                }
                alias = std::move(*a);
            }
            items.emplace_back(ExprItem{.expr = std::move(*e), .alias = std::move(alias)});
        }
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    return items;
}

auto Parser::parse_expr_list() -> Result<std::vector<Expr>> {
    std::vector<Expr> exprs;
    while (!check(TokenKind::RParen) && !check(TokenKind::Eof)) {
        auto e = parse_expr();
        if (!e) {
            return std::unexpected(e.error());
        }
        exprs.push_back(std::move(*e));
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    return exprs;
}

// ---------------------------------------------------------------------------
// Expression grammar (precedence climbing)
// ---------------------------------------------------------------------------

auto Parser::parse_expr() -> Result<Expr> { return parse_or(); }

auto Parser::parse_or() -> Result<Expr> {
    auto left = parse_and();
    if (!left) {
        return left;
    }
    while (match(TokenKind::KwOr)) {
        auto right = parse_and();
        if (!right) {
            return right;
        }
        auto node = std::make_unique<BinaryExpr>(BinaryExpr{
            .op    = BinaryOp::Or,
            .left  = std::move(*left),
            .right = std::move(*right),
        });
        left = Expr{std::move(node)};
    }
    return left;
}

auto Parser::parse_and() -> Result<Expr> {
    auto left = parse_not();
    if (!left) {
        return left;
    }
    while (match(TokenKind::KwAnd)) {
        auto right = parse_not();
        if (!right) {
            return right;
        }
        auto node = std::make_unique<BinaryExpr>(BinaryExpr{
            .op    = BinaryOp::And,
            .left  = std::move(*left),
            .right = std::move(*right),
        });
        left = Expr{std::move(node)};
    }
    return left;
}

auto Parser::parse_not() -> Result<Expr> {
    if (match(TokenKind::KwNot)) {
        // NOT support deferred to Phase 5b (requires UnaryExpr AST node).
        parse_err("NOT is not yet supported in WHERE expressions");
        return std::unexpected(Error::ParseError);
    }
    return parse_cmp();
}

auto Parser::parse_cmp() -> Result<Expr> {
    auto left = parse_primary();
    if (!left) {
        return left;
    }

    BinaryOp op{};
    bool is_cmp = true;
    if (match(TokenKind::Eq)) {
        op = BinaryOp::Eq;
    } else if (match(TokenKind::Neq)) {
        op = BinaryOp::Neq;
    } else if (match(TokenKind::Lt)) {
        op = BinaryOp::Lt;
    } else if (match(TokenKind::Le)) {
        op = BinaryOp::Le;
    } else if (match(TokenKind::Gt)) {
        op = BinaryOp::Gt;
    } else if (match(TokenKind::Ge)) {
        op = BinaryOp::Ge;
    } else {
        is_cmp = false;
    }

    if (is_cmp) {
        auto right = parse_primary();
        if (!right) {
            return right;
        }
        auto node = std::make_unique<BinaryExpr>(BinaryExpr{
            .op    = op,
            .left  = std::move(*left),
            .right = std::move(*right),
        });
        return Expr{std::move(node)};
    }
    return left;
}

auto Parser::parse_primary() -> Result<Expr> {
    // Parenthesised expression
    if (match(TokenKind::LParen)) {
        auto inner = parse_expr();
        if (!inner) {
            return inner;
        }
        auto rp = expect(TokenKind::RParen);
        if (!rp) {
            return std::unexpected(rp.error());
        }
        return inner;
    }

    if (match(TokenKind::KwNull)) {
        return Expr{Literal{NullLiteral{}}};
    }

    if (match(TokenKind::KwTrue)) {
        return Expr{Literal{BoolLiteral{b8{true}}}};
    }
    if (match(TokenKind::KwFalse)) {
        return Expr{Literal{BoolLiteral{b8{false}}}};
    }

    // Integer literal
    if (check(TokenKind::LitInteger)) {
        std::string_view text = cur.text;
        i64 val{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) — from_chars requires raw pointer range
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), val.value);
        if (ec != std::errc{}) {
            parse_err(std::format("invalid integer literal '{}'", text));
            return std::unexpected(Error::ParseError);
        }
        advance();
        return Expr{Literal{IntLiteral{val}}};
    }

    // Float literal
    if (check(TokenKind::LitFloat)) {
        std::string_view text = cur.text;
        f64 val{};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) — from_chars requires raw pointer range
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), val.value);
        if (ec != std::errc{}) {
            parse_err(std::format("invalid float literal '{}'", text));
            return std::unexpected(Error::ParseError);
        }
        advance();
        return Expr{Literal{FloatLiteral{val}}};
    }

    // String literal  'content'  (may contain '' escapes)
    if (check(TokenKind::LitString)) {
        std::string_view raw = cur.text;  // includes surrounding quotes
        // Unescape: strip outer quotes, replace '' → '
        std::string value;
        value.reserve(raw.size());
        for (std::size_t i = 1; i + 1 < raw.size(); ++i) {  // raw-primitive: size_t loop index
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — i bounds-checked by loop condition
            if (raw[i] == '\'' && i + 2 < raw.size() && raw[i + 1] == '\'') {
                value += '\'';
                ++i;
            } else {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — i bounds-checked by loop condition
                value += raw[i];
            }
        }
        advance();
        return Expr{Literal{StrLiteral{std::move(value)}}};
    }

    // Column reference (unqualified)
    if (check(TokenKind::Identifier)) {
        std::string name{cur.text};
        advance();
        return Expr{ColumnRef{std::move(name)}};
    }

    parse_err(std::format("unexpected token '{}' in expression at line {}:{}",
                          cur.text,
                          static_cast<uint32_t>(cur.line),   // raw-primitive
                          static_cast<uint32_t>(cur.col)));  // raw-primitive
    return std::unexpected(Error::ParseError);
}

}  // namespace edb
