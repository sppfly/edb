# Phase 4 — Table and Catalog Layer 🔄

Developer-facing table/relation abstraction plus system tables that describe database objects. The catalog is initially stored using the heap engine from Phase 2.

The first goal is not SQL. The first goal is a code-level table usability milestone: construct a table object from a schema, insert typed rows, scan them back, decode values, and verify correctness.

## Current Status

Phase 4 is functionally usable but not architecturally complete. The table API, system catalog tables, bootstrap path, catalog cache, and DDL API exist and are used by the SQL path. However, `Catalog::OpenedTableBundle` currently owns `EdbHeapEngine` directly, which hardwires heap storage into the catalog layer.

That coupling violates the long-term storage-engine extensibility goal. Phase 4 remains open until catalog table opening goes through a storage-engine factory or equivalent relation storage boundary. Heap can remain the default engine, but catalog should not need to include or name `EdbHeapEngine` directly.

## Phase 4a — Developer Table API ✅

`EdbTable` / `EdbRelation` wraps a schema, a relation identifier, and a storage engine handle. It is the first layer where a heap tuple becomes a logical table row.

```cpp
struct EdbTableSchema {
    u32 relation_oid;
    std::string name;
    std::vector<EdbColumnSchema> columns;
};

class EdbTable {
public:
    auto insert(std::span<const EdbValue> values) -> EdbResult<EdbTupleId>;
    auto scan() -> EdbResult<std::vector<std::vector<EdbValue>>>;
};
```

Phase 4a may be backed by an in-memory schema instead of persisted catalog rows. It should still use the real Phase 2 heap engine and Phase 3 row codec.

### Phase 4a Integration Test

```
define schema
    → construct EdbTable
    → insert typed rows
    → scan heap tuples
    → decode rows
    → verify values
```

This is the earliest point where developers can test table usability without SQL.

## Phase 4b — System Catalog Persistence ✅

System tables describe all database objects.

### System Tables

| Table | Key Columns | Purpose |
|---|---|---|
| `edb_type` | `oid`, `name`, `typlen` | Registered types |
| `edb_class` | `oid`, `name`, `relkind` | Tables, indexes, sequences |
| `edb_attribute` | `attrelid`, `attnum`, `name`, `type_oid` | Columns |
| `edb_index` | `oid`, `indrelid`, `am_oid` | Indexes |

## Phase 4c — initdb Bootstrap ✅

`initdb` inserts rows for built-in types and the catalog tables themselves. After bootstrap, catalog metadata should be recoverable by scanning heap-backed system tables.

## Phase 4d — Catalog API and Cache (`src/catalog/catalog.hpp`) ✅

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
- **DDL**: `create_table` persists metadata and returns a table/relation handle usable by Phase 5

## Phase 4e — Storage Engine Factory Boundary 🔲

Decouple catalog from concrete storage engine classes.

Current problem:

```cpp
struct OpenedTableBundle {
    std::unique_ptr<StorageIOOps> backend;
    PageStore page_store;
    EdbHeapEngine engine;
    std::unique_ptr<Table> table;
};
```

This makes heap storage a catalog implementation detail and prevents future relation storage choices such as heap, columnar, PAX, vector-aware, or full-text-aware engines from plugging in through the same catalog path.

Target shape:

- catalog stores relation metadata and storage-engine choice, not concrete engine objects
- a `StorageEngineFactory` or relation storage factory creates `StorageEngineOps` instances
- heap remains the default engine for system tables and user tables
- `Catalog` depends on `StorageEngineOps`, `PageStore`, and factories, not on `EdbHeapEngine`
- tests can open catalog/user tables through a fake or alternate engine factory without changing catalog code

This boundary should be introduced before adding more storage engines, before Phase 8 async storage work, and before Phase 9 distributed placement metadata relies on relation storage choices.

## Deliverables

- [x] `EdbTableSchema` and `EdbTable` / `EdbRelation` developer API
- [x] Integration test: construct table in code → insert typed rows → scan/decode → verify values
- [x] System table schemas and heap-engine-backed storage
- [x] `initdb` bootstrap sequence
- [x] `EdbCatalog` read/write API with cache
- [x] Unit tests for bootstrap + DDL round-trips
- [ ] Storage-engine factory / relation storage boundary
- [ ] Remove direct `EdbHeapEngine` ownership from `Catalog::OpenedTableBundle`
- [ ] Catalog tests that prove heap is selected through the factory rather than hardcoded
