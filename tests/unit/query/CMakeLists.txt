add_executable(test_query
  test_lexer.cpp
  test_parser.cpp
)

target_link_libraries(test_query
  PRIVATE edb_query GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_query)
