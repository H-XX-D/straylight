// services/mesh/mesh_daemon.cpp
// StrayLight Mesh — Daemon implementation.
#include "mesh_daemon.h"

#include <sstream>

namespace straylight {

// ---------------------------------------------------------------------------
// DaemonBase overrides
// ---------------------------------------------------------------------------

Result<void, SLError> MeshDaemon::init(const Config& cfg) {
    SL_INFO("mesh: initializing daemon");

    refresh_interval_s_  = cfg.get<int>("refresh_interval_seconds", 5);
    discover_interval_s_ = cfg.get<int>("discover_interval_seconds", 60);

    float temp_threshold = cfg.get<float>("temperature_threshold_celsius", 90.0f);
    float util_threshold = cfg.get<float>("utilization_threshold_percent", 95.0f) / 100.0f;
    int stale_timeout    = cfg.get<int>("stale_timeout_seconds", 30);

    // Initial discovery
    auto disc_result = pool_.discover();
    if (!disc_result.has_value()) {
        SL_WARN("mesh: initial discovery partially failed: {}", disc_result.error());
    }

    // Start monitor
    monitor_ = std::make_unique<MeshMonitor>(pool_);
    monitor_->set_temperature_threshold(temp_threshold);
    monitor_->set_utilization_threshold(util_threshold);
    monitor_->set_stale_timeout(std::chrono::seconds(stale_timeout));

    // Set alert callback to emit D-Bus signals
    monitor_->set_alert_callback([](const MeshAlert& alert) {
        // In production, this would emit a D-Bus signal on org.straylight.Mesh1
        // For now, just log it
        SL_INFO("mesh-alert: {}", alert.message);
    });

    auto mon_result = monitor_->start(std::chrono::seconds(refresh_interval_s_));
    if (!mon_result.has_value()) {
        SL_ERROR("mesh: failed to start monitor: {}", mon_result.error());
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal, mon_result.error()});
    }

    auto now = std::chrono::steady_clock::now();
    last_refresh_  = now;
    last_discover_ = now;

    SL_INFO("mesh: daemon initialized with {} GPU(s)", pool_.gpu_count());
    return Result<void, SLError>::ok();
}

Result<void, SLError> MeshDaemon::tick() {
    auto now = std::chrono::steady_clock::now();

    // Periodic re-discovery of new nodes
    auto since_discover = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_discover_);
    if (since_discover.count() >= discover_interval_s_) {
        last_discover_ = now;
        SL_DEBUG("mesh: running periodic discovery");
        pool_.discover();
    }

    // Sleep briefly to avoid busy-spinning
    usleep(100000); // 100ms

    return Result<void, SLError>::ok();
}

void MeshDaemon::shutdown() {
    SL_INFO("mesh: shutting down");

    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }

    SL_INFO("mesh: shutdown complete");
}

// ---------------------------------------------------------------------------
// D-Bus method handlers
// ---------------------------------------------------------------------------

std::string MeshDaemon::dbus_pool_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (monitor_) {
        return monitor_->status_summary();
    }
    return "Monitor not running";
}

std::string MeshDaemon::dbus_list_gpus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto gpus = pool_.all_gpus();

    std::ostringstream out;
    out << "HOST             GPU  NAME                 VENDOR     VRAM(GiB)  FREE(GiB)  TEMP  UTIL  LATENCY\n";
    out << "---------------  ---  -------------------  ---------  ---------  ---------  ----  ----  -------\n";

    for (const auto& gpu : gpus) {
        char line[256];
        std::snprintf(line, sizeof(line),
            "%-15s  %3u  %-19s  %-9s  %9.1f  %9.1f  %3.0fC  %3.0f%%  %5.1fms%s\n",
            gpu.host.c_str(),
            gpu.gpu_index,
            gpu.name.c_str(),
            gpu.vendor.c_str(),
            static_cast<double>(gpu.vram_total) / (1024.0 * 1024.0 * 1024.0),
            static_cast<double>(gpu.vram_available) / (1024.0 * 1024.0 * 1024.0),
            static_cast<double>(gpu.temperature),
            static_cast<double>(gpu.utilization * 100.0f),
            static_cast<double>(gpu.latency_ms),
            gpu.is_available ? "" : " [UNAVAIL]");
        out << line;
    }

    return out.str();
}

std::string MeshDaemon::dbus_submit(const std::string& command, size_t vram_needed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = pool_.submit(command, vram_needed);
    if (result.has_value()) {
        return result.value();
    }
    return "ERROR: " + result.error();
}

std::string MeshDaemon::dbus_allocate(size_t bytes, const std::string& policy_str) {
    std::lock_guard<std::mutex> lock(mutex_);

    PlacementPolicy policy = PlacementPolicy::LeastLoaded;
    if (policy_str == "best_fit")      policy = PlacementPolicy::BestFit;
    else if (policy_str == "local")    policy = PlacementPolicy::LocalFirst;
    else if (policy_str == "round")    policy = PlacementPolicy::RoundRobin;
    else if (policy_str == "pinned")   policy = PlacementPolicy::Pinned;

    auto result = pool_.allocate(bytes, policy);
    if (result.has_value()) {
        const auto& alloc = result.value();
        return std::to_string(alloc.handle) + " " + alloc.host + " " +
               std::to_string(alloc.gpu_index);
    }
    return "ERROR: " + result.error();
}

std::string MeshDaemon::dbus_free(uint64_t handle, const std::string& host,
                                    uint32_t gpu_index) {
    std::lock_guard<std::mutex> lock(mutex_);

    MeshAllocation alloc;
    alloc.handle    = handle;
    alloc.host      = host;
    alloc.gpu_index = gpu_index;
    alloc.is_local  = (host == "localhost" || host == "127.0.0.1");

    auto result = pool_.free(alloc);
    if (result.has_value()) {
        return "OK";
    }
    return "ERROR: " + result.error();
}

std::string MeshDaemon::dbus_transfer(const std::string& src_host, uint32_t src_gpu,
                                        uint64_t src_handle,
                                        const std::string& dst_host, uint32_t dst_gpu,
                                        uint64_t dst_handle, size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);

    MeshAllocation src;
    src.handle    = src_handle;
    src.host      = src_host;
    src.gpu_index = src_gpu;
    src.size_bytes = bytes;
    src.is_local  = (src_host == "localhost" || src_host == "127.0.0.1");

    MeshAllocation dst;
    dst.handle    = dst_handle;
    dst.host      = dst_host;
    dst.gpu_index = dst_gpu;
    dst.size_bytes = bytes;
    dst.is_local  = (dst_host == "localhost" || dst_host == "127.0.0.1");

    auto result = pool_.transfer(src, dst);
    if (result.has_value()) {
        return "OK";
    }
    return "ERROR: " + result.error();
}

} // namespace straylight
