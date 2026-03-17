// services/mesh/gpu_pool.cpp
// StrayLight Mesh — GPU pool implementation.
#include "gpu_pool.h"

#include <straylight/log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace straylight {

// VPU ioctl command numbers (matching straylight-vpu driver)
static constexpr unsigned long VPU_IOC_MAGIC   = 'V';
static constexpr unsigned long VPU_IOC_ALLOC   = (VPU_IOC_MAGIC << 8) | 0x01;
static constexpr unsigned long VPU_IOC_FREE    = (VPU_IOC_MAGIC << 8) | 0x02;
static constexpr unsigned long VPU_IOC_INFO    = (VPU_IOC_MAGIC << 8) | 0x10;
static constexpr unsigned long VPU_IOC_P2P_DMA = (VPU_IOC_MAGIC << 8) | 0x20;

struct VpuAllocRequest {
    uint64_t size;
    uint64_t handle;  // output
};

struct VpuFreeRequest {
    uint64_t handle;
};

struct VpuInfo {
    char     name[64];
    char     vendor[64];
    uint64_t vram_total;
    uint64_t vram_free;
    uint32_t temperature;   // millidegrees C
    uint32_t utilization;   // percent * 100
};

struct VpuP2pDma {
    uint64_t src_handle;
    uint64_t dst_handle;
    uint64_t size;
    uint32_t dst_gpu;
};

// ---------------------------------------------------------------------------
// Helper: run a command and capture stdout
// ---------------------------------------------------------------------------

