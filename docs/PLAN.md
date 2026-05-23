# EDB Development Plan

> Status legend: 🔲 not started · 🔄 in progress · ✅ done
>
> **Current priority**: Phase 0 → 1 → 2. Goal: end-to-end write-page / read-page through the full storage stack.

---

## Phase 0 — Project Infrastructure 🔲

Stand up the build system, tooling, and shared utilities that every later phase depends on. No feature code yet.

| Deliverable | Details |
|---|---|
| CMake skeleton | Root + per-subsystem `CMakeLists.txt`, `compile_commands.json`, `format`/`format-check`/`tidy` targets |
| `.clang-format` | Enforce consistent style across all `src/` and `tests/` |
| `.clang-tidy` | `cppcoreguidelines-*`, `modernize-*`, `bugprone-*`, `readability-*`, `performance-*` |
| `src/utils/primitives.hpp` | `i8`…`i64`, `u8`…`u64`, `f32`/`f64`, `b8`, `usize`/`isize` wrappers |
| `src/utils/error.hpp` | `EdbError` enum, `EdbResult<T>` alias for `std::expected<T, EdbError>` |
| `src/utils/log.hpp` | `edb_log(level, module, fmt, ...)` structured logging |
| gtest integration | CMake `FetchContent` for googletest; `tests/` skeleton |
| CI gate | `format-check` → `tidy` → `ctest` (ASan + UBSan) |

---

## Phase 1 — Storage I/O Backend 🔲

Define the lowest layer: how EDB reads and writes bytes to a device. The interface must be stable before any engine code is written.

### Interface: `EdbStorageIOOps` (`src/storage/io/io_ops.hpp`)

```cpp
/// Abstract interface for all storage I/O backends.
/// Implementations: posix, xnvme, io_uring (future), RDMA (future).
/// Thread-safety: all methods must be safe to call concurrently from
/// different threads with non-overlapping offset ranges.
struct EdbStorageIOOps {
    virtual ~EdbStorageIOOps() = default;

    // --- Lifecycle ---

    /// Open a file/device at `path`. Config is backend-specific.
    virtual auto open(const char* path, const EdbIOConfig& cfg)
        -> EdbResult<void>
        pre { path != nullptr } = 0;

    virtual auto close() -> EdbResult<void> = 0;

    // --- Synchronous I/O ---

    /// Read `buf.size()` bytes from `offset`. Returns bytes actually read.
    virtual auto read(u64 offset, std::span<std::byte> buf)
        -> EdbResult<usize>
        pre { !buf.empty() } = 0;

    /// Write `buf.size()` bytes to `offset`. Returns bytes actually written.
    virtual auto write(u64 offset, std::span<const std::byte> buf)
        -> EdbResult<usize>
        pre { !buf.empty() } = 0;

    // --- Scatter / gather (optional, default: loop over read/write) ---

    virtual auto readv(std::span<EdbIOVec> iov)  -> EdbResult<usize>;
    virtual auto writev(std::span<const EdbIOVec> iov) -> EdbResult<usize>;

    // --- Memory mapping (optional) ---

    virtual auto mmap(u64 offset, usize len, i32 prot)
        -> EdbResult<std::byte*> {
        return std::unexpected(EdbError::NotSupported);
    }
    virtual auto munmap(std::byte* addr, usize len) -> EdbResult<void> {
        return std::unexpected(EdbError::NotSupported);
    }

    // --- Durability ---

    virtual auto sync()      -> EdbResult<void> = 0;  // full device sync
    virtual auto datasync()  -> EdbResult<void> = 0;  // data-only
    virtual auto sync_range(u64 offset, usize len) -> EdbResult<void>;

    // --- File management ---

    virtual auto truncate(u64 size) -> EdbResult<void> = 0;
    virtual auto file_size()        -> EdbResult<u64>  = 0;
};

struct EdbIOVec {
    u64              offset;
    std::span<std::byte> buf;
};

struct EdbIOConfig {
    usize page_size     = usize{4096};
    b8    use_direct_io = b8{false};   // O_DIRECT / xNVMe bypass
    u32   queue_depth   = u32{1};      // async queue depth (xNVMe)
};
```

### Implementations

| Backend | Path | Notes |
|---|---|---|
| POSIX | `src/storage/io/posix/` | `pread64`/`pwrite64`, optional `O_DIRECT`, `fsync`/`fdatasync` |
| xNVMe | `src/storage/io/xnvme/` | Phase 8; direct NVMe submission, 4 KB alignment mandatory |

