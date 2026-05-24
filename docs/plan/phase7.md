# Phase 7 — Embedded Session First, Local REPL Next 🔲

Phase 7 should not block core database progress on external networking. Before building a shell, EDB needs an embedded database/session API that owns the same parser, binder, planner, executor, transaction, lock, WAL, catalog, and storage state used by tests.

The current `QueryEngine::execute()` autocommit path is useful, but it is not yet a full session boundary. Phase 7 starts by making that ownership explicit, then layers a local in-process REPL on top.

PostgreSQL wire protocol support is still desirable, but it is explicitly a later sub-phase rather than the immediate next milestone.

## Why Embedded Then REPL

- keeps the focus on database semantics rather than socket/protocol work
- makes single-node development and debugging faster
- gives a human-usable interface for local testing before client/server concerns exist
- reuses the same SQL engine without introducing network framing, authentication, or connection state too early

## Phase 7 Entry Criteria

Phase 7 should start after Phase 6 has a stable local durability boundary:

- normal SQL execution emits heap and transaction WAL records where required
- commit appends and flushes the durable commit record before exposing committed status
- transaction, lock, WAL, catalog, and storage ownership is explicit enough to move from one `QueryEngine` object into a database/session context
- single-statement autocommit remains covered by regression tests

## Phase 7 Sub-Phases

### 7a. Embedded Session API

Define the minimal process-local database/session entry points needed by both tests and the REPL.

Examples:

- open database instance
- create session context
- execute one SQL statement in autocommit mode
- later: begin, commit, and rollback explicit transaction scopes
- return result schema, rows, command status, and errors

Initial shape:

```cpp
class Database {
public:
    static auto open(DatabaseConfig config) -> Result<Database>;
    auto create_session() -> Result<Session>;
};

class Session {
public:
    auto execute(std::string_view sql) -> Result<QueryResult>;
};
```

The exact names may change, but ownership should not: database-level state owns catalog, storage factories, WAL, transaction manager, and lock manager; session-level state owns current transaction scope, error state, and eventually protocol/session options.

### 7b. Local REPL

Build a simple command-line shell on top of the embedded session API.

Initial capabilities:

- read one statement at a time
- execute locally in-process
- print rows and command status
- surface parse/bind/runtime errors cleanly
- support basic meta-commands such as `.quit` and `.help`

This is enough to develop and manually test the single-node engine.

### 7c. Optional PostgreSQL Wire Protocol

Only after the local SQL path is stable should the project add external client protocol support.

Initial scope if implemented:

- PostgreSQL wire protocol v3
- simple query mode only
- trust authentication for local development
- enough compatibility for `psql` smoke tests

## Deliverables

- [ ] Embedded session API for local SQL execution
- [ ] REPL executable for single-node interactive use
- [ ] Integration tests that drive SQL through the embedded/session path
- [ ] Optional later deliverable: `psql`-compatible wire protocol endpoint
