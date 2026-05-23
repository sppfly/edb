add_library(edb_lock STATIC
  lock_manager.cpp
)

target_include_directories(edb_lock PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(edb_lock PUBLIC
  edb_engine
  edb_transaction
  edb_utils
)