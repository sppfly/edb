# EDB Project Guidelines

## Overview

EDB is a compact but fully-featured database with strong extensibility:

1. **Pluggable data types** — Like PostgreSQL's type extension system
2. **Pluggable storage engines** — Swappable storage backends
3. **Pluggable storage I/O backends** — POSIX file I/O, xNVMe direct NVMe access, and future interfaces
4. **Single-node first, distributed later** — Shared-nothing architecture prep from day one

## Development Plan

See [docs/PLAN.md](docs/PLAN.md) for the full phased plan with interface designs.

| Phase | Focus | Status |
|-------|-------|--------|
| **0** | Project infrastructure (CMake, tooling, primitives, error, logging, gtest) | 🔲 |
| **1** | Storage I/O backend (`EdbStorageIOOps`, POSIX backend) | 🔲 |
| **2** | Storage engine — Heap (page format, buffer pool, `EdbStorageEngineOps`) | 🔲 |
| **3** | Type system (`EdbTypeRegistry`, `EdbTypeOps`, built-in types) | 🔲 |
| **4** | Catalog (system tables, initdb bootstrap, catalog cache) | 🔲 |
| **5** | Query engine — basic (parser, analyzer, Volcano executor) | 🔲 |
| **6** | Transactions (MVCC, WAL, row locks, deadlock detection) | 🔲 |
| **7** | Network (PostgreSQL wire protocol v3, simple query mode) | 🔲 |
| **8** | xNVMe I/O backend + POSIX vs xNVMe benchmark | 🔲 |
| **9** | Distributed — Raft, partitioning, 2PC (future) | 🔲 |

**Current priority**: Phase 0 → 1 → 2. Goal: end-to-end write-page / read-page through the full storage stack.

Status legend: 🔲 not started · 🔄 in progress · ✅ done

## Development Workflow

When working on any task, break it into the smallest independently buildable and testable units. **Each unit must build clean, pass all tests, and be committed before moving on.**

### Per-task loop

```
1. Identify the next atomic unit of work (one interface / one struct / one function)
2. Write the interface + C++26 contracts  (pre/post clauses)
3. Write tests  (happy path · boundary · contract-violation)
4. Implement until tests pass
5. make format && make tidy  →  fix any issues
6. make -j$(nproc) && ctest --output-on-failure  →  must be green
7. git add -p && git commit  →  one focused commit per unit
8. Repeat
```

### Commit message format

```
<phase>(<module>): <imperative summary>

- <what changed and why, if not obvious>
- <contract / invariant added>
- <test coverage added>
```

Examples:
```
phase0(primitives): add i32/u64/b8 wrappers with deleted implicit conversions
phase1(io/posix): implement EdbStorageIOOps::read via pread64
phase2(buffer): add clock-sweep eviction with pin-count guard
```

### What counts as one atomic unit

- A single header with a new struct/class/concept (no implementation yet)
- A single interface method + its contracts + its tests
- A single concrete implementation of an existing interface
- A bug fix + its regression test (always together, never separately)

### Rules

- **Never commit red tests.** If tests break, fix before committing.
- **Never commit with format or tidy warnings.** Run `make format && make tidy` as the last step before every commit.
- **Never batch unrelated changes** into one commit. Reviewers and `git bisect` both depend on atomic history.
- A commit that only adds tests (no implementation) is valid and encouraged.

## Architecture Principles

- **Interface before implementation**: Define stable interfaces (C headers or traits) before writing concrete implementations
- **Layered storage**: Query Engine → Storage Engine → Storage I/O Backend → Device
- **Shared-nothing readiness**: Each layer assumes horizontal partitioning is possible; avoid global mutable state
- **Zero-copy where possible**: Prefer `mmap`, `io_uring`, xNVMe `SPDK` zero-copy paths over buffered I/O

## Directory Layout

```
edb/
├── src/
│   ├── types/          # Extensible type system (type registry, operators, casts)
│   ├── catalog/        # System catalog (tables, columns, indexes metadata)
│   ├── query/          # SQL parser, planner, executor
│   ├── storage/        # Storage engine abstraction & implementations
│   │   ├── engine/     # Storage engine interface + implementations (heap, columnar, etc.)
│   │   └── io/         # Storage I/O backend interface + implementations
│   │       ├── posix/  # POSIX file I/O backend (pread/pwrite, mmap, fsync)
│   │       └── xnvme/  # xNVMe backend (direct NVMe command submission)
│   ├── transaction/    # MVCC, WAL, lock manager
│   ├── index/          # Pluggable index access methods (btree, hash, gin, gist)
│   ├── network/        # Wire protocol (PostgreSQL-compatible if feasible)
│   ├── distributed/    # Placeholder: raft/paxos, sharding, replication
│   └── utils/          # Arena allocators, skiplists, bitmaps
├── tests/              # Unit + integration tests
├── docs/
│   ├── ARCHITECTURE.md # Detailed design docs
│   └── STORAGE.md      # Storage engine & I/O backend developer guide
└── extensions/         # Example custom types & storage engines
```

