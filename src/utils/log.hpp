#pragma once

// src/utils/log.hpp
//
// Structured logging for EDB.
//
// Usage:
//   edb_log(edb::LogLevel::Info, "io/posix", "opened file {}", path);
//   EDB_LOG_INFO("buffer", "evicted page {}", page_id.value);
//   EDB_LOG_ERROR("catalog", "type not found: {}", name);
//
// Output format (stderr):
//   [LEVEL] [module] message
//
// Thread-safety: edb_log() is safe to call from multiple threads.
//   std::println is internally synchronized per the C++23 standard.
//
// Log level filtering: set EDB_LOG_LEVEL env var at runtime:
//   EDB_LOG_LEVEL=debug ./edb
//   Levels: debug < info < warn < error < none
//   Default level: info (debug messages suppressed).

#include <cstdint>
#include <cstdlib>
#include <format>
#include <print>
#include <string_view>

namespace edb {

// ---------------------------------------------------------------------------
// Log levels
// ---------------------------------------------------------------------------
enum class LogLevel : uint8_t {  // raw-primitive: enum base type requires stdint typedef
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4,  // suppress all output
};

constexpr std::string_view log_level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::None:
            return "NONE ";
    }
    return "?????";
}

// ---------------------------------------------------------------------------
// Active log level — read once from EDB_LOG_LEVEL at first use.
// ---------------------------------------------------------------------------
namespace detail {

inline LogLevel active_log_level() noexcept {
    static const LogLevel active = [] {
        const char* env = std::getenv("EDB_LOG_LEVEL");  // raw-primitive: C env API returns char*
        if (env == nullptr) {
            return LogLevel::Info;
        }
        const std::string_view s{env};
        if (s == "debug") {
            return LogLevel::Debug;
        }
        if (s == "info") {
            return LogLevel::Info;
        }
        if (s == "warn") {
            return LogLevel::Warn;
        }
        if (s == "error") {
            return LogLevel::Error;
        }
        if (s == "none") {
            return LogLevel::None;
        }
        return LogLevel::Info;
    }();
    return active;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Core logging function
// ---------------------------------------------------------------------------
template <typename... Args>
void edb_log(LogLevel level, std::string_view module, std::format_string<Args...> fmt,
             Args&&... args) {
    if (level < detail::active_log_level()) {
        return;
    }
    const auto msg = std::format(fmt, std::forward<Args>(args)...);
    // std::println to FILE* is C++23; synchronized per standard (no torn lines).
    std::println(stderr, "[{}] [{}] {}", log_level_name(level), module, msg);
}

}  // namespace edb

// ---------------------------------------------------------------------------
// Convenience macros — reduce boilerplate at call sites.
// ---------------------------------------------------------------------------
#define EDB_LOG_DEBUG(module, fmt, ...) \
    ::edb::edb_log(::edb::LogLevel::Debug, (module), (fmt)__VA_OPT__(, __VA_ARGS__))

#define EDB_LOG_INFO(module, fmt, ...) \
    ::edb::edb_log(::edb::LogLevel::Info, (module), (fmt)__VA_OPT__(, __VA_ARGS__))

#define EDB_LOG_WARN(module, fmt, ...) \
    ::edb::edb_log(::edb::LogLevel::Warn, (module), (fmt)__VA_OPT__(, __VA_ARGS__))

#define EDB_LOG_ERROR(module, fmt, ...) \
    ::edb::edb_log(::edb::LogLevel::Error, (module), (fmt)__VA_OPT__(, __VA_ARGS__))
