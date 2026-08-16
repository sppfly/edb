cmake_minimum_required(VERSION 3.25)

# EDB requires GCC 16 for C++26 contracts via -fcontracts.
find_program(EDB_GXX16_EXECUTABLE NAMES g++-16 REQUIRED)

# Pick the project compiler before project() performs compiler detection.
if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER ${EDB_GXX16_EXECUTABLE} CACHE FILEPATH "C++ compiler")
endif()

project(edb LANGUAGES CXX)

get_filename_component(EDB_REQUIRED_CXX_REAL ${EDB_GXX16_EXECUTABLE} REALPATH)
get_filename_component(EDB_ACTUAL_CXX_REAL ${CMAKE_CXX_COMPILER} REALPATH)
if(NOT EDB_ACTUAL_CXX_REAL STREQUAL EDB_REQUIRED_CXX_REAL)
  message(FATAL_ERROR
    "EDB must be built with g++-16. "
    "Detected '${CMAKE_CXX_COMPILER}', required '${EDB_GXX16_EXECUTABLE}'. "
    "Reconfigure with -DCMAKE_CXX_COMPILER=${EDB_GXX16_EXECUTABLE} "
    "or clear the build directory."
  )
endif()

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # -fno-gnu-extensions / ISO C++ only

# ---------------------------------------------------------------------------
# compile_commands.json  (used by clang-tidy)
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# ccache
# ---------------------------------------------------------------------------
find_program(CCACHE_EXECUTABLE ccache)
if(CCACHE_EXECUTABLE)
  set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_EXECUTABLE})
  message(STATUS "Using ccache: ${CCACHE_EXECUTABLE}")
else()
  message(STATUS "ccache not found; building without it")
endif()

# ---------------------------------------------------------------------------
# Linker: mold
# ---------------------------------------------------------------------------
set(EDB_USE_MOLD ON CACHE BOOL "Use mold linker when available")
find_program(EDB_MOLD_LINKER mold)
if(EDB_USE_MOLD AND EDB_MOLD_LINKER)
  add_link_options(-fuse-ld=mold)
  message(STATUS "Using linker: mold (${EDB_MOLD_LINKER})")
else()
  message(STATUS "mold disabled or not found; using default linker")
endif()

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
add_subdirectory(src/transaction)
add_subdirectory(src/storage)
add_subdirectory(src/wal)
add_subdirectory(src/lock)
add_subdirectory(src/types)
add_subdirectory(src/catalog)
add_subdirectory(src/query)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# format / format-check / tidy targets
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT clang-format REQUIRED)
find_program(CLANG_TIDY   clang-tidy REQUIRED)

file(GLOB_RECURSE EDB_ALL_SOURCES
  "${CMAKE_SOURCE_DIR}/src/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/**/*.hpp"
  "${CMAKE_SOURCE_DIR}/tests/**/*.cpp"
  "${CMAKE_SOURCE_DIR}/tests/**/*.hpp"
)

add_custom_target(format
  COMMAND ${CLANG_FORMAT} -i ${EDB_ALL_SOURCES}
  COMMENT "Formatting all sources with clang-format"
)
add_custom_target(format-check
  COMMAND ${CLANG_FORMAT} --dry-run --Werror ${EDB_ALL_SOURCES}
  COMMENT "Checking format (no files modified)"
)

add_custom_target(tidy
  # clang-tidy uses clang which doesn't understand -fcontracts (GCC-only).
  # Strip it from compile_commands before running.
  COMMAND ${CMAKE_COMMAND}
    -DINPUT=${CMAKE_BINARY_DIR}/compile_commands.json
      -DOUTPUT=${CMAKE_BINARY_DIR}/tidy/compile_commands.json
    -P ${CMAKE_SOURCE_DIR}/cmake/StripContracts.cmake
  COMMAND ${CLANG_TIDY}
      -p ${CMAKE_BINARY_DIR}/tidy
          ${EDB_ALL_SOURCES}
  COMMENT "Running clang-tidy"
)
