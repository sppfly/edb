add_library(edb_utils STATIC
  contracts.cpp
)

target_include_directories(edb_utils PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
