cmake_minimum_required(VERSION 3.25)
project(edb LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -fno-gnu-extensions / ISO C++ only

# ---------------------------------------------------------------------------
# Compiler
# ---------------------------------------------------------------------------
if(NOT CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER g++-16)
endif()

# ---------------------------------------------------------------------------
# compile_commands.json  (used by clang-tidy)
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
set(EDB_WARN_FLAGS
  -Wall -Wextra -Werror -Wpedantic
  -Wconversion -Wsign-conversion
  -Wno-unused-parameter            # relax during early dev; tighten later
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(${EDB_WARN_FLAGS} -g -fcontracts)
  add_compile_definitions(EDB_DEBUG)
else()
  add_compile_options(${EDB_WARN_FLAGS} -O2 -DNDEBUG -fcontracts)
endif()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
if(EDB_SANITIZE)
  add_compile_options(-fsanitize=${EDB_SANITIZE} -fno-omit-frame-pointer)
  add_link_options(-fsanitize=${EDB_SANITIZE})
endif()

# ---------------------------------------------------------------------------
# Subsystem libraries
# ---------------------------------------------------------------------------
add_subdirectory(src/utils)
add_subdirectory(src/storage)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# format / format-check / tidy targets
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format)
find_program(CLANG_TIDY   clang-tidy)

file(GLOB_RECURSE EDB_ALL_SOURCES
  "${CMAKE_SOURCE_DIR}/src/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/**/*.hpp"
  "${CMAKE_SOURCE_DIR}/tests/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/tests/**/*.hpp"
)

if(CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${EDB_ALL_SOURCES}
    COMMENT "Formatting all sources with clang-format"
  )
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT} --dry-run --Werror ${EDB_ALL_SOURCES}
    COMMENT "Checking format (no files modified)"
  )
else()
  message(WARNING "clang-format not found — 'format' and 'format-check' targets unavailable")
endif()

if(CLANG_TIDY)
  add_custom_target(tidy
    # clang-tidy uses clang which doesn't understand -fcontracts (GCC-only).
    # Strip it from compile_commands before running. Use sh -c for shell redirect.
    COMMAND sh -c "sed 's/ -fcontracts//g' ${CMAKE_BINARY_DIR}/compile_commands.json > ${CMAKE_BINARY_DIR}/compile_commands_tidy.json"
    COMMAND ${CLANG_TIDY}
            -p ${CMAKE_BINARY_DIR}/compile_commands_tidy.json
            ${EDB_ALL_SOURCES}
    COMMENT "Running clang-tidy"
  )
else()
  message(WARNING "clang-tidy not found — 'tidy' target unavailable")
endif()
