add_executable(straylight-network
    main.cpp
    network_manager.cpp
)
target_include_directories(straylight-network PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(straylight-network PRIVATE straylight-common)
install(TARGETS straylight-network DESTINATION bin)