### Registration

```cpp
// src/storage/io/registry.hpp
auto edb_io_backend_register(std::string_view name,
                              std::unique_ptr<EdbStorageIOOps> backend)
    -> EdbResult<void>;

auto edb_io_backend_get(std::string_view name)
    -> EdbResult<EdbStorageIOOps*>;
```

### Deliverables

- [ ] `EdbStorageIOOps` abstract class with contracts
- [ ] POSIX backend passing all unit tests
- [ ] I/O benchmark harness (`tests/storage/io/io_bench.cpp`)

---

## Phase 2 — Storage Engine (Heap) 🔲

Define how the database reads and writes logical pages. The engine delegates all device I/O to an `EdbStorageIOOps` instance — it never calls POSIX directly.

### Interface: `EdbStorageEngineOps` (`src/storage/engine/engine_ops.hpp`)

```cpp
/// Abstract interface for all storage engines.
/// Implementations: heap (row-store), columnar, LSM-tree.
/// Thread-safety: methods are safe to call concurrently across different
/// transactions; same-transaction calls must be serialized by the caller.
struct EdbStorageEngineOps {
    virtual ~EdbStorageEngineOps() = default;

    // --- Lifecycle ---

    /// Attach engine to an already-open I/O backend.
    virtual auto open(EdbStorageIOOps& io, const EdbEngineConfig& cfg)
        -> EdbResult<void> = 0;

    virtual auto close() -> EdbResult<void> = 0;

    // --- Page I/O ---

    virtual auto read_page(u64 page_id, std::span<std::byte> buf)
        -> EdbResult<void>
        pre { buf.size() >= page_size() } = 0;

    virtual auto write_page(u64 page_id, std::span<const std::byte> buf)
        -> EdbResult<void>
        pre { buf.size() == page_size() } = 0;

    virtual auto allocate_page() -> EdbResult<u64> = 0;
    virtual auto free_page(u64 page_id) -> EdbResult<void> = 0;

    // --- Transaction lifecycle ---

    virtual auto begin(EdbTransaction& txn)  -> EdbResult<void> = 0;
    virtual auto commit(EdbTransaction& txn) -> EdbResult<void> = 0;
    virtual auto abort(EdbTransaction& txn)  -> EdbResult<void> = 0;

    // --- Durability ---

    virtual auto checkpoint() -> EdbResult<void> = 0;
    virtual auto recover()    -> EdbResult<void> = 0;

    // --- Metadata ---

    virtual auto page_size() const -> usize = 0;
    virtual auto page_count()      -> EdbResult<u64> = 0;
};

struct EdbEngineConfig {
    usize page_size         = usize{8192};
    usize buffer_pool_pages = usize{1024};  // buffer pool capacity in pages
};
```

### Page Format (Heap)

```
 0                   8                  16                  24
 +-------------------+-------------------+-------------------+
 | checksum (u32)    | flags (u16)       | free_off (u16)    |
 +-------------------+-------------------+-------------------+
 | page_id (u64)                                             |
 +-----------------------------------------------------------+
 | lsn (u64)                                                 |
 +-----------------------------------------------------------+
 | slot[0] offset(u16) + len(u16) | slot[1] ... | slot[N]   |  ← grows ↓
 +-----------------------------------------------------------+
 |                    free space                             |
 +-----------------------------------------------------------+
 |           tuple data (packed)                             |  ← grows ↑
 +-----------------------------------------------------------+
```

### Buffer Pool (`src/storage/buffer/`)

- Fixed-size frame array, each frame holds one page
- Clock-sweep replacement (second-chance bit)
- Pin count per frame; pinned frames are never evicted
- Dirty frames written back via `EdbStorageIOOps::write`

### Registration

```cpp
auto edb_engine_register(std::string_view name,
                          std::function<std::unique_ptr<EdbStorageEngineOps>()> factory)
    -> EdbResult<void>;

auto edb_engine_create(std::string_view name)
    -> EdbResult<std::unique_ptr<EdbStorageEngineOps>>;
```

### Deliverables

- [ ] `EdbStorageEngineOps` abstract class with contracts
- [ ] Page format header (`src/storage/engine/page.hpp`)
- [ ] Buffer pool with clock-sweep
- [ ] Heap engine: `read_page`, `write_page`, `allocate_page`, `free_page`
- [ ] Unit tests + integration test (write N pages, reopen, verify)

---

## Phase 3 — Type System 🔲

Extensible type registry modelled after PostgreSQL's `pg_type`. Types can be built-in or loaded at runtime from extensions.

