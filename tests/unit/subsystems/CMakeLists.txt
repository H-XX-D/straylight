# Subsystem daemon tests — populated by Plan 2 chunks

add_executable(test_bus test_bus.cpp
    ${CMAKE_SOURCE_DIR}/bin/bus/bus_daemon.cpp)
target_link_libraries(test_bus PRIVATE straylight-common GTest::gtest_main)
target_include_directories(test_bus PRIVATE ${CMAKE_SOURCE_DIR}/bin/bus)
gtest_discover_tests(test_bus)

add_executable(test_registry test_registry.cpp
    ${CMAKE_SOURCE_DIR}/bin/registry/store.cpp)
target_link_libraries(test_registry PRIVATE straylight-common GTest::gtest_main)
target_include_directories(test_registry PRIVATE ${CMAKE_SOURCE_DIR}/bin/registry)
gtest_discover_tests(test_registry)

add_executable(test_entropy_daemon test_entropy_daemon.cpp
    ${CMAKE_SOURCE_DIR}/bin/entropy/drbg.cpp)
target_link_libraries(test_entropy_daemon PRIVATE straylight-common straylight-hw GTest::gtest_main)
target_include_directories(test_entropy_daemon PRIVATE ${CMAKE_SOURCE_DIR}/bin/entropy)
gtest_discover_tests(test_entropy_daemon)

add_executable(test_scheduler test_scheduler.cpp
    ${CMAKE_SOURCE_DIR}/bin/scheduler/topology.cpp
    ${CMAKE_SOURCE_DIR}/bin/scheduler/cgroup.cpp)
target_link_libraries(test_scheduler PRIVATE straylight-common GTest::gtest_main)
target_include_directories(test_scheduler PRIVATE ${CMAKE_SOURCE_DIR}/bin/scheduler)
gtest_discover_tests(test_scheduler)
