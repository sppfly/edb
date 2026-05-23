add_library(edb_query STATIC
  lexer.cpp
  parser.cpp
)

target_include_directories(edb_query PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(edb_query PUBLIC
  edb_utils
)
