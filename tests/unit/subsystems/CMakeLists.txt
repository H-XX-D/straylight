# Subsystem daemon tests — populated by Plan 2 chunks

add_executable(test_bus test_bus.cpp
    ${CMAKE_SOURCE_DIR}/bin/bus/bus_daemon.cpp)
target_link_libraries(test_bus PRIVATE straylight-common GTest::gtest_main)
target_include_directories(test_bus PRIVATE ${CMAKE_SOURCE_DIR}/bin/bus)
gtest_discover_tests(test_bus)
