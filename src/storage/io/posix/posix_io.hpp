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

class PosixIO final : public EdbStorageIOOps {
   public:
    PosixIO() = default;
    PosixIO(const PosixIO&) = delete;
    PosixIO& operator=(const PosixIO&) = delete;
    PosixIO(PosixIO&&) = delete;
    PosixIO& operator=(PosixIO&&) = delete;
    ~PosixIO() override;

   private:
    // Lifecycle
    auto open_impl(const char* path, const EdbIOConfig& cfg) -> EdbStatus override;
    auto close_impl() -> EdbStatus override;

    // Synchronous I/O
    auto read_impl(u64 offset, std::span<std::byte> buf) -> EdbResult<usize> override;
    auto write_impl(u64 offset, std::span<const std::byte> buf) -> EdbResult<usize> override;

    // Memory mapping
    auto mmap_impl(u64 offset, usize len, i32 prot) -> EdbResult<std::byte*> override;
    auto munmap_impl(std::byte* addr, usize len) -> EdbStatus override;

    // Durability
    auto sync_impl() -> EdbStatus override;
    auto datasync_impl() -> EdbStatus override;
    auto sync_range_impl(u64 offset, usize len) -> EdbStatus override;

    // File management
    auto truncate_impl(u64 size) -> EdbStatus override;
    auto file_size_impl() -> EdbResult<u64> override;

    // Returns EdbError::IoError if the file is not open.
    [[nodiscard]]
    auto check_open() const -> EdbStatus;

    int fd{-1};             // raw-primitive: POSIX fd is a signed int
    std::string path_name;  // stored for diagnostics / error messages
};

}  // namespace edb
