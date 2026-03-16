find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED libbpf)
pkg_check_modules(LIBXDP libxdp)

add_executable(straylight-xdp
    main.cpp
    loader.cpp
    maps.cpp
    af_xdp.cpp)

target_link_libraries(straylight-xdp PRIVATE
    straylight-common
    ${LIBBPF_LIBRARIES})

target_include_directories(straylight-xdp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBBPF_INCLUDE_DIRS})

if(LIBXDP_FOUND)
    target_link_libraries(straylight-xdp PRIVATE ${LIBXDP_LIBRARIES})
    target_include_directories(straylight-xdp PRIVATE ${LIBXDP_INCLUDE_DIRS})
    target_compile_definitions(straylight-xdp PRIVATE HAVE_LIBXDP=1)
endif()

target_compile_options(straylight-xdp PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS straylight-xdp DESTINATION bin)
