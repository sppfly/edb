add_library(edb_types STATIC
  registry.cpp
)

target_link_libraries(edb_types PUBLIC
  edb_utils
)

target_include_directories(edb_types PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)