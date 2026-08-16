# EDB

EDB is a compact, extensible database kernel written in C++26. It is aimed at being a strong single-node foundation first, with clear internal boundaries for storage, types, and query execution.

## What It Is

EDB is not trying to be a full production database all at once. The project is building a database core in layers:

- pluggable storage I/O backends
- page store and buffer pool
- pluggable storage engine boundary
- typed row encoding and type registry
- catalog and table layer
- basic SQL path

The current implementation includes a working storage stack, type system, catalog, and a minimal non-transactional SQL execution path: `CREATE TABLE`, `INSERT`, and `SELECT`. Transactions (MVCC, WAL, locks) are future work and are not part of the current codebase.

## Design Goals

EDB is designed to make database internals easy to evolve and experiment with.

- single-node correctness before distributed features
- explicit extension boundaries instead of hardwired subsystems
- pluggable storage engines, not one permanent layout
- pluggable storage I/O backends, not one permanent device interface
- a simple reference execution path first, with room for later backends
- architecture that stays usable for research, experimentation, and incremental growth

In practice, that means the project favors clean interfaces and layered design over short-term convenience.

## Why It Exists

The goal is to have a database codebase where new ideas can be added without rewriting the whole system. Examples include:

- new storage layouts
- new type implementations
- alternate I/O backends
- different execution strategies
- future distributed components built on top of a solid local engine

## Repository Guide

- `src/` contains the database implementation
- `tests/` contains unit and regression tests
- `docs/PLAN.md` contains the high-level development plan
- `docs/plan/` contains phase-by-phase design notes

## Status

Current priorities are:

- introduce explicit database/session ownership before REPL or async I/O work
- revisit transactions (MVCC, WAL, locks) once the local session boundary is stable