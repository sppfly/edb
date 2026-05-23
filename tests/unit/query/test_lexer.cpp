// tests/unit/query/test_lexer.cpp

#include <gtest/gtest.h>

#include "query/lexer.hpp"
#include "query/token.hpp"

using namespace edb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static auto collect(std::string_view src) -> std::vector<TokenKind> {
    Lexer lex{src};
    std::vector<TokenKind> kinds;
    while (true) {
        Token t = lex.next();
        kinds.push_back(t.kind);
        if (t.kind == TokenKind::Eof) break;
    }
    return kinds;
}

// ---------------------------------------------------------------------------
// Punctuation & operators
// ---------------------------------------------------------------------------
TEST(Lexer, Punctuation) {
    auto kinds = collect("( ) , ; * . = <> < <= > >=");
    ASSERT_EQ(kinds.size(), 13U);
    EXPECT_EQ(kinds[0],  TokenKind::LParen);
    EXPECT_EQ(kinds[1],  TokenKind::RParen);
    EXPECT_EQ(kinds[2],  TokenKind::Comma);
    EXPECT_EQ(kinds[3],  TokenKind::Semicolon);
    EXPECT_EQ(kinds[4],  TokenKind::Star);
    EXPECT_EQ(kinds[5],  TokenKind::Dot);
    EXPECT_EQ(kinds[6],  TokenKind::Eq);
    EXPECT_EQ(kinds[7],  TokenKind::Neq);
    EXPECT_EQ(kinds[8],  TokenKind::Lt);
    EXPECT_EQ(kinds[9],  TokenKind::Le);
    EXPECT_EQ(kinds[10], TokenKind::Gt);
    EXPECT_EQ(kinds[11], TokenKind::Ge);
    EXPECT_EQ(kinds[12], TokenKind::Eof);
}

// ---------------------------------------------------------------------------
// Keywords — case-insensitive
// ---------------------------------------------------------------------------
TEST(Lexer, KeywordsCaseLower) {
    auto kinds = collect("create table insert into values select from where");
    EXPECT_EQ(kinds[0], TokenKind::KwCreate);
    EXPECT_EQ(kinds[1], TokenKind::KwTable);
    EXPECT_EQ(kinds[2], TokenKind::KwInsert);
    EXPECT_EQ(kinds[3], TokenKind::KwInto);
    EXPECT_EQ(kinds[4], TokenKind::KwValues);
    EXPECT_EQ(kinds[5], TokenKind::KwSelect);
    EXPECT_EQ(kinds[6], TokenKind::KwFrom);
    EXPECT_EQ(kinds[7], TokenKind::KwWhere);
}

TEST(Lexer, KeywordsCaseUpper) {
    auto kinds = collect("CREATE TABLE INSERT INTO VALUES SELECT FROM WHERE");
    EXPECT_EQ(kinds[0], TokenKind::KwCreate);
    EXPECT_EQ(kinds[1], TokenKind::KwTable);
}

TEST(Lexer, KeywordsMixed) {
    auto kinds = collect("And Or Not Null True False Primary Key Unique If Exists Default As");
    EXPECT_EQ(kinds[0],  TokenKind::KwAnd);
    EXPECT_EQ(kinds[1],  TokenKind::KwOr);
    EXPECT_EQ(kinds[2],  TokenKind::KwNot);
    EXPECT_EQ(kinds[3],  TokenKind::KwNull);
    EXPECT_EQ(kinds[4],  TokenKind::KwTrue);
    EXPECT_EQ(kinds[5],  TokenKind::KwFalse);
    EXPECT_EQ(kinds[6],  TokenKind::KwPrimary);
    EXPECT_EQ(kinds[7],  TokenKind::KwKey);
    EXPECT_EQ(kinds[8],  TokenKind::KwUnique);
    EXPECT_EQ(kinds[9],  TokenKind::KwIf);
    EXPECT_EQ(kinds[10], TokenKind::KwExists);
    EXPECT_EQ(kinds[11], TokenKind::KwDefault);
    EXPECT_EQ(kinds[12], TokenKind::KwAs);
}

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------
TEST(Lexer, Identifier) {
    Lexer lex{"foo_bar123"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::Identifier);
    EXPECT_EQ(t.text, "foo_bar123");
}

TEST(Lexer, IdentifierUnderscore) {
    Lexer lex{"_private"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::Identifier);
}

