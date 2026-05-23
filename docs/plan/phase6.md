# Phase 6 — Transactions 🔲

MVCC visibility + WAL for crash recovery.

## MVCC

- Each tuple header carries `xmin` (inserting transaction ID) and `xmax` (deleting transaction ID)
- Visibility rule: tuple visible if `xmin` committed before snapshot **and** `xmax` not yet committed at snapshot time
- Snapshot taken at transaction start (snapshot isolation)

## WAL (`src/transaction/wal/`)

```cpp
struct WalRecord {
    u64  lsn;      // log sequence number, monotonically increasing
    u32  xid;      // transaction ID
    u8   type;     // INSERT / UPDATE / DELETE / COMMIT / ABORT / CHECKPOINT
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

## Lock Manager

- Row-level read/write locks, two-phase locking (2PL)
- Deadlock detection via wait-for graph (DFS cycle detection)

## Deliverables

- [ ] Transaction ID allocator
- [ ] MVCC tuple visibility (`xmin`/`xmax` headers in heap tuple)
- [ ] WAL append / flush / replay
- [ ] Lock manager with deadlock detection
- [ ] Crash recovery test: write → crash-simulate → recover → verify
