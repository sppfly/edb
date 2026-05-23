add_library(edb_catalog STATIC
  catalog.cpp
  table.cpp
)

target_link_libraries(edb_catalog PUBLIC
  edb_engine
  edb_types
)

target_include_directories(edb_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)