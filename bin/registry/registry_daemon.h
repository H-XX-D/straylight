#pragma once

#include <straylight/common.h>
#include <straylight/daemon.h>
#include "store.h"
#include <filesystem>
#include <string>

namespace straylight {

class RegistryDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    // Expose store for testing
    Store& store() { return store_; }

private:
    Store store_;
    std::filesystem::path persist_path_;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point last_persist_{};

    void persist();
};

} // namespace straylight
