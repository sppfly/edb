add_library(edb_query STATIC
  binder.cpp
  exec.cpp
  lexer.cpp
  logical_plan.cpp
  physical_plan.cpp
  parser.cpp
  query_engine.cpp
)

target_include_directories(edb_query PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(edb_query PUBLIC
  edb_catalog
  edb_utils
)
