add_executable(test_wal
  wal_manager_test.cpp
)

target_link_libraries(test_wal
  PRIVATE edb_wal GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_wal)