add_library(straylight-common SHARED
    src/stub.cpp
)

target_include_directories(straylight-common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(straylight-common PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-common PRIVATE straylight_common_EXPORTS)
