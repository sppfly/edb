# Phase 4 — Catalog 🔲

System tables that describe all database objects. The catalog is itself stored using the heap engine from Phase 2.

## System Tables

| Table | Key Columns | Purpose |
|---|---|---|
| `edb_type` | `oid`, `name`, `typlen` | Registered types |
| `edb_class` | `oid`, `name`, `relkind` | Tables, indexes, sequences |
| `edb_attribute` | `attrelid`, `attnum`, `name`, `type_oid` | Columns |
| `edb_index` | `oid`, `indrelid`, `am_oid` | Indexes |

## Catalog API (`src/catalog/catalog.hpp`)

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
- **Bootstrap**: `initdb` inserts rows for system types and the catalog tables themselves

## Deliverables

- [ ] System table schemas and heap-engine-backed storage
- [ ] `initdb` bootstrap sequence
- [ ] `EdbCatalog` read/write API with cache
- [ ] Unit tests for bootstrap + DDL round-trips
