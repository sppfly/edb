# Phase — Extensible Cascades-Style Query Optimizer & JOIN Support

This phase adds a Cascades-style extensible query optimizer and multi-table JOIN support to EDB. The optimizer uses a memo structure, transformation rules, and implementation rules to search for low-cost query plans. All four extension points—rewrite rules, physical operators, cost model, and statistics providers—are pluggable.

## Architecture Overview

```
SQL text
   │
   ▼
Parser ──► Binder ──► Logical Plan (unoptimized)
                          │
                          ▼
                    ┌──────────────┐
                    │   Cascades   │
                    │   Optimizer  │
                    │              │
                    │  ┌────────┐  │
                    │  │  Memo  │  │
                    │  └────────┘  │
                    │  ┌────────┐  │
                    │  │ Rules  │  │
                    │  └────────┘  │
                    │  ┌────────┐  │
                    │  │ Cost   │  │
                    │  │ Model  │  │
                    │  └────────┘  │
                    └──────────────┘
                          │
                          ▼
                    Physical Plan ──► Executor
```

## Core Concepts

### Memo

The memo is the central data structure. It partitions the search space into **groups** of equivalent expressions. Each group represents a set of logically equivalent relational expressions (same output schema, same semantics).

```
Group 1: [LogicalScan(t1)]
Group 2: [LogicalScan(t2)]
Group 3: [LogicalFilter(Group 1, pred)]
Group 4: [LogicalJoin(Group 1, Group 2, cond)]
         [LogicalJoin(Group 2, Group 1, cond)]  -- commutative equivalent
```

Physical implementations are also stored in groups:

```
Group 1: [LogicalScan(t1)] → [PhysicalSeqScan(t1)]
Group 4: [LogicalJoin(...)] → [PhysicalNestedLoopJoin(...), PhysicalHashJoin(...)]
```

### Groups

A `MemoGroup` contains:

- `group_id` — unique identifier
- `logical_expressions` — vector of logically equivalent `MemoExpr`
- `physical_expressions` — vector of physical `MemoExpr` (implementations)
- `best_physical` — pointer to the cheapest physical expression found so far
- `best_cost` — cost of `best_physical` (infinity until found)
- `schema` — output columns/types (set from the first expression added)
- `properties` — physical properties (sort order, etc.) — initially empty, filled by enforcers

### MemoExpr

A `MemoExpr` is a node in the memo. It references child groups instead of child expressions:

```cpp
struct MemoExpr {
    PlanNode* node;               // logical or physical operator
    std::vector<GroupId> children; // references to child groups
    Cost cost;                     // total cost (physical only)
    Cost local_cost;               // cost of this operator alone
    RowCount row_count;            // estimated output rows
};
```

### Rules

Rules are the extensibility mechanism. Two kinds:

**Transformation rules** — rewrite one logical expression into an equivalent logical expression. Examples: filter pushdown, join commutativity, predicate simplification.

**Implementation rules** — convert a logical expression into a physical expression. Examples: LogicalScan → PhysicalSeqScan, LogicalJoin → PhysicalNestedLoopJoin.

```cpp
class Rule {
public:
    virtual ~Rule() = default;

    // Does this rule apply to the given expression?
    [[nodiscard]] virtual auto match(const MemoExpr& expr,
                                     const OptimizerContext& ctx) const -> b8 = 0;

    // Apply the rule, producing new expressions to add to the memo.
    // Called only if match() returned true.
    [[nodiscard]] virtual auto apply(MemoExpr expr,
                                     OptimizerContext& ctx) const
        -> std::vector<MemoExpr> = 0;

    // Rule priority for application ordering (higher = earlier).
    [[nodiscard]] virtual auto priority() const -> i32 { return i32{0}; }
};
```

### OptimizerContext

The context passed through the optimizer:

```cpp
class OptimizerContext {
public:
    Memo& memo;
    const CostModel& cost_model;
    const StatisticsProvider& stats;
    std::vector<std::unique_ptr<Rule>> rules;
    std::vector<std::unique_ptr<Rule>> impl_rules;

    // Rule registration
    auto add_rule(std::unique_ptr<Rule> rule) -> void;
    auto add_implementation_rule(std::unique_ptr<Rule> rule) -> void;

    // Cost and statistics access
    [[nodiscard]] auto estimate_cost(const MemoExpr& expr) const -> Cost;
    [[nodiscard]] auto estimate_rows(GroupId group) const -> RowCount;
};
```

