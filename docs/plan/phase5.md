# Phase 5 — Query Engine Foundation 🔲

Phase 5 should not try to finish the entire query engine in one step. The right goal is to define the stable skeleton that later executors can plug into, then implement the smallest reference path that proves end-to-end SQL usability.

By Phase 5, table usability already exists for developers through Phase 4. Phase 5 turns that internal API into user-facing SQL without hardwiring the system to one execution model forever.

## Design Goals

- Keep SQL frontend, semantic binding, logical planning, and physical execution as separate layers.
- Make the first implementation the simplest correct reference path.
- Preserve room for later execution backends: Volcano, vectorized, morsel-driven, and specialized scan/execution paths.
- Reuse the Phase 4 table/catalog API instead of duplicating storage logic in the query layer.

## Stable Skeleton

The long-term query pipeline should be:

```
SQL text
   │
   ▼
┌──────────┐   tokens    ┌──────────┐   AST      ┌──────────┐
│  Lexer   │ ──────────► │  Parser  │ ─────────► │  Binder  │
└──────────┘             └──────────┘            └──────────┘
                                            │ bound tree
                                            ▼
                                        ┌──────────────┐
                                        │ Logical Plan │
                                        └──────────────┘
                                            │
                                            ▼
                                        ┌───────────────┐
                                        │ Physical Plan │
                                        └───────────────┘
                                            │
                                            ▼
                                        ┌────────────────┐
                                        │ Exec Backend   │
                                        └────────────────┘
```

### Layer Responsibilities

| Layer | Responsibility | Must not depend on |
|---|---|---|
| Lexer / Parser | Syntax only | Catalog internals, execution model |
| Binder | Resolve names, types, and function/operator semantics | Physical operators, scheduler details |
| Logical Plan | Stable relational/operator IR | Storage layout, pull vs batch execution |
| Physical Plan | Lower logical operators to backend-specific operators | SQL syntax details |
| Execution Backend | Execute one physical operator family | Parser/Binder internals |

## Extensibility Requirements

These abstractions should be preserved even if the first implementation is minimal:

### 1. Logical and Physical Plan Separation

Multiple execution backends should consume the same logical plan and lower it differently.

Examples:

- `LogicalScan` → `PhysicalSeqScan` for the reference row executor
- `LogicalScan` → `PhysicalVectorScan` for a vectorized backend
- `LogicalScan` → `PhysicalMorselScan` for a morsel-driven backend
- `LogicalScan` → `PhysicalFtsScan` / `PhysicalAnnScan` for specialized access paths

### 2. Expression Layer Independent of Execution Style

Bound expressions should be reusable across:

- tuple-at-a-time evaluation
- batch/vector evaluation
- future compiled/JIT evaluation

### 3. Scan Work Must Be Splittable Later

The first implementation can use a simple serial scan, but the query and storage layers should leave room for scan work units such as:

- page ranges for heap storage
- row groups or segments for PAX / columnar storage
- vector blocks for vectorized execution

### 4. Data Representation Must Admit Both Rows and Batches

The first executor may run on rows only, but the physical execution boundary should leave room for:

- row-oriented tuples for the simplest reference executor
- row batches for coarse-grained batching
- column/vector batches for analytical backends

### 5. Backend Capability-Based Planning

Not every backend must implement every operator immediately. Physical planning should be able to choose a backend based on supported operator families.

Examples:

- Reference row executor: full basic coverage, lowest complexity
- Vectorized executor: scan/filter/project/aggregate first
- Morsel-driven executor: scan/join/aggregate once scheduling exists
- Specialized executor: only selected scan and predicate forms

## Phase 5 Sub-Phases

### 5a. SQL Frontend

Implement the smallest useful SQL syntax surface:

- `CREATE TABLE`
- `INSERT INTO ... VALUES (...)`
- `SELECT <columns> FROM <table>`
- optional first predicate form: `WHERE <column> = <literal>`

Deliverables:

- lexer
- recursive-descent parser
- AST definitions
- parser tests

### 5b. Binder

Resolve:

- table names through `EdbCatalog`
- column names through table schema / catalog metadata
- type names and literals through `EdbTypeRegistry`
- simple expression typing and coercion rules

Deliverables:

- bound statement / expression IR
- binder tests for name resolution and type errors

### 5c. Logical Plan

Introduce a backend-neutral logical operator tree.

Initial logical operators:

- `LogicalCreateTable`
- `LogicalInsert`
- `LogicalScan`
- `LogicalFilter`
- `LogicalProject`

Deliverables:

- logical plan IR
- lowering from bound statements to logical plan
- tests for logical plan shape

### 5d. Reference Physical Plan + Reference Executor

Implement the simplest end-to-end path that is easy to reason about and validate.

This first backend should be row-oriented and Volcano-style, not because it is the final design, but because it is the cheapest correctness baseline.

Reference executor interface:

```cpp
struct EdbExecNode {
    virtual auto open() -> EdbResult<void> = 0;
    virtual auto next(EdbExecRow& out) -> EdbResult<b8> = 0;
    virtual auto close() -> EdbResult<void> = 0;
    virtual ~EdbExecNode() = default;
};
```

Initial physical operators:

- `PhysicalSeqScan`
- `PhysicalFilter`
- `PhysicalProject`
- `PhysicalInsert`
- `PhysicalCreateTable`

Deliverables:

- physical lowering for the reference backend
- row-oriented reference executor
- integration tests: SQL `CREATE TABLE` → `INSERT` → `SELECT`

### 5e. Physical Execution Boundary Hardening

Before adding a second executor, solidify the shared physical boundary:

- backend capability description
- reusable executable expression layer
- row result representation plus batch-ready extension points
- scan work abstraction that can later produce morsels

This sub-phase may involve refactoring the reference executor without changing SQL behavior.

### 5f. Second Backend

Only after 5a through 5e are stable should a second backend be added.

Preferred order:

1. vectorized executor
2. morsel-driven scheduling on top of vectorized or batched operators
3. specialized physical operators for full-text or vector search

## What We Will Implement First

The immediate implementation plan is intentionally conservative:

- keep the stable layered skeleton above
- implement only the smallest SQL subset needed to prove the architecture
- use one simple reference executor first
- defer parallelism, scheduling, batch execution, and specialized access paths

In other words: design for multiple execution backends now, but implement only the easiest correct one first.

## Non-Goals for the First Implementation

- cost-based optimization
- join reordering
- vectorized execution
- morsel scheduling
- JIT / compiled execution
- specialized ANN / full-text operators
- full SQL coverage

## Success Criterion

Phase 5 is successful when the repository has both:

1. a stable query-engine skeleton that does not block future executors
2. a minimal SQL path that is easy to run, test, and extend

Minimum end-to-end milestone:

- SQL `CREATE TABLE`
- SQL `INSERT`
- SQL `SELECT`
- all routed through the existing Phase 4 catalog/table path
