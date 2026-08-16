# Phase 0 — Project Infrastructure ✅

Stand up the build system, tooling, and shared utilities that every later phase depends on. No feature code yet.

## Deliverables

| Deliverable | Details | Status |
|---|---|---|
| CMake skeleton | Root + per-subsystem `CMakeLists.txt`, `compile_commands.json`, `format`/`format-check`/`tidy` targets | ✅ |
| `.clang-format` | Enforce consistent style across all `src/` and `tests/` | ✅ |
| `.clang-tidy` | `cppcoreguidelines-*`, `modernize-*`, `bugprone-*`, `readability-*`, `performance-*` | ✅ |
| `src/utils/primitives.hpp` | `i8`…`i64`, `u8`…`u64`, `f32`/`f64`, `b8`, `usize`/`isize` wrappers | ✅ |
| `src/utils/error.hpp` | `EdbError` enum, `EdbResult<T>` alias for `std::expected<T, EdbError>` | ✅ |
| `src/utils/log.hpp` | `edb_log(level, module, fmt, ...)` structured logging | ✅ |
| `src/utils/assert.hpp` | `EDB_ASSERT(condition)` macro; expands to `assert()`, compiled out under `NDEBUG` | ✅ |
| gtest integration | CMake `FetchContent` for googletest; `tests/` skeleton | ✅ |
| Toolchain enforcement | `clang++-22` required; hard CMake `FATAL_ERROR` if compiler differs; clang-format/tidy `REQUIRED` | ✅ |
| CI gate | `format-check` → `tidy` → `ctest` (ASan + UBSan) | ✅ |

## Notes

- `EDB_ASSERT` macro lives in `src/utils/assert.hpp`; it expands to `assert()` and is compiled out in Release (`NDEBUG`)
- Interface preconditions are asserted at the top of each implementation; the public non-virtual wrapper + virtual `*_impl` hook pattern keeps those checks centralized
