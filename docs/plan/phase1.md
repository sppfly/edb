# Phase 1 — Storage I/O Backend ✅

Define the lowest layer: how EDB reads and writes bytes to a device. The interface must be stable before any engine code is written.

## Interface: `EdbStorageIOOps` (`src/storage/io/io_ops.hpp`)

```cpp
/// Abstract interface for all storage I/O backends.
/// Implementations: posix, xnvme, io_uring (future), RDMA (future).
/// Thread-safety: all methods must be safe to call concurrently from
/// different threads with non-overlapping offset ranges.
struct EdbStorageIOOps {
    virtual ~EdbStorageIOOps() = default;

    // Public non-virtual wrappers carry EDB_PRE contracts and dispatch to
    // protected *_impl hooks (GCC disallows contracts on virtual functions).

    auto open(const char* path, const EdbIOConfig& cfg) -> EdbResult<void>;
    auto close()                                         -> EdbResult<void>;
    auto read(u64 offset, std::span<std::byte> buf)      -> EdbResult<usize>;
    auto write(u64 offset, std::span<const std::byte> buf) -> EdbResult<usize>;
    auto readv(std::span<EdbIOVec> iov)                  -> EdbResult<usize>;
    auto writev(std::span<const EdbIOVec> iov)           -> EdbResult<usize>;
    auto mmap(u64 offset, usize len, i32 prot)           -> EdbResult<std::byte*>;
    auto munmap(std::byte* addr, usize len)              -> EdbResult<void>;
    auto sync()                                          -> EdbResult<void>;
    auto datasync()                                      -> EdbResult<void>;
    auto sync_range(u64 offset, usize len)               -> EdbResult<void>;
    auto truncate(u64 size)                              -> EdbResult<void>;
    auto file_size()                                     -> EdbResult<u64>;

protected:
    // Pure-virtual hooks; concrete backends override these.
    virtual auto open_impl(...) -> EdbResult<void> = 0;
    // ... one *_impl per public method
};

struct EdbIOVec {
    u64                  offset;
    std::span<std::byte> buf;
};

struct EdbIOConfig {
    usize page_size     = usize{4096};
    b8    use_direct_io = b8{false};   // O_DIRECT / async-backend bypass of page cache
    u32   queue_depth   = u32{1};      // async queue depth (io_uring / xNVMe)
};
```

## Backends

| Backend | Path | Notes | Status |
|---|---|---|---|
| POSIX | `src/storage/io/posix/` | `pread64`/`pwrite64`, optional `O_DIRECT`, `fsync`/`fdatasync`, `sync_file_range` | ✅ |
| xNVMe | `src/storage/io/xnvme/` | Phase 8; direct NVMe submission, 4 KB alignment mandatory | 🔲 |
| io_uring | `src/storage/io/uring/` | Phase 8; Linux async submission/completion backend | 🔲 |

## Deliverables

- [x] `EdbStorageIOOps` abstract class with `EDB_PRE` contracts
- [x] POSIX backend: `pread64`/`pwrite64` with EINTR retry, `mmap`/`munmap`, `fsync`/`fdatasync`, `sync_file_range`, `ftruncate64`/`lseek64`
- [x] I/O benchmark harness (`tests/storage/io/io_bench.cpp`)
- [x] 69 unit tests passing under ASan + UBSan
- [x] `format-check` and `tidy` clean
