#pragma once

// src/query/lexer.hpp
//
// Lexer — converts a SQL source string into a flat token stream.
//
// Usage:
//   Lexer lex{"SELECT 1"};
//   Token t = lex.next();   // consumes next token
//   Token p = lex.peek();   // looks ahead without consuming
//
// Keywords are matched case-insensitively.
// SQL string literals use single quotes; '' is an escaped single quote.
// Line comments begin with --.
// Block comments: /* ... */ (non-nesting).
//
// Token.text is a view into the source string passed to the constructor.
// The caller must keep the source string alive for as long as any derived
// Token or AST node referencing it lives.
//
// Thread-safety: not thread-safe; use one Lexer per query.

#include <string_view>

#include "query/token.hpp"
#include "utils/primitives.hpp"

namespace edb {

class Lexer {
public:
    explicit Lexer(std::string_view src) noexcept;

    // Return the next token and advance past it.
    [[nodiscard]] auto next() -> Token;

    // Return the next token without advancing (idempotent).
    [[nodiscard]] auto peek() -> Token;

private:
    std::string_view src;
    usize pos;
    u32 line{u32{1}};
    u32 col{u32{1}};

    bool has_peek{false};
    Token peek_tok{};

    auto scan_one() -> Token;
    auto skip_whitespace_and_comments() -> void;
    auto skip_line_comment() noexcept -> void;
    auto skip_block_comment() noexcept -> void;
    [[nodiscard]] auto scan_ident_or_kw(usize start, u32 tok_line, u32 tok_col) -> Token;
    [[nodiscard]] auto scan_number(usize start, u32 tok_line, u32 tok_col) -> Token;
    [[nodiscard]] auto scan_string(usize start, u32 tok_line, u32 tok_col) -> Token;

    [[nodiscard]] auto cur_char() const noexcept -> char;
    [[nodiscard]] auto at_end() const noexcept -> bool;
    auto advance() noexcept -> char;
    [[nodiscard]] auto make_tok(TokenKind k, usize start, u32 tl, u32 tc) const noexcept -> Token;
};

}  // namespace edb
