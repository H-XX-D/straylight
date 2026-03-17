// services/cron/main.cpp
// straylight-cron daemon — Smart task scheduler with dependency awareness.
#include "scheduler.h"

#include <straylight/config.h>
#include <straylight/daemon.h>
#include <straylight/log.h>

#include <chrono>
#include <thread>

namespace straylight {

/// Cron daemon — wraps CronScheduler in the DaemonBase lifecycle.
class CronDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override {
        SL_INFO("cron: initializing daemon");

        config_path_ = cfg.get<std::string>(
            "tasks_file", "/etc/straylight/cron-tasks.json");
        tick_interval_s_ = cfg.get<int>("tick_interval_seconds", 10);
        auto_save_ = cfg.get<bool>("auto_save", true);
        socket_path_ = cfg.get<std::string>(
            "ipc.socket_path", "/run/straylight/cron.sock");

        auto lr = scheduler_.load_tasks(config_path_);
        if (lr.has_value()) {
            SL_INFO("cron: loaded {} task(s) from {}", lr.value(), config_path_);
        } else {
            SL_WARN("cron: no tasks loaded: {}", lr.error());
        }

        // Catch up any missed runs from downtime
        int caught = scheduler_.catch_up_missed();
        if (caught > 0) {
            SL_INFO("cron: caught up {} missed task(s)", caught);
        }

        SL_INFO("cron: daemon initialized (tick={}s)", tick_interval_s_);
        return Result<void, SLError>::ok();
    }

    Result<void, SLError> tick() override {
        auto r = scheduler_.tick();
        if (r.has_value() && r.value() > 0) {
            SL_DEBUG("cron: tick executed {} task(s)", r.value());

            if (auto_save_) {
                auto sr = scheduler_.save_tasks(config_path_);
                if (!sr.has_value()) {
                    SL_WARN("cron: auto-save failed: {}", sr.error());
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(tick_interval_s_));
        return Result<void, SLError>::ok();
    }

    void shutdown() override {
        SL_INFO("cron: shutting down");
        auto sr = scheduler_.save_tasks(config_path_);
        if (!sr.has_value()) {
            SL_WARN("cron: save on shutdown failed: {}", sr.error());
        }
        SL_INFO("cron: shutdown complete");
    }

private:
    CronScheduler scheduler_;
    std::string config_path_;
    std::string socket_path_;
    int tick_interval_s_ = 10;
    bool auto_save_ = true;
};

} // namespace straylight

int main(int /*argc*/, char* /*argv*/[]) {
    straylight::Log::init("straylight-cron");

    auto cfg_result = straylight::Config::load("/etc/straylight/cron.conf");
    if (!cfg_result.has_value()) {
        SL_ERROR("cron: failed to load config: {}", cfg_result.error());
        return 1;
    }

    straylight::CronDaemon daemon;
    return daemon.run(cfg_result.value());
}