### CostModel

Pluggable cost interface:

```cpp
struct Cost {
    f64 cpu{0.0};
    f64 io{0.0};
    f64 total{0.0};  // cpu + io (or weighted sum)
};

class CostModel {
public:
    virtual ~CostModel() = default;

    [[nodiscard]] virtual auto cost_seq_scan(RowCount rows) const -> Cost = 0;
    [[nodiscard]] virtual auto cost_filter(Cost input, RowCount rows,
                                           f64 selectivity) const -> Cost = 0;
    [[nodiscard]] virtual auto cost_project(Cost input, RowCount rows) const -> Cost = 0;
    [[nodiscard]] virtual auto cost_nested_loop_join(Cost outer, RowCount outer_rows,
                                                     Cost inner) const -> Cost = 0;
    [[nodiscard]] virtual auto cost_hash_join(Cost build, RowCount build_rows,
                                              Cost probe, RowCount probe_rows) const -> Cost = 0;
    [[nodiscard]] virtual auto cost_index_scan(RowCount rows, i32 depth) const -> Cost = 0;
};
```

Default implementation (`TupleCostModel`) uses the simple formulas from before.

### StatisticsProvider

Pluggable statistics interface:

```cpp
class StatisticsProvider {
public:
    virtual ~StatisticsProvider() = default;

    [[nodiscard]] virtual auto row_count(u32 relation_oid) const -> RowCount = 0;
    [[nodiscard]] virtual auto ndv(u32 relation_oid, u32 attnum) const -> f64 = 0;
    [[nodiscard]] virtual auto null_fraction(u32 relation_oid, u32 attnum) const -> f64 = 0;

    // Default selectivity for a predicate
    [[nodiscard]] virtual auto selectivity(const BoundExpr& predicate,
                                           const std::vector<BoundTableRef>& tables) const -> f64 = 0;
};
```

Default implementation (`SimpleStatsProvider`) tracks row counts per table and uses flat selectivity defaults.

### Physical Properties

Physical properties are requirements on the output of a physical plan:

```cpp
struct PhysicalProperties {
    // Sort order: list of (column, direction) pairs
    std::vector<SortKey> sort_order;

    // Distribution: for future distributed planning
    // Distribution dist;
};
```

Properties propagate top-down during search. If a group's best physical plan doesn't satisfy the required properties, an enforcer (e.g., `PhysicalSort`) is inserted.

For Phase 1, we only track sort order when explicitly requested (ORDER BY). Distribution is a future concern.

## Search Algorithm

### Cascades Top-Down Search

The Cascades algorithm searches top-down with memoization:

1. Start from the root group (the entire query)
2. For each group, explore logical expressions and apply transformation rules
3. For each logical expression, apply implementation rules to get physical alternatives
4. For each physical alternative, recursively optimize child groups with required properties
5. Cost each physical plan bottom-up
6. Keep the cheapest plan per group per required properties

The search uses a **task stack** for lazy exploration:

```
OptimizeGroup(group_id, required_properties)
  └─ For each logical expr in group:
       ├─ ApplyTransformationRules(expr)     // adds new logical exprs to group
       └─ For each impl rule matching expr:
            ├─ OptimizeGroup(child_group, child_properties)  // recursive
            └─ CostPhysicalExpr(impl_expr)   // bottom-up costing
```

### Pruning

- If a partial plan's cost exceeds the best known cost for the group, prune it
- If a rule's pattern doesn't match, skip it immediately
- Groups are memoized: once optimized for a given required properties, the result is reused

## Pluggable Registration

All four extension points use a registration pattern consistent with EDB:

```cpp
// Register transformation rules
optimizer.add_rule(std::make_unique<FilterPushdownRule>());
optimizer.add_rule(std::make_unique<JoinCommutativityRule>());
optimizer.add_rule(std::make_unique<ConstantFoldingRule>());

// Register implementation rules
optimizer.add_implementation_rule(std::make_unique<SeqScanRule>());
optimizer.add_implementation_rule(std::make_unique<NestedLoopJoinRule>());
optimizer.add_implementation_rule(std::make_unique<HashJoinRule>());

// Register cost model
optimizer.set_cost_model(std::make_unique<TupleCostModel>());

// Register statistics provider
optimizer.set_stats_provider(std::make_unique<SimpleStatsProvider>(catalog));
```

