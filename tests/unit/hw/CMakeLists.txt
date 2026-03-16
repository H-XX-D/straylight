add_executable(test_hw_gpu test_gpu.cpp)
target_link_libraries(test_hw_gpu PRIVATE straylight-hw GTest::gtest_main)
gtest_discover_tests(test_hw_gpu)

add_executable(test_hw_entropy test_entropy.cpp)
target_link_libraries(test_hw_entropy PRIVATE straylight-hw GTest::gtest_main)
gtest_discover_tests(test_hw_entropy)
