# Phase 8 — Async I/O Foundation + io_uring/xNVMe + Disk Scheduler 🔲

Phase 8 should not begin by assuming that xNVMe alone will produce meaningful gains. The real prerequisite for async I/O is that the upper storage/query layers can keep multiple independent requests in flight through prefetch, batched scans, or queued page requests.

The correct goal for this phase is:

1. add an async-capable request boundary above `EdbStorageIOOps`
2. implement at least one practical async backend for common Linux systems
3. add a scheduler only once the system can actually benefit from queue depth
4. keep xNVMe as a hardware-specific backend, not the only reason Phase 8 exists

## Phase 8 Entry Criteria

Phase 8 should wait until the local engine has enough stable behavior to measure. The entry criteria are:

- a `Database` / `Session` boundary owns storage state explicitly
- at least one workload can naturally issue multiple useful page requests, such as scan prefetch, batched page fetch, background flush, or background sync
- the benchmark harness can report queue depth, buffered/direct I/O mode, access pattern, latency, and throughput separately

Without these, an async backend can be implemented but not evaluated honestly.

## Why This Phase Exists

Synchronous POSIX I/O (`pread64`/`pwrite64`) delegates scheduling to the kernel. Replacing it with an async API only matters once EDB can issue more than one useful request at a time.

That does not require transactions first. It requires asynchronous consumers such as:

- scan readahead windows
- queued page fetches in the buffer pool or page store
- batched read/write submission for scans, spills, or maintenance
- future multi-query concurrency, WAL, and background flushing

So the dependency is not "wait for Phase 6 transactions". The dependency is "first create a request model that can exploit queue depth".

## Stack

```
Executor / Scan Path
      │
      ▼
Buffer Pool / Page Store
      │
      ▼
Async Request Boundary    ← introduced in Phase 8
      │
      ▼
Disk Scheduler            ← introduced only when requests can queue
      │
      ▼
EdbStorageIOOps
  ├─ POSIX sync backend (existing)
  ├─ io_uring backend (common Linux async path)
  └─ xNVMe backend (hardware-specific direct NVMe path)
```

## Phase 8 Sub-Phases

### 8a. Async Request Foundation

Add the minimal abstractions needed for upper layers to benefit from async I/O later.

Examples:

- page fetch requests that may be pending or completed
- scan readahead / prefetch windows
- buffer/page interfaces that can express multiple outstanding reads
- queue-depth-aware benchmark harnesses

This is the first Phase 8 task only after Phase 7a gives the storage/query stack a stable place to own request queues and benchmark state.

The request abstractions can be sketched early, but implementation should wait until the storage/query stack has a real consumer for queue depth. It still comes before any complex scheduler.

### 8b. io_uring Backend

Implement a Linux async backend that is practical on commodity SSDs and developer machines.

Why first:

- available on ordinary Linux systems
- lower integration friction than SPDK
- good reference backend for async submission/completion semantics
- likely the most realistic async backend to benchmark in this repository early

### 8c. Disk Scheduler

Introduce a scheduler only after 8a exists and there are real queued requests to manage.

Scheduler responsibilities:

- track in-flight requests and queue depth
- batch submission / completion
- issue prefetches
- optionally prioritize foreground reads over background work

Without async consumers, a scheduler is mostly ceremony.

### 8d. xNVMe Backend

Implement xNVMe as an additional backend for environments where direct NVMe submission is available and operationally justified.

This backend is valuable, but it should not be the only success criterion for Phase 8. On simple single-SSD developer setups, `io_uring` may be the more practical async comparison point.

## Key Constraints

### Async Foundation Constraints

- the request boundary must work in single-threaded execution first
- queue depth must be explicit and measurable
- synchronous fallback paths must remain available for correctness and debugging
- benchmarks must separate buffered I/O, direct I/O, and queue depth effects

### xNVMe Constraints

- I/O buffers **must** be aligned to device sector size (typically 4 KB) — use `posix_memalign` or an xNVMe DMA allocator
- offsets **must** be sector-aligned: `EDB_ASSERT(offset % sector_size == u64{0})`
- no kernel page cache involvement — explicit buffer management required
- command queue depth is configured at `open()` time; never exceed it

## Deliverables

- [ ] Async request boundary above `EdbStorageIOOps`
- [ ] Scan/buffer prefetch or queued-fetch capability sufficient to keep multiple requests in flight
- [ ] `io_uring` backend implementing async submission/completion semantics
- [ ] Queue-depth-aware benchmark matrix: POSIX vs `io_uring` under buffered/direct I/O and multiple access patterns
- [ ] Disk scheduler: async queue, submission/completion handling, optional priority / merge policy
- [ ] xNVMe backend implementing full `EdbStorageIOOps`
- [ ] Benchmark: POSIX vs `io_uring` vs xNVMe where hardware support exists
