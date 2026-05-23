add_executable(test_catalog
  catalog_test.cpp
  table_test.cpp
)

target_link_libraries(test_catalog
  PRIVATE edb_catalog GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_catalog)