static std::pair<std::string, bool> run_command(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {"", false};

    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    int status = pclose(pipe);
    return {output, status == 0};
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GpuPool::GpuPool()  = default;
GpuPool::~GpuPool() = default;

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

Result<void, std::string> GpuPool::discover() {
    std::lock_guard<std::mutex> lock(mutex_);
    gpus_.clear();

    auto local_result = discover_local();
    if (!local_result.has_value()) {
        SL_WARN("mesh: local GPU discovery failed: {}", local_result.error());
    }

    auto remote_result = discover_remote();
    if (!remote_result.has_value()) {
        SL_WARN("mesh: remote GPU discovery failed: {}", remote_result.error());
    }

    SL_INFO("mesh: discovered {} GPU(s) total", gpus_.size());
    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::discover_local() {
    // Enumerate /dev/straylight-vpu* devices
    DIR* dev_dir = opendir("/dev");
    if (!dev_dir) {
        return Result<void, std::string>::error("Cannot open /dev");
    }

    uint32_t gpu_idx = 0;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dev_dir)) != nullptr) {
        if (strncmp(entry->d_name, "straylight-vpu", 14) != 0) continue;

        std::string dev_path = std::string("/dev/") + entry->d_name;
        int fd = open(dev_path.c_str(), O_RDWR);
        if (fd < 0) {
            SL_WARN("mesh: cannot open {}", dev_path);
            continue;
        }

        VpuInfo info{};
        int ret = ioctl(fd, VPU_IOC_INFO, &info);
        if (ret == 0) {
            RemoteGpu gpu;
            gpu.host           = "localhost";
            gpu.gpu_index      = gpu_idx;
            gpu.name           = info.name;
            gpu.vendor         = info.vendor;
            gpu.vram_total     = info.vram_total;
            gpu.vram_available = info.vram_free;
            gpu.temperature    = static_cast<float>(info.temperature) / 1000.0f;
            gpu.utilization    = static_cast<float>(info.utilization) / 10000.0f;
            gpu.latency_ms     = 0.0f;
            gpu.is_local       = true;
            gpu.is_available   = true;
            gpu.last_seen      = std::chrono::steady_clock::now();
            gpus_.push_back(std::move(gpu));
            gpu_idx++;
        } else {
            SL_WARN("mesh: VPU_IOC_INFO failed on {}", dev_path);
        }

        close(fd);
    }
    closedir(dev_dir);

    // Fallback: check /sys/class/drm for any GPUs if VPU devices not found
    if (gpu_idx == 0) {
        DIR* drm_dir = opendir("/sys/class/drm");
        if (drm_dir) {
            while ((entry = readdir(drm_dir)) != nullptr) {
                if (strncmp(entry->d_name, "card", 4) != 0) continue;
                if (strchr(entry->d_name, '-') != nullptr) continue;

                std::string sysfs_path = std::string("/sys/class/drm/") +
                                          entry->d_name + "/device";

                std::string vendor_str = "unknown";
                {
                    std::ifstream vf(sysfs_path + "/vendor");
                    if (vf.is_open()) std::getline(vf, vendor_str);
                }

                size_t vram = 0;
                {
                    std::ifstream vf(sysfs_path + "/mem_info_vram_total");
                    if (vf.is_open()) vf >> vram;
                }

                float util = 0.0f;
                {
                    std::ifstream uf(sysfs_path + "/gpu_busy_percent");
                    int pct = 0;
                    if (uf.is_open() && (uf >> pct)) {
                        util = static_cast<float>(pct) / 100.0f;
                    }
                }

                RemoteGpu gpu;
                gpu.host           = "localhost";
                gpu.gpu_index      = gpu_idx;
                gpu.name           = entry->d_name;
                gpu.vendor         = vendor_str;
                gpu.vram_total     = vram;
                gpu.vram_available = vram;
                gpu.temperature    = 0.0f;
                gpu.utilization    = util;
                gpu.latency_ms     = 0.0f;
                gpu.is_local       = true;
                gpu.is_available   = true;
                gpu.last_seen      = std::chrono::steady_clock::now();
                gpus_.push_back(std::move(gpu));
                gpu_idx++;
            }
            closedir(drm_dir);
        }
    }

    if (gpu_idx == 0) {
        SL_INFO("mesh: no local GPUs found");
    } else {
        SL_INFO("mesh: found {} local GPU(s)", gpu_idx);
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::discover_remote() {
    // Query swarm daemon for node list via D-Bus
    auto [output, ok] = run_command(
        "busctl call org.straylight.Swarm1 "
        "/org/straylight/Swarm1 org.straylight.Swarm1 "
        "ListNodes 2>/dev/null");

    if (!ok || output.empty()) {
        SL_DEBUG("mesh: swarm daemon not available, skipping remote discovery");
        return Result<void, std::string>::ok();
    }

    // Parse node list: each line is "hostname ip_address port gpu_count vram_total vram_free"
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream lss(line);
        std::string hostname, ip;
        int port = 0, gpu_count = 0;
        uint64_t vram_total = 0, vram_free = 0;

        if (!(lss >> hostname >> ip >> port >> gpu_count >> vram_total >> vram_free)) {
            continue;
        }

        if (hostname == "localhost" || ip == "127.0.0.1") continue;

        for (int gi = 0; gi < gpu_count; gi++) {
            auto [gpu_output, gpu_ok] = run_command(
                "straylight-remote " + ip + " gpu-info " +
                std::to_string(gi) + " 2>/dev/null");

            RemoteGpu gpu;
            gpu.host      = ip;
            gpu.gpu_index = static_cast<uint32_t>(gi);
            gpu.is_local  = false;
            gpu.is_available = true;
            gpu.last_seen = std::chrono::steady_clock::now();

            if (gpu_ok && !gpu_output.empty()) {
                std::istringstream gss(gpu_output);
                std::string name, vendor;
                size_t vtotal = 0, vfree = 0;
                float temp = 0, util = 0;

                if (gss >> name >> vendor >> vtotal >> vfree >> temp >> util) {
                    gpu.name           = name;
                    gpu.vendor         = vendor;
                    gpu.vram_total     = vtotal;
                    gpu.vram_available = vfree;
                    gpu.temperature    = temp;
                    gpu.utilization    = util;
                }
            } else {
                gpu.name           = hostname + "-gpu" + std::to_string(gi);
                gpu.vendor         = "unknown";
                gpu.vram_total     = vram_total / std::max(gpu_count, 1);
                gpu.vram_available = vram_free / std::max(gpu_count, 1);
            }

            // Measure latency
            auto [ping_out, ping_ok] = run_command(
                "ping -c 1 -W 1 " + ip +
                " 2>/dev/null | grep time= | sed 's/.*time=//;s/ ms//'");
            if (ping_ok && !ping_out.empty()) {
                gpu.latency_ms = static_cast<float>(atof(ping_out.c_str()));
            }

            gpus_.push_back(std::move(gpu));
        }
    }

    return Result<void, std::string>::ok();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<RemoteGpu> GpuPool::all_gpus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gpus_;
}

std::vector<RemoteGpu> GpuPool::available_gpus(float max_utilization) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RemoteGpu> result;
    for (const auto& gpu : gpus_) {
        if (gpu.is_available && gpu.utilization < max_utilization) {
            result.push_back(gpu);
        }
    }
    return result;
}

size_t GpuPool::gpu_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return gpus_.size();
}

