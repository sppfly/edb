---
name: storage-engine
description: 'Implement or extend EDB storage engines and storage I/O backends. Use when adding a storage engine, implementing storage I/O backends (POSIX, xNVMe, io_uring), designing page formats, adding WAL/buffer pool, or optimizing I/O performance.'
argument-hint: 'Storage component to implement (e.g., heap engine, xNVMe backend, buffer pool)'
user-invocable: true
disable-model-invocation: false
---

# Storage Engine & I/O Backend Development

## When to Use
- Implementing a new storage engine (heap, columnar, LSM-tree)
- Adding a new storage I/O backend (POSIX, xNVMe, io_uring, SPDK)
- Designing page formats or record layouts
- Implementing buffer pool, replacement policy, or prefetching
- Adding WAL (write-ahead logging) or checkpointing
- Optimizing I/O performance or latency

## Two-Level Abstraction

EDB storage is split into two independent layers:

1. **Storage Engine** — Understands pages, tuples, indexes, transactions
2. **Storage I/O Backend** — Understands devices, offsets, commands, sync

A storage engine does NOT call POSIX or NVMe directly. It delegates all I/O to a configured `EdbStorageIOOps` vtable. This lets the same heap engine run on:
- Local ext4 via POSIX pread/pwrite
- Raw NVMe namespace via xNVMe
- Remote block device via future RDMA backend

## Storage Engine Interface (`src/storage/engine/`)

```cpp
struct EdbStorageEngineOps {
    std::string_view name;

    // Lifecycle
    virtual std::expected<void, EdbError> open(const char* db_path, const EdbStorageConfig& cfg) = 0;
    virtual void close() = 0;

    // Page-level I/O
    virtual std::expected<void, EdbError> read_page(uint64_t page_id, std::span<std::byte> buf) = 0;
    virtual std::expected<void, EdbError> write_page(uint64_t page_id, std::span<const std::byte> buf) = 0;
    virtual std::expected<uint64_t, EdbError> allocate_page() = 0;
    virtual std::expected<void, EdbError> free_page(uint64_t page_id) = 0;

    // Transaction integration
    virtual std::expected<void, EdbError> begin_transaction(EdbTransaction& txn) = 0;
    virtual std::expected<void, EdbError> commit(EdbTransaction& txn) = 0;
    virtual std::expected<void, EdbError> abort(EdbTransaction& txn) = 0;

    // Checkpoint / recovery
    virtual std::expected<void, EdbError> checkpoint() = 0;
    virtual std::expected<void, EdbError> recover() = 0;

    virtual ~EdbStorageEngineOps() = default;
};
```

### Built-in Engines

| Engine | Path | Description |
|--------|------|-------------|
| Heap (row-store) | `src/storage/engine/heap/` | Standard slotted-page row store |
| Columnar | `src/storage/engine/columnar/` | Column-oriented for analytics |
| LSM-Tree | `src/storage/engine/lsm/` | Log-structured merge tree |

## Storage I/O Backend Interface (`src/storage/io/`)

```cpp
struct EdbStorageIOOps {
    std::string_view name;

    // Lifecycle
    virtual std::expected<void, EdbError> open(const char* path, const EdbIOConfig& cfg) = 0;
    virtual void close() = 0;

    // I/O — buffers passed as std::span for bounds safety
    virtual std::expected<size_t, EdbError> read(uint64_t offset, std::span<std::byte> buf) = 0;
    virtual std::expected<size_t, EdbError> write(uint64_t offset, std::span<const std::byte> buf) = 0;

    // Optional: scatter/gather I/O
    virtual std::expected<size_t, EdbError> readv(std::span<EdbIOVec> iov) { /* default: loop */ }
    virtual std::expected<size_t, EdbError> writev(std::span<const EdbIOVec> iov) { /* default: loop */ }

    // Optional: memory mapping
    virtual std::expected<std::byte*, EdbError> mmap(uint64_t offset, size_t len, int prot) { return std::unexpected(EdbError::NotSupported); }
    virtual std::expected<void, EdbError>       munmap(std::byte* addr, size_t len) { return std::unexpected(EdbError::NotSupported); }

    // Durability
    virtual std::expected<void, EdbError> sync() = 0;
    virtual std::expected<void, EdbError> datasync() = 0;
    virtual std::expected<void, EdbError> sync_range(uint64_t offset, size_t len) = 0;

    // Management
    virtual std::expected<void, EdbError>     truncate(uint64_t size) = 0;
    virtual std::expected<uint64_t, EdbError> get_size() = 0;

    virtual ~EdbStorageIOOps() = default;
};
```

