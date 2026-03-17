cmake_minimum_required(VERSION 3.25)
project(straylight-fabric LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Include paths ────────────────────────────────────────────────────
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# ── Library: fabric-core ─────────────────────────────────────────────
add_library(fabric-core STATIC
    topology.cpp
    query_engine.cpp
    udev_monitor.cpp
)

target_include_directories(fabric-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# ── Daemon executable ────────────────────────────────────────────────
add_executable(straylight-fabric main.cpp)
target_link_libraries(straylight-fabric PRIVATE fabric-core)

# pthreads for udev monitor thread
find_package(Threads REQUIRED)
target_link_libraries(straylight-fabric PRIVATE Threads::Threads)
target_link_libraries(fabric-core PUBLIC Threads::Threads)

# ── Install ──────────────────────────────────────────────────────────
install(TARGETS straylight-fabric DESTINATION sbin)
install(DIRECTORY DESTINATION /var/lib/straylight/fabric)