// ---------------------------------------------------------------------------
// Integer literals
// ---------------------------------------------------------------------------
TEST(Lexer, IntegerLiteral) {
    Lexer lex{"42"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::LitInteger);
    EXPECT_EQ(t.text, "42");
}

TEST(Lexer, IntegerZero) {
    Lexer lex{"0"};
    EXPECT_EQ(lex.next().kind, TokenKind::LitInteger);
}

// ---------------------------------------------------------------------------
// Float literals
// ---------------------------------------------------------------------------
TEST(Lexer, FloatDot) {
    Lexer lex{"3.14"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::LitFloat);
    EXPECT_EQ(t.text, "3.14");
}

TEST(Lexer, FloatExponent) {
    Lexer lex{"1e10"};
    EXPECT_EQ(lex.next().kind, TokenKind::LitFloat);
}

TEST(Lexer, FloatNegativeExponent) {
    Lexer lex{"2.5e-3"};
    EXPECT_EQ(lex.next().kind, TokenKind::LitFloat);
}

// ---------------------------------------------------------------------------
// String literals
// ---------------------------------------------------------------------------
TEST(Lexer, StringSimple) {
    Lexer lex{"'hello'"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::LitString);
    EXPECT_EQ(t.text, "'hello'");
}

TEST(Lexer, StringEmpty) {
    Lexer lex{"''"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::LitString);
}

TEST(Lexer, StringEscapedQuote) {
    Lexer lex{"'it''s'"};
    Token t = lex.next();
    EXPECT_EQ(t.kind, TokenKind::LitString);
    EXPECT_EQ(t.text, "'it''s'");  // raw text preserved in Token
}

// ---------------------------------------------------------------------------
// Comments
// ---------------------------------------------------------------------------
TEST(Lexer, LineComment) {
    auto kinds = collect("42 -- this is a comment\n99");
    ASSERT_EQ(kinds.size(), 3U);
    EXPECT_EQ(kinds[0], TokenKind::LitInteger);
    EXPECT_EQ(kinds[1], TokenKind::LitInteger);
    EXPECT_EQ(kinds[2], TokenKind::Eof);
}

TEST(Lexer, BlockComment) {
    auto kinds = collect("1 /* block */ 2");
    ASSERT_EQ(kinds.size(), 3U);
    EXPECT_EQ(kinds[0], TokenKind::LitInteger);
    EXPECT_EQ(kinds[1], TokenKind::LitInteger);
}

// ---------------------------------------------------------------------------
// Line/column tracking
// ---------------------------------------------------------------------------
TEST(Lexer, LineTracking) {
    Lexer lex{"foo\nbar"};
    Token t1 = lex.next();
    Token t2 = lex.next();
    EXPECT_EQ(static_cast<uint32_t>(t1.line), 1U);
    EXPECT_EQ(static_cast<uint32_t>(t2.line), 2U);
}

TEST(Lexer, ColTracking) {
    Lexer lex{"ab cd"};
    Token t1 = lex.next();
    Token t2 = lex.next();
    EXPECT_EQ(static_cast<uint32_t>(t1.col), 1U);
    EXPECT_EQ(static_cast<uint32_t>(t2.col), 4U);
}

// ---------------------------------------------------------------------------
// Peek is idempotent
// ---------------------------------------------------------------------------
TEST(Lexer, PeekIdempotent) {
    Lexer lex{"42 99"};
    Token p1 = lex.peek();
    Token p2 = lex.peek();
    EXPECT_EQ(p1.kind, p2.kind);
    EXPECT_EQ(p1.text, p2.text);
    Token n = lex.next();
    EXPECT_EQ(n.kind, TokenKind::LitInteger);
    EXPECT_EQ(n.text, "42");
}

// ---------------------------------------------------------------------------
// EOF on empty input
// ---------------------------------------------------------------------------
TEST(Lexer, EmptyInput) {
    Lexer lex{""};
    EXPECT_EQ(lex.next().kind, TokenKind::Eof);
    EXPECT_EQ(lex.next().kind, TokenKind::Eof);  // repeated calls stay at Eof
}

TEST(Lexer, WhitespaceOnly) {
    Lexer lex{"   \t\n  "};
    EXPECT_EQ(lex.next().kind, TokenKind::Eof);
}
