add_executable(straylight-wifi
    main.cpp
    wifi_manager.cpp
)
target_include_directories(straylight-wifi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(straylight-wifi PRIVATE straylight-common)
install(TARGETS straylight-wifi DESTINATION bin)
