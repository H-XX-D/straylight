add_executable(test_common_result test_result.cpp)
target_link_libraries(test_common_result PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_result)

add_executable(test_common_log test_log.cpp)
target_link_libraries(test_common_log PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_common_log)
