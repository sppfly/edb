add_library(edb_wal STATIC
  wal_manager.cpp
)

target_include_directories(edb_wal PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(edb_wal PUBLIC
  edb_io
  edb_transaction
  edb_utils
)