add_executable(test_engine
    engine_ops_test.cpp
)

target_link_libraries(test_engine
    PRIVATE edb_engine GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_engine)