### Interface: `EdbTypeOps` (`src/types/type_ops.hpp`)

```cpp
/// Operations every registered type must implement.
template <typename T>
concept EdbTypeImpl = requires(T v, std::string_view text,
                                const T& a, const T& b) {
    // Parsing / formatting
    { T::from_text(text) } -> std::same_as<EdbResult<T>>;
    { T::to_text(v)      } -> std::same_as<std::string>;

    // Comparison — must form a strict total order
    { T::compare(a, b)   } -> std::same_as<std::strong_ordering>;

    // Hashing — consistent with compare
    { T::hash(v)         } -> std::same_as<usize>;

    // Fixed or variable size
    { T::fixed_size()    } -> std::same_as<std::optional<usize>>;
};

struct EdbType {
    std::string  name;
    std::optional<usize> fixed_size;   // nullopt → variable-length

    std::function<EdbResult<std::vector<std::byte>>(std::string_view)> from_text;
    std::function<std::string(std::span<const std::byte>)>             to_text;
    std::function<std::strong_ordering(std::span<const std::byte>,
                                       std::span<const std::byte>)>    compare;
    std::function<usize(std::span<const std::byte>)>                   hash;
};
```

### Registry (`src/types/registry.hpp`)

```cpp
class EdbTypeRegistry {
public:
    template <EdbTypeImpl T>
    auto register_type(std::string_view name) -> EdbResult<void>;

    auto lookup(std::string_view name) const -> EdbResult<const EdbType*>;
    auto lookup(u32 oid)               const -> EdbResult<const EdbType*>;
};
```

### Built-in Types

| Name | C++ type | Fixed size |
|---|---|---|
| `int32` | `i32` | 4 |
| `int64` | `i64` | 8 |
| `float64` | `f64` | 8 |
| `bool` | `b8` | 1 |
| `text` | `std::string` | variable |

### Deliverables

- [ ] `EdbTypeOps` concept + `EdbType` struct
- [ ] `EdbTypeRegistry` with OID assignment
- [ ] All 5 built-in types with unit tests (from_text/to_text round-trip, compare ordering, hash consistency)

---

## Phase 4 — Catalog 🔲

System tables that describe all database objects. The catalog is itself stored using the heap engine from Phase 2.

### System Tables

| Table | Key Columns | Purpose |
|---|---|---|
| `edb_type` | `oid`, `name`, `typlen` | Registered types |
| `edb_class` | `oid`, `name`, `relkind` | Tables, indexes, sequences |
| `edb_attribute` | `attrelid`, `attnum`, `name`, `type_oid` | Columns |
| `edb_index` | `oid`, `indrelid`, `am_oid` | Indexes |

### Catalog API (`src/catalog/catalog.hpp`)

```cpp
class EdbCatalog {
public:
    // Lookup (read through cache)
    auto get_type(std::string_view name)  const -> EdbResult<CatalogType>;
    auto get_class(std::string_view name) const -> EdbResult<CatalogClass>;
    auto get_attributes(u32 class_oid)    const -> EdbResult<std::vector<CatalogAttribute>>;

    // DDL (invalidates cache entries)
    auto create_table(const CreateTableSpec&) -> EdbResult<u32 /*oid*/>;
    auto drop_table(u32 class_oid)            -> EdbResult<void>;
};
```

- **Cache**: hash map keyed by OID + name; invalidated on any DDL
- **Bootstrap**: `initdb` inserts rows for system types and the catalog tables themselves

### Deliverables

- [ ] System table schemas and heap-engine-backed storage
- [ ] `initdb` bootstrap sequence
- [ ] `EdbCatalog` read/write API with cache
- [ ] Unit tests for bootstrap + DDL round-trips

---

## Phase 5 — Basic Query Engine 🔲

Parse SQL, bind to catalog, execute via Volcano iterator model.

### Components

```
SQL text
   │
   ▼
┌──────────┐   tokens    ┌──────────┐   AST      ┌──────────┐
│  Lexer   │ ──────────► │  Parser  │ ─────────► │ Analyzer │
└──────────┘             └──────────┘            └──────────┘
                                                      │ bound plan
                                                      ▼
                                                 ┌──────────┐
                                                 │ Executor │
                                                 └──────────┘
```

### Executor Nodes (`src/query/executor/`)

| Node | Interface |
|---|---|
| `SeqScan` | Iterate all tuples in a heap relation |
| `Filter` | Evaluate predicate, pass matching tuples |
| `Project` | Evaluate expression list, emit result tuples |
| `Insert` / `Update` / `Delete` | DML nodes, write through heap engine |