Storage engines or indexes can register their own rules:

```cpp
// B-tree index registers its scan rule
optimizer.add_implementation_rule(std::make_unique<IndexScanRule>(index_manager));
```

## Built-in Transformation Rules

### 1. ConstantFoldingRule

Folds constant expressions at plan time:

- `3 + 4` → `7`
- `TRUE AND pred` → `pred`
- `FALSE AND pred` → `FALSE`
- `NOT NOT pred` → `pred`

### 2. FilterPushdownRule

Pushes `LogicalFilter` closer to the scan:

- Through `LogicalProject`: `Filter(Project(G))` → `Project(Filter(G))`
- Into join children: `Filter(Join(A, B))` → `Join(Filter(A), Filter(B))` for single-table predicates

### 3. PredicateSimplificationRule

Simplifies predicates:

- Flatten nested AND/OR: `(a AND b) AND c` → AND(a, b, c)
- Remove tautologies: `TRUE AND x` → `x`
- Remove contradictions: `FALSE OR x` → `x`

### 4. JoinCommutativityRule

`Join(A, B, cond)` → `Join(B, A, cond)` (swap children)

### 5. JoinAssociativityRule

`Join(Join(A, B, c1), C, c2)` → `Join(A, Join(B, C, c2), c1)` (reassociate)

This enables different join orderings in the DP search.

### 6. ProjectionPushdownRule

Pushes projection closer to the scan:

- `Project(Scan(t, all_cols))` → `Project(Scan(t, needed_cols))`
- Reduces tuple width through the plan tree

## Built-in Implementation Rules

### 1. SeqScanRule

`LogicalScan(t)` → `PhysicalSeqScan(t)`

### 2. FilterRule

`LogicalFilter(G, pred)` → `PhysicalFilter(G, pred)`

Always valid; alternative implementations can push filter into scan.

### 3. ProjectRule

`LogicalProject(G, items)` → `PhysicalProject(G, items)`

### 4. NestedLoopJoinRule

`LogicalJoin(outer, inner, cond)` → `PhysicalNestedLoopJoin(outer, inner, cond)`

Always valid; works for any join type and condition.

### 5. HashJoinRule

`LogicalJoin(left, right, cond)` → `PhysicalHashJoin(build, probe, keys, residual)`

Valid only when the condition has at least one equi-join predicate (`left.col = right.col`). Chooses build/probe sides based on estimated row counts (smaller side builds).

### 6. IndexScanRule (future)

`LogicalScan(t)` with a predicate matching an index → `PhysicalIndexScan(t, index, predicate)`

Requires index metadata from catalog. Registers when an index access method is available.

## JOIN Support

### Parser Extensions

New keywords: `JOIN`, `ON`, `INNER`, `LEFT`, `RIGHT`, `CROSS`, `OUTER`.

Syntax:

```sql
SELECT items
  FROM table_ref
  {[join_type] JOIN table_ref ON condition}
  [WHERE condition]

join_type ::= INNER | LEFT [OUTER] | RIGHT [OUTER] | CROSS | (empty = INNER)
```

Table-qualified column references (`t.col`) supported via optional qualifier on `ColumnRef`.

### AST Extensions

```cpp
enum class JoinType : uint8_t {
    Inner,
    LeftOuter,
    RightOuter,
    Cross,
};

struct JoinClause {
    JoinType type;
    std::string table_name;
    std::optional<Expr> on;  // absent for CROSS JOIN
};

struct SelectStmt {
    std::vector<SelectItem> items;
    std::string table_name;              // first table
    std::vector<JoinClause> joins;       // subsequent joined tables
    std::optional<Expr> where;
};
```

### Binder Extensions

- `BoundSelectStmt` carries `vector<BoundTableRef>` (all tables in scope)
- Column resolution searches all tables; ambiguous unqualified columns produce an error
- Table-qualified columns (`t.col`) resolved by matching qualifier against table names

### Logical Plan Extensions

```cpp
struct LogicalJoin {
    std::unique_ptr<LogicalPlan> left;
    std::unique_ptr<LogicalPlan> right;
    JoinType type;
    std::optional<BoundExpr> condition;
};
```

