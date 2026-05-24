# EDB Development Plan

> Status legend: 🔲 not started · 🔄 in progress · ✅ done
>
> **Current priority**: Phase 4e + Phase 6 hardening → Phase 7a. Goal: decouple catalog table storage from the heap engine, then turn the current autocommit transaction slice into a durable, session-scoped database context before adding a REPL or async I/O work.
>
> Detailed per-phase plans live in [`docs/plan/`](plan/).

---

## Overview

| Phase | Focus | Status | Detail |
|-------|-------|--------|--------|
| **0** | Project infrastructure (CMake, tooling, primitives, error, logging, gtest) | ✅ | [phase0.md](plan/phase0.md) |
| **1** | Storage I/O backend (`EdbStorageIOOps`, POSIX backend) | ✅ | [phase1.md](plan/phase1.md) |
| **2** | Storage layer (Page Store → Engine interface → Buffer Pool → Heap Engine) | ✅ | [phase2.md](plan/phase2.md) |
| **3** | Type system + typed value encoding (`EdbTypeRegistry`, built-in types, row serialization) | ✅ | [phase3.md](plan/phase3.md) |
| **4** | Table/catalog layer (developer table API, catalog persistence, initdb, cache, engine factory boundary) | 🔄 | [phase4.md](plan/phase4.md) |
| **5** | Query engine — basic SQL front-end over the table/catalog API | ✅ | [phase5.md](plan/phase5.md) |
| **6** | Transactions (MVCC, WAL, row locks, deadlock detection) | 🔄 | [phase6.md](plan/phase6.md) |
| **7** | Embedded database/session API + local REPL; optional network later | 🔲 | [phase7.md](plan/phase7.md) |
| **8** | Async I/O foundation + io_uring/xNVMe + Disk Scheduler | 🔲 | [phase8.md](plan/phase8.md) |
| **9** | Distributed shared-nothing cluster — coordinators, shard groups, Raft, 2PC | 🔲 | [phase9.md](plan/phase9.md) |

---

## Current Reality Check

The plan tracks two different things: completed vertical slices and full production-grade phase closure. Phase 5 has reached its minimum success criterion: SQL `CREATE TABLE`, `INSERT`, and `SELECT` flow through parser, binder, logical plan, physical plan, executor, catalog, table, row codec, heap engine, buffer pool, page store, and storage I/O.

Phase 4 is functionally usable but not architecturally closed. The catalog currently owns `EdbHeapEngine` directly inside its opened-table bundle, so catalog metadata and user table opening are still hardwired to the heap engine. Phase 4 remains in progress until catalog opens table storage through a storage-engine factory or equivalent relation storage boundary.

Phase 6 is in progress, not fully complete. EDB now has transaction IDs, snapshots, MVCC tuple visibility, transaction-aware heap scans/inserts/deletes/updates, autocommit wrapping in `QueryEngine::execute()`, a lock manager with deadlock detection, and WAL/recovery primitives. The remaining Phase 6 work is to connect WAL to the normal SQL commit path, introduce explicit database/session transaction ownership, expose SQL update/delete and later explicit transaction statements, and add cleanup for obsolete MVCC versions.

Near-term priorities:

- finish Phase 4e: replace catalog's direct `EdbHeapEngine` ownership with an engine factory / relation storage boundary
- finish the Phase 6 durability path: heap WAL append, commit record, flush rule, and recovery through the normal execution path
- introduce a `Database` / `Session` context that owns transaction, lock, WAL, catalog, and storage state explicitly
- keep the existing single-statement autocommit API as a compatibility path over the session API
- delay REPL, network, and async I/O until the local session and durability boundary is stable

---

## Architecture Layers

```
Query Executor
      │
      ▼
EdbStorageEngineOps   (pluggable: Heap, Columnar, PAX, Vector, FullText)
      │
      ▼
Buffer Pool Manager   (shared page cache; pluggable eviction policy)
      │
      ▼
Page Store            (page_id → byte offset)
      │
      ▼
EdbStorageIOOps       (pluggable: POSIX ✅, io_uring/xNVMe Phase 8)
      │
      ▼
Device
```

> **Disk Scheduler** is a Phase 8 component that only becomes worthwhile once the buffer/page path can keep multiple async requests in flight. POSIX sync I/O still relies on the kernel scheduler.

---

## Design Philosophy

