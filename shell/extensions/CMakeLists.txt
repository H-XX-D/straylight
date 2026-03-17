# Shell extension loader (statically linked into straylight-shell)
add_library(straylight-extensions STATIC
    extension_loader.cpp
)

target_include_directories(straylight-extensions PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
)

target_link_libraries(straylight-extensions PUBLIC
    straylight-common
    ${CMAKE_DL_LIBS}
)

# Built-in extensions — compiled as shared libraries
# Each produces a .so that can be loaded by ExtensionLoader.

set(BUILTIN_EXTENSIONS clock sysmon weather quicklaunch notes)

foreach(ext ${BUILTIN_EXTENSIONS})
    add_library(sl-ext-${ext} SHARED builtin_${ext}.cpp)
    target_include_directories(sl-ext-${ext} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/lib/common/include
    )
    target_link_libraries(sl-ext-${ext} PRIVATE imgui)

    set_target_properties(sl-ext-${ext} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "sl-ext-${ext}"
    )

    install(TARGETS sl-ext-${ext}
        LIBRARY DESTINATION lib/straylight/extensions
    )
endforeach()

# quicklaunch and notes need nlohmann_json
target_link_libraries(sl-ext-quicklaunch PRIVATE straylight-common)
target_link_libraries(sl-ext-notes PRIVATE straylight-common)