### Physical Plan Extensions

```cpp
struct PhysicalNestedLoopJoin {
    std::unique_ptr<PhysicalPlan> outer;
    std::unique_ptr<PhysicalPlan> inner;
    JoinType type;
    std::optional<BoundExpr> condition;
};

struct PhysicalHashJoin {
    std::unique_ptr<PhysicalPlan> build;
    std::unique_ptr<PhysicalPlan> probe;
    std::vector<BoundExpr> build_keys;
    std::vector<BoundExpr> probe_keys;
    JoinType type;
    std::optional<BoundExpr> residual;
};
```

### Executor Extensions

**NestedLoopJoinExecNode**: For each outer row, scan the inner side and emit matching pairs. LEFT OUTER emits outer row with NULLs if no match.

**HashJoinExecNode**: Build hash table on build side during `open()`. Probe during `next()`. LEFT OUTER emits unmatched probe rows with NULLs.

## Row Count Tracking

Minimal approach:

- `Catalog` maintains `std::unordered_map<u32, u64>` mapping relation_oid → row_count
- `INSERT` increments; `DELETE` decrements
- `CREATE TABLE` initializes to 0
- `SimpleStatsProvider` reads from this map
- Periodic persistence to a system table (future; in-memory first)

## Sub-Phases

### A. Cascades Core: Memo, Rules, Search

Status: 🔲 not started

Deliverables:

- `MemoGroup`, `MemoExpr`, `GroupId` types
- `Memo` class: add expression, get group, iterate groups
- `Rule` abstract class: `match()`, `apply()`, `priority()`
- `OptimizerContext`: rule registry, memo access
- `CostModel` abstract class with `TupleCostModel` default
- `StatisticsProvider` abstract class with `SimpleStatsProvider` default
- Cascades search algorithm: `optimize_group()` with task stack
- Cost propagation: bottom-up costing through memo
- Pruning: skip partial plans exceeding best known cost
- Optimizer entry point: `Optimizer::optimize(LogicalPlan) -> PhysicalPlan`
- Tests: memo construction, rule application, cost propagation, pruning

Validation:

- Empty memo has no groups
- Adding a logical expression creates a group
- Transformation rule produces new equivalent expressions in the same group
- Implementation rule produces physical expressions in the group
- Search finds the cheapest physical plan for a single-table query
- Pruning skips plans that exceed the best cost

### B. Single-Table Optimizer Integration

Status: 🔲 not started

Depends on: A

Deliverables:

- Built-in rules: ConstantFolding, PredicateSimplification, FilterPushdown, ProjectionPushdown
- Built-in implementation rules: SeqScanRule, FilterRule, ProjectRule
- Integration with `QueryEngine::execute()`: logical plan → optimizer → physical plan → executor
- Row count tracking in `Catalog`
- `SimpleStatsProvider` reading from catalog row counts
- Optimizer tests: constant folding, filter pushdown, projection pushdown
- Regression tests: existing SQL tests pass through optimizer

Validation:

- `SELECT * FROM t WHERE 3 + 4 = 7` folds to `SELECT * FROM t WHERE TRUE`
- `SELECT * FROM (SELECT * FROM t) WHERE x > 5` pushes filter below project
- Optimizer produces same result as direct planning for simple queries
- Cost of filtered scan < cost of unfiltered scan when selectivity < 1.0

### C. JOIN Parser, Binder, Logical Plan

Status: 🔲 not started

Depends on: — (can parallel with A and B)

Deliverables:

- `JOIN`, `ON`, `INNER`, `LEFT`, `RIGHT`, `CROSS`, `OUTER` keywords in lexer
- `JoinClause`, `JoinType` in AST
- `ColumnRef` extended with optional table qualifier
- Parser: FROM clause with joins
- Binder: multi-table column resolution, qualified column refs
- `BoundJoinClause` in bound IR
- `LogicalJoin` node in logical plan
- Logical planner: join tree construction
- Tests: parse, bind, logical plan shape for joins

Validation:

- `SELECT * FROM t1 JOIN t2 ON t1.id = t2.id` parses and binds
- Ambiguous unqualified column produces clear error
- Qualified `t1.id` resolves correctly
- Logical plan is `Project(Join(Scan(t1), Scan(t2), cond))`

### D. JOIN Physical Plan & Executor

