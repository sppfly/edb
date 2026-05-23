add_executable(test_lock
  lock_manager_test.cpp
)

target_link_libraries(test_lock
  PRIVATE edb_lock GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_lock)