#include "utils/log.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

using namespace edb;

// ---------------------------------------------------------------------------
// log_level_name
// ---------------------------------------------------------------------------

TEST(Log, LevelNameDebug) {
    EXPECT_EQ(log_level_name(LogLevel::Debug), "DEBUG");
}
TEST(Log, LevelNameInfo) {
    EXPECT_EQ(log_level_name(LogLevel::Info), "INFO ");
}
TEST(Log, LevelNameWarn) {
    EXPECT_EQ(log_level_name(LogLevel::Warn), "WARN ");
}
TEST(Log, LevelNameError) {
    EXPECT_EQ(log_level_name(LogLevel::Error), "ERROR");
}
TEST(Log, LevelNameNone) {
    EXPECT_EQ(log_level_name(LogLevel::None), "NONE ");
}

// ---------------------------------------------------------------------------
// LogLevel ordering (used for filtering comparisons)
// ---------------------------------------------------------------------------

TEST(Log, LevelOrdering) {
    EXPECT_LT(LogLevel::Debug, LogLevel::Info);
    EXPECT_LT(LogLevel::Info, LogLevel::Warn);
    EXPECT_LT(LogLevel::Warn, LogLevel::Error);
    EXPECT_LT(LogLevel::Error, LogLevel::None);
}

// ---------------------------------------------------------------------------
// edb_log does not crash — smoke test (output goes to stderr, not captured)
// ---------------------------------------------------------------------------

TEST(Log, SmokeDoesNotCrash) {
    // Temporarily suppress output by setting level to None.
    // We cannot set the static-once active_log_level from a test, so we
    // just call at a level that should be filtered (Debug < default Info).
    EXPECT_NO_FATAL_FAILURE(
        edb_log(LogLevel::Debug, "test", "smoke {}", 42)  // raw-primitive: literal int in format
    );
    EXPECT_NO_FATAL_FAILURE(edb_log(LogLevel::Info, "test", "info message"));
    EXPECT_NO_FATAL_FAILURE(edb_log(LogLevel::Error, "test", "error {}", "detail"));
}

// ---------------------------------------------------------------------------
// Convenience macros compile and do not crash
// ---------------------------------------------------------------------------

TEST(Log, MacrosSmokeTest) {
    EXPECT_NO_FATAL_FAILURE(EDB_LOG_DEBUG("test", "debug macro"));
    EXPECT_NO_FATAL_FAILURE(EDB_LOG_INFO("test", "info macro"));
    EXPECT_NO_FATAL_FAILURE(EDB_LOG_WARN("test", "warn macro"));
    EXPECT_NO_FATAL_FAILURE(EDB_LOG_ERROR("test", "error macro"));
}
