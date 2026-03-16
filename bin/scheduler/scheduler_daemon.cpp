// bin/scheduler/scheduler_daemon.cpp
#include "scheduler_daemon.h"

#include <straylight/log.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace straylight {

// ---------------------------------------------------------------------------
// PriorityQueue
// ---------------------------------------------------------------------------

namespace {
constexpr unsigned kWeightHigh   = 800;
constexpr unsigned kWeightNormal = 100;
constexpr unsigned kWeightLow    = 10;

unsigned weight_for(Priority p) {
    switch (p) {
        case Priority::High:   return kWeightHigh;
        case Priority::Normal: return kWeightNormal;
        case Priority::Low:    return kWeightLow;
    }
    return kWeightNormal;
}
} // anonymous namespace

void PriorityQueue::enqueue(const std::string& name, Priority prio) {
    entries_[name] = prio;
}

unsigned PriorityQueue::cpu_weight(const std::string& name) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return kWeightNormal;
    return weight_for(it->second);
}

// ---------------------------------------------------------------------------
// SchedulerDaemon
// ---------------------------------------------------------------------------

static constexpr const char* kCgroupBase = "/sys/fs/cgroup/straylight";
static constexpr const char* kKernelModulePath = "/proc/straylight/sched";

Result<void, SLError> SchedulerDaemon::init(const Config& /*cfg*/) {
    SL_INFO("scheduler: initializing");

    // Parse CPU topology
    {
        std::ifstream f("/proc/cpuinfo");
        if (f.is_open()) {
            std::ostringstream buf;
            buf << f.rdbuf();
            auto res = topology_.parse_cpuinfo(buf.str());
            if (res.has_value()) {
                SL_INFO("scheduler: detected {} logical CPUs, {} physical cores",
                        topology_.logical_cpu_count(),
                        topology_.physical_core_count());
            } else {
                SL_WARN("scheduler: failed to parse cpuinfo: {}", res.error().message());
            }
        } else {
            SL_WARN("scheduler: /proc/cpuinfo not available (not running on Linux?)");
        }
    }

    // Create cgroup hierarchy base directory
    {
        std::error_code ec;
        std::filesystem::create_directories(kCgroupBase, ec);
        if (ec) {
            SL_WARN("scheduler: cannot create cgroup dir {}: {}", kCgroupBase, ec.message());
        }
    }

    // Probe for kernel module
    kernel_module_available_ = std::filesystem::exists(kKernelModulePath);
    if (kernel_module_available_) {
        SL_INFO("scheduler: kernel module detected at {}", kKernelModulePath);
    } else {
        SL_INFO("scheduler: kernel module not present, running in userspace-only mode");
    }

    // Register default subsystems
    register_subsystem("straylight-bus",      Priority::High);
    register_subsystem("straylight-registry", Priority::High);
    register_subsystem("straylight-entropy",  Priority::Normal);
    register_subsystem("straylight-fuse",     Priority::Normal);
    register_subsystem("straylight-agent",    Priority::Low);

    // Apply initial priorities
    apply_priorities();

    SL_INFO("scheduler: initialization complete");
    return Result<void, SLError>::ok();
}

Result<void, SLError> SchedulerDaemon::tick() {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Re-read cgroup stats and reapply if needed
    for (const auto& [name, prio] : queue_.entries()) {
        auto subsys_path = std::filesystem::path(kCgroupBase) / name;
        if (!std::filesystem::exists(subsys_path)) continue;

        CgroupV2 cg(subsys_path);
        auto weight_result = cg.read_cpu_weight();
        if (weight_result.has_value()) {
            unsigned expected = queue_.cpu_weight(name);
            if (weight_result.value() != expected) {
                SL_WARN("scheduler: {} cpu.weight drifted ({} -> {}), correcting",
                        name, weight_result.value(), expected);
                cg.set_cpu_weight(expected);
            }
        }
    }

    return Result<void, SLError>::ok();
}

void SchedulerDaemon::shutdown() {
    SL_INFO("scheduler: shutting down");
}

void SchedulerDaemon::register_subsystem(const std::string& name, Priority prio) {
    queue_.enqueue(name, prio);
    SL_DEBUG("scheduler: registered subsystem '{}' at priority {}",
             name, static_cast<int>(prio));
}

void SchedulerDaemon::apply_priorities() {
    for (const auto& [name, prio] : queue_.entries()) {
        auto subsys_path = std::filesystem::path(kCgroupBase) / name;

        // Create per-subsystem cgroup directory
        std::error_code ec;
        std::filesystem::create_directories(subsys_path, ec);
        if (ec) {
            SL_WARN("scheduler: cannot create cgroup dir for {}: {}", name, ec.message());
            continue;
        }

        // Set cpu.weight
        CgroupV2 cg(subsys_path);
        unsigned weight = queue_.cpu_weight(name);
        auto res = cg.set_cpu_weight(weight);
        if (!res.has_value()) {
            SL_WARN("scheduler: failed to set cpu.weight for {}: {}", name, res.error().message());
        } else {
            SL_DEBUG("scheduler: set {} cpu.weight = {}", name, weight);
        }
    }
}

} // namespace straylight
