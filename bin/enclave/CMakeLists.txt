add_executable(straylight-enclave
    main.cpp
    enclave_def/enclave.cpp
    attestation.cpp
    sealed_storage.cpp
    secure_inference.cpp)
target_link_libraries(straylight-enclave PRIVATE straylight-common)
target_include_directories(straylight-enclave PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-enclave DESTINATION bin)