void GpuPool::mesh_totals(size_t& total, size_t& available) const {
    std::lock_guard<std::mutex> lock(mutex_);
    total = 0;
    available = 0;
    for (const auto& gpu : gpus_) {
        if (gpu.is_available) {
            total     += gpu.vram_total;
            available += gpu.vram_available;
        }
    }
}

// ---------------------------------------------------------------------------
// GPU selection
// ---------------------------------------------------------------------------

Result<RemoteGpu*, std::string> GpuPool::select_gpu(size_t bytes, PlacementPolicy policy) {
    std::vector<RemoteGpu*> candidates;
    for (auto& gpu : gpus_) {
        if (gpu.is_available && gpu.vram_available >= bytes) {
            candidates.push_back(&gpu);
        }
    }

    if (candidates.empty()) {
        return Result<RemoteGpu*, std::string>::error(
            "No GPU with sufficient VRAM available (" +
            std::to_string(bytes / (1024 * 1024)) + " MiB requested)");
    }

    RemoteGpu* selected = nullptr;

    switch (policy) {
        case PlacementPolicy::BestFit:
            selected = *std::min_element(candidates.begin(), candidates.end(),
                [](const RemoteGpu* a, const RemoteGpu* b) {
                    return a->vram_available < b->vram_available;
                });
            break;

        case PlacementPolicy::LeastLoaded:
            selected = *std::min_element(candidates.begin(), candidates.end(),
                [](const RemoteGpu* a, const RemoteGpu* b) {
                    return a->utilization < b->utilization;
                });
            break;

        case PlacementPolicy::LocalFirst: {
            std::vector<RemoteGpu*> local, remote;
            for (auto* g : candidates) {
                if (g->is_local) local.push_back(g);
                else              remote.push_back(g);
            }
            auto& pool = local.empty() ? remote : local;
            selected = *std::min_element(pool.begin(), pool.end(),
                [](const RemoteGpu* a, const RemoteGpu* b) {
                    return a->utilization < b->utilization;
                });
            break;
        }

        case PlacementPolicy::RoundRobin:
            selected = candidates[round_robin_index_ % candidates.size()];
            round_robin_index_++;
            break;

        case PlacementPolicy::Pinned:
            selected = candidates[0];
            break;
    }

    return Result<RemoteGpu*, std::string>::ok(selected);
}

// ---------------------------------------------------------------------------
// Allocation
// ---------------------------------------------------------------------------

Result<uint64_t, std::string> GpuPool::local_alloc(uint32_t gpu_index, size_t bytes) {
    std::string dev_path = "/dev/straylight-vpu" + std::to_string(gpu_index);
    int fd = open(dev_path.c_str(), O_RDWR);
    if (fd < 0) {
        return Result<uint64_t, std::string>::error(
            "Cannot open " + dev_path + ": " + strerror(errno));
    }

    VpuAllocRequest req{};
    req.size = bytes;
    int ret = ioctl(fd, VPU_IOC_ALLOC, &req);
    close(fd);

    if (ret != 0) {
        return Result<uint64_t, std::string>::error(
            "VPU_IOC_ALLOC failed on gpu" + std::to_string(gpu_index) +
            ": " + strerror(errno));
    }

    return Result<uint64_t, std::string>::ok(req.handle);
}

Result<uint64_t, std::string> GpuPool::remote_alloc(const std::string& host,
                                                      uint32_t gpu_index, size_t bytes) {
    auto [output, ok] = run_command(
        "straylight-remote " + host + " alloc " +
        std::to_string(gpu_index) + " " +
        std::to_string(bytes) + " 2>&1");

    if (!ok || output.empty()) {
        return Result<uint64_t, std::string>::error(
            "Remote alloc failed on " + host + ": " + output);
    }

    uint64_t handle = 0;
    if (sscanf(output.c_str(), "%lu", &handle) != 1) {
        return Result<uint64_t, std::string>::error(
            "Cannot parse handle from remote alloc on " + host);
    }

    return Result<uint64_t, std::string>::ok(handle);
}