## Build & Test

```bash
# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=26 -DCMAKE_CXX_COMPILER=g++-16
make -j$(nproc)

# Run all tests (unit + regression)
ctest --output-on-failure

# Run specific test suite
ctest -R unit --output-on-failure
ctest -R regression --output-on-failure

# Format all source files
make format

# Check format without modifying (CI)
make format-check

# Run clang-tidy
make tidy

# With address sanitizer
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=26 -DEDB_SANITIZE=address
make -j$(nproc)
```

**CMakeLists conventions**:
- Root `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 26` and `CMAKE_CXX_STANDARD_REQUIRED ON`
- Each subsystem (`src/storage/`, `src/types/`, etc.) has its own `CMakeLists.txt` as a library target
- Use `target_link_libraries` with visibility (`PRIVATE`/`PUBLIC`) — no `include_directories` globally
- Compiler: `g++-16` (`-DCMAKE_CXX_COMPILER=g++-16`)
- Compiler flags: `-Wall -Wextra -Werror -Wpedantic -fcontracts` in Debug; `-O2 -DNDEBUG -fcontracts` in Release
- `-fno-gnu-extensions` / `-fno-ms-extensions` always on — **no compiler extensions permitted**
- `format` target: runs `clang-format -i` over all `src/**` and `tests/**`
- `format-check` target: runs `clang-format --dry-run --Werror` (used in CI)
- `tidy` target: runs `clang-tidy` with `compile_commands.json` over all sources

**Pre-merge gate** (CI must pass before any PR is merged):
1. `make format-check` — zero diff
2. `make tidy` — zero warnings
3. `ctest --output-on-failure` under ASan + UBSan

## Conventions

- **Language**: C++26 throughout. Use `std::expected` for error propagation, concepts/constraints for interface boundaries, `std::span` for buffer views
- **Naming**: `edb_<module>_<action>` for public APIs; file-local helpers are `static` or in anonymous namespaces
- **Error handling**: Return `std::expected<T, EdbError>`; never `abort()` or throw in library code
- **Memory**: Use arena allocators for query execution; RAII wrappers for long-lived objects
- **Threading**: Assume multi-threaded; document thread-safety per module header
- **Logging**: Structured logging via `edb_log(level, module, fmt, ...)`

### Type Safety

- **No raw primitive types**: Never use `int`, `unsigned`, `long`, `float`, `double`, `bool` directly in EDB code. Use the type-safe wrappers from `src/utils/primitives.hpp` instead.
- **No implicit conversions**: All narrowing conversions are compile errors (`-Wconversion -Wsign-conversion`). Use explicit casts with a comment explaining why.
- **`explicit` everywhere**: Every single-argument constructor and conversion operator must be marked `explicit`. No exceptions.
- **No compiler extensions**: Code must compile clean with `-Wpedantic -fno-gnu-extensions`. Stick to ISO C++26.

#### Primitive Type Wrappers (`src/utils/primitives.hpp`)

All integer, floating-point, and boolean types are wrapped to prevent silent narrowing and implicit cross-type arithmetic:

| Wrapper | Underlying | Notes |
|---------|-----------|-------|
| `i8`, `i16`, `i32`, `i64` | `int8_t` … `int64_t` | Signed integers |
| `u8`, `u16`, `u32`, `u64` | `uint8_t` … `uint64_t` | Unsigned integers |
| `f32`, `f64` | `float`, `double` | Floating-point |
| `b8` | `bool` | Boolean (avoids `int`/`bool` promotion pitfalls) |
| `usize` | `size_t` | Sizes and indices |
| `isize` | `ptrdiff_t` | Signed pointer differences |

Each wrapper is a `struct` with an `explicit` constructor, deleted implicit conversions, and arithmetic operators that return the same wrapper type. Mixing types (e.g., `i32 + u32`) is a compile error.

```cpp
// BAD — raw types, implicit narrowing, unclear intent
int read_page(unsigned page_id, void* buf, int len);
int result = read_page(42, buf, 4096);   // what do the return values mean?

// GOOD — wrapped types, explicit, self-documenting
auto read_page(u64 page_id, std::span<std::byte> buf)
    -> std::expected<void, EdbError>
    pre { page_id < max_page_count() }
    pre { buf.size() >= page_size() };

// BAD — implicit construction, implicit narrowing
struct PageId { PageId(uint64_t v); };   // missing explicit
void foo(PageId id);
foo(42);                                 // compiles, but unclear intent

// GOOD
struct PageId {
    explicit PageId(u64 v) : value(v) {}
    u64 value;
};
foo(PageId{u64{42}});                    // intent is clear
```

