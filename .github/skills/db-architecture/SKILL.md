---
name: db-architecture
description: 'Design and implement EDB database core components: type system, catalog, query engine, transactions, indexes. Use when adding SQL features, implementing data types, designing catalog tables, or modifying the query planner/executor.'
argument-hint: 'Component to design or implement (e.g., type system, catalog, query planner)'
user-invocable: true
disable-model-invocation: false
---

# Database Architecture Design

## When to Use
- Adding new SQL features or operators
- Implementing/extending the type system
- Designing catalog tables or system schemas
- Modifying query planner, executor, or optimizer
- Adding new index access methods
- Designing transaction/MVCC behavior

## Core Principles

1. **Pluggable by default**: Every subsystem must expose a registration/extension API. No hardcoded type lists, storage format assumptions, or global singletons.
2. **Interface-first + invariant-first**: Define the C++26 interface (abstract class or concept) and write `EDB_ASSERT` preconditions before any implementation. Changing interfaces later is expensive.
3. **Test-before-implement**: Invariants are written, then tests, then code — in that order always.
4. **Shared-nothing ready**: Even single-node code must work if data is sharded. Always pass a context handle (database instance, transaction, session) instead of global variables.

## Subsystems

### Type System (`src/types/`)

- `EdbTypeRegistry`: runtime hash table mapping type names to `EdbType*` 
- Each type provides: `in`, `out`, `send`, `receive`, `cmp`, `hash`, `typmod_in`, `typmod_out`
- Composite types, arrays, and domains built on top of base types
- **Pattern**: See PostgreSQL `pg_type` catalog design; keep it lightweight

### Catalog (`src/catalog/`)

- System tables: `edb_class`, `edb_attribute`, `edb_index`, `edb_type`
- Bootstrapped from a template file at initdb time
- Caching layer (`EdbCatalogCache`) with invalidation on DDL
- **Rule**: All metadata lookups go through catalog API, never hardcode OIDs

### Query Engine (`src/query/`)

- **Parser**: Hand-written recursive descent or lemon/LALR grammar. Target PostgreSQL syntax compatibility where feasible.
- **Analyzer**: Bind identifiers to catalog entries, resolve types, check permissions
- **Planner**: Cost-based optimizer with extensible statistics
- **Executor**: Volcano-style iterator model initially; consider vectorized execution for OLAP later

### Transaction (`src/transaction/`)

- MVCC with tuple visibility rules (xmin/xmax like PostgreSQL)
- WAL: append-only log with LSN, group commit, fsync policy configurable per I/O backend
- Lock manager: two-phase locking with deadlock detection

### Index (`src/index/`)

- Access method interface: `aminsert`, `amdelete`, `amscan`, `amgettuple`, `amgetbitmap`
- B-tree default; hash, GiST, GIN as extensions
- **Note**: Index builds should use the storage engine's I/O backend, not raw POSIX

## Common Patterns

### Adding a New Data Type

1. Define C++26 struct with concept constraint in `src/types/`
2. Write `pre`/`post` contracts on `from_text`, `compare`, `hash`
3. Write unit tests in `tests/unit/types/` covering: valid input, malformed text, comparison ordering, hash consistency
4. Implement `from_text`/`to_text`/`compare`/`hash`
5. Register via `edb_type_register<MyType>("mytype")`
6. Add regression test if fixing a related bug

### Adding a New Catalog Table

1. Add SQL DDL to `src/catalog/bootstrap.sql`
2. Generate C headers via bootstrap script
3. Implement cache invalidation in `src/catalog/cache.c`
4. Add recovery/replay logic if table is WAL-logged

## Anti-patterns

- **Global state**: `static EdbGlobal* g_db;` — use handles instead
- **Hardcoded type IDs**: `if (type_id == 23)` — use catalog lookups
- **Bypassing storage I/O abstraction**: Storage engines must NOT call `pread`/`pwrite` directly; always route through `EdbStorageIOOps`
