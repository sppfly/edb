# Phase 6 - Transactions

Phase 6 adds transactional correctness to the SQL path without turning the storage engine into one untestable mega-change. The target is a PostgreSQL-like first implementation: MVCC tuple versions, transaction status tracking, redo WAL for crash recovery, and transaction locks for write conflicts. Serializable isolation, predicate locking, vacuum, and advanced group commit are later phases.

The implementation must stay shared-nothing ready: transaction, lock, WAL, buffer, catalog, and storage state are owned by explicit database/session context handles, not process-wide globals.

## Design Choice

EDB will start with:

```text
Snapshot Isolation + PostgreSQL-like MVCC + redo WAL + transaction status log
```

Why this path:

- Reads do not block writes; writes do not block reads.
- Tuple visibility is local and testable: `xmin`, `xmax`, transaction status, and snapshot.
- Abort does not require physical undo for user tuples. Aborted inserts/deletes are hidden by visibility rules and later cleaned by vacuum.
- Redo WAL plus page LSNs gives a clear crash-recovery path without implementing full ARIES undo immediately.
- The design leaves room for future storage engines, indexes, group commit, checkpoints, and eventually serializable isolation.

Alternatives considered:

- **InnoDB-style undo MVCC**: powerful, but requires undo segments and historical version reconstruction from day one. Too much machinery for the first EDB transaction layer.
- **Full ARIES**: excellent recovery model, but analysis/redo/undo, dirty page tables, transaction tables, and compensation log records are too heavy for the first pass. EDB should borrow page LSN, redo, and checkpoint ideas first.
- **Pure two-phase locking**: simpler conceptually, but makes reads and writes block each other and is a poor fit for the SQL engine we are building.
- **SQLite-style rollback journal**: simple and robust, but does not match the desired multi-version, multi-backend architecture.

## Isolation Semantics

The first committed isolation level is **snapshot isolation**.

- A snapshot is taken at transaction start.
- A transaction sees versions committed before its snapshot and its own writes.
- A transaction does not see versions created by transactions active at snapshot time.
- `SELECT` uses MVCC visibility and does not acquire row read locks.
- `UPDATE` and `DELETE` acquire tuple exclusive locks and detect write-write conflicts.
- Serializable isolation is out of scope for Phase 6 because it needs predicate locks or SSI conflict tracking.

## Lock vs Latch

EDB must keep transaction locks and internal latches separate.

```text
lock  = transaction-level semantic lock on a logical object
latch = short-lived in-memory protection for an internal data structure
```

Locks:

- Protect logical objects such as relations and tuples.
- May wait.
- May be held until commit or abort.
- Participate in deadlock detection.
- Examples: tuple exclusive lock, relation exclusive lock for DDL.

Latches:

- Protect memory structures such as lock-table buckets, buffer frames, page contents, WAL buffers, and transaction maps.
- Must be held only for short critical sections.
- Must not be held while waiting for a transaction lock.
- Must not be held while performing storage I/O or fsync.
- Do not participate in transaction semantics.

Hard rules:

- Never wait for a transaction lock while holding any latch.
- Never perform storage I/O while holding a latch.
- Never expose latches through SQL or transaction APIs.
- Keep latch acquisition order fixed and documented.

Initial latch order:

```text
TransactionManager latch
  < LockManager partition latch
  < BufferPool mapping latch
  < BufferFrame latch
  < Heap page content latch
  < WAL insert latch
  < WAL flush latch
```

Transaction lock waiting happens outside this latch order after all latches have been released.

## Core Types

Names should not carry an `Edb` prefix inside `namespace edb`.

```cpp
struct TxId {
    u64 value;
};

enum class TxStatus : std::uint8_t {
    InProgress,
    Committed,
    Aborted,
};

struct Snapshot {
    TxId xmin;
    TxId xmax;
    std::vector<TxId> active;
};

struct Transaction {
    TxId id;
    Snapshot snapshot;
};
```

Heap tuple metadata belongs to the storage layer, not to `RowCodec`:

```cpp
struct TupleHeader {
    TxId xmin;
    TxId xmax;
    u16 flags;
};
```

Logical row encoding remains the job of the type layer; transaction metadata is added by heap/storage around the row payload.

## WAL Design

WAL must use the storage I/O abstraction, not raw POSIX calls. The first WAL format should be extensible enough for heap, transaction status, catalog, and future index records.

