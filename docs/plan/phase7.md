# Phase 7 — Local REPL First, Network Later 🔲

Phase 7 should not block core database progress on external networking. The first user-facing shell should be a local in-process REPL that drives the same parser, binder, planner, executor, and transaction path used by tests.

PostgreSQL wire protocol support is still desirable, but it is explicitly a later sub-phase rather than the immediate next milestone.

## Why Local REPL First

- keeps the focus on database semantics rather than socket/protocol work
- makes single-node development and debugging faster
- gives a human-usable interface for local testing before client/server concerns exist
- reuses the same SQL engine without introducing network framing, authentication, or connection state too early

## Phase 7 Sub-Phases

### 7a. Embedded Session API

Define the minimal process-local database/session entry points needed by both tests and the REPL.

Examples:

- open database instance
- create session / transaction context
- execute one SQL statement
- return result schema, rows, command status, and errors

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
