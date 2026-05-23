add_executable(test_regression
  phase5_query_engine_sql_path.cpp
)

target_link_libraries(test_regression
  PRIVATE edb_query GTest::gtest_main
)

target_compile_definitions(test_regression
  PRIVATE EDB_REGRESSION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

include(GoogleTest)
gtest_discover_tests(test_regression)