# Phase 8 — xNVMe I/O Backend + Disk Scheduler 🔲

Second `EdbStorageIOOps` implementation using the [xNVMe](https://xnvme.io/) library for direct NVMe command submission. Also the right phase to introduce a Disk Scheduler, since async submission requires explicit request queueing.

## Why a Disk Scheduler here (not Phase 2)

Synchronous POSIX I/O (`pread64`/`pwrite64`) delegates scheduling to the kernel — adding an application-level scheduler buys nothing. With xNVMe and io_uring, commands are submitted to a ring buffer and completed asynchronously; an application-level scheduler can:
- Merge/reorder requests to saturate the NVMe queue depth
- Implement priority classes (WAL writes ahead of background scans)
- Prefetch pages predicted by the access pattern

## Stack (this phase only)

```
Buffer Pool
      │
      ▼
Disk Scheduler            ← new in Phase 8
  (submission ring, priority queue, merge window)
      │
      ▼
EdbStorageIOOps (xNVMe backend)
      │
      ▼
NVMe device (via xNVMe / SPDK)
```

## Key Constraints (xNVMe)

- I/O buffers **must** be aligned to device sector size (typically 4 KB) — use `posix_memalign` or xNVMe DMA allocator
- Offsets **must** be sector-aligned: `EDB_PRE(offset % sector_size == u64{0})`
- No kernel page cache involvement — explicit buffer management required
- Command queue depth is configured at `open()` time; never exceed it

## Deliverables

- [ ] xNVMe backend implementing full `EdbStorageIOOps`
- [ ] Alignment contracts on `read`/`write` preconditions
- [ ] Queue depth management (submission / completion ring)
- [ ] Disk scheduler: async queue, priority levels, merge window
- [ ] Benchmark: POSIX vs xNVMe — latency p50/p99, throughput (4 KB / 64 KB pages)
