add_executable(test_types
  builtin_types_test.cpp
  registry_test.cpp
)

target_link_libraries(test_types
  PRIVATE edb_types GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_types)