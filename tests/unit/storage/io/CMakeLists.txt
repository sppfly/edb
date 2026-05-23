add_executable(test_io
    io_ops_test.cpp
    posix_io_test.cpp
)

target_link_libraries(test_io
    PRIVATE edb_io_posix GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_io)
