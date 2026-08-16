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
struct StorageIOOps {
    StorageIOOps() = default;

    // Non-copyable, non-movable (backends own OS resources).
    StorageIOOps(const StorageIOOps&) = delete;
    StorageIOOps& operator=(const StorageIOOps&) = delete;
    StorageIOOps(StorageIOOps&&) = delete;
    StorageIOOps& operator=(StorageIOOps&&) = delete;

    virtual ~StorageIOOps() = default;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Open a file or device at `path` using the given config.
    /// Must be called before any I/O method.
    /// Returns EdbError::IoError if the path cannot be opened.
    auto open(const char* path, const IOConfig& cfg) -> VoidResult {
        EDB_ASSERT(path != nullptr);
        return open_impl(path, cfg);
    }

    /// Close the file or device. No-op if already closed.
    auto close() -> VoidResult { return close_impl(); }

    // -----------------------------------------------------------------------
    // Synchronous I/O
    // -----------------------------------------------------------------------

    /// Read exactly buf.size() bytes from byte offset `offset`.
    /// Returns the number of bytes actually read (may be < buf.size() at EOF).
    /// Returns EdbError::IoError on OS failure.
    auto read(u64 offset, std::span<std::byte> buf) -> Result<usize> {
        EDB_ASSERT(!buf.empty());
        return read_impl(offset, buf);
    }

    /// Write exactly buf.size() bytes to byte offset `offset`.
    /// Returns the number of bytes actually written.
    /// Returns EdbError::IoError on OS failure.
    auto write(u64 offset, std::span<const std::byte> buf) -> Result<usize> {
        EDB_ASSERT(!buf.empty());
        return write_impl(offset, buf);
    }

    // -----------------------------------------------------------------------
    // Scatter / gather I/O (optional — default: sequential read/write loop)
    // -----------------------------------------------------------------------

    /// Read multiple non-contiguous regions. Default implementation loops
    /// over read(). Override with ::readv or io_uring for better performance.
    /// Returns total bytes read across all vectors.
    auto readv(std::span<IOVec> iov) -> Result<usize> { return readv_impl(iov); }

    /// Write multiple non-contiguous regions. Default loops over write().
    /// Returns total bytes written across all vectors.
    auto writev(std::span<const IOVec> iov) -> Result<usize> { return writev_impl(iov); }

    // -----------------------------------------------------------------------
    // Memory mapping (optional — default: not supported)
    // -----------------------------------------------------------------------

    /// Map `len` bytes at `offset` into the process address space.
    /// `prot` is passed directly to mmap(2) (PROT_READ, PROT_WRITE, etc.).
    /// Default returns EdbError::NotSupported; POSIX backend overrides this.
    auto mmap(u64 offset, usize len, i32 prot) -> Result<std::byte*> {
        EDB_ASSERT(len > usize{0});
        return mmap_impl(offset, len, prot);
    }

    /// Unmap a region previously returned by mmap().
    auto munmap(std::byte* addr, usize len) -> VoidResult {
        EDB_ASSERT(len > usize{0});
        return munmap_impl(addr, len);
    }

    // -----------------------------------------------------------------------
    // Durability
    // -----------------------------------------------------------------------

    /// Flush all dirty pages (data + metadata) to the underlying device.
    auto sync() -> VoidResult { return sync_impl(); }

    /// Flush dirty data pages only (metadata may be deferred).
    auto datasync() -> VoidResult { return datasync_impl(); }

    /// Flush dirty data in the byte range [offset, offset+len).
    /// Default falls back to datasync().
    auto sync_range(u64 offset, usize len) -> VoidResult {
        EDB_ASSERT(len > usize{0});
        return sync_range_impl(offset, len);
    }

    // -----------------------------------------------------------------------
    // File management
    // -----------------------------------------------------------------------

    /// Set the file/device size to exactly `size` bytes.
    /// May extend (zero-filled) or shrink the file.
    auto truncate(u64 size) -> VoidResult { return truncate_impl(size); }

    /// Return the current file/device size in bytes.
    auto file_size() -> Result<u64> { return file_size_impl(); }

protected:
    virtual auto open_impl(const char* path, const IOConfig& cfg) -> VoidResult = 0;
    virtual auto close_impl() -> VoidResult = 0;
    virtual auto read_impl(u64 offset, std::span<std::byte> buf) -> Result<usize> = 0;
    virtual auto write_impl(u64 offset, std::span<const std::byte> buf) -> Result<usize> = 0;
    virtual auto readv_impl(std::span<IOVec> iov) -> Result<usize>;
    virtual auto writev_impl(std::span<const IOVec> iov) -> Result<usize>;
    virtual auto mmap_impl(u64 offset, usize len, i32 prot) -> Result<std::byte*>;
    virtual auto munmap_impl(std::byte* addr, usize len) -> VoidResult;
    virtual auto sync_impl() -> VoidResult = 0;
    virtual auto datasync_impl() -> VoidResult = 0;
    virtual auto sync_range_impl(u64 offset, usize len) -> VoidResult;
    virtual auto truncate_impl(u64 size) -> VoidResult = 0;
    virtual auto file_size_impl() -> Result<u64> = 0;
};

// ---------------------------------------------------------------------------
// Default implementations (defined inline — no .cpp needed for the interface)
// ---------------------------------------------------------------------------

inline auto StorageIOOps::readv_impl(std::span<IOVec> iov) -> Result<usize> {
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

inline auto StorageIOOps::writev_impl(std::span<const IOVec> iov) -> Result<usize> {
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

inline auto StorageIOOps::mmap_impl([[maybe_unused]] u64 offset, [[maybe_unused]] usize len,
                                    [[maybe_unused]] i32 prot) -> Result<std::byte*> {
    return std::unexpected(Error::NotSupported);
}

inline auto StorageIOOps::munmap_impl([[maybe_unused]] std::byte* addr, [[maybe_unused]] usize len)
    -> VoidResult {
    return std::unexpected(Error::NotSupported);
}

inline auto StorageIOOps::sync_range_impl([[maybe_unused]] u64 offset, [[maybe_unused]] usize len)
    -> VoidResult {
    return datasync();
}

}  // namespace edb
