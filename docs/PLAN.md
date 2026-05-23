# EDB Development Plan

> Status legend: 🔲 not started · 🔄 in progress · ✅ done
>
> **Current priority**: Phase 2. Goal: end-to-end write-tuple / scan-tuple through the full storage stack.
>
> Detailed per-phase plans live in [`docs/plan/`](plan/).

---

## Overview

| Phase | Focus | Status | Detail |
|-------|-------|--------|--------|
| **0** | Project infrastructure (CMake, tooling, primitives, error, logging, gtest) | ✅ | [phase0.md](plan/phase0.md) |
| **1** | Storage I/O backend (`EdbStorageIOOps`, POSIX backend) | ✅ | [phase1.md](plan/phase1.md) |
| **2** | Storage layer (Page Store → Engine interface → Buffer Pool → Heap Engine) | 🔄 | [phase2.md](plan/phase2.md) |
| **3** | Type system (`EdbTypeRegistry`, `EdbTypeImpl`, built-in types) | 🔲 | [phase3.md](plan/phase3.md) |
| **4** | Catalog (system tables, initdb bootstrap, catalog cache) | 🔲 | [phase4.md](plan/phase4.md) |
| **5** | Query engine — basic (parser, analyzer, Volcano executor) | 🔲 | [phase5.md](plan/phase5.md) |
| **6** | Transactions (MVCC, WAL, row locks, deadlock detection) | 🔲 | [phase6.md](plan/phase6.md) |
| **7** | Network (PostgreSQL wire protocol v3, simple query mode) | 🔲 | [phase7.md](plan/phase7.md) |
| **8** | xNVMe I/O backend + Disk Scheduler + POSIX vs xNVMe benchmark | 🔲 | [phase8.md](plan/phase8.md) |
| **9** | Distributed — Raft, partitioning, 2PC (future) | 🔲 | [phase9.md](plan/phase9.md) |

---

## Architecture Layers

```
Query Executor
      │
      ▼
EdbStorageEngineOps   (pluggable: Heap, Columnar, PAX, Vector, FullText)
      │
      ▼
Buffer Pool Manager   (shared, format-agnostic page cache)
      │
      ▼
Page Store            (page_id → byte offset)
      │
      ▼
EdbStorageIOOps       (pluggable: POSIX ✅, xNVMe Phase 8, io_uring future)
      │
      ▼
Device
```

> **Disk Scheduler** sits between the Buffer Pool and the xNVMe I/O backend, added in Phase 8 when async submission makes it worthwhile. POSIX sync I/O relies on the kernel scheduler.

---

## Phase 0 — Project Infrastructure ✅

→ See [plan/phase0.md](plan/phase0.md) for full detail.

---

## Phase 1 — Storage I/O Backend ✅

→ See [plan/phase1.md](plan/phase1.md) for full detail.

---

## Phase 2 — Storage Layer 🔄

→ See [plan/phase2.md](plan/phase2.md) for full detail.

Sub-phases: **2a** Page Store · **2b** `EdbStorageEngineOps` interface · **2c** Buffer Pool · **2d** Heap Engine

---

## Phase 3 — Type System 🔲

→ See [plan/phase3.md](plan/phase3.md) for full detail.

---

## Phase 4 — Catalog 🔲

→ See [plan/phase4.md](plan/phase4.md) for full detail.

---

## Phase 5 — Basic Query Engine 🔲

→ See [plan/phase5.md](plan/phase5.md) for full detail.

---

## Phase 6 — Transactions 🔲

→ See [plan/phase6.md](plan/phase6.md) for full detail.

---

## Phase 7 — Network 🔲

→ See [plan/phase7.md](plan/phase7.md) for full detail.

---

## Phase 8 — xNVMe I/O Backend + Disk Scheduler 🔲

→ See [plan/phase8.md](plan/phase8.md) for full detail.

---

## Phase 9 — Distributed (Future) 🔲

→ See [plan/phase9.md](plan/phase9.md) for full detail.