```cpp
struct WalRecordHeader {
    u64 lsn;
    u64 prev_lsn;
    u32 xid;
    u16 resource_manager;
    u16 type;
    u32 payload_len;
    u32 crc;
};
```

Initial resource managers:

- `Transaction`: commit, abort, checkpoint status records.
- `Heap`: insert, delete, update redo records.
- `Catalog`: can initially reuse heap records, but the boundary should remain explicit because catalog recovery has special bootstrap concerns later.

Initial WAL record types:

- `TX_COMMIT`
- `TX_ABORT`
- `HEAP_INSERT`
- `HEAP_DELETE`
- `HEAP_UPDATE`
- `CHECKPOINT`

Durability rule:

```text
commit record LSN = wal.append(...)
wal.flush(commit record LSN)
transaction status becomes durable committed
```

Data pages must not be flushed past WAL durability. Every heap page carries a `page_lsn`; recovery skips redo records where `page_lsn >= record.lsn`.

## Crash Recovery Model

Phase 6 uses redo-first recovery, not full ARIES undo.

Recovery outline:

1. Start from the beginning of WAL, or from the latest checkpoint once checkpoints exist.
2. Read WAL records in LSN order.
3. Rebuild durable transaction status from transaction records.
4. Redo idempotent heap records if the target page LSN is older than the record LSN.
5. Leave uncommitted or aborted versions on disk; MVCC visibility hides them.

Crash tests should be staged:

- committed insert survives crash and recovery
- uncommitted insert is invisible after recovery
- committed delete remains deleted after recovery
- uncommitted delete leaves the old row visible after recovery

Checkpointing starts simple:

- First implementation can replay from WAL start.
- Next add a checkpoint record with a `redo_lsn`.
- Later add dirty-page-table/fuzzy checkpoint support.

## Concurrency Model

The first implementation should be thread-safe but not prematurely lock-free.

Use:

- `std::mutex` / `std::condition_variable` for manager state and wait queues.
- `std::atomic` for monotonic counters such as next transaction ID and durable flush LSN where useful.
- Partitioned lock tables later; a single mutex is acceptable for the first correct implementation if the public interface does not assume it.

Thread-safe components:

- `TransactionManager`: protects next XID, transaction status, and active set.
- `LockManager`: protects holders, waiters, and wait-for graph.
- `WalManager`: separates append LSN allocation from durable flush state.
- `BufferPool`: protects mapping table and frame metadata.
- `HeapPage`: protects page slot array and free-space mutation with page latches.

Initial deterministic multi-thread tests:

- concurrent transaction begin produces unique XIDs
- exclusive tuple lock blocks a second writer until release
- two-transaction lock cycle is detected as a deadlock
- concurrent WAL append produces unique increasing LSNs
- concurrent autocommit inserts are all visible after commit

## Lock Manager

The lock manager handles transaction-level locks only.

Initial lock tags:

```cpp
enum class LockTagKind : std::uint8_t {
    Relation,
    Tuple,
};

enum class LockMode : std::uint8_t {
    Shared,
    Exclusive,
};

struct LockTag {
    LockTagKind kind;
    u32 relation_oid;
    TupleId tuple_id;
};
```

Initial lock policy:

- `SELECT`: no tuple read locks; use snapshot visibility.
- `INSERT`: no tuple lock for the new tuple; relation intent/write lock can be added after the tuple path is stable.
- `UPDATE` / `DELETE`: tuple exclusive lock held until commit or abort.
- DDL: relation exclusive lock.

Deadlock detection:

- Maintain a wait-for graph: transaction A waits for transaction B if B holds a conflicting lock A needs.
- Detect cycles with DFS when adding a wait edge.
- Abort the requester or youngest transaction first; exact victim policy can be refined later.

Timeout-only deadlock handling is not enough because it produces nondeterministic tests and poor diagnostics.

## Query Integration

`QueryEngine::execute()` initially remains a single-statement API. Phase 6 adds autocommit behavior around it:

```text
begin transaction
parse / bind / logical plan / physical plan / execute
commit on success
abort on error
```

Later SQL syntax can expose explicit transaction control:

```sql
BEGIN;
INSERT ...;
SELECT ...;
COMMIT;
```

Do not add explicit SQL transaction statements until autocommit transaction boundaries are correct and covered by regression tests.

