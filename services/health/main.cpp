// services/health/main.cpp
// straylight-health daemon — Continuous system health monitoring.
#include "health_scorer.h"
#include "checks.h"

#include <straylight/config.h>
#include <straylight/daemon.h>
#include <straylight/log.h>

#include <chrono>
#include <thread>

namespace straylight {

/// Health monitoring daemon.
class HealthDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override {
        SL_INFO("health: initializing daemon");

        tick_interval_s_ = cfg.get<int>("tick_interval_seconds", 60);
        socket_path_ = cfg.get<std::string>(
            "ipc.socket_path", "/run/straylight/health.sock");

        // Load custom thresholds
        if (cfg.has("thresholds")) {
            auto raw = cfg.raw();
            if (raw.contains("thresholds") && raw["thresholds"].is_object()) {
                for (auto& [name, val] : raw["thresholds"].items()) {
                    if (val.is_object()) {
                        int warn = val.value("warn_below", 70);
                        int crit = val.value("critical_below", 30);
                        scorer_.set_threshold(name, warn, crit);
                    }
                }
            }
        }

        // Run initial check
        auto checks = HealthChecks::run_all();
        auto snap = scorer_.score(checks);
        SL_INFO("health: initial score = {} ({})",
                snap.overall_score,
                HealthScorer::status_string(snap.overall_status));

        SL_INFO("health: daemon initialized (tick={}s)", tick_interval_s_);
        return Result<void, SLError>::ok();
    }

    Result<void, SLError> tick() override {
        auto checks = HealthChecks::run_all();
        auto snap = scorer_.score(checks);

        // Log status changes
        auto prev = scorer_.latest();
        if (snap.overall_status != prev.overall_status) {
            if (snap.overall_status == HealthStatus::Critical) {
                SL_ERROR("health: score dropped to {} — CRITICAL", snap.overall_score);
            } else if (snap.overall_status == HealthStatus::Warn) {
                SL_WARN("health: score at {} — WARNING", snap.overall_score);
            } else {
                SL_INFO("health: recovered to {} — OK", snap.overall_score);
            }
        }

        // Log individual critical checks
        for (const auto& cr : checks) {
            if (cr.status == HealthStatus::Critical) {
                SL_WARN("health: {} is CRITICAL: {}", cr.name, cr.detail);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(tick_interval_s_));
        return Result<void, SLError>::ok();
    }

    void shutdown() override {
        SL_INFO("health: shutting down");
        SL_INFO("health: shutdown complete");
    }

private:
    HealthScorer scorer_;
    std::string socket_path_;
    int tick_interval_s_ = 60;
};

} // namespace straylight

int main(int /*argc*/, char* /*argv*/[]) {
    straylight::Log::init("straylight-health");

    auto cfg_result = straylight::Config::load("/etc/straylight/health.conf");
    if (!cfg_result.has_value()) {
        SL_ERROR("health: failed to load config: {}", cfg_result.error());
        return 1;
    }

    straylight::HealthDaemon daemon;
    return daemon.run(cfg_result.value());
}
