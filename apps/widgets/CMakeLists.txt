# apps/widgets/CMakeLists.txt
# ML/HPC/System/Research widgets — compiled as a static library linked into the shell

file(GLOB_RECURSE WIDGET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ml/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/hpc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/system/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/widget_registry.cpp"
)

add_library(straylight-widgets STATIC ${WIDGET_SOURCES})

target_include_directories(straylight-widgets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(straylight-widgets PUBLIC
    straylight-common
    imgui
)

install(TARGETS straylight-widgets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Standalone widget launcher
add_executable(straylight-widget-launcher main.cpp)

target_link_libraries(straylight-widget-launcher PRIVATE
    straylight-widgets
    straylight-common
    imgui
)

install(TARGETS straylight-widget-launcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
