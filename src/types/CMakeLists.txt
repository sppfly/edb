add_library(edb_types STATIC
  builtin_types.cpp
  registry.cpp
  row_codec.cpp
)

target_link_libraries(edb_types PUBLIC
  edb_utils
)

target_include_directories(edb_types PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)