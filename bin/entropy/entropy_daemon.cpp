#include "entropy_daemon.h"

#include <straylight/log.h>
#include <thread>

namespace straylight {

Result<void, SLError> EntropyDaemon::init(const Config& cfg) {
    health_interval_s_ = cfg.get<int>("entropy.health_interval_s", 60);
    reseed_interval_s_ = cfg.get<int>("entropy.reseed_interval_s", 3600);

    // Create hardware entropy source if not injected (normal path).
    if (!source_) {
        source_ = std::make_unique<hw::EntropySource>();
    }

    SL_INFO("entropy: hardware RNG available: {}",
            source_->has_hardware_rng() ? "yes" : "no");

    // Seed the CTR-DRBG from hardware entropy.
    std::array<uint8_t, 32> seed_buf{};
    auto fill_res = source_->fill(seed_buf.data(), seed_buf.size());
    if (!fill_res.has_value()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::HardwareFault,
                    "entropy: failed to obtain seed: " + fill_res.error()});
    }

    auto seed_res = drbg_.seed(seed_buf);
    if (!seed_res.has_value()) {
        return Result<void, SLError>::error(seed_res.error());
    }

    // Run initial health check.
    auto hc = run_health_check();
    if (!hc.has_value()) {
        SL_WARN("entropy: initial health check failed: {}", hc.error().message());
    }

    auto now = std::chrono::steady_clock::now();
    last_health_ = now;
    last_reseed_ = now;

    SL_INFO("entropy: daemon initialized (health={}s, reseed={}s)",
            health_interval_s_, reseed_interval_s_);
    return Result<void, SLError>::ok();
}

Result<void, SLError> EntropyDaemon::tick() {
    auto now = std::chrono::steady_clock::now();

    // Periodic health check.
    auto since_health = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_health_);
    if (since_health.count() >= health_interval_s_) {
        auto hc = run_health_check();
        if (!hc.has_value()) {
            SL_ERROR("entropy: health check failed: {}", hc.error().message());
        }
        last_health_ = now;
    }

    // Periodic reseed.
    auto since_reseed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_reseed_);
    if (since_reseed.count() >= reseed_interval_s_) {
        std::array<uint8_t, 32> fresh{};
        auto fill_res = source_->fill(fresh.data(), fresh.size());
        if (fill_res.has_value()) {
            auto rs = drbg_.reseed(fresh);
            if (rs.has_value()) {
                SL_DEBUG("entropy: DRBG reseeded");
            } else {
                SL_WARN("entropy: reseed failed: {}", rs.error().message());
            }
        } else {
            SL_WARN("entropy: could not read hardware entropy for reseed: {}",
                    fill_res.error());
        }
        last_reseed_ = now;
    }

    // Sleep to avoid busy-looping.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return Result<void, SLError>::ok();
}

void EntropyDaemon::shutdown() {
    SL_INFO("entropy: shutting down");
    source_.reset();
}

Result<void, SLError> EntropyDaemon::run_health_check() {
    auto hc = source_->health_check();
    if (!hc.has_value()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::HardwareFault,
                    "entropy health check failed: " + hc.error()});
    }
    SL_DEBUG("entropy: health check passed");
    return Result<void, SLError>::ok();
}

} // namespace straylight
