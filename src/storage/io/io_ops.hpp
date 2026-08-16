#pragma once

// src/storage/io/io_ops.hpp
//
// Abstract interface for all EDB storage I/O backends.
//
// Implementations: posix (Phase 1), xnvme (Phase 8), io_uring (future).
//
// Layering rule: storage engines NEVER call POSIX or NVMe directly. They
// delegate all byte-level I/O through an EdbStorageIOOps reference. This
// lets the same engine run on a local file, a raw NVMe namespace, or a
// future RDMA block device without any engine-side changes.
//
// Thread-safety: all implementation methods must be safe to call concurrently
// from different threads with non-overlapping offset ranges. Concurrent calls
// on overlapping ranges have undefined behaviour — callers must serialize.
//
// Error model: all fallible operations return EdbResult<T>. Implementations
// must never throw; propagate OS errors as EdbError::IoError.

#include <cstddef>
#include <span>

#include "utils/assert.hpp"
#include "utils/error.hpp"
#include "utils/primitives.hpp"

namespace edb {

// ---------------------------------------------------------------------------
// EdbIOConfig — backend-agnostic open parameters
// ---------------------------------------------------------------------------
struct IOConfig {
    usize page_size{4096};    // logical page size in bytes; must be power-of-two
    b8 use_direct_io{false};  // bypass OS page cache (O_DIRECT / xNVMe passthrough)
    u32 queue_depth{1};       // async command queue depth (xNVMe); 1 = synchronous
};

// ---------------------------------------------------------------------------
// EdbIOVec — one element of a scatter/gather list
// ---------------------------------------------------------------------------
struct IOVec {
    u64 offset;                // byte offset from the start of the file/device
    std::span<std::byte> buf;  // buffer to read into / write from
};

// ---------------------------------------------------------------------------
// EdbStorageIOOps — abstract I/O backend interface
// ---------------------------------------------------------------------------
struct StorageIO {
    StorageIO() = default;

    // Non-copyable, non-movable (backends own OS resources).
    StorageIO(const StorageIO&) = delete;
    StorageIO& operator=(const StorageIO&) = delete;
    StorageIO(StorageIO&&) = delete;
    StorageIO& operator=(StorageIO&&) = delete;

    virtual ~StorageIO() = default;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Open a file or device at `path` using the given config.
    /// Must be called before any I/O method.
    /// Returns EdbError::IoError if the path cannot be opened.
    /// Precondition: path != nullptr — enforced by concrete backends.
    virtual auto open(const char* path, const IOConfig& cfg) -> VoidResult = 0;

    /// Close the file or device. No-op if already closed.
    virtual auto close() -> VoidResult = 0;

    // -----------------------------------------------------------------------
    // Synchronous I/O
    // -----------------------------------------------------------------------

    /// Read exactly buf.size() bytes from byte offset `offset`.
    /// Returns the number of bytes actually read (may be < buf.size() at EOF).
    /// Returns EdbError::IoError on OS failure.
    virtual auto read(u64 offset, std::span<std::byte> buf) -> Result<usize> = 0;

    /// Write exactly buf.size() bytes to byte offset `offset`.
    /// Returns the number of bytes actually written.
    /// Returns EdbError::IoError on OS failure.
    virtual auto write(u64 offset, std::span<const std::byte> buf) -> Result<usize> = 0;

    // -----------------------------------------------------------------------
    // Scatter / gather I/O (optional — default: sequential read/write loop)
    // -----------------------------------------------------------------------

    /// Read multiple non-contiguous regions. Default loops over read().
    /// Override with ::readv or io_uring for better performance.
    /// Returns total bytes read across all vectors.
    virtual auto readv(std::span<IOVec> iov) -> Result<usize> {
        usize total{0};
        for (auto& vec : iov) {
            auto res = read(vec.offset, vec.buf);
            if (!res) {
                return std::unexpected(res.error());
            }
            total = usize{total.value + res->value};
        }
        return total;
    }

    /// Write multiple non-contiguous regions. Default loops over write().
    /// Returns total bytes written across all vectors.
    virtual auto writev(std::span<const IOVec> iov) -> Result<usize> {
        usize total{0};
        for (const auto& vec : iov) {
            auto res = write(vec.offset, vec.buf);
            if (!res) {
                return std::unexpected(res.error());
            }
            total = usize{total.value + res->value};
        }
        return total;
    }

    // -----------------------------------------------------------------------
    // Memory mapping (optional — default: not supported)
    // -----------------------------------------------------------------------

    /// Map `len` bytes at `offset` into the process address space.
    /// `prot` is passed directly to mmap(2) (PROT_READ, PROT_WRITE, etc.).
    /// Default returns EdbError::NotSupported; POSIX backend overrides this.
    virtual auto mmap(u64 offset, usize len, i32 prot) -> Result<std::byte*> {
        EDB_ASSERT(len > usize{0});
        (void)offset;
        (void)prot;
        return std::unexpected(Error::NotSupported);
    }

    /// Unmap a region previously returned by mmap().
    virtual auto munmap(std::byte* addr, usize len) -> VoidResult {
        EDB_ASSERT(len > usize{0});
        (void)addr;
        return std::unexpected(Error::NotSupported);
    }

    // -----------------------------------------------------------------------
    // Durability
    // -----------------------------------------------------------------------

    /// Flush all dirty pages (data + metadata) to the underlying device.
    virtual auto sync() -> VoidResult = 0;

    /// Flush dirty data pages only (metadata may be deferred).
    virtual auto datasync() -> VoidResult = 0;

    /// Flush dirty data in the byte range [offset, offset+len).
    /// Default falls back to datasync().
    virtual auto sync_range(u64 offset, usize len) -> VoidResult {
        EDB_ASSERT(len > usize{0});
        (void)offset;
        (void)len;
        return datasync();
    }

    // -----------------------------------------------------------------------
    // File management
    // -----------------------------------------------------------------------

    /// Set the file/device size to exactly `size` bytes.
    /// May extend (zero-filled) or shrink the file.
    virtual auto truncate(u64 size) -> VoidResult = 0;

    /// Return the current file/device size in bytes.
    virtual auto file_size() -> Result<u64> = 0;
};

}  // namespace edb
