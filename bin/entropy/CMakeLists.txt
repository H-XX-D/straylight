add_executable(straylight-entropy main.cpp entropy_daemon.cpp drbg.cpp)
target_link_libraries(straylight-entropy PRIVATE straylight-common straylight-hw)
target_include_directories(straylight-entropy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-entropy DESTINATION bin)