All nodes implement:

```cpp
struct EdbExecNode {
    virtual auto open()              -> EdbResult<void> = 0;
    virtual auto next(Tuple& out)    -> EdbResult<b8>   = 0;  // false = done
    virtual auto close()             -> EdbResult<void> = 0;
    virtual ~EdbExecNode() = default;
};
```

### Deliverables

- [ ] Lexer + recursive-descent parser (SELECT / INSERT / UPDATE / DELETE / CREATE TABLE)
- [ ] Analyzer: resolve table/column names via catalog, type-check expressions
- [ ] `SeqScan`, `Filter`, `Project`, DML nodes
- [ ] Integration test: CREATE TABLE → INSERT → SELECT → verify

---

## Phase 6 — Transactions 🔲

MVCC visibility + WAL for crash recovery.

### MVCC

- Each tuple header carries `xmin` (inserting transaction ID) and `xmax` (deleting transaction ID)
- Visibility rule: tuple visible if `xmin` committed before snapshot and `xmax` not yet committed

### WAL (`src/transaction/wal/`)

```cpp
struct WalRecord {
    u64  lsn;         // log sequence number, monotonically increasing
    u32  xid;         // transaction ID
    u8   type;        // INSERT / UPDATE / DELETE / COMMIT / ABORT / CHECKPOINT
    u64  page_id;
    std::vector<std::byte> payload;
};

struct EdbWal {
    virtual auto append(const WalRecord&) -> EdbResult<u64 /*lsn*/> = 0;
    virtual auto flush(u64 lsn)           -> EdbResult<void>        = 0;
    virtual auto replay(u64 from_lsn)     -> EdbResult<void>        = 0;
};
```

- Group commit: buffer multiple records, flush on commit or timeout
- Checkpoint: record dirty page set + current LSN; truncate WAL tail

### Lock Manager

- Row-level read/write locks, two-phase locking
- Deadlock detection via wait-for graph (DFS cycle detection)

### Deliverables

- [ ] Transaction ID allocator
- [ ] MVCC tuple visibility
- [ ] WAL append / flush / replay
- [ ] Lock manager with deadlock detection
- [ ] Crash recovery test: write → crash-simulate → recover → verify

---

## Phase 7 — Network 🔲

PostgreSQL wire protocol v3, simple query mode only.

### Message Flow

```
Client                         Server
  │── StartupMessage ─────────►│
  │◄── AuthenticationOk ───────│
  │◄── ReadyForQuery ──────────│
  │── Query("SELECT ...") ─────►│
  │◄── RowDescription ─────────│
  │◄── DataRow (×N) ───────────│
  │◄── CommandComplete ─────────│
  │◄── ReadyForQuery ──────────│
```

### Deliverables

- [ ] TCP listener + connection handler (one thread per connection initially)
- [ ] Startup / auth (trust mode for now)
- [ ] `Query` → parse → execute → `RowDescription` + `DataRow` + `CommandComplete`
- [ ] Error response (`ErrorResponse` message)
- [ ] Integration test with `psql` client

---

## Phase 8 — xNVMe I/O Backend 🔲

Second `EdbStorageIOOps` implementation using the [xNVMe](https://xnvme.io/) library for direct NVMe command submission.

### Key Constraints

- I/O buffers **must** be aligned to device sector size (typically 4 KB) — use `posix_memalign` or xNVMe DMA allocator
- Offsets **must** be sector-aligned: `pre { offset % sector_size == u64{0} }`
- No kernel page cache involvement — explicit buffer management required
- Command queue depth is configured at `open()` time; do not exceed it

### Deliverables

- [ ] xNVMe backend implementing full `EdbStorageIOOps`
- [ ] Alignment contract on `read`/`write` preconditions
- [ ] Queue depth management (submission / completion ring)
- [ ] Benchmark comparing POSIX vs xNVMe: latency p50/p99, throughput (4 KB / 64 KB pages)

---

## Phase 9 — Distributed (Future) 🔲

Horizontal scaling via shared-nothing sharding + Raft replication. Design is deferred; foundations are laid in earlier phases (no global state, context handles everywhere).

| Component | Approach |
|---|---|
| Replication | Raft (catalog + WAL) |
| Partitioning | Hash / range on partition key from catalog |
| Routing | Coordinator routes queries to shard owners |
| Distributed txn | 2PC coordinator, or Percolator-style |
| Distributed deadlock | Cycle detection across lock manager instances |
