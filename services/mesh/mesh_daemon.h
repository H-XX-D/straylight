// services/mesh/mesh_daemon.h
// StrayLight Mesh — Daemon combining GpuPool + MeshMonitor + D-Bus interface.
#pragma once

#include <straylight/common.h>
#include <straylight/daemon.h>
#include "gpu_pool.h"
#include "mesh_monitor.h"

#include <chrono>
#include <mutex>

namespace straylight {

class MeshDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    // D-Bus method handlers
    std::string dbus_pool_status() const;
    std::string dbus_list_gpus() const;
    std::string dbus_submit(const std::string& command, size_t vram_needed);
    std::string dbus_allocate(size_t bytes, const std::string& policy);
    std::string dbus_free(uint64_t handle, const std::string& host, uint32_t gpu_index);
    std::string dbus_transfer(const std::string& src_host, uint32_t src_gpu,
                               uint64_t src_handle,
                               const std::string& dst_host, uint32_t dst_gpu,
                               uint64_t dst_handle, size_t bytes);

private:
    GpuPool pool_;
    std::unique_ptr<MeshMonitor> monitor_;

    int refresh_interval_s_ = 5;
    int discover_interval_s_ = 60;

    std::chrono::steady_clock::time_point last_refresh_;
    std::chrono::steady_clock::time_point last_discover_;

    mutable std::mutex mutex_;
};

} // namespace straylight
