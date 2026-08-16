#pragma once

// src/storage/io/posix/posix_io.hpp
//
// POSIX file I/O backend for EdbStorageIOOps.
//
// Uses pread64/pwrite64 for thread-safe offset-based I/O without needing
// per-thread file positions. Supports O_DIRECT for bypassing the OS page
// cache (requires 4 KB-aligned buffers and offsets on most filesystems).
//
// mmap/munmap: implemented (not just NotSupported) so the Phase 2 buffer
// pool can optionally memory-map pages directly.
//
// sync_range: uses sync_file_range(2) on Linux; falls back to fdatasync
// on non-Linux platforms.
//
// Thread-safety: all public methods are safe to call concurrently from
// different threads with non-overlapping byte ranges.

#include <string>

#include "storage/io/io_ops.hpp"

namespace edb {

class PosixIO final : public StorageIO {
public:
    PosixIO() = default;
    PosixIO(const PosixIO&) = delete;
    PosixIO& operator=(const PosixIO&) = delete;
    PosixIO(PosixIO&&) = delete;
    PosixIO& operator=(PosixIO&&) = delete;
    ~PosixIO() override;

    // Lifecycle
    auto open(const char* path, const IOConfig& cfg) -> VoidResult override;
    auto close() -> VoidResult override;

    // Synchronous I/O
    auto read(u64 offset, std::span<std::byte> buf) -> Result<usize> override;
    auto write(u64 offset, std::span<const std::byte> buf) -> Result<usize> override;

    // Memory mapping
    auto mmap(u64 offset, usize len, i32 prot) -> Result<std::byte*> override;
    auto munmap(std::byte* addr, usize len) -> VoidResult override;

    // Durability
    auto sync() -> VoidResult override;
    auto datasync() -> VoidResult override;
    auto sync_range(u64 offset, usize len) -> VoidResult override;

    // File management
    auto truncate(u64 size) -> VoidResult override;
    auto file_size() -> Result<u64> override;

private:
    // Returns EdbError::IoError if the file is not open.
    [[nodiscard]]
    auto check_open() const -> VoidResult;

    int fd{-1};             // raw-primitive: POSIX fd is a signed int
    std::string path_name;  // stored for diagnostics / error messages
};

}  // namespace edb
