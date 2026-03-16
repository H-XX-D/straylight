add_library(straylight-hw SHARED src/stub.cpp)
target_include_directories(straylight-hw PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(straylight-hw PUBLIC straylight-common)
set_target_properties(straylight-hw PROPERTIES VERSION ${STRAYLIGHT_VERSION} SOVERSION ${STRAYLIGHT_SO_VERSION})
