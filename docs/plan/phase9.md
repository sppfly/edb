# Phase 9 — Distributed Shared-Nothing Cluster 🔲

The long-term distributed shape should be a shared-nothing database with explicit sharding, replicated shard groups, and a stateless SQL-facing coordinator tier. Single-node remains the primary implementation path until the local engine is solid.

The important design rule is that distributed mode should reuse as much of the single-node engine as possible. A distributed node is still an EDB instance with catalog, storage engine, executor, transaction manager, and WAL; the cluster layer adds routing, replication, metadata placement, and cross-shard coordination.

## Target Shape

```
Client / REPL / future driver
		│
		▼
	Coordinator tier
   (parse/bind/plan/route/merge)
		│
		▼
   Shard groups / tablets / partitions
    ├─ leader replica
    ├─ follower replica
    └─ follower replica
		│
		▼
     Local EDB storage + WAL
```

## Main Roles

### Coordinator

The coordinator should be stateless or near-stateless.

Responsibilities:

- parse, bind, and plan distributed queries
- resolve table and partition metadata from the cluster catalog
- route single-shard requests directly to shard leaders
- fan out multi-shard requests and merge results
- coordinate distributed transactions when needed

The coordinator should not own durable table data.

### Data Node

Each data node runs the normal local EDB stack for the shards it owns:

- storage engine
- buffer pool
- WAL / recovery
- local transaction manager
- local executor

This keeps single-node and distributed code paths closely aligned.

### Shard Group

The basic unit of placement should be a shard group: one logical partition replicated by a Raft group.

Properties:

- one leader accepts writes
- followers replicate WAL or logical mutation records
- failover promotes a new leader
- placement and movement happen at shard-group granularity

## Data Model

### Partitioning

User tables should become partitioned relations in the catalog.

Recommended first model:

- hash partitioning by explicit partition key
- optional later support for range partitioning

The catalog should know:

- partition key columns
- shard count / mapping rules
- shard group placement
- replica set membership

### Catalog Placement

The cluster catalog should itself be replicated and strongly consistent.

Recommended shape:

- a small replicated metadata group using Raft
- stores table metadata, partition maps, node membership, and placement info
- coordinators cache metadata but treat the replicated catalog as source of truth

## Execution Model

### Single-Shard Queries First

The first distributed execution target should be queries that route to exactly one shard group.

Examples:

- point lookups on partition key
- inserts with deterministic partition routing
- updates/deletes restricted to one shard

This gives meaningful distributed value without requiring a full distributed optimizer immediately.

### Multi-Shard Queries Later

Once single-shard routing is stable, add fan-out/fan-in execution for:

- partitioned scans
- distributed aggregates
- distributed joins with repartition or broadcast strategies

The coordinator should act as a merge node first; more advanced distributed physical planning can come later.

## Replication and Consensus

Raft should be the default replication model.

Recommended scope:

- replicate shard-group WAL or mutation log
- replicate cluster catalog metadata via a separate metadata Raft group
- keep leader-based writes for simplicity

This is a better fit for the project than trying to make every node a symmetric peer from day one.

## Transactions

Distributed transaction support should be staged.

### First stage

- local transactions only
- distributed SQL limited to single-shard writes and reads

### Second stage

- coordinator-managed 2PC for cross-shard transactions
- explicit transaction participants and prepare/commit records

### Possible later stage

- more optimistic or Percolator-like designs if write/write coordination cost becomes too high

The practical advice is to delay cross-shard write transactions until single-shard correctness and failover are proven.

## Failure Model

The initial cluster should tolerate:

- node restart
- leader failover inside a shard group
- coordinator restart
- stale metadata cache refresh after placement changes

It does not need to solve every rebalancing and network-partition corner case in its first version.

## Recommended Delivery Order

### 9a. Metadata and Placement Model

- cluster catalog schema
- node membership records
- partition map / shard-group metadata

### 9b. Single-Shard Routing

- coordinator routes requests by partition key
- single-shard reads and writes only

### 9c. Replicated Shard Groups

- Raft-backed shard-group replication
- leader election and failover

### 9d. Cross-Shard Read Execution

- fan-out scans
- merge results at coordinator

### 9e. Cross-Shard Transactions

- 2PC across shard groups
- distributed lock/deadlock considerations

### 9f. Rebalancing and Advanced Planning

- shard movement
- repartition/broadcast distributed joins
- more adaptive routing and placement

## What This Is Not

- not a shared-disk architecture
- not a global buffer pool across nodes
- not a design that assumes every query is distributed by default
- not a requirement that PostgreSQL wire protocol exist before cluster internals

## Deliverables

- [ ] Cluster catalog / metadata model
- [ ] Coordinator role and routing layer
- [ ] Partitioned table metadata and shard-group placement
- [ ] Raft-replicated shard groups
- [ ] Single-shard distributed SQL path
- [ ] Later: cross-shard reads, then cross-shard transactions