Status: 🔲 not started

Depends on: C

Deliverables:

- `PhysicalNestedLoopJoin` physical plan node
- `NestedLoopJoinExecNode` executor
- `NestedLoopJoinRule` implementation rule
- Optimizer integration: joins optimize through Cascades
- Query engine: end-to-end JOIN execution
- Tests: INNER JOIN, LEFT OUTER JOIN, CROSS JOIN, join with WHERE

Validation:

- `SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id` returns matching rows
- LEFT JOIN includes unmatched left rows with NULLs
- CROSS JOIN returns cartesian product
- `SELECT t1.id, t2.name FROM t1 JOIN t2 ON t1.id = t2.id WHERE t2.val > 5` works

### E. Hash Join & Algorithm Selection

Status: 🔲 not started

Depends on: A, D

Deliverables:

- `PhysicalHashJoin` physical plan node
- `HashJoinExecNode` executor (hash table on smaller side)
- `HashJoinRule` implementation rule (requires equi-join predicate)
- Cost-based algorithm selection: Cascades picks hash join when cheaper
- Tests: hash join execution, algorithm selection

Validation:

- Hash join produces correct results for equi-join
- When build side is small, Cascades chooses hash join over nested-loop
- When no equi-join predicate exists, only nested-loop is offered
- Cost comparison: hash join cheaper for large-small join

### F. Join Ordering via Rule Application

Status: 🔲 not started

Depends on: D, E

Deliverables:

- `JoinCommutativityRule` transformation rule
- `JoinAssociativityRule` transformation rule
- Cascades explores join orderings through rule application
- Tests: 3-way join reordering, cost-based join order selection

Validation:

- `SELECT * FROM t1 JOIN t2 ON ... JOIN t3 ON ...` explores multiple join orders
- Cascades picks the cheapest join order
- For 3 tables with different sizes, the optimizer avoids cross-joins and orders by selectivity

### G. Filter Pushdown Across Joins

Status: 🔲 not started

Depends on: F

Deliverables:

- Extended `FilterPushdownRule`: push single-table predicates below joins
- Predicate analysis: determine which tables a predicate references
- Push equi-join predicates into join condition
- Tests: filter pushdown changes plan shape and cost

Validation:

- `SELECT * FROM t1 JOIN t2 ON t1.id = t2.id WHERE t1.val > 5` pushes filter below join
- Pushed filter reduces intermediate row count
- Plan with pushed filter has lower cost

## File Layout

```
src/query/
├── optimizer/
│   ├── memo.hpp / memo.cpp              # Memo, MemoGroup, MemoExpr, GroupId
│   ├── rule.hpp                         # Rule abstract class
│   ├── context.hpp / context.cpp        # OptimizerContext
│   ├── cost_model.hpp / cost_model.cpp  # CostModel, TupleCostModel
│   ├── stats.hpp / stats.cpp            # StatisticsProvider, SimpleStatsProvider
│   ├── search.hpp / search.cpp          # Cascades search algorithm
│   ├── optimizer.hpp / optimizer.cpp    # Optimizer entry point
│   └── rules/
│       ├── constant_folding.hpp/cpp
│       ├── predicate_simplification.hpp/cpp
│       ├── filter_pushdown.hpp/cpp
│       ├── projection_pushdown.hpp/cpp
│       ├── join_commutativity.hpp/cpp
│       ├── join_associativity.hpp/cpp
│       ├── seq_scan.hpp/cpp
│       ├── filter_impl.hpp/cpp
│       ├── project_impl.hpp/cpp
│       ├── nested_loop_join.hpp/cpp
│       └── hash_join.hpp/cpp
├── ... (existing files)
```

## Success Criteria

The optimizer phase is successful when EDB can:

1. Parse, bind, plan, and execute multi-table JOINs (INNER, LEFT, CROSS)
2. Search for optimal plans using the Cascades memo and rule framework
3. Choose between nested-loop and hash join based on cost estimates
4. Reorder joins via commutativity and associativity rules
5. Apply constant folding, predicate simplification, filter pushdown, and projection pushdown
6. Track row counts through insert/delete operations
7. Allow new rules and physical operators to be registered without modifying optimizer core code
8. Keep all existing single-table tests passing
9. Have regression tests covering join execution, algorithm selection, rewrites, and extensibility
