#include "utils/error.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace edb;

// ---------------------------------------------------------------------------
// EdbResult<T> — success path
// ---------------------------------------------------------------------------

TEST(EdbError, ResultSuccessHasValue) {
    EdbResult<int> r{42};  // raw-primitive: gtest int
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);  // raw-primitive: gtest int
}

TEST(EdbError, StatusSuccessHasValue) {
    EdbStatus s{};
    EXPECT_TRUE(s.has_value());
}

// ---------------------------------------------------------------------------
// EdbResult<T> — failure path
// ---------------------------------------------------------------------------

TEST(EdbError, ResultErrorCarriesCode) {
    EdbResult<int> r{std::unexpected(EdbError::NotFound)};  // raw-primitive: gtest int
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), EdbError::NotFound);
}

TEST(EdbError, StatusErrorCarriesCode) {
    EdbStatus s{std::unexpected(EdbError::IoError)};
    EXPECT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), EdbError::IoError);
}

// ---------------------------------------------------------------------------
// edb_error_name — every code has a non-empty name
// ---------------------------------------------------------------------------

TEST(EdbError, ErrorNameNonEmpty) {
    constexpr std::array codes = {
        EdbError::Ok,
        EdbError::InvalidArgument,
        EdbError::OutOfMemory,
        EdbError::NotSupported,
        EdbError::NotFound,
        EdbError::AlreadyExists,
        EdbError::Overflow,
        EdbError::Corruption,
        EdbError::IoError,
        EdbError::IoTimeout,
        EdbError::IoAlignment,
        EdbError::PageNotFound,
        EdbError::BufferPoolFull,
        EdbError::InvalidPageId,
        EdbError::TransactionAborted,
        EdbError::DeadlockDetected,
        EdbError::SnapshotTooOld,
        EdbError::TypeNotFound,
        EdbError::TypeAlreadyRegistered,
        EdbError::CatalogCorrupted,
        EdbError::ParseError,
        EdbError::AnalyzerError,
        EdbError::ExecutorError,
    };
    for (auto code : codes) {
        EXPECT_FALSE(edb_error_name(code).empty())
            << "Missing name for code "
            << static_cast<int>(code);  // raw-primitive: enum underlying cast for message
    }
}

TEST(EdbError, ErrorNameOkIsOk) {
    EXPECT_EQ(edb_error_name(EdbError::Ok), "Ok");
}

// ---------------------------------------------------------------------------
// value_or / transform / and_then chaining
// ---------------------------------------------------------------------------

TEST(EdbError, ValueOr) {
    EdbResult<int> bad{std::unexpected(EdbError::NotFound)};  // raw-primitive: gtest int
    EXPECT_EQ(bad.value_or(-1), -1);                          // raw-primitive: gtest int
}

TEST(EdbError, AndThenChaining) {
    EdbResult<int> r{10};  // raw-primitive: gtest int
    auto doubled = r.and_then([](int v) -> EdbResult<int> {
        return v * 2;  // raw-primitive: gtest int arithmetic
    });
    EXPECT_EQ(doubled.value(), 20);  // raw-primitive: gtest int
}

TEST(EdbError, AndThenShortCircuitsOnError) {
    EdbResult<int> r{std::unexpected(EdbError::IoError)};  // raw-primitive: gtest int
    bool called = false;
    auto result = r.and_then([&called](int) -> EdbResult<int> {
        called = true;
        return 99;  // raw-primitive: gtest int
    });
    EXPECT_FALSE(called);
    EXPECT_EQ(result.error(), EdbError::IoError);
}

TEST(EdbError, OrElseRecoversFromError) {
    EdbResult<int> r{std::unexpected(EdbError::NotFound)};  // raw-primitive: gtest int
    auto recovered = r.or_else([](EdbError) -> EdbResult<int> {
        return 0;  // raw-primitive: gtest int
    });
    EXPECT_EQ(recovered.value(), 0);  // raw-primitive: gtest int
}
