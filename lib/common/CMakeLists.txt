find_package(spdlog 1.13 REQUIRED)

add_library(straylight-common SHARED
    src/log.cpp
)

target_include_directories(straylight-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-common
    PUBLIC spdlog::spdlog
)

set_target_properties(straylight-common PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-common PRIVATE straylight_common_EXPORTS)