EDB is not primarily trying to be the fastest or most feature-complete database first. Its primary goal is to be a strong single-node database kernel with explicit extension boundaries so that new storage layouts, access methods, execution strategies, and research ideas can be implemented without rewriting unrelated subsystems.

### Guiding Rules

- single-node correctness and local usability come before networked or distributed deployment
- each major layer should expose at least one explicit extension boundary
- new work should reuse existing boundaries instead of bypassing layers for short-term convenience
- distributed features must build on top of the single-node engine, not redefine its core abstractions
- experiments should be possible by replacing one component while leaving the rest of the stack unchanged

### Primary Experimentation Tracks

The plan should preserve room for experimentation in at least these areas:

- type system: new built-in or extension types, casts, and encodings
- storage I/O backends: POSIX, `io_uring`, xNVMe, future device-specific paths
- storage engines and layouts: heap, columnar, PAX, vector-aware, full-text-aware layouts
- access methods: B-tree, hash, inverted index, ANN/vector index, learned or hybrid index ideas
- execution backends: reference row executor, vectorized execution, morsel-driven scheduling, specialized operators
- physical algorithms: scan strategies, join algorithms, aggregation layouts, sorting/materialization strategies
- distributed placement/routing: shard mapping, replica placement, coordinator policies

### What The Plan Must Avoid

- hardwiring one storage layout into the catalog or query layers forever
- making the first reference executor the only possible execution model
- treating indexes as fixed built-ins instead of access-method plugins
- letting network or distributed work distort single-node interfaces too early

---

## Phase 0 — Project Infrastructure ✅

→ See [plan/phase0.md](plan/phase0.md) for full detail.

---

## Phase 1 — Storage I/O Backend ✅

→ See [plan/phase1.md](plan/phase1.md) for full detail.

---

## Phase 2 — Storage Layer ✅

→ See [plan/phase2.md](plan/phase2.md) for full detail.

Sub-phases: **2a** Page Store · **2b** `EdbStorageEngineOps` interface · **2c** Buffer Pool · **2d** Heap Engine

---

## Phase 3 — Type System ✅

→ See [plan/phase3.md](plan/phase3.md) for full detail.

Sub-phases: **3a** Type registry · **3b** Built-in types · **3c** Typed values and row encoding

---

## Phase 4 — Table and Catalog Layer 🔄

→ See [plan/phase4.md](plan/phase4.md) for full detail.

Sub-phases: **4a** Developer table API ✅ · **4b** System catalog persistence ✅ · **4c** initdb/bootstrap ✅ · **4d** Catalog cache and DDL API ✅ · **4e** Storage engine factory boundary 🔲

**Earliest table usability milestone**: Phase 4a. Developers can construct an `EdbTable` / `EdbRelation` in code, insert typed rows through the heap engine, scan them back, decode them through Phase 3 types, and verify values without SQL.

---

## Phase 5 — Query Engine Foundation ✅

→ See [plan/phase5.md](plan/phase5.md) for full detail.

Sub-phases: **5a** SQL frontend ✅ · **5b** Binder ✅ · **5c** Logical plan ✅ · **5d** Reference physical plan + executor ✅ · **5e** Physical boundary hardening 🔲 · **5f** Second backend 🔲

---

## Phase 6 — Transactions 🔄

→ See [plan/phase6.md](plan/phase6.md) for full detail.

Sub-phases: **6a** Transaction core ✅ · **6b** MVCC visibility ✅ · **6c** Heap tuple header integration ✅ · **6d** Query autocommit ✅ · **6e** WAL manager ✅ · **6f** Heap WAL/recovery 🔄 · **6g** Delete/update MVCC 🔄 · **6h** Lock manager/deadlock detection ✅ · **6i** Checkpoint/group-commit groundwork 🔄

---

## Phase 7 — Embedded Session API / Local REPL 🔲

→ See [plan/phase7.md](plan/phase7.md) for full detail.

Sub-phases: **7a** Embedded session API · **7b** Local REPL · **7c** Optional PostgreSQL wire protocol

---

## Phase 8 — Async I/O Foundation + io_uring/xNVMe + Disk Scheduler 🔲

→ See [plan/phase8.md](plan/phase8.md) for full detail.

---

## Phase 9 — Distributed Shared-Nothing Cluster 🔲

→ See [plan/phase9.md](plan/phase9.md) for full detail.

