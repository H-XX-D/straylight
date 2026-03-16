find_package(GTest 1.14 REQUIRED)
include(GoogleTest)

add_subdirectory(unit/common)
add_subdirectory(unit/ml)
add_subdirectory(unit/net)
add_subdirectory(unit/hw)
add_subdirectory(unit/subsystems)
add_subdirectory(integration)

if(BUILD_COMPOSITOR)
    add_subdirectory(unit/compositor)
endif()
