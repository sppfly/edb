add_executable(test_page
    page_store_test.cpp
)

target_link_libraries(test_page
    PRIVATE edb_page GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_page)
