#include "registry_daemon.h"
#include <fstream>
#include <chrono>
#include <thread>

namespace straylight {

Result<void, SLError> RegistryDaemon::init(const Config& cfg) {
    // Use config if available, otherwise default path
    persist_path_ = cfg.get<std::string>("registry.persist_path",
                                         "/var/lib/straylight/registry.json");

    // Load existing state if file exists
    if (std::filesystem::exists(persist_path_)) {
        std::ifstream f(persist_path_);
        std::string content{std::istreambuf_iterator<char>(f), {}};
        if (auto r = store_.deserialize(content); !r.has_value())
            SL_WARN("registry: failed to load {}: {}",
                    persist_path_.string(), r.error().message());
        else
            SL_INFO("registry: loaded state from {}", persist_path_.string());
    }

    last_persist_ = std::chrono::steady_clock::now();
    SL_INFO("registry: initialized (persist_path={})", persist_path_.string());
    return Result<void, SLError>::ok();
}

Result<void, SLError> RegistryDaemon::tick() {
    // Persist every 30 seconds
    auto now = std::chrono::steady_clock::now();
    if (now - last_persist_ > std::chrono::seconds(30)) {
        persist();
        last_persist_ = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return Result<void, SLError>::ok();
}

void RegistryDaemon::shutdown() {
    persist();
    SL_INFO("registry: shutting down");
}

void RegistryDaemon::persist() {
    std::ofstream f(persist_path_);
    if (f) {
        f << store_.serialize();
        SL_DEBUG("registry: persisted to {}", persist_path_.string());
    }
}

} // namespace straylight
