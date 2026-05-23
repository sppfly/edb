# Phase 2 — Storage Layer 🔄

Build the storage stack above the I/O backend. The goal is end-to-end write-page / read-page through the full stack, with a clean multi-engine abstraction.

## Storage Stack

```
Query Executor
      │  scan_next() / insert_tuple()
      ▼
EdbStorageEngineOps        ← pluggable engine interface (the multi-engine seam) ✅
  ├── HeapEngine           (row-store, slot array)     Phase 2d
  ├── ColumnarEngine       (column stripes)            future
  ├── PAXEngine            (columns within pages)      future
  ├── VectorEngine         (ANN / HNSW)                future
  └── FullTextEngine       (inverted index)            future
      │ (engines that need paged I/O use the buffer pool)
      ▼
Buffer Pool Manager        ← shared, format-agnostic page cache   Phase 2c
      │
      ▼
Page Store                 ← maps page_id → byte offset           Phase 2a ✅
      │
      ▼
EdbStorageIOOps            ✅ Phase 1
```

**Key design decisions:**

- `EdbStorageEngineOps` is at the **tuple/scan** level, not the raw-page level. The query executor never sees page IDs.
- The Buffer Pool is **format-agnostic** — it caches raw bytes and has no heap-specific knowledge. Columnar engines may bypass it entirely and use `mmap` directly.
- The Disk Scheduler is **not needed for POSIX sync I/O** — the kernel handles it. A scheduler will be added in Phase 8 alongside xNVMe async submission.

---

## Phase 2a — Page Store ✅

Thin mapping layer between logical `page_id` and byte offsets. Sits directly on `EdbStorageIOOps`.

### Interface (`src/storage/page/page_store.hpp`)

```cpp
struct EdbPageStoreConfig {
    usize page_size = usize{8192};
};

class EdbPageStore {
public:
    auto open(EdbStorageIOOps& io, const EdbPageStoreConfig& cfg) -> EdbResult<void>;
    auto close() -> EdbResult<void>;

    auto read_page(u64 page_id, std::span<std::byte> buf)       -> EdbResult<void>;
    auto write_page(u64 page_id, std::span<const std::byte> buf) -> EdbResult<void>;

    auto allocate_page() -> EdbResult<u64>;   // extends file if needed
    auto page_count()    -> EdbResult<u64>;
    auto page_size() const -> usize;
    auto sync()          -> EdbResult<void>;
};
```

`page_id × page_size = byte offset`. No caching, no format knowledge.

### Deliverables

- [x] `EdbPageStore` class with contracts
- [x] `allocate_page` extends the file via `truncate`
- [x] Unit tests: write/read round-trip, multi-page, unallocated page errors, corruption detection

---

## Phase 2b — `EdbStorageEngineOps` Interface ✅

Define the tuple-level interface that **all** storage engines implement. This must be stable before any concrete engine is written.

### Interface (`src/storage/engine/engine_ops.hpp`)

```cpp
struct EdbStorageEngineOps {
    virtual ~EdbStorageEngineOps() = default;

    // Lifecycle
    auto open(EdbPageStore& store, const EdbEngineConfig& cfg) -> EdbResult<void>;
    auto close() -> EdbResult<void>;

    // DML — tuple level
    auto insert(std::span<const std::byte> tuple) -> EdbResult<TupleId>;
    auto delete_(TupleId id)                       -> EdbResult<void>;
    auto update(TupleId id, std::span<const std::byte> tuple) -> EdbResult<TupleId>;

    // Scan
    auto begin_scan()             -> EdbResult<ScanHandle>;
    auto scan_next(ScanHandle& h) -> EdbResult<std::optional<Tuple>>;
    auto end_scan(ScanHandle& h)  -> void;

    // Metadata
    auto page_size() const -> usize;

protected:
    // *_impl hooks (same pattern as EdbStorageIOOps)
};

struct TupleId {
    u64 page_id;
    u16 slot_idx;
};
```

### Deliverables

- [x] `EdbStorageEngineOps` abstract class with `EDB_PRE` contracts
- [x] `EdbTupleId`, `EdbTuple`, `EdbScanHandle` supporting types
- [x] Mock engine + interface contract tests (no concrete engine yet)

---

## Phase 2c — Buffer Pool Manager 🔲

Shared page cache used by engines that need paged I/O. Format-agnostic.

### Design

- Fixed-size frame array; each frame holds one page (`page_size` bytes)
- **Clock-sweep** replacement (second-chance bit); O(1) amortized eviction
- Pin count per frame — pinned frames are never evicted
- Dirty flag per frame — dirty frames written back to `EdbPageStore` on eviction or explicit flush
- Thread-safe: one mutex per frame bucket (or a latch-free design later)

### Interface (`src/storage/buffer/buffer_pool.hpp`)

```cpp
struct EdbBufferPoolConfig {
    usize capacity_pages = usize{1024};
};

class EdbBufferPool {
public:
    auto open(EdbPageStore& store, const EdbBufferPoolConfig& cfg) -> EdbResult<void>;
    auto close() -> EdbResult<void>;   // flush all dirty frames

    // Returns a pinned frame. Caller must call unpin() when done.
    auto fetch(u64 page_id)        -> EdbResult<FrameHandle>;
    auto fetch_new(u64 page_id)    -> EdbResult<FrameHandle>;  // new/blank page

    auto unpin(FrameHandle& h, b8 dirty) -> void;
    auto flush(u64 page_id)              -> EdbResult<void>;
    auto flush_all()                     -> EdbResult<void>;
};

struct FrameHandle {
    std::span<std::byte> data;   // direct access to frame bytes
    // move-only; destructor asserts pin was released
};
```

### Deliverables

- [ ] `EdbBufferPool` with clock-sweep eviction
- [ ] `FrameHandle`: move-only RAII; asserts unpin before destruction
- [ ] Unit tests: hit/miss/eviction/dirty-writeback/pin-guard

---

## Phase 2d — Heap Storage Engine 🔲

First concrete implementation of `EdbStorageEngineOps`. Row-store with slot-array page format.

### Page Format

```
Offset  Size  Field
──────  ────  ─────────────────────────────────────────
0       4     checksum  (u32, CRC32C over bytes 8..end)
4       2     flags     (u16: dirty, has-free-space, ...)
6       2     slot_count (u16)
8       2     free_start (u16, offset of start of free space)
10      2     free_end   (u16, offset of end of free space)
12      8     page_id   (u64)
20      8     lsn       (u64, filled in by WAL in Phase 6)
28      ...   slot array: slot[i] = {offset: u16, len: u16}, grows ↓
              free space
              tuple data, grows ↑
```

Each tuple is opaque bytes from the engine's perspective; the type system (Phase 3) interprets the payload.

### Deliverables

- [ ] `HeapEngine : EdbStorageEngineOps`
- [ ] `page.hpp`: page header layout, slot array helpers, free-space calculation
- [ ] `insert`: find page with free space (free-space map), write slot + tuple
- [ ] `delete_`: mark slot as dead (tombstone), no immediate compaction
- [ ] `scan_next`: iterate live slots across all pages, skip tombstones
- [ ] `vacuum`: compact dead slots, reclaim free space (can be a later sub-task)
- [ ] Integration test: insert N tuples → reopen → scan → verify all present
