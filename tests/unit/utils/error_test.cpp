#include "utils/error.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace edb;

// ---------------------------------------------------------------------------
// EdbResult<T> — success path
// ---------------------------------------------------------------------------

TEST(EdbError, ResultSuccessHasValue) {
    Result<int> r{42};  // raw-primitive: gtest int
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);  // raw-primitive: gtest int
}

TEST(EdbError, StatusSuccessHasValue) {
    VoidResult s{};
    EXPECT_TRUE(s.has_value());
}

// ---------------------------------------------------------------------------
// EdbResult<T> — failure path
// ---------------------------------------------------------------------------

TEST(EdbError, ResultErrorCarriesCode) {
    Result<int> r{std::unexpected(Error::NotFound)};  // raw-primitive: gtest int
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Error::NotFound);
}

TEST(EdbError, StatusErrorCarriesCode) {
    VoidResult s{std::unexpected(Error::IoError)};
    EXPECT_FALSE(s.has_value());
    EXPECT_EQ(s.error(), Error::IoError);
}

// ---------------------------------------------------------------------------
// edb_error_name — every code has a non-empty name
// ---------------------------------------------------------------------------

TEST(EdbError, ErrorNameNonEmpty) {
    constexpr std::array codes = {
        Error::Ok,
        Error::InvalidArgument,
        Error::OutOfMemory,
        Error::NotSupported,
        Error::NotFound,
        Error::AlreadyExists,
        Error::Overflow,
        Error::Corruption,
        Error::IoError,
        Error::IoTimeout,
        Error::IoAlignment,
        Error::PageNotFound,
        Error::BufferPoolFull,
        Error::InvalidPageId,
        Error::TransactionAborted,
        Error::DeadlockDetected,
        Error::SnapshotTooOld,
        Error::TypeNotFound,
        Error::TypeAlreadyRegistered,
        Error::CatalogCorrupted,
        Error::ParseError,
        Error::AnalyzerError,
        Error::ExecutorError,
    };
    for (auto code : codes) {
        EXPECT_FALSE(edb_error_name(code).empty())
            << "Missing name for code "
            << static_cast<int>(code);  // raw-primitive: enum underlying cast for message
    }
}

TEST(EdbError, ErrorNameOkIsOk) {
    EXPECT_EQ(edb_error_name(Error::Ok), "Ok");
}

// ---------------------------------------------------------------------------
// value_or / transform / and_then chaining
// ---------------------------------------------------------------------------

TEST(EdbError, ValueOr) {
    Result<int> bad{std::unexpected(Error::NotFound)};  // raw-primitive: gtest int
    EXPECT_EQ(bad.value_or(-1), -1);                    // raw-primitive: gtest int
}

TEST(EdbError, AndThenChaining) {
    Result<int> r{10};  // raw-primitive: gtest int
    auto doubled = r.and_then([](int v) -> Result<int> {
        return v * 2;  // raw-primitive: gtest int arithmetic
    });
    EXPECT_EQ(doubled.value(), 20);  // raw-primitive: gtest int
}

TEST(EdbError, AndThenShortCircuitsOnError) {
    Result<int> r{std::unexpected(Error::IoError)};  // raw-primitive: gtest int
    bool called = false;
    auto result = r.and_then([&called](int) -> Result<int> {
        called = true;
        return 99;  // raw-primitive: gtest int
    });
    EXPECT_FALSE(called);
    EXPECT_EQ(result.error(), Error::IoError);
}

TEST(EdbError, OrElseRecoversFromError) {
    Result<int> r{std::unexpected(Error::NotFound)};  // raw-primitive: gtest int
    auto recovered = r.or_else([](Error) -> Result<int> {
        return 0;  // raw-primitive: gtest int
    });
    EXPECT_EQ(recovered.value(), 0);  // raw-primitive: gtest int
}
