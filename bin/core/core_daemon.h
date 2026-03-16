// bin/core/core_daemon.h
#pragma once

#include <straylight/common.h>
#include <straylight/daemon.h>
#include "pipeline.h"
#include "doctor.h"
#include <unordered_map>

namespace straylight {

class CoreDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    void register_subsystem(const std::string& name, SubsystemPriority prio);
    void on_health_update(const std::string& name, HealthStatus status);
    bool is_ready() const;

private:
    Pipeline pipeline_;
    Doctor doctor_;
    int poll_interval_s_ = 10;
    int restart_max_ = 5;
    std::unordered_map<std::string, int> restart_counts_;
    bool ready_ = false;

    void check_readiness();
};

} // namespace straylight
