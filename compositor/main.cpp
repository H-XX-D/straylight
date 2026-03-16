// compositor/main.cpp
#include "server.h"
#include <straylight/log.h>
#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    straylight::Log::init("straylight-compositor");

    auto result = straylight::compositor::Server::create();
    if (!result) {
        std::cerr << "Failed to create compositor: " << result.error().message << '\n';
        return EXIT_FAILURE;
    }

    if (auto r = (*result)->run(); !r) {
        std::cerr << "Compositor error: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
