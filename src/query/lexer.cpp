// src/query/lexer.cpp

#include "query/lexer.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace edb {

namespace {

// Case-insensitive ASCII lowercase conversion.
constexpr auto to_lower(char c) noexcept -> char {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

// Flat keyword table — sorted for readability; linear scan is fine for ~20 entries.
struct KwEntry {
    std::string_view word;
    TokenKind        kind;
};

constexpr std::array KEYWORDS = std::to_array<KwEntry>({
    {.word = "and",     .kind = TokenKind::KwAnd},
    {.word = "as",      .kind = TokenKind::KwAs},
    {.word = "create",  .kind = TokenKind::KwCreate},
    {.word = "default", .kind = TokenKind::KwDefault},
    {.word = "exists",  .kind = TokenKind::KwExists},
    {.word = "false",   .kind = TokenKind::KwFalse},
    {.word = "from",    .kind = TokenKind::KwFrom},
    {.word = "if",      .kind = TokenKind::KwIf},
    {.word = "insert",  .kind = TokenKind::KwInsert},
    {.word = "into",    .kind = TokenKind::KwInto},
    {.word = "key",     .kind = TokenKind::KwKey},
    {.word = "not",     .kind = TokenKind::KwNot},
    {.word = "null",    .kind = TokenKind::KwNull},
    {.word = "or",      .kind = TokenKind::KwOr},
    {.word = "primary", .kind = TokenKind::KwPrimary},
    {.word = "select",  .kind = TokenKind::KwSelect},
    {.word = "table",   .kind = TokenKind::KwTable},
    {.word = "true",    .kind = TokenKind::KwTrue},
    {.word = "unique",  .kind = TokenKind::KwUnique},
    {.word = "values",  .kind = TokenKind::KwValues},
    {.word = "where",   .kind = TokenKind::KwWhere},
});

auto match_keyword(std::string_view lower_text) noexcept -> TokenKind {
    for (const auto& [word, kind] : KEYWORDS) {
        if (lower_text == word) {
            return kind;
        }
    }
    return TokenKind::Identifier;
}

}  // namespace

Lexer::Lexer(std::string_view src) noexcept : src{src} {}

auto Lexer::at_end() const noexcept -> bool {
    return pos >= usize{src.size()};
}

auto Lexer::cur_char() const noexcept -> char {
    if (at_end()) {
        return '\0';
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — at_end() checked above
    return src[static_cast<std::size_t>(pos)];  // raw-primitive: operator[] takes size_t
}

auto Lexer::advance() noexcept -> char {
    char c = cur_char();
    if (c == '\n') {
        ++line;
        col = u32{1};
    } else {
        ++col;
    }
    ++pos;
    return c;
}

auto Lexer::make_tok(TokenKind k, usize start, u32 tl, u32 tc) const noexcept -> Token {
    auto off = static_cast<std::size_t>(start);         // raw-primitive: substr takes size_t
    auto len = static_cast<std::size_t>(pos - start);  // raw-primitive: substr takes size_t
    return Token{.kind = k, .text = src.substr(off, len), .line = tl, .col = tc};
}

auto Lexer::skip_line_comment() noexcept -> void {
    while (!at_end() && cur_char() != '\n') {
        advance();
    }
}

auto Lexer::skip_block_comment() noexcept -> void {
    advance();  // consume /
    advance();  // consume *
    while (!at_end()) {
        char bc = advance();
        if (bc == '*' && !at_end() && cur_char() == '/') {
            advance();  // consume closing /
            break;
        }
    }
}

auto Lexer::skip_whitespace_and_comments() -> void {
    while (!at_end()) {
        char c = cur_char();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '-') {
            auto next_pos = static_cast<std::size_t>(pos) + 1U;  // raw-primitive: size_t arithmetic
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — bounds checked above
            if (next_pos < src.size() && src[next_pos] == '-') {
                skip_line_comment();
            } else {
                break;
            }
        } else if (c == '/') {
            auto next_pos = static_cast<std::size_t>(pos) + 1U;  // raw-primitive: size_t arithmetic
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — bounds checked above
            if (next_pos < src.size() && src[next_pos] == '*') {
                skip_block_comment();
            } else {
                break;
            }
        } else {
            break;
        }
    }
}

auto Lexer::scan_ident_or_kw(usize start, u32 tok_line, u32 tok_col) -> Token {
    while (!at_end()) {
        char c = cur_char();
        // raw-primitive: std::isalnum/std::isdigit take int
        if (static_cast<bool>(std::isalnum(static_cast<unsigned char>(c))) || c == '_') {
            advance();
        } else {
            break;
        }
    }
    auto off = static_cast<std::size_t>(start);         // raw-primitive
    auto len = static_cast<std::size_t>(pos - start);  // raw-primitive
    std::string_view raw = src.substr(off, len);

    // Build a lowercased copy for keyword matching (no SQL keyword exceeds ~10 chars).
    // Use std::string to avoid C-style array and pointer-decay issues.
    static constexpr std::size_t max_kw_len = 64U;  // raw-primitive: size_t constant
    std::string lower_str{raw.substr(0, std::min(raw.size(), max_kw_len))};
    for (char& ch : lower_str) {
        ch = to_lower(ch);
    }
    std::string_view lower_text{lower_str};

    TokenKind kind = match_keyword(lower_text);
    return Token{.kind = kind, .text = raw, .line = tok_line, .col = tok_col};
}

auto Lexer::scan_number(usize start, u32 tok_line, u32 tok_col) -> Token {
    // Integer part
    while (!at_end() && static_cast<bool>(std::isdigit(static_cast<unsigned char>(cur_char())))) {
        advance();
    }
    // Optional fractional part
    bool is_float = false;
    if (!at_end() && cur_char() == '.') {
        auto next_pos = static_cast<std::size_t>(pos) + 1U;  // raw-primitive
        // Ensure the dot is followed by a digit (not e.g. a range operator)
        if (next_pos < src.size() &&
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) — bounds checked above
            static_cast<bool>(std::isdigit(static_cast<unsigned char>(src[next_pos])))) {
            is_float = true;
            advance();  // consume .
            while (!at_end() &&
                   static_cast<bool>(std::isdigit(static_cast<unsigned char>(cur_char())))) {
                advance();
            }
        }
    }
    // Optional exponent
    if (!at_end() && (cur_char() == 'e' || cur_char() == 'E')) {
        is_float = true;
        advance();
        if (!at_end() && (cur_char() == '+' || cur_char() == '-')) {
            advance();
        }
        while (!at_end() &&
               static_cast<bool>(std::isdigit(static_cast<unsigned char>(cur_char())))) {
            advance();
        }
    }
    TokenKind kind = is_float ? TokenKind::LitFloat : TokenKind::LitInteger;
    return make_tok(kind, start, tok_line, tok_col);
}

