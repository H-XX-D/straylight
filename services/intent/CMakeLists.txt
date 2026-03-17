cmake_minimum_required(VERSION 3.25)
project(straylight-intent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# StrayLight shared include path
set(STRAYLIGHT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../../straylight-os/include")

add_executable(straylight-intent
    main.cpp
    intent_engine.cpp
    action_registry.cpp
    pattern_matcher.cpp
    executor.cpp
)

target_include_directories(straylight-intent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STRAYLIGHT_INCLUDE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(straylight-intent PRIVATE Threads::Threads)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(straylight-intent PRIVATE rt)
endif()

# Installation
install(TARGETS straylight-intent RUNTIME DESTINATION bin)
install(DIRECTORY DESTINATION /etc/straylight/intent.d)
