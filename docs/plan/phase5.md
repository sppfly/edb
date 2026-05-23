# Phase 5 — Basic Query Engine 🔲

Parse SQL, bind to the Phase 4 table/catalog API, and execute via Volcano iterator model.

By Phase 5, table usability already exists for developers through Phase 4a. Phase 5 turns that internal API into user-facing SQL: `CREATE TABLE`, `INSERT`, and `SELECT` should call the same table/relation layer instead of duplicating storage logic.

## Pipeline

```
SQL text
   │
   ▼
┌──────────┐   tokens    ┌──────────┐   AST      ┌──────────┐
│  Lexer   │ ──────────► │  Parser  │ ─────────► │ Analyzer │
└──────────┘             └──────────┘            └──────────┘
                                                      │ bound plan
                                                      ▼
                                                 ┌──────────┐
                                                 │ Executor │
                                                 └──────────┘
```

## Executor Interface

All nodes implement the Volcano iterator model:

```cpp
struct EdbExecNode {
    virtual auto open()           -> EdbResult<void> = 0;
    virtual auto next(Tuple& out) -> EdbResult<b8>   = 0;  // false = done
    virtual auto close()          -> EdbResult<void> = 0;
    virtual ~EdbExecNode() = default;
};
```

## Executor Nodes (`src/query/executor/`)

| Node | Function |
|---|---|
| `SeqScan` | Iterate rows via `EdbTable` / `EdbRelation`, which delegates to `EdbStorageEngineOps::scan_next` |
| `Filter` | Evaluate predicate, pass matching tuples downstream |
| `Project` | Evaluate expression list, emit result tuples |
| `Insert` | Encode rows through the table schema and write via `EdbTable::insert` |
| `Update` | Rewrite rows through the table API, which delegates to the storage engine |
| `Delete` | Mark rows dead through the table API, which delegates to `EdbStorageEngineOps::delete_tuple` |

## Deliverables

- [ ] Lexer + recursive-descent parser (SELECT / INSERT / UPDATE / DELETE / CREATE TABLE)
- [ ] Analyzer: resolve table/column names via catalog, type-check expressions
- [ ] `SeqScan`, `Filter`, `Project`, DML nodes
- [ ] Integration test: SQL `CREATE TABLE` → `INSERT` → `SELECT` → verify through the Phase 4 table API
