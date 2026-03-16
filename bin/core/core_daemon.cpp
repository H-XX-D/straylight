// bin/core/core_daemon.cpp
#include "core_daemon.h"
#include <thread>
#include <chrono>

namespace straylight {

Result<void, SLError> CoreDaemon::init(const Config& cfg) {
    poll_interval_s_ = cfg.get<int>("core.poll_interval_s", 10);
    restart_max_ = cfg.get<int>("core.restart_max", 5);

    // Register known subsystems in boot order
    register_subsystem("straylight-entropy",   SubsystemPriority::Critical);
    register_subsystem("straylight-bus",       SubsystemPriority::Critical);
    register_subsystem("straylight-registry",  SubsystemPriority::Critical);
    register_subsystem("straylight-scheduler", SubsystemPriority::Normal);

    SL_INFO("core: initialized with {} subsystems ({} critical)",
            pipeline_.subsystem_count(), pipeline_.critical_count());

    return Result<void, SLError>::ok();
}

Result<void, SLError> CoreDaemon::tick() {
    // In production this would poll each subsystem's D-Bus Health() method.
    // On macOS (no sdbus-c++), we log and skip the actual poll.
    for (auto& entry : pipeline_.subsystems()) {
        SL_DEBUG("core: would poll Health() on {}", entry.name);

        // Check if doctor flagged this subsystem for restart
        if (doctor_.needs_restart(entry.name)) {
            auto it = restart_counts_.find(entry.name);
            int count = (it != restart_counts_.end()) ? it->second : 0;

            if (count < restart_max_) {
                SL_WARN("core: restarting {} (attempt {}/{})",
                        entry.name, count + 1, restart_max_);
                // In production: systemctl restart <name>
                restart_counts_[entry.name] = count + 1;
            } else {
                SL_ERROR("core: {} exceeded max restarts ({}), marking failed",
                         entry.name, restart_max_);
            }
        }
    }

    check_readiness();

    std::this_thread::sleep_for(std::chrono::seconds(poll_interval_s_));
    return Result<void, SLError>::ok();
}

void CoreDaemon::shutdown() {
    SL_INFO("core: shutting down orchestrator");
}

void CoreDaemon::register_subsystem(const std::string& name, SubsystemPriority prio) {
    std::lock_guard lock(mutex_);
    pipeline_.register_subsystem(name, prio);
}

void CoreDaemon::on_health_update(const std::string& name, HealthStatus status) {
    std::lock_guard lock(mutex_);
    doctor_.record_health(name, status);

    // Update the pipeline entry's cached health
    for (auto& entry : pipeline_.subsystems()) {
        if (entry.name == name) {
            entry.last_health = status;
            break;
        }
    }

    check_readiness();
}

bool CoreDaemon::is_ready() const {
    std::lock_guard lock(mutex_);
    return ready_;
}

void CoreDaemon::check_readiness() {
    // Ready when all Critical subsystems have reported Healthy at least once
    for (const auto& entry : pipeline_.subsystems()) {
        if (entry.priority == SubsystemPriority::Critical &&
            entry.last_health != HealthStatus::Healthy) {
            ready_ = false;
            return;
        }
    }
    if (!ready_) {
        SL_INFO("core: all critical subsystems healthy — system ready");
    }
    ready_ = true;
}

} // namespace straylight
