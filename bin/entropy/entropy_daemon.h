#pragma once

#include <straylight/daemon.h>
#include <straylight/hw/entropy.h>
#include "drbg.h"

#include <chrono>
#include <memory>

namespace straylight {

class EntropyDaemon : public DaemonBase {
public:
    /// Default constructor — creates a real hw::EntropySource on init().
    EntropyDaemon() = default;

    /// Test constructor with injected entropy source.
    explicit EntropyDaemon(std::unique_ptr<hw::EntropySource> source)
        : source_(std::move(source)) {}

    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    /// Run NIST SP 800-90B health check on hardware source.
    Result<void, SLError> run_health_check();

private:
    std::unique_ptr<hw::EntropySource> source_;
    CtrDrbg drbg_;
    int health_interval_s_ = 60;
    int reseed_interval_s_ = 3600;
    std::chrono::steady_clock::time_point last_health_{};
    std::chrono::steady_clock::time_point last_reseed_{};
};

} // namespace straylight
