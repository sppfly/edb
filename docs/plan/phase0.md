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
| `src/utils/contracts.hpp` | `EDB_PRE(condition)` macro; expands to GCC `pre(condition)`, empty under Clang | ✅ |
| gtest integration | CMake `FetchContent` for googletest; `tests/` skeleton | ✅ |
| Toolchain enforcement | `g++-16` required; hard CMake `FATAL_ERROR` if compiler differs; clang-format/tidy `REQUIRED` | ✅ |
| `.clangd` | Strips `-fcontracts` so VS Code/clangd analyses cleanly | ✅ |
| CI gate | `format-check` → `tidy` → `ctest` (ASan + UBSan) | ✅ |

## Notes

- `EDB_PRE` macro lives in `src/utils/contracts.hpp`; `contracts.cpp` provides `handle_contract_violation → std::terminate` for the GCC contracts runtime
- `cmake/StripContracts.cmake` generates a tidy-compatible `compile_commands.json` with `-fcontracts` stripped
- GCC 16 does **not** allow contracts on virtual functions → all interfaces use the **public non-virtual wrapper + protected virtual `*_impl` hook** pattern
