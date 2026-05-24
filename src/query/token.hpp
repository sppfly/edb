#pragma once

// src/query/token.hpp
//
// Token — single lexical unit produced by the Lexer.
// Token.text is a view into the original source string; the source must
// outlive all tokens derived from it.
//
// Thread-safety: Token is a trivially-copyable value type; no shared state.

#include <string_view>

#include "utils/primitives.hpp"

namespace edb {

// ---------------------------------------------------------------------------
// TokenKind
// ---------------------------------------------------------------------------
enum class TokenKind : uint8_t {  // raw-primitive: enum base requires stdint typedef
    // Literals
    LitInteger,  // e.g.  42
    LitFloat,    // e.g.  3.14
    LitString,   // e.g.  'hello'

    // Identifier
    Identifier,  // table / column names not matched as keywords

    // Keywords (alphabetical)
    KwAnd,
    KwAs,
    KwCreate,
    KwDefault,
    KwExists,
    KwFalse,
    KwFrom,
    KwIf,
    KwInsert,
    KwInto,
    KwKey,
    KwNot,
    KwNull,
    KwOr,
    KwPrimary,
    KwSelect,
    KwTable,
    KwTrue,
    KwUnique,
    KwValues,
    KwWhere,

    // Punctuation
    LParen,     // (
    RParen,     // )
    Comma,      // ,
    Semicolon,  // ;
    Star,       // *
    Dot,        // .

    // Comparison operators
    Eq,   // =
    Neq,  // <>
    Lt,   // <
    Le,   // <=
    Gt,   // >
    Ge,   // >=

    // Special
    Eof,
    Unknown,
};

// Human-readable name — useful in error messages.
constexpr std::string_view token_kind_name(TokenKind k) noexcept {
    switch (k) {
        case TokenKind::LitInteger:
            return "integer literal";
        case TokenKind::LitFloat:
            return "float literal";
        case TokenKind::LitString:
            return "string literal";
        case TokenKind::Identifier:
            return "identifier";
        case TokenKind::KwAnd:
            return "AND";
        case TokenKind::KwAs:
            return "AS";
        case TokenKind::KwCreate:
            return "CREATE";
        case TokenKind::KwDefault:
            return "DEFAULT";
        case TokenKind::KwExists:
            return "EXISTS";
        case TokenKind::KwFalse:
            return "FALSE";
        case TokenKind::KwFrom:
            return "FROM";
        case TokenKind::KwIf:
            return "IF";
        case TokenKind::KwInsert:
            return "INSERT";
        case TokenKind::KwInto:
            return "INTO";
        case TokenKind::KwKey:
            return "KEY";
        case TokenKind::KwNot:
            return "NOT";
        case TokenKind::KwNull:
            return "NULL";
        case TokenKind::KwOr:
            return "OR";
        case TokenKind::KwPrimary:
            return "PRIMARY";
        case TokenKind::KwSelect:
            return "SELECT";
        case TokenKind::KwTable:
            return "TABLE";
        case TokenKind::KwTrue:
            return "TRUE";
        case TokenKind::KwUnique:
            return "UNIQUE";
        case TokenKind::KwValues:
            return "VALUES";
        case TokenKind::KwWhere:
            return "WHERE";
        case TokenKind::LParen:
            return "(";
        case TokenKind::RParen:
            return ")";
        case TokenKind::Comma:
            return ",";
        case TokenKind::Semicolon:
            return ";";
        case TokenKind::Star:
            return "*";
        case TokenKind::Dot:
            return ".";
        case TokenKind::Eq:
            return "=";
        case TokenKind::Neq:
            return "<>";
        case TokenKind::Lt:
            return "<";
        case TokenKind::Le:
            return "<=";
        case TokenKind::Gt:
            return ">";
        case TokenKind::Ge:
            return ">=";
        case TokenKind::Eof:
            return "<EOF>";
        case TokenKind::Unknown:
            return "<unknown>";
    }
    return "<unknown>";
}

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------
struct Token {
    TokenKind kind{TokenKind::Unknown};
    std::string_view text;
    u32 line{u32{1}};
    u32 col{u32{1}};
};

}  // namespace edb
