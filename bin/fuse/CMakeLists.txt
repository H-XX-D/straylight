# bin/fuse/CMakeLists.txt
# straylight-fuse — Tensor compression FUSE filesystem daemon.
# libfuse3 is optional; builds in simulation mode without it.

add_executable(straylight-fuse
    main.cpp
    fuse_daemon.cpp
    operations.cpp
    compression.cpp
    tensor_format.cpp
    cache.cpp)

target_link_libraries(straylight-fuse PRIVATE straylight-common)
target_include_directories(straylight-fuse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Try to find libfuse3.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FUSE3 QUIET fuse3)
    if(FUSE3_FOUND)
        target_include_directories(straylight-fuse PRIVATE ${FUSE3_INCLUDE_DIRS})
        target_link_libraries(straylight-fuse PRIVATE ${FUSE3_LIBRARIES})
        target_compile_definitions(straylight-fuse PRIVATE FUSE_USE_VERSION=31)
        message(STATUS "straylight-fuse: libfuse3 found")
    else()
        message(STATUS "straylight-fuse: libfuse3 not found, simulation mode only")
    endif()
else()
    message(STATUS "straylight-fuse: pkg-config not found, simulation mode only")
endif()

install(TARGETS straylight-fuse DESTINATION bin)
