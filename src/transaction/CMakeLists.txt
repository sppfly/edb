add_library(edb_transaction STATIC
  transaction_manager.cpp
)

target_include_directories(edb_transaction PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(edb_transaction PUBLIC
  edb_utils
)