Exceptions (must be commented with `// raw-primitive: <reason>`):
- Interfacing with OS/C APIs that require `int`, `size_t`, etc. — use a local cast at the call site only
- `std` library template parameters where the type is fixed by the standard

### Formatting & Linting

- Code style is enforced by `.clang-format` at the repo root. Run `make format` before committing.
- `clang-tidy` is configured in `.clang-tidy` and aligns with the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines). Key enabled checks:
  - `cppcoreguidelines-*`
  - `modernize-*`
  - `bugprone-*`
  - `readability-*`
  - `performance-*`
- **Never suppress a tidy warning without a comment** explaining the exception.
4. **Write the implementation**: Code that satisfies the contracts and passes all tests.

```cpp
// Step 1+2: Signature with contracts
auto read_page(uint64_t page_id, std::span<std::byte> buf)
    -> std::expected<void, EdbError>
    pre  { page_id < max_page_count() }        // precondition
    pre  { buf.size() >= page_size() }         // precondition
    post (result) { !result || buf[0] != std::byte{0xFF} }; // postcondition (example)

// Step 3: Tests written before implementation (see tests/ layout below)
// Step 4: Implementation body
```

Contracts are compiled in **Check_Semantic_Equivalent** mode in Debug, and **Ignore** in Release unless `EDB_CONTRACTS_RELEASE=1` is set.

## Testing Requirements

Strict, comprehensive testing is mandatory. All tests live under `tests/`:

```
tests/
├── unit/               # Per-module unit tests (mirror src/ structure)
│   ├── types/
│   ├── storage/
│   │   ├── engine/
│   │   └── io/
│   ├── catalog/
│   ├── query/
│   └── transaction/
└── regression/         # One test file per bug/PR, named <issue-id>_<description>.cpp
```

**Rules:**
- Every public function has at least one unit test covering: happy path, boundary values, and contract-violation behavior
- Regression tests are mandatory for every bug fix — add before fixing the bug
- Tests must be deterministic and hermetic (no shared state, no relying on filesystem side effects)
- Use Google Test (`gtest`) as the test framework
- Test names follow `TEST(ModuleName, FunctionName_Scenario)` convention
- CI runs all tests under AddressSanitizer and UBSan

## Storage Engine & I/O Interface

The storage subsystem has two levels of abstraction:

### 1. Storage Engine Interface (`src/storage/engine/`)

Defines how the query engine reads/writes tuples/pages:

```c
typedef struct EdbStorageEngineOps {
    void* (*create)(const char* path, EdbStorageConfig* cfg);
    void  (*destroy)(void* engine);
    int   (*read_page)(void* engine, uint64_t page_id, void* buf);
    int   (*write_page)(void* engine, uint64_t page_id, const void* buf);
    int   (*begin_transaction)(void* engine);
    int   (*commit)(void* engine);
    int   (*abort)(void* engine);
} EdbStorageEngineOps;
```

Implementations: heap (row-store), columnar, LSM-tree.

### 2. Storage I/O Backend Interface (`src/storage/io/`)

Defines how pages are actually read from / written to the underlying device:

```c
typedef struct EdbStorageIOOps {
    void* (*open)(const char* path, EdbIOConfig* cfg);
    void  (*close)(void* handle);
    ssize_t (*read)(void* handle, uint64_t offset, void* buf, size_t len);
    ssize_t (*write)(void* handle, uint64_t offset, const void* buf, size_t len);
    int   (*sync)(void* handle);
    int   (*truncate)(void* handle, uint64_t size);
} EdbStorageIOOps;
```

Implementations:
- **POSIX**: Standard `pread`/`pwrite`, optional `mmap`, `fsync`/`fdatasync`
- **xNVMe**: Direct NVMe command submission via [xNVMe](https://xnvme.io/) library for ultra-low latency I/O
- **Future**: `io_uring`, SPDK, RDMA

Storage engines do not call POSIX directly; they use the `EdbStorageIOOps` vtable. This lets the same heap engine run on both a local file and an NVMe namespace without code changes.

## Type System Extension

Types are registered at runtime via `edb_type_register()`:

```c
EdbType* mytype = edb_type_register("mytype",
    sizeof(MyType),
    mytype_in,    // text → internal
    mytype_out,   // internal → text
    mytype_cmp,   // comparison
    mytype_hash   // hash
);
```

See `src/types/README.md` for full extension API.

## Distributed Roadmap (Future)

- Partition keys derived from catalog metadata
- Transaction coordinator (2PC or Percolator-style)
- Raft-based replication for catalog and WAL
- **Do not** let single-node code assume a global buffer pool or single log — always pass context handles
