/**
 * straylight-thermal — Predictive thermal management daemon.
 *
 * Polls thermal sensors every second, runs linear regression prediction,
 * and applies pre-emptive throttling before hardware limits are reached.
 *
 * Usage:
 *   straylight-thermal [--config /etc/straylight/thermal.conf] [--foreground]
 */

#include "thermal_model.h"
#include "throttle_controller.h"
#include "thermal_log.h"
#include "straylight/daemon_base.h"
#include "straylight/result.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace straylight::thermal {

class ThermalDaemon : public DaemonBase {
public:
    ThermalDaemon()
        : DaemonBase("straylight-thermal")
    {}

protected:
    VoidResult<> init() override {
        // Load configuration.
        std::string cfg_path = "/etc/straylight/thermal.conf";

        // Check if a custom config was passed (we peek at the daemon's config_path
        // which would be set by a --config argument if DaemonBase supported it;
        // for now we use a known path).
        auto cfg_result = ThermalConfig::load(cfg_path);
        if (cfg_result) {
            config_ = cfg_result.value();
            fprintf(stdout, "[straylight-thermal] loaded config from %s\n", cfg_path.c_str());
        } else {
            fprintf(stderr, "[straylight-thermal] config load failed (%s), using defaults\n",
                    cfg_result.err().c_str());
        }

        set_tick_interval_ms(config_.poll_interval_ms);

        // Initialize thermal log.
        auto log_result = log_.init();
        if (!log_result) {
            fprintf(stderr, "[straylight-thermal] log init warning: %s\n",
                    log_result.err().c_str());
            // Non-fatal: continue without logging.
        }

        // Discover thermal zones.
        auto discover_result = model_.discover_zones();
        if (!discover_result) {
            return VoidResult<>::error("zone discovery failed: " + discover_result.err());
        }

        fprintf(stdout, "[straylight-thermal] discovered %zu thermal zones\n",
                model_.zones().size());
        for (const auto& z : model_.zones()) {
            fprintf(stdout, "  zone: %s (%s) = %dC\n",
                    z.name.c_str(), z.type.c_str(), z.current_temp);
        }

        return VoidResult<>::ok();
    }

    void tick() override {
        // Poll all thermal zones.
        auto poll_result = model_.poll();
        if (!poll_result) {
            fprintf(stderr, "[straylight-thermal] poll error: %s\n",
                    poll_result.err().c_str());
            return;
        }

        // Update predictions.
        if (config_.enable_prediction) {
            for (auto& zone : model_.zones()) {
                zone.predicted_temp_5s = model_.predict_temperature(
                    zone, config_.prediction_horizon_s);
            }
        }

        // Evaluate throttling.
        bool was_throttled = throttle_.is_throttled();
        auto throttle_result = throttle_.evaluate_and_act(model_, config_);
        if (!throttle_result) {
            fprintf(stderr, "[straylight-thermal] throttle error: %s\n",
                    throttle_result.err().c_str());
        }

        // Log throttle state changes.
        if (throttle_.is_throttled() != was_throttled) {
            int max_temp = 0;
            double max_pred = 0.0;
            for (const auto& z : model_.zones()) {
                if (z.current_temp > max_temp) max_temp = z.current_temp;
                if (z.predicted_temp_5s > max_pred) max_pred = z.predicted_temp_5s;
            }
            log_.log_throttle_change(
                throttle_.is_throttled() ? "engaged" : "released",
                max_temp, max_pred);
        }

        // Log poll data.
        log_.log_poll(model_, config_, throttle_);

        // Periodic flush.
        ++tick_count_;
        if (tick_count_ % 10 == 0) {
            log_.flush();
        }
    }

    void shutdown() override {
        // Release any active throttles.
        if (throttle_.is_throttled()) {
            fprintf(stdout, "[straylight-thermal] releasing throttles on shutdown\n");
            // Create a "cool" config to force release.
            ThermalConfig cool_cfg = config_;
            cool_cfg.throttle_temp = 999;
            cool_cfg.critical_temp = 999;
            (void)throttle_.evaluate_and_act(model_, cool_cfg);
        }

        log_.log_throttle_change("daemon_shutdown", 0, 0.0);
        log_.flush();
    }

    void on_reload() override {
        fprintf(stdout, "[straylight-thermal] reloading configuration\n");
        auto cfg_result = ThermalConfig::load("/etc/straylight/thermal.conf");
        if (cfg_result) {
            config_ = cfg_result.value();
            set_tick_interval_ms(config_.poll_interval_ms);
            fprintf(stdout, "[straylight-thermal] config reloaded\n");
        } else {
            fprintf(stderr, "[straylight-thermal] config reload failed: %s\n",
                    cfg_result.err().c_str());
        }

        // Re-discover zones in case hardware changed.
        (void)model_.discover_zones();
    }

private:
    ThermalConfig config_;
    ThermalModel model_;
    ThrottleController throttle_;
    ThermalLog log_;
    uint64_t tick_count_ = 0;
};

} // namespace straylight::thermal

int main(int argc, char** argv) {
    straylight::thermal::ThermalDaemon daemon;
    return daemon.run();
}