Result<MeshAllocation, std::string> GpuPool::allocate(size_t bytes, PlacementPolicy policy) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto gpu_result = select_gpu(bytes, policy);
    if (!gpu_result.has_value()) {
        return Result<MeshAllocation, std::string>::error(gpu_result.error());
    }

    RemoteGpu* gpu = gpu_result.value();

    uint64_t handle = 0;
    if (gpu->is_local) {
        auto alloc_result = local_alloc(gpu->gpu_index, bytes);
        if (!alloc_result.has_value()) {
            return Result<MeshAllocation, std::string>::error(alloc_result.error());
        }
        handle = alloc_result.value();
    } else {
        auto alloc_result = remote_alloc(gpu->host, gpu->gpu_index, bytes);
        if (!alloc_result.has_value()) {
            return Result<MeshAllocation, std::string>::error(alloc_result.error());
        }
        handle = alloc_result.value();
    }

    if (gpu->vram_available >= bytes) {
        gpu->vram_available -= bytes;
    }

    MeshAllocation alloc;
    alloc.handle     = handle;
    alloc.host       = gpu->host;
    alloc.gpu_index  = gpu->gpu_index;
    alloc.size_bytes = bytes;
    alloc.is_local   = gpu->is_local;

    SL_DEBUG("mesh: allocated {} bytes on {}:gpu{} (handle={})",
             bytes, gpu->host, gpu->gpu_index, handle);

    return Result<MeshAllocation, std::string>::ok(alloc);
}

// ---------------------------------------------------------------------------
// Free
// ---------------------------------------------------------------------------

