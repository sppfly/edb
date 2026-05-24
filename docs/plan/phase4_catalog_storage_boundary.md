# Phase 4 Catalog / Storage Boundary Notes

This note summarizes the Phase 4e design and the boundary now implemented around `Catalog`, `Table`, `StorageEngineOps`, `BufferPool`, and `PageStore`.

## Current Layering

The current implementation path is:

```text
Query executor
  -> Catalog
  -> Table
  -> StorageEngineOps
  -> BufferPool   (for heap today)
  -> PageStore
  -> StorageIOOps
```

Important nuance:

- `StorageIOOps` is the byte-level I/O backend interface.
- `PageStore` maps logical page IDs to byte offsets over `StorageIOOps`.
- `BufferPool` is a page cache over `PageStore`.
- `StorageEngineOps` is the tuple/RID-level storage-engine abstraction.
- `Table` is schema-aware and uses `RowCodec` to translate typed values to tuple bytes.
- `Catalog` is the metadata manager and relation opener.

For the current heap implementation, the concrete path is:

```text
HeapEngine -> BufferPool -> PageStore -> StorageIOOps
```

This does not mean every future storage engine must use `BufferPool` in the same way. The current `StorageEngineOps` comments already leave room for engines that use `mmap` or specialized files below the tuple interface.

## What Catalog Does Today

`Catalog` currently has several responsibilities:

1. Own and open the system catalog tables: `edb_type`, `edb_class`, `edb_attribute`, and `edb_index`.
2. Bootstrap those system tables when the catalog is empty.
3. Expose metadata lookup APIs such as `get_type`, `get_class`, and `get_attributes`.
4. Implement DDL metadata writes such as `create_table` and `drop_table`.
5. Turn metadata into a live `Table` object via `open_table()`.

So `Catalog` is not just “a bunch of system tables”. It is the API layer over those tables and the component that translates metadata into a runtime relation handle.

## The Coupling Problem Phase 4e Closed

The coupling issue is not that system tables currently use heap storage. That is a reasonable default.

The real issue was that `Catalog` directly named and owned `EdbHeapEngine` in `OpenedTableBundle`:

```cpp
struct OpenedTableBundle {
    std::unique_ptr<StorageIOOps> backend;
    PageStore page_store;
    EdbHeapEngine engine;
    std::unique_ptr<Table> table;
};
```

This has two effects:

1. `Catalog` is directly coupled to one concrete engine class.
2. The same bundle shape is used for both system tables and user tables, so user-table opening is also hardwired to heap today.

That is why Phase 4 remained open even though system tables being OLTP-style heap relations was acceptable.

## What Is Not A Problem

These statements are compatible with the intended design:

- system tables may continue to default to heap storage
- heap may remain the default storage engine for ordinary tables for a long time
- `Catalog` may still own relation runtime objects and caches

The architectural issue is specifically where the heap choice is expressed.

## What Changed In Phase 4e

The heap decision moved out of `Catalog`'s concrete member types and into a storage factory boundary.

Implemented direction:

- `Catalog` depends on `StorageEngineOps` and a factory, not directly on `EdbHeapEngine`
- the factory chooses heap for system tables by policy
- the factory can later choose heap, columnar, PAX, vector, or other engines for user tables
- `open_table()` still returns a `Table*`, but the `Table` is backed by an engine selected through the factory

Possible shape:

```cpp
class StorageEngineFactory {
public:
    virtual auto open_engine(u32 relation_oid, std::string_view relation_name,
                             PageStore& page_store, const EngineConfig& config)
        -> Result<std::unique_ptr<StorageEngineOps>> = 0;
};
```

With the implemented boundary:

- `RelationBackendFactory` chooses the byte-level backend
- `StorageEngineFactory` chooses the tuple-level engine
- `Catalog` composes the two without naming a specific engine type
- `HeapStorageEngineFactory` provides the default policy when no alternate engine factory is injected

## Why This Matters

Without this boundary:

- adding a second storage engine requires changing `Catalog`
- testing relation-opening behavior with alternate engines is harder
- catalog metadata cannot naturally grow to describe per-relation storage-engine choice
- Phase 8 and Phase 9 work inherit a hardcoded heap assumption too early

With this boundary:

- system tables can still use heap by default
- user tables can later select another engine without rewriting `Catalog`
- `Catalog` returns to being a metadata manager plus relation opener, not a heap-specific owner

## Current Working Summary

- `Catalog` is more than system-table maintenance; it is the metadata API and relation opener.
- The problem is not “system tables use heap”.
- The problem was “Catalog directly owns `EdbHeapEngine`, and therefore all relation opening is currently heap-specific”.
- Phase 4e now introduces a storage-engine factory boundary while preserving heap as the default policy.