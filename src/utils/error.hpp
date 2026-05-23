#pragma once

// src/utils/error.hpp
//
// EdbError — canonical error codes for all EDB subsystems.
// EdbResult<T> — alias for std::expected<T, EdbError>.
//
// Usage:
//   auto read_page(u64 id) -> EdbResult<Page>;
//
//   auto result = read_page(u64{0});
//   if (!result) { handle(result.error()); }
//
// Thread-safety: EdbError is a plain enum; EdbResult<T> follows the
// thread-safety of std::expected (value-type semantics, no shared state).

#include <cstdint>
#include <expected>
#include <string_view>

namespace edb {

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------
enum class Error : uint8_t {  // raw-primitive: enum base type requires stdint typedef
    // Generic
    Ok = 0,           // not an error; used as a sentinel
    InvalidArgument,  // precondition violated by caller
    OutOfMemory,
    NotSupported,  // operation not implemented by this backend
    NotFound,      // requested object does not exist
    AlreadyExists,
    Overflow,    // arithmetic or buffer overflow
    Corruption,  // on-disk data failed integrity check (checksum etc.)

    // I/O
    IoError,  // underlying read/write failed
    IoTimeout,
    IoAlignment,  // buffer or offset not aligned to sector size (xNVMe)

    // Storage engine
    PageNotFound,
    BufferPoolFull,  // all frames pinned; cannot evict
    InvalidPageId,

    // Transactions
    TransactionAborted,
    DeadlockDetected,
    SnapshotTooOld,

    // Catalog / types
    TypeNotFound,
    TypeAlreadyRegistered,
    CatalogCorrupted,

    // Query
    ParseError,
    AnalyzerError,
    ExecutorError,
};

// ---------------------------------------------------------------------------
// EdbResult<T>
// ---------------------------------------------------------------------------
template <typename T>
using Result = std::expected<T, Error>;

// Convenience: a result carrying no value (just success/failure).
using VoidResult = Result<void>;

// ---------------------------------------------------------------------------
// Human-readable description (useful in logs and error messages).
// ---------------------------------------------------------------------------
constexpr std::string_view edb_error_name(Error e) noexcept {
    switch (e) {
        case Error::Ok:
            return "Ok";
        case Error::InvalidArgument:
            return "InvalidArgument";
        case Error::OutOfMemory:
            return "OutOfMemory";
        case Error::NotSupported:
            return "NotSupported";
        case Error::NotFound:
            return "NotFound";
        case Error::AlreadyExists:
            return "AlreadyExists";
        case Error::Overflow:
            return "Overflow";
        case Error::Corruption:
            return "Corruption";
        case Error::IoError:
            return "IoError";
        case Error::IoTimeout:
            return "IoTimeout";
        case Error::IoAlignment:
            return "IoAlignment";
        case Error::PageNotFound:
            return "PageNotFound";
        case Error::BufferPoolFull:
            return "BufferPoolFull";
        case Error::InvalidPageId:
            return "InvalidPageId";
        case Error::TransactionAborted:
            return "TransactionAborted";
        case Error::DeadlockDetected:
            return "DeadlockDetected";
        case Error::SnapshotTooOld:
            return "SnapshotTooOld";
        case Error::TypeNotFound:
            return "TypeNotFound";
        case Error::TypeAlreadyRegistered:
            return "TypeAlreadyRegistered";
        case Error::CatalogCorrupted:
            return "CatalogCorrupted";
        case Error::ParseError:
            return "ParseError";
        case Error::AnalyzerError:
            return "AnalyzerError";
        case Error::ExecutorError:
            return "ExecutorError";
    }
    return "UnknownError";
}

}  // namespace edb
