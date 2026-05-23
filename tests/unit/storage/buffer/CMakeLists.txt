add_executable(test_buffer
    buffer_pool_test.cpp
)

target_link_libraries(test_buffer
    PRIVATE edb_buffer GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_buffer)
