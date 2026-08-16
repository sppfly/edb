cmake_minimum_required(VERSION 3.25)

# EDB requires a Clang compiler of major version 22 or newer for C++26. The
# binary may be named clang++-22 or plain clang++ (e.g. a distro where clang++
# is 22).
find_program(EDB_CLANGXX_EXECUTABLE NAMES clang++-22 clang++)

# Pick the project compiler before project() performs compiler detection.
if(NOT DEFINED CMAKE_CXX_COMPILER AND EDB_CLANGXX_EXECUTABLE)
  set(CMAKE_CXX_COMPILER ${EDB_CLANGXX_EXECUTABLE} CACHE FILEPATH "C++ compiler")
endif()

project(edb LANGUAGES CXX)

# Accept any compiler that really is Clang 22 or newer, regardless of the
# binary name (clang++-22 vs plain clang++).
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR
    "EDB must be built with Clang 22 or newer. "
    "Detected '${CMAKE_CXX_COMPILER}' (compiler ID '${CMAKE_CXX_COMPILER_ID}'). "
    "Reconfigure with -DCMAKE_CXX_COMPILER=clang++-22 (or clang++) "
    "and clear the build directory."
  )
endif()
if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "22.0.0")
  message(FATAL_ERROR
    "EDB requires Clang 22 or newer; detected '${CMAKE_CXX_COMPILER}' "
    "version ${CMAKE_CXX_COMPILER_VERSION}. "
    "Reconfigure with -DCMAKE_CXX_COMPILER=clang++-22 (or clang++) "
    "and clear the build directory."
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
  add_compile_options(${EDB_WARN_FLAGS} -g)
  add_compile_definitions(EDB_DEBUG)
else()
  add_compile_options(${EDB_WARN_FLAGS} -O2 -DNDEBUG)
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
# Locate a clang tool (version 22 or newer): prefer the -22 suffixed binary,
# fall back to the plain name, and verify the tool's actual version.
function(edb_find_clang_tool tool_name out_var)
  find_program(${out_var} NAMES ${tool_name}-22 ${tool_name})
  if(NOT ${out_var})
    message(FATAL_ERROR
      "EDB requires ${tool_name} version 22 or newer; "
      "neither '${tool_name}-22' nor '${tool_name}' was found on PATH."
    )
  endif()
  execute_process(
    COMMAND ${${out_var}} --version
    OUTPUT_VARIABLE EDB_TOOL_VERSION_RAW
    ERROR_QUIET
  )
  string(REGEX MATCH "version [0-9]+(\\.[0-9]+)*" EDB_TOOL_VERSION_STR "${EDB_TOOL_VERSION_RAW}")
  string(REPLACE "version " "" EDB_TOOL_VERSION "${EDB_TOOL_VERSION_STR}")
  if(EDB_TOOL_VERSION VERSION_LESS "22")
    message(FATAL_ERROR
      "EDB requires ${tool_name} version 22 or newer; "
      "'${${out_var}}' reported '${EDB_TOOL_VERSION_STR}'."
    )
  endif()
  message(STATUS "Using ${tool_name}: ${${out_var}} (${EDB_TOOL_VERSION})")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

edb_find_clang_tool(clang-format CLANG_FORMAT)
edb_find_clang_tool(clang-tidy CLANG_TIDY)

# run-clang-tidy is a versionless Python wrapper; pin it to the verified
# clang-tidy binary instead of relying on its own version.
find_program(EDB_RUN_CLANG_TIDY NAMES run-clang-tidy-22 run-clang-tidy)
if(NOT EDB_RUN_CLANG_TIDY)
  message(FATAL_ERROR
    "EDB requires run-clang-tidy (clang 22 toolchain); "
    "neither 'run-clang-tidy-22' nor 'run-clang-tidy' was found on PATH."
  )
endif()

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

cmake_host_system_information(RESULT EDB_NPROC QUERY NUMBER_OF_LOGICAL_CORES)

add_custom_target(tidy
  COMMAND ${EDB_RUN_CLANG_TIDY} -p ${CMAKE_BINARY_DIR}
          -clang-tidy-binary ${CLANG_TIDY}
          -source-filter "${CMAKE_SOURCE_DIR}/src/.*"
          -j ${EDB_NPROC}
  COMMENT "Running clang-tidy (src only)"
)
