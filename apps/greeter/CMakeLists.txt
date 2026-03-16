set(GREETER_SOURCES
    main.cpp
    auth.cpp
    session.cpp
    ui.cpp
)

add_executable(straylight-greeter ${GREETER_SOURCES})

target_include_directories(straylight-greeter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

# Generate ext-session-lock-v1 protocol bindings
find_program(WAYLAND_SCANNER wayland-scanner)
set(SESSION_LOCK_PROTOCOL
    "${CMAKE_SOURCE_DIR}/protocols/ext-session-lock-v1.xml")

if(WAYLAND_SCANNER AND EXISTS "${SESSION_LOCK_PROTOCOL}")
    set(SESSION_LOCK_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h")
    set(SESSION_LOCK_CODE
        "${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c")

    add_custom_command(
        OUTPUT ${SESSION_LOCK_HEADER}
        COMMAND ${WAYLAND_SCANNER} client-header
                ${SESSION_LOCK_PROTOCOL} ${SESSION_LOCK_HEADER}
        DEPENDS ${SESSION_LOCK_PROTOCOL}
    )
    add_custom_command(
        OUTPUT ${SESSION_LOCK_CODE}
        COMMAND ${WAYLAND_SCANNER} private-code
                ${SESSION_LOCK_PROTOCOL} ${SESSION_LOCK_CODE}
        DEPENDS ${SESSION_LOCK_PROTOCOL}
    )
    target_sources(straylight-greeter PRIVATE
        ${SESSION_LOCK_HEADER}
        ${SESSION_LOCK_CODE}
    )
    target_include_directories(straylight-greeter PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

target_link_libraries(straylight-greeter PRIVATE
    straylight-common
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    imgui
    pam
)

install(TARGETS straylight-greeter
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
