// bin/fuse/main.cpp
// straylight-fuse — Tensor compression filesystem daemon
// Uses DaemonBase::run pattern.

#include "fuse_daemon.h"

#include <straylight/config.h>
#include <straylight/log.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace straylight;

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --config <path>   Config file (default: /etc/straylight/fuse.conf)\n"
              << "  --mount <path>    Override mount point\n"
              << "  --help            Show this message\n";
}

int main(int argc, char* argv[]) {
    std::string config_path = "/etc/straylight/fuse.conf";
    std::string mount_override;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--mount" && i + 1 < argc) {
            mount_override = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    Log::init("straylight-fuse");

    // Load or create default config.
    auto cfg_result = Config::load(config_path);
    if (!cfg_result.has_value()) {
        // Create a default config with minimal settings.
        namespace fs = std::filesystem;
        const fs::path tmp = fs::temp_directory_path() / "sl_fuse_defaults.json";
        {
            std::ofstream f(tmp);
            f << "{\n"
              << "  \"fuse\": {\n"
              << "    \"mount_point\": \""
              << (mount_override.empty() ? "/mnt/straylight-tensors" : mount_override)
              << "\",\n"
              << "    \"backing_dir\": \"/var/lib/straylight/tensors\"\n"
              << "  }\n"
              << "}\n";
        }
        cfg_result = Config::load(tmp);
        fs::remove(tmp);
        if (!cfg_result.has_value()) {
            std::cerr << "Cannot create default config: " << cfg_result.error() << "\n";
            return 1;
        }
    }

    straylight::fuse_fs::FuseDaemon daemon;
    return daemon.run(cfg_result.value());
}
