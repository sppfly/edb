# Phase 9 — Distributed 🔲

Horizontal scaling via shared-nothing sharding + Raft replication. Design is deferred; foundations are laid in earlier phases (no global state, context handles everywhere).

## Components

| Component | Approach |
|---|---|
| Replication | Raft (catalog + WAL) |
| Partitioning | Hash / range on partition key from catalog |
| Routing | Coordinator routes queries to shard owners |
| Distributed txn | 2PC coordinator, or Percolator-style |
| Distributed deadlock | Cycle detection across lock manager instances |

## Deliverables

TBD — design will be refined once Phases 6–7 are complete.