## Sub-Phases

### 6a. Transaction Core

Deliverables:

- `TxId`, `TxStatus`, `Snapshot`, `Transaction`
- `TransactionManager::begin`, `commit`, `abort`, `status`, `snapshot`
- thread-safe active transaction tracking
- deterministic multi-thread XID allocation test

Validation:

- status transitions
- snapshot active set contents
- monotonic unique transaction IDs
- commit and abort remove transactions from active set

### 6b. MVCC Visibility Pure Layer

Deliverables:

- `TupleHeader`
- transaction-status reader interface
- pure visibility function

Validation:

- committed insert visible to later snapshot
- in-progress insert invisible to other transaction
- own insert visible to self
- aborted insert invisible
- committed delete invisible to later snapshot
- in-progress delete still visible to other transaction
- aborted delete visible

### 6c. Heap Tuple Header Integration

Deliverables:

- heap tuple header wrapping row payload
- transaction-aware `insert`, `delete`, `update`, and `scan`
- page latch around slot/free-space mutation
- compatibility wrappers for existing autocommit-style table APIs

Validation:

- existing heap/table/query tests still pass through compatibility path
- scan applies snapshot visibility
- update is represented as delete old + insert new

### 6d. Query Autocommit Transactions

Deliverables:

- `QueryEngine::execute()` wraps one statement in begin/commit/abort
- executor passes transaction context into table/storage operations
- SQL regression continues to pass

Validation:

- successful insert commits and is visible later
- failed statement aborts and leaves no visible partial write
- existing `sql/expected` regression remains stable

### 6e. WAL Record Format and WAL Manager

Deliverables:

- WAL record header with LSN, previous LSN, XID, resource manager, type, payload length, and CRC
- append/read/flush APIs backed by `StorageIOOps`
- synchronous commit flush policy
- concurrent append test

Validation:

- records round-trip through WAL file
- LSNs are unique and increasing under concurrent append
- flush makes records durable through reopen
- corrupt record CRC is rejected

### 6f. Heap WAL Redo and Recovery

Deliverables:

- heap `page_lsn`
- `HEAP_INSERT` redo
- transaction commit/abort WAL records
- recovery routine that rebuilds transaction status and replays heap redo

Validation:

- committed insert survives crash simulation
- uncommitted insert is invisible after recovery
- redo is idempotent when page LSN is already current

### 6g. Delete / Update MVCC

Deliverables:

- `xmax` delete marker
- tuple exclusive lock acquisition for delete/update
- write-write conflict detection
- update implemented as delete old + insert new

Validation:

- committed delete hides tuple from later snapshots
- uncommitted delete does not hide tuple from other transactions
- conflicting update/delete returns transaction error

### 6h. Lock Manager and Deadlock Detection

Deliverables:

- relation and tuple lock tags
- shared/exclusive compatibility matrix
- wait queues with condition variables
- wait-for graph and DFS cycle detection
- release-all on commit/abort

Validation:

- exclusive tuple lock blocks conflicting writer
- lock is granted after holder commits/aborts
- two-transaction deadlock is detected deterministically
- release-all wakes compatible waiters

### 6i. Checkpoint and Group Commit Groundwork

Deliverables:

- checkpoint record with `redo_lsn`
- recovery starts from checkpoint when present
- append LSN and flush LSN tracked separately
- group commit API shape, even if first policy flushes synchronously

Validation:

- recovery from checkpoint produces same visible state as replay from WAL start
- multiple commits can wait for a single flush boundary later

### 6j. Threading and Latch Discipline

Deliverables:

- documented latch order in code comments near manager/page implementations
- no transaction-lock wait while holding latches
- no storage I/O while holding latches
- deterministic two-thread and multi-thread tests for transaction manager, lock manager, WAL, and autocommit insert path

Validation:

- sanitizer-friendly tests pass
- no data races in manager-level tests
- lock/latch rules are enforced by code structure and reviewed in tests

## Phase 6 Success Criteria

Phase 6 is successful when EDB can:

1. run SQL statements inside autocommit transactions
2. expose correct snapshot visibility for committed, in-progress, and aborted tuple versions
3. recover committed writes after a simulated crash
4. hide uncommitted writes after recovery
5. block or abort conflicting writers deterministically
6. detect a simple transaction deadlock
7. keep regression SQL fixtures stable through the transactional path
