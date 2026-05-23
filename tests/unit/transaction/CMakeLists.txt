add_executable(test_transaction
  transaction_manager_test.cpp
  visibility_test.cpp
)

target_link_libraries(test_transaction
  PRIVATE edb_transaction GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_transaction)