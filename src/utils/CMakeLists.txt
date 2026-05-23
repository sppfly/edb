add_library(edb_utils INTERFACE)

target_include_directories(edb_utils INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
