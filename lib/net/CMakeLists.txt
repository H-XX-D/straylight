add_library(straylight-net SHARED src/stub.cpp)
target_include_directories(straylight-net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(straylight-net PUBLIC straylight-common)
set_target_properties(straylight-net PROPERTIES VERSION ${STRAYLIGHT_VERSION} SOVERSION ${STRAYLIGHT_SO_VERSION})
