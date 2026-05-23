# Phase 5 — Basic Query Engine 🔲

Parse SQL, bind to catalog, execute via Volcano iterator model.

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
| `SeqScan` | Iterate all tuples in a relation via `EdbStorageEngineOps::scan_next` |
| `Filter` | Evaluate predicate, pass matching tuples downstream |
| `Project` | Evaluate expression list, emit result tuples |
| `Insert` | Write tuples via `EdbStorageEngineOps::insert` |
| `Update` | Delete old tuple + insert new via engine |
| `Delete` | Mark tuples dead via `EdbStorageEngineOps::delete_` |

## Deliverables

- [ ] Lexer + recursive-descent parser (SELECT / INSERT / UPDATE / DELETE / CREATE TABLE)
- [ ] Analyzer: resolve table/column names via catalog, type-check expressions
- [ ] `SeqScan`, `Filter`, `Project`, DML nodes
- [ ] Integration test: CREATE TABLE → INSERT → SELECT → verify