auto Lexer::scan_string(usize start, u32 tok_line, u32 tok_col) -> Token {
    // Opening quote already consumed by scan_one.
    while (!at_end()) {
        char c = advance();
        if (c == '\'') {
            // '' is an escaped single quote; single ' terminates the string.
            if (!at_end() && cur_char() == '\'') {
                advance();  // consume second quote (escape)
            } else {
                break;  // end of string
            }
        }
    }
    // Token text includes both surrounding quotes.
    return make_tok(TokenKind::LitString, start, tok_line, tok_col);
}

auto Lexer::scan_one() -> Token {
    skip_whitespace_and_comments();

    if (at_end()) {
        return Token{.kind = TokenKind::Eof, .text = {}, .line = line, .col = col};
    }

    usize start    = pos;
    u32   tok_line = line;
    u32   tok_col  = col;
    char  c        = advance();

    switch (c) {
        case '(':  return make_tok(TokenKind::LParen,    start, tok_line, tok_col);
        case ')':  return make_tok(TokenKind::RParen,    start, tok_line, tok_col);
        case ',':  return make_tok(TokenKind::Comma,     start, tok_line, tok_col);
        case ';':  return make_tok(TokenKind::Semicolon, start, tok_line, tok_col);
        case '*':  return make_tok(TokenKind::Star,      start, tok_line, tok_col);
        case '.':  return make_tok(TokenKind::Dot,       start, tok_line, tok_col);
        case '=':  return make_tok(TokenKind::Eq,        start, tok_line, tok_col);
        case '<':
            if (!at_end() && cur_char() == '=') { advance(); return make_tok(TokenKind::Le,  start, tok_line, tok_col); }
            if (!at_end() && cur_char() == '>') { advance(); return make_tok(TokenKind::Neq, start, tok_line, tok_col); }
            return make_tok(TokenKind::Lt, start, tok_line, tok_col);
        case '>':
            if (!at_end() && cur_char() == '=') { advance(); return make_tok(TokenKind::Ge, start, tok_line, tok_col); }
            return make_tok(TokenKind::Gt, start, tok_line, tok_col);
        case '\'':
            return scan_string(start, tok_line, tok_col);
        default:
            break;
    }

    if (static_cast<bool>(std::isalpha(static_cast<unsigned char>(c))) || c == '_') {
        return scan_ident_or_kw(start, tok_line, tok_col);
    }
    if (static_cast<bool>(std::isdigit(static_cast<unsigned char>(c)))) {
        return scan_number(start, tok_line, tok_col);
    }

    return make_tok(TokenKind::Unknown, start, tok_line, tok_col);
}

auto Lexer::next() -> Token {
    if (has_peek) {
        has_peek = false;
        return peek_tok;
    }
    return scan_one();
}

auto Lexer::peek() -> Token {
    if (!has_peek) {
        peek_tok = scan_one();
        has_peek = true;
    }
    return peek_tok;
}

}  // namespace edb
