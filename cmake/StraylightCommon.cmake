# Shared compiler flags for all StrayLight targets

# Strict warnings
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Werror=return-type
    -Werror=uninitialized
)

# Symbol visibility: hidden by default
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Position-independent code for shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Sanitizers in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(ENABLE_SANITIZERS "Enable ASan + UBSan" ON)
    if(ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# LTO in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

# Helper function to create a StrayLight shared library
function(straylight_add_library target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_HEADERS;DEPS" ${ARGN})

    add_library(${target_name} SHARED ${ARG_SOURCES})

    target_include_directories(${target_name}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    )

    set_target_properties(${target_name} PROPERTIES
        VERSION ${STRAYLIGHT_VERSION}
        SOVERSION ${STRAYLIGHT_SO_VERSION}
        EXPORT_NAME ${target_name}
    )

    if(ARG_DEPS)
        target_link_libraries(${target_name} PUBLIC ${ARG_DEPS})
    endif()
endfunction()
