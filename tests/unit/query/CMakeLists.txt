add_executable(test_query
  test_binder.cpp
  test_exec.cpp
  test_lexer.cpp
  test_logical_plan.cpp
  test_physical_plan.cpp
  test_parser.cpp
  test_query_engine.cpp
  test_query_engine_wal.cpp
)

target_link_libraries(test_query
  PRIVATE edb_query edb_wal GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_query)
