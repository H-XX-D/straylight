add_executable(straylight-secrets
    main.cpp
    secrets_engine.cpp
)
target_include_directories(straylight-secrets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(straylight-secrets PRIVATE straylight-common)
install(TARGETS straylight-secrets DESTINATION bin)
install(DIRECTORY etc/ DESTINATION etc)
