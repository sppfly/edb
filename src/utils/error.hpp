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
enum class EdbError : uint8_t {  // raw-primitive: enum base type requires stdint typedef
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
using EdbResult = std::expected<T, EdbError>;

// Convenience: a result carrying no value (just success/failure).
using EdbStatus = EdbResult<void>;

// ---------------------------------------------------------------------------
// Human-readable description (useful in logs and error messages).
// ---------------------------------------------------------------------------
constexpr std::string_view edb_error_name(EdbError e) noexcept {
    switch (e) {
        case EdbError::Ok:
            return "Ok";
        case EdbError::InvalidArgument:
            return "InvalidArgument";
        case EdbError::OutOfMemory:
            return "OutOfMemory";
        case EdbError::NotSupported:
            return "NotSupported";
        case EdbError::NotFound:
            return "NotFound";
        case EdbError::AlreadyExists:
            return "AlreadyExists";
        case EdbError::Overflow:
            return "Overflow";
        case EdbError::Corruption:
            return "Corruption";
        case EdbError::IoError:
            return "IoError";
        case EdbError::IoTimeout:
            return "IoTimeout";
        case EdbError::IoAlignment:
            return "IoAlignment";
        case EdbError::PageNotFound:
            return "PageNotFound";
        case EdbError::BufferPoolFull:
            return "BufferPoolFull";
        case EdbError::InvalidPageId:
            return "InvalidPageId";
        case EdbError::TransactionAborted:
            return "TransactionAborted";
        case EdbError::DeadlockDetected:
            return "DeadlockDetected";
        case EdbError::SnapshotTooOld:
            return "SnapshotTooOld";
        case EdbError::TypeNotFound:
            return "TypeNotFound";
        case EdbError::TypeAlreadyRegistered:
            return "TypeAlreadyRegistered";
        case EdbError::CatalogCorrupted:
            return "CatalogCorrupted";
        case EdbError::ParseError:
            return "ParseError";
        case EdbError::AnalyzerError:
            return "AnalyzerError";
        case EdbError::ExecutorError:
            return "ExecutorError";
    }
    return "UnknownError";
}

}  // namespace edb
