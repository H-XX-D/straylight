find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS    REQUIRED IMPORTED_TARGET wlroots>=0.18)
pkg_check_modules(WAYLAND    REQUIRED IMPORTED_TARGET wayland-server>=1.22)
pkg_check_modules(LIBINPUT   REQUIRED IMPORTED_TARGET libinput>=1.25)
pkg_check_modules(XKB        REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN     REQUIRED IMPORTED_TARGET pixman-1>=0.42)
pkg_check_modules(EGL        REQUIRED IMPORTED_TARGET egl)

# Generate Wayland protocol headers
find_package(WaylandProtocols REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

function(wayland_generate_protocol name xml_path)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.h")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${WAYLAND_SCANNER}" server-header "${xml_path}" "${header}"
        DEPENDS "${xml_path}"
    )
    add_custom_command(
        OUTPUT "${source}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml_path}" "${source}"
        DEPENDS "${xml_path}"
    )
    target_sources(straylight-compositor PRIVATE "${source}")
    target_include_directories(straylight-compositor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

add_executable(straylight-compositor
    main.cpp
    server.cpp
    output.cpp
    view.cpp
    workspace.cpp
    tiling.cpp
    input.cpp
    layer_shell.cpp
    session_lock.cpp
    decorations.cpp
    ipc.cpp
)

target_compile_features(straylight-compositor PRIVATE cxx_std_20)

target_link_libraries(straylight-compositor PRIVATE
    straylight-common
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND
    PkgConfig::LIBINPUT
    PkgConfig::XKB
    PkgConfig::PIXMAN
    PkgConfig::EGL
)

# Protocol: wlr-layer-shell-unstable-v1
set(LAYER_SHELL_XML
    "${CMAKE_SOURCE_DIR}/cmake/protocols/wlr-layer-shell-unstable-v1.xml")
wayland_generate_protocol(wlr-layer-shell "${LAYER_SHELL_XML}")

# Protocol: ext-session-lock-v1 (wayland-protocols staging)
pkg_get_variable(WP_DIR wayland-protocols pkgdatadir)
wayland_generate_protocol(ext-session-lock
    "${WP_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")

install(TARGETS straylight-compositor DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/services/compositor/straylight-compositor.service"
        DESTINATION lib/systemd/user)
install(FILES "${CMAKE_SOURCE_DIR}/config/compositor/straylight-compositor.conf"
        DESTINATION etc/straylight/compositor)
