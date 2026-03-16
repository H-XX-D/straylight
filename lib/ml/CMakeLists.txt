add_library(straylight-ml SHARED
    src/tensor.cpp
    src/graph.cpp
    src/kv_cache.cpp
)

target_include_directories(straylight-ml
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(straylight-ml PUBLIC straylight-common)

set_target_properties(straylight-ml PROPERTIES
    VERSION ${STRAYLIGHT_VERSION}
    SOVERSION ${STRAYLIGHT_SO_VERSION}
)

target_compile_definitions(straylight-ml PRIVATE straylight_ml_EXPORTS)