### Backend Implementations

#### POSIX Backend (`src/storage/io/posix/`)

- Uses `open`, `pread64`/`pwrite64`, `mmap`, `msync`, `ftruncate`
- Configurable `O_DIRECT` for bypassing page cache
- Supports `fsync`, `fdatasync`, `sync_file_range`
- Default backend for compatibility

#### xNVMe Backend (`src/storage/io/xnvme/`)

- Uses [xNVMe](https://xnvme.io/) library for direct NVMe command submission
- Supports both sync and async I/O via command queues
- Bypasses kernel block layer entirely for ultra-low latency
- Requires NVMe device access (may need `vfio-pci` or `uio_pci_generic`)

Key considerations:
- Sector size alignment (typically 4KB or 512B)
- Command queue depth management
- No implicit caching — explicit buffer management required
- See `docs/XNVME.md` for setup and device binding

### Backend Selection

Configured at database initialization:

```c
EdbIOConfig io_cfg = {
    .backend = "xnvme",        /* or "posix" */
    .path = "/dev/ng0n1",     /* NVMe namespace or file path */
    .page_size = 4096,
    .use_direct_io = true,
    .queue_depth = 256         /* xNVMe specific */
};
```

## Page Format

Default page size: 8KB (configurable at init time).

```
+--------------------------------------------------+
| Page Header (24 bytes)                           |
|   - checksum (4)                                 |
|   - page_id (8)                                  |
|   - lsn (8)                                      |
|   - flags (2)                                    |
|   - free_space_offset (2)                        |
+--------------------------------------------------+
| Tuple Directory (slot array, grows downward)     |
+--------------------------------------------------+
| Free Space                                       |
+--------------------------------------------------+
| Tuple Data (grows upward)                        |
+--------------------------------------------------+
```

## Buffer Pool (`src/storage/buffer/`)

- Clock-sweep replacement policy initially; consider LRU-2 later
- Pin count + usage bit per frame
- Dirty page tracking per frame
- Write-back via storage I/O backend (not directly to POSIX)
- **Important**: Buffer pool is storage-engine-agnostic but I/O-backend-aware

## WAL (`src/storage/wal/`)

- Append-only log, sequential writes via storage I/O backend
- LSN monotonically increasing
- Group commit with configurable flush policy
- Checkpoints truncate old WAL

## Common Tasks

### Adding a New Storage Engine

1. Create directory under `src/storage/engine/<name>/`
2. Define abstract class extending `EdbStorageEngineOps`; add `EDB_ASSERT` preconditions on every public method
3. Write unit tests in `tests/unit/storage/engine/<name>/` before any implementation
4. Implement the vtable methods
5. Register in `src/storage/engine/registry.cpp`
6. Document page format and limitations in `docs/storage/<name>.md`

### Adding a New I/O Backend

1. Create directory under `src/storage/io/<name>/`
2. Define class extending `EdbStorageIOOps`; add `EDB_ASSERT` preconditions (e.g., alignment requirements for xNVMe)
3. Write tests **before** implementing — verify behavior matches POSIX backend for the same byte sequences
4. Implement `open`/`close`, `read`/`write`, `sync`, `truncate`
5. Register in `src/storage/io/registry.cpp`
6. Add regression test for any device-specific edge case discovered
7. Document device setup and performance characteristics

### Performance Testing

Use `tests/storage/io/io_bench.c` to compare backends:

```bash
./build/tests/storage/io/io_bench --backend=posix --path=/tmp/test.db --page-size=4096
./build/tests/storage/io/io_bench --backend=xnvme --path=/dev/ng0n1 --page-size=4096
```

## Anti-patterns

- **Direct POSIX in engine**: Storage engines must NEVER call `pread`/`pwrite`/`mmap` directly
- **Backend assuming page size**: I/O backends work in bytes; page interpretation is engine-level
- **Global buffer pool**: Pass buffer pool handle explicitly; shared-nothing requires per-instance pools
- **Ignoring alignment**: xNVMe requires 4KB aligned offsets and buffers; always round up
