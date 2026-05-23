add_executable(test_utils
  primitives_test.cpp
  error_test.cpp
  log_test.cpp
)

target_link_libraries(test_utils
  PRIVATE edb_utils GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_utils)
