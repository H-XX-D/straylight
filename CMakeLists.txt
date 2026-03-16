cmake_minimum_required(VERSION 3.25)
project(straylight VERSION 1.0.0 LANGUAGES CXX C)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include shared cmake modules
include(cmake/StraylightVersion.cmake)
include(cmake/StraylightCommon.cmake)

# Build options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_COMPOSITOR "Build Wayland compositor" ON)
option(BUILD_SHELL "Build desktop shell" ON)
option(BUILD_APPS "Build built-in applications" ON)
option(BUILD_SUBSYSTEMS "Build subsystem binaries" ON)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Shared libraries
add_subdirectory(lib/common)
add_subdirectory(lib/ml)
add_subdirectory(lib/net)
add_subdirectory(lib/hw)

# Subsystem daemons
if(BUILD_SUBSYSTEMS)
    add_subdirectory(bin/bus)
    add_subdirectory(bin/registry)
    add_subdirectory(bin/entropy)
    add_subdirectory(bin/scheduler)
    add_subdirectory(bin/core)
endif()

# Compositor
if(BUILD_COMPOSITOR)
    add_subdirectory(compositor)
endif()

# Shell, Greeter, OOBE, Wizard
if(BUILD_SHELL)
    add_subdirectory(shell)
    add_subdirectory(apps/greeter)
    add_subdirectory(apps/oobe)
    add_subdirectory(apps/wizard)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Status
message(STATUS "========================================")
message(STATUS "StrayLight OS v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "  C++ Standard:  ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests:         ${BUILD_TESTS}")
message(STATUS "  Compositor:    ${BUILD_COMPOSITOR}")
message(STATUS "  Shell:         ${BUILD_SHELL}")
message(STATUS "  Apps:          ${BUILD_APPS}")
message(STATUS "  Subsystems:    ${BUILD_SUBSYSTEMS}")
message(STATUS "========================================")
