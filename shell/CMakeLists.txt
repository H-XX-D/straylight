set(SHELL_SOURCES
    main.cpp
    renderer.cpp
    layer_surface.cpp
    panels/top_bar.cpp
    panels/app_launcher.cpp
    panels/left_dock.cpp
    panels/bottom_dock.cpp
    widgets/notification.cpp
    widgets/volume_osd.cpp
    widgets/screenshot.cpp
    themes/theme_engine.cpp
    keybinds/keybind_manager.cpp
    settings/display.cpp
    settings/input.cpp
    settings/appearance.cpp
    settings/network.cpp
)

add_executable(straylight-shell ${SHELL_SOURCES})

target_include_directories(straylight-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
)

# Find Wayland and EGL packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

# Generate Wayland protocol bindings for wlr-layer-shell-unstable-v1
find_program(WAYLAND_SCANNER wayland-scanner)
set(LAYER_SHELL_PROTOCOL
    "${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml")

if(WAYLAND_SCANNER AND EXISTS "${LAYER_SHELL_PROTOCOL}")
    set(LAYER_SHELL_CLIENT_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h")
    set(LAYER_SHELL_CODE
        "${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c")

    add_custom_command(
        OUTPUT ${LAYER_SHELL_CLIENT_HEADER}
        COMMAND ${WAYLAND_SCANNER} client-header
                ${LAYER_SHELL_PROTOCOL} ${LAYER_SHELL_CLIENT_HEADER}
        DEPENDS ${LAYER_SHELL_PROTOCOL}
    )
    add_custom_command(
        OUTPUT ${LAYER_SHELL_CODE}
        COMMAND ${WAYLAND_SCANNER} private-code
                ${LAYER_SHELL_PROTOCOL} ${LAYER_SHELL_CODE}
        DEPENDS ${LAYER_SHELL_PROTOCOL}
    )
    target_sources(straylight-shell PRIVATE
        ${LAYER_SHELL_CLIENT_HEADER}
        ${LAYER_SHELL_CODE}
    )
    target_include_directories(straylight-shell PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

target_link_libraries(straylight-shell PRIVATE
    straylight-common
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    imgui
)

target_compile_options(straylight-shell PRIVATE
    ${WAYLAND_CFLAGS_OTHER}
    ${EGL_CFLAGS_OTHER}
    ${GLES_CFLAGS_OTHER}
)

install(TARGETS straylight-shell
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install default keybind configuration to /etc/straylight/
install(FILES keybinds/default_keybinds.json
    DESTINATION /etc/straylight
    RENAME keybinds.json
)