Result<void, std::string> GpuPool::local_free(uint32_t gpu_index, uint64_t handle) {
    std::string dev_path = "/dev/straylight-vpu" + std::to_string(gpu_index);
    int fd = open(dev_path.c_str(), O_RDWR);
    if (fd < 0) {
        return Result<void, std::string>::error("Cannot open " + dev_path);
    }

    VpuFreeRequest req{};
    req.handle = handle;
    int ret = ioctl(fd, VPU_IOC_FREE, &req);
    close(fd);

    if (ret != 0) {
        return Result<void, std::string>::error(
            "VPU_IOC_FREE failed: " + std::string(strerror(errno)));
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::remote_free(const std::string& host,
                                                 uint32_t gpu_index, uint64_t handle) {
    auto [output, ok] = run_command(
        "straylight-remote " + host + " free " +
        std::to_string(gpu_index) + " " +
        std::to_string(handle) + " 2>&1");

    if (!ok) {
        return Result<void, std::string>::error(
            "Remote free failed on " + host + ": " + output);
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::free(const MeshAllocation& alloc) {
    std::lock_guard<std::mutex> lock(mutex_);

    Result<void, std::string> result = alloc.is_local
        ? local_free(alloc.gpu_index, alloc.handle)
        : remote_free(alloc.host, alloc.gpu_index, alloc.handle);

    if (result.has_value()) {
        for (auto& gpu : gpus_) {
            if (gpu.host == alloc.host && gpu.gpu_index == alloc.gpu_index) {
                gpu.vram_available += alloc.size_bytes;
                break;
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Transfer
// ---------------------------------------------------------------------------

Result<void, std::string> GpuPool::local_to_local_transfer(
    uint32_t src_gpu, uint64_t src_handle,
    uint32_t dst_gpu, uint64_t dst_handle, size_t bytes)
{
    std::string dev_path = "/dev/straylight-vpu" + std::to_string(src_gpu);
    int fd = open(dev_path.c_str(), O_RDWR);
    if (fd < 0) {
        return Result<void, std::string>::error("Cannot open " + dev_path);
    }

    VpuP2pDma req{};
    req.src_handle = src_handle;
    req.dst_handle = dst_handle;
    req.size       = bytes;
    req.dst_gpu    = dst_gpu;

    int ret = ioctl(fd, VPU_IOC_P2P_DMA, &req);
    close(fd);

    if (ret != 0) {
        return Result<void, std::string>::error(
            "P2P DMA transfer failed: " + std::string(strerror(errno)));
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::remote_transfer(
    const MeshAllocation& src, const MeshAllocation& dst)
{
    // Download from source, pipe to destination
    auto [output, ok] = run_command(
        "straylight-remote " + src.host + " download " +
        std::to_string(src.gpu_index) + " " +
        std::to_string(src.handle) + " " +
        std::to_string(src.size_bytes) + " | " +
        "straylight-remote " + dst.host + " upload " +
        std::to_string(dst.gpu_index) + " " +
        std::to_string(dst.handle) + " 2>&1");

    if (!ok) {
        return Result<void, std::string>::error("Remote transfer failed: " + output);
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> GpuPool::transfer(const MeshAllocation& src,
                                              const MeshAllocation& dst) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (src.size_bytes != dst.size_bytes) {
        return Result<void, std::string>::error(
            "Transfer size mismatch: src=" + std::to_string(src.size_bytes) +
            " dst=" + std::to_string(dst.size_bytes));
    }

    if (src.is_local && dst.is_local) {
        return local_to_local_transfer(src.gpu_index, src.handle,
                                        dst.gpu_index, dst.handle,
                                        src.size_bytes);
    }

    return remote_transfer(src, dst);
}

// ---------------------------------------------------------------------------
// Submit
// ---------------------------------------------------------------------------

Result<std::string, std::string> GpuPool::remote_exec(const std::string& host,
                                                        const std::string& command) {
    auto [output, ok] = run_command(
        "straylight-remote " + host + " exec '" + command + "' 2>&1");

    if (!ok) {
        return Result<std::string, std::string>::error(
            "Remote exec failed on " + host + ": " + output);
    }

    return Result<std::string, std::string>::ok(output);
}

Result<std::string, std::string> GpuPool::submit(const std::string& command,
                                                   size_t vram_needed) {
    std::lock_guard<std::mutex> lock(mutex_);

    RemoteGpu* best = nullptr;
    for (auto& gpu : gpus_) {
        if (!gpu.is_available) continue;
        if (gpu.vram_available < vram_needed) continue;
        if (!best || gpu.utilization < best->utilization) {
            best = &gpu;
        }
    }

    if (!best) {
        return Result<std::string, std::string>::error(
            "No GPU available with " + std::to_string(vram_needed / (1024 * 1024)) +
            " MiB free VRAM");
    }

    SL_INFO("mesh: submitting '{}' to {}:gpu{} (util={:.1f}%, vram_free={}MiB)",
            command, best->host, best->gpu_index,
            best->utilization * 100.0f,
            best->vram_available / (1024 * 1024));

    if (best->is_local) {
        auto [output, ok] = run_command(
            "CUDA_VISIBLE_DEVICES=" + std::to_string(best->gpu_index) +
            " " + command + " 2>&1");

        if (!ok) {
            return Result<std::string, std::string>::error(
                "Local execution failed: " + output);
        }

        return Result<std::string, std::string>::ok(output);
    }

    return remote_exec(best->host, command);
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

Result<void, std::string> GpuPool::refresh_stats() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& gpu : gpus_) {
        if (gpu.is_local) {
            std::string dev_path = "/dev/straylight-vpu" + std::to_string(gpu.gpu_index);
            int fd = open(dev_path.c_str(), O_RDWR);
            if (fd < 0) continue;

            VpuInfo info{};
            if (ioctl(fd, VPU_IOC_INFO, &info) == 0) {
                gpu.vram_available = info.vram_free;
                gpu.temperature    = static_cast<float>(info.temperature) / 1000.0f;
                gpu.utilization    = static_cast<float>(info.utilization) / 10000.0f;
                gpu.last_seen      = std::chrono::steady_clock::now();
            }
            close(fd);
        } else {
            auto [output, ok] = run_command(
                "straylight-remote " + gpu.host + " gpu-info " +
                std::to_string(gpu.gpu_index) + " 2>/dev/null");

            if (ok && !output.empty()) {
                std::istringstream iss(output);
                std::string name, vendor;
                size_t vtotal = 0, vfree = 0;
                float temp = 0, util = 0;
                if (iss >> name >> vendor >> vtotal >> vfree >> temp >> util) {
                    gpu.vram_total     = vtotal;
                    gpu.vram_available = vfree;
                    gpu.temperature    = temp;
                    gpu.utilization    = util;
                    gpu.last_seen      = std::chrono::steady_clock::now();
                }
            }
        }
    }

    return Result<void, std::string>::ok();
}

void GpuPool::mark_unavailable(const std::string& host, uint32_t gpu_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& gpu : gpus_) {
        if (gpu.host == host && gpu.gpu_index == gpu_index) {
            gpu.is_available = false;
            SL_WARN("mesh: marked {}:gpu{} as unavailable", host, gpu_index);
            break;
        }
    }
}

void GpuPool::mark_available(const std::string& host, uint32_t gpu_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& gpu : gpus_) {
        if (gpu.host == host && gpu.gpu_index == gpu_index) {
            gpu.is_available = true;
            SL_INFO("mesh: marked {}:gpu{} as available", host, gpu_index);
            break;
        }
    }
}

void GpuPool::remove_host(const std::string& host) {
    std::lock_guard<std::mutex> lock(mutex_);
    gpus_.erase(
        std::remove_if(gpus_.begin(), gpus_.end(),
            [&](const RemoteGpu& g) { return g.host == host; }),
        gpus_.end());
    SL_INFO("mesh: removed all GPUs from host {}", host);
}

} // namespace straylight
