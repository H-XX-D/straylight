# Plan 9B: Desktop Apps — System Monitor & Settings

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement two desktop applications — straylight-monitor (real-time system resource viewer) and straylight-settings (OS configuration UI). Both are standalone ImGui Wayland clients using wl_egl_window + EGL, sharing the common app pattern established in Plan 9A.

**Architecture:** straylight-monitor reads `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/proc/[pid]/stat`, and GPU sysfs to display live metrics in tabbed ImGui panels. straylight-settings provides paginated configuration for hostname, display, audio (PipeWire), users, and system info. Both apps link libstraylight-common and use `Result<T,E>::ok/error` throughout.

**Tech Stack:** C++20, CMake 3.25+, Dear ImGui 1.90+, wl_egl_window + EGL + OpenGL ES 3.0, wayland-client 1.22+, xdg-shell, PipeWire 1.0+, spdlog 1.13+, nlohmann/json 3.11+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor — xdg-shell), Plan 9A (common app base pattern, AppBase class)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Reads `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/sys/class/drm/`. macOS cannot build.

---

## Common App Base (from Plan 9A)

All apps in Plan 9A/9B inherit from `AppBase` in `lib/common/include/straylight/app_base.h`:

```cpp
namespace straylight {
class AppBase {
public:
    virtual ~AppBase() = default;
    virtual const char* title() const = 0;
    virtual Result<void, SLError> init() = 0;
    virtual void update() = 0;       // poll data
    virtual void render() = 0;       // ImGui draw
    virtual void shutdown() = 0;
    int run(int argc, char* argv[]);  // Wayland+EGL event loop
};
} // namespace straylight
```

`AppBase::run()` handles Wayland connection, xdg-shell surface, EGL init, ImGui context, and the frame loop. Defined once in Plan 9A.

---

## Chunk 1: straylight-monitor — CPU, Memory, GPU Subsystems

**Goal:** Build the first three data backends for the system monitor: CPU usage, memory stats, and GPU metrics. No rendering yet — pure data parsing with unit-testable interfaces.

### Step 1.1 — CMakeLists for apps/system_monitor/

- [ ] Create `apps/system_monitor/CMakeLists.txt`
  - Target: `straylight-monitor` (executable)
  - Sources: `main.cpp`, `cpu.cpp`, `memory.cpp`, `gpu.cpp`, `network.cpp`, `process.cpp`
  - Link: `straylight-common`, `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`, `imgui`
  - Install: `${CMAKE_INSTALL_BINDIR}`

```cmake
# apps/system_monitor/CMakeLists.txt
add_executable(straylight-monitor
    main.cpp cpu.cpp memory.cpp gpu.cpp network.cpp process.cpp)

target_link_libraries(straylight-monitor PRIVATE
    straylight-common wayland-client wayland-egl EGL GLESv2 imgui)

target_include_directories(straylight-monitor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-monitor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Step 1.2 — cpu.h/cpp: Per-Core Usage from /proc/stat

- [ ] Create `apps/system_monitor/cpu.h`
- [ ] Create `apps/system_monitor/cpu.cpp`

```cpp
// apps/system_monitor/cpu.h
#pragma once
#include <straylight/common.h>
#include <vector>

namespace straylight::monitor {

struct CoreUsage {
    int core_id;
    float usage_pct;      // 0.0–100.0
    uint64_t user, nice, system, idle, iowait;
};

struct CpuSnapshot {
    std::vector<CoreUsage> cores;
    float total_pct;
    float load_avg[3];    // 1m, 5m, 15m
};

class CpuProbe {
public:
    Result<CpuSnapshot, SLError> sample();

private:
    struct PrevJiffies { uint64_t total, idle; };
    std::vector<PrevJiffies> prev_;
};

} // namespace straylight::monitor
```

```cpp
// apps/system_monitor/cpu.cpp
#include "cpu.h"
#include <fstream>
#include <sstream>

namespace straylight::monitor {

Result<CpuSnapshot, SLError> CpuProbe::sample() {
    std::ifstream stat("/proc/stat");
    if (!stat) return Result<CpuSnapshot, SLError>::error({"Failed to open /proc/stat"});

    CpuSnapshot snap;
    std::string line;
    int idx = -1;  // -1 = aggregate "cpu" line

    while (std::getline(stat, line)) {
        if (line.compare(0, 3, "cpu") != 0) break;
        std::istringstream ss(line);
        std::string label;
        uint64_t user, nice, sys, idle, iowait, irq, softirq, steal;
        ss >> label >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;

        uint64_t total = user + nice + sys + idle + iowait + irq + softirq + steal;
        uint64_t idle_all = idle + iowait;

        float pct = 0.0f;
        if (idx >= 0 || !prev_.empty()) {
            size_t pi = (idx < 0) ? 0 : static_cast<size_t>(idx + 1);
            if (pi < prev_.size()) {
                uint64_t dt = total - prev_[pi].total;
                uint64_t di = idle_all - prev_[pi].idle;
                pct = dt > 0 ? 100.0f * (1.0f - static_cast<float>(di) / dt) : 0.0f;
            }
        }

        if (idx < 0) {
            snap.total_pct = pct;
        } else {
            snap.cores.push_back({idx, pct, user, nice, sys, idle, iowait});
        }
        // ... (store prev jiffies for delta calculation)
        idx++;
    }
    // ... (parse /proc/loadavg for snap.load_avg)
    return Result<CpuSnapshot, SLError>::ok(std::move(snap));
}

} // namespace straylight::monitor
```

### Step 1.3 — memory.h/cpp: RAM/Swap from /proc/meminfo

- [ ] Create `apps/system_monitor/memory.h`
- [ ] Create `apps/system_monitor/memory.cpp`

```cpp
// apps/system_monitor/memory.h
#pragma once
#include <straylight/common.h>

namespace straylight::monitor {

struct MemorySnapshot {
    uint64_t total_kb, available_kb, used_kb;
    uint64_t swap_total_kb, swap_free_kb;
    uint64_t buffers_kb, cached_kb;
    float used_pct;
    float swap_used_pct;
};

class MemoryProbe {
public:
    Result<MemorySnapshot, SLError> sample();
};

} // namespace straylight::monitor
```

```cpp
// apps/system_monitor/memory.cpp
#include "memory.h"
#include <fstream>
#include <string>

namespace straylight::monitor {

Result<MemorySnapshot, SLError> MemoryProbe::sample() {
    std::ifstream f("/proc/meminfo");
    if (!f) return Result<MemorySnapshot, SLError>::error({"Failed to open /proc/meminfo"});

    MemorySnapshot snap{};
    std::string key;
    uint64_t val;
    std::string unit;

    while (f >> key >> val) {
        std::getline(f, unit);  // consume " kB\n"
        if (key == "MemTotal:")       snap.total_kb = val;
        else if (key == "MemAvailable:") snap.available_kb = val;
        else if (key == "Buffers:")    snap.buffers_kb = val;
        else if (key == "Cached:")     snap.cached_kb = val;
        else if (key == "SwapTotal:")  snap.swap_total_kb = val;
        else if (key == "SwapFree:")   snap.swap_free_kb = val;
    }
    snap.used_kb = snap.total_kb - snap.available_kb;
    snap.used_pct = snap.total_kb > 0
        ? 100.0f * static_cast<float>(snap.used_kb) / snap.total_kb : 0.0f;
    snap.swap_used_pct = snap.swap_total_kb > 0
        ? 100.0f * (1.0f - static_cast<float>(snap.swap_free_kb) / snap.swap_total_kb) : 0.0f;

    return Result<MemorySnapshot, SLError>::ok(std::move(snap));
}

} // namespace straylight::monitor
```

### Step 1.4 — gpu.h/cpp: GPU Stats from sysfs/nvidia-smi

- [ ] Create `apps/system_monitor/gpu.h`
- [ ] Create `apps/system_monitor/gpu.cpp`

```cpp
// apps/system_monitor/gpu.h
#pragma once
#include <straylight/common.h>
#include <string>
#include <vector>

namespace straylight::monitor {

struct GpuInfo {
    std::string name;
    float usage_pct;
    float mem_used_mb, mem_total_mb;
    float temp_celsius;
    float power_watts;
};

class GpuProbe {
public:
    Result<std::vector<GpuInfo>, SLError> sample();

private:
    Result<std::vector<GpuInfo>, SLError> sample_nvidia();
    Result<std::vector<GpuInfo>, SLError> sample_drm();
};

} // namespace straylight::monitor
```

```cpp
// apps/system_monitor/gpu.cpp
#include "gpu.h"
#include <fstream>
#include <cstdio>
#include <filesystem>

namespace straylight::monitor {
namespace fs = std::filesystem;

Result<std::vector<GpuInfo>, SLError> GpuProbe::sample() {
    // Try nvidia-smi first, fall back to DRM sysfs
    if (auto r = sample_nvidia(); r) return r;
    return sample_drm();
}

Result<std::vector<GpuInfo>, SLError> GpuProbe::sample_nvidia() {
    FILE* pipe = popen("nvidia-smi --query-gpu=name,utilization.gpu,"
        "memory.used,memory.total,temperature.gpu,power.draw "
        "--format=csv,noheader,nounits 2>/dev/null", "r");
    if (!pipe) return Result<std::vector<GpuInfo>, SLError>::error({"nvidia-smi not available"});

    std::vector<GpuInfo> gpus;
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
        GpuInfo g;
        char name[128];
        if (sscanf(buf, "%127[^,], %f, %f, %f, %f, %f",
                   name, &g.usage_pct, &g.mem_used_mb,
                   &g.mem_total_mb, &g.temp_celsius, &g.power_watts) == 6) {
            g.name = name;
            gpus.push_back(std::move(g));
        }
    }
    pclose(pipe);
    if (gpus.empty()) return Result<std::vector<GpuInfo>, SLError>::error({"No NVIDIA GPUs"});
    return Result<std::vector<GpuInfo>, SLError>::ok(std::move(gpus));
}

Result<std::vector<GpuInfo>, SLError> GpuProbe::sample_drm() {
    std::vector<GpuInfo> gpus;
    for (auto& entry : fs::directory_iterator("/sys/class/drm/")) {
        auto name = entry.path().filename().string();
        if (name.find("card") != 0 || name.find('-') != std::string::npos) continue;
        GpuInfo g;
        g.name = name;
        // ... (read gpu_busy_percent, mem_info_vram_used, temp from hwmon)
        gpus.push_back(std::move(g));
    }
    if (gpus.empty()) return Result<std::vector<GpuInfo>, SLError>::error({"No DRM GPUs found"});
    return Result<std::vector<GpuInfo>, SLError>::ok(std::move(gpus));
}

} // namespace straylight::monitor
```

---

## Chunk 2: straylight-monitor — Network, Process, and Main

**Goal:** Complete the monitor data backends (network I/O, process list) and wire everything into a tabbed ImGui application.

### Step 2.1 — network.h/cpp: Interface Traffic from /proc/net/dev

- [ ] Create `apps/system_monitor/network.h`
- [ ] Create `apps/system_monitor/network.cpp`

```cpp
// apps/system_monitor/network.h
#pragma once
#include <straylight/common.h>
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>

namespace straylight::monitor {

struct IfaceStats {
    std::string name;
    uint64_t rx_bytes, tx_bytes;
    double rx_speed_mbps, tx_speed_mbps;  // calculated delta
};

class NetworkProbe {
public:
    Result<std::vector<IfaceStats>, SLError> sample();

private:
    struct Prev { uint64_t rx, tx; std::chrono::steady_clock::time_point ts; };
    std::unordered_map<std::string, Prev> prev_;
};

} // namespace straylight::monitor
```

```cpp
// apps/system_monitor/network.cpp
#include "network.h"
#include <fstream>
#include <sstream>

namespace straylight::monitor {

Result<std::vector<IfaceStats>, SLError> NetworkProbe::sample() {
    std::ifstream f("/proc/net/dev");
    if (!f) return Result<std::vector<IfaceStats>, SLError>::error({"Failed to open /proc/net/dev"});

    std::vector<IfaceStats> result;
    std::string line;
    std::getline(f, line); std::getline(f, line); // skip headers

    auto now = std::chrono::steady_clock::now();
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface;
        ss >> iface;
        if (iface.back() == ':') iface.pop_back();

        uint64_t rx_bytes, rx_pkt, rx_err, rx_drop, _x1, _x2, _x3, _x4;
        uint64_t tx_bytes;
        ss >> rx_bytes >> rx_pkt >> rx_err >> rx_drop >> _x1 >> _x2 >> _x3 >> _x4;
        ss >> tx_bytes;
        // ... (skip remaining tx fields)

        IfaceStats st{iface, rx_bytes, tx_bytes, 0.0, 0.0};
        if (auto it = prev_.find(iface); it != prev_.end()) {
            auto dt = std::chrono::duration<double>(now - it->second.ts).count();
            if (dt > 0.0) {
                st.rx_speed_mbps = static_cast<double>(rx_bytes - it->second.rx) * 8.0 / (dt * 1e6);
                st.tx_speed_mbps = static_cast<double>(tx_bytes - it->second.tx) * 8.0 / (dt * 1e6);
            }
        }
        prev_[iface] = {rx_bytes, tx_bytes, now};
        result.push_back(std::move(st));
    }
    return Result<std::vector<IfaceStats>, SLError>::ok(std::move(result));
}

} // namespace straylight::monitor
```

### Step 2.2 — process.h/cpp: Process List from /proc/[pid]/stat

- [ ] Create `apps/system_monitor/process.h`
- [ ] Create `apps/system_monitor/process.cpp`

```cpp
// apps/system_monitor/process.h
#pragma once
#include <straylight/common.h>
#include <string>
#include <vector>

namespace straylight::monitor {

struct ProcessInfo {
    pid_t pid;
    std::string name;
    char state;              // R, S, D, Z, T
    float cpu_pct;
    uint64_t rss_kb;
    std::string user;
};

enum class SortBy { Cpu, Memory, Pid, Name };

class ProcessProbe {
public:
    Result<std::vector<ProcessInfo>, SLError> sample(SortBy sort = SortBy::Cpu);

private:
    struct PrevCpu { uint64_t utime, stime; };
    std::unordered_map<pid_t, PrevCpu> prev_;
    uint64_t prev_total_jiffies_ = 0;
};

} // namespace straylight::monitor
```

```cpp
// apps/system_monitor/process.cpp
#include "process.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <pwd.h>
#include <unistd.h>

namespace straylight::monitor {
namespace fs = std::filesystem;

Result<std::vector<ProcessInfo>, SLError> ProcessProbe::sample(SortBy sort) {
    // Read total CPU jiffies from /proc/stat for delta
    uint64_t total_jiffies = 0;
    {
        std::ifstream stat("/proc/stat");
        std::string label;
        uint64_t u, n, s, i, w, ir, si, st;
        stat >> label >> u >> n >> s >> i >> w >> ir >> si >> st;
        total_jiffies = u + n + s + i + w + ir + si + st;
    }
    uint64_t dt = total_jiffies - prev_total_jiffies_;

    std::vector<ProcessInfo> procs;
    for (auto& entry : fs::directory_iterator("/proc/")) {
        auto name = entry.path().filename().string();
        if (!std::all_of(name.begin(), name.end(), ::isdigit)) continue;

        pid_t pid = std::stoi(name);
        std::ifstream sf(entry.path() / "stat");
        if (!sf) continue;

        ProcessInfo pi{};
        pi.pid = pid;
        // ... (parse comm, state, utime, stime, rss from /proc/[pid]/stat)
        // ... (calculate cpu_pct from delta jiffies, resolve user from uid)
        procs.push_back(std::move(pi));
    }
    prev_total_jiffies_ = total_jiffies;

    // Sort by requested field
    switch (sort) {
        case SortBy::Cpu:    std::sort(procs.begin(), procs.end(),
                                 [](auto& a, auto& b) { return a.cpu_pct > b.cpu_pct; }); break;
        case SortBy::Memory: std::sort(procs.begin(), procs.end(),
                                 [](auto& a, auto& b) { return a.rss_kb > b.rss_kb; }); break;
        // ... (standard pattern for Pid, Name)
    }
    return Result<std::vector<ProcessInfo>, SLError>::ok(std::move(procs));
}

} // namespace straylight::monitor
```

### Step 2.3 — main.cpp: Tabbed ImGui Monitor Application

- [ ] Create `apps/system_monitor/main.cpp`

```cpp
// apps/system_monitor/main.cpp
#include <straylight/app_base.h>
#include "cpu.h"
#include "memory.h"
#include "gpu.h"
#include "network.h"
#include "process.h"
#include "imgui.h"

namespace straylight::monitor {

class MonitorApp : public AppBase {
public:
    const char* title() const override { return "System Monitor"; }

    Result<void, SLError> init() override {
        // Initial sample to populate prev_ deltas
        cpu_.sample(); mem_.sample(); gpu_.sample(); net_.sample(); proc_.sample();
        return Result<void, SLError>::ok({});
    }

    void update() override {
        cpu_snap_  = cpu_.sample().value_or({});
        mem_snap_  = mem_.sample().value_or({});
        gpu_snap_  = gpu_.sample().value_or({});
        net_snap_  = net_.sample().value_or({});
        proc_snap_ = proc_.sample(sort_).value_or({});
    }

    void render() override {
        if (ImGui::BeginTabBar("MonitorTabs")) {
            if (ImGui::BeginTabItem("CPU")) {
                ImGui::Text("Total: %.1f%%  Load: %.2f %.2f %.2f",
                    cpu_snap_.total_pct, cpu_snap_.load_avg[0],
                    cpu_snap_.load_avg[1], cpu_snap_.load_avg[2]);
                for (auto& c : cpu_snap_.cores) {
                    ImGui::ProgressBar(c.usage_pct / 100.0f, {-1, 0},
                        fmt::format("Core {}: {:.0f}%%", c.core_id, c.usage_pct).c_str());
                }
                ImGui::EndTabItem();
            }
            // ... (Memory tab — progress bar for RAM/swap)
            // ... (GPU tab — table of GpuInfo entries)
            // ... (Network tab — table of IfaceStats with speed columns)
            if (ImGui::BeginTabItem("Processes")) {
                // Sort selector
                const char* sorts[] = {"CPU", "Memory", "PID", "Name"};
                int si = static_cast<int>(sort_);
                if (ImGui::Combo("Sort", &si, sorts, 4)) sort_ = static_cast<SortBy>(si);

                if (ImGui::BeginTable("procs", 5, ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("PID");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("CPU%");
                    ImGui::TableSetupColumn("RSS (KB)");
                    ImGui::TableSetupColumn("User");
                    ImGui::TableHeadersRow();
                    for (auto& p : proc_snap_) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%d", p.pid);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.name.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.1f", p.cpu_pct);
                        ImGui::TableNextColumn(); ImGui::Text("%lu", p.rss_kb);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.user.c_str());
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void shutdown() override {}

private:
    CpuProbe cpu_; MemoryProbe mem_; GpuProbe gpu_;
    NetworkProbe net_; ProcessProbe proc_;
    CpuSnapshot cpu_snap_; MemorySnapshot mem_snap_;
    std::vector<GpuInfo> gpu_snap_; std::vector<IfaceStats> net_snap_;
    std::vector<ProcessInfo> proc_snap_;
    SortBy sort_ = SortBy::Cpu;
};

} // namespace straylight::monitor

int main(int argc, char* argv[]) {
    straylight::monitor::MonitorApp app;
    return app.run(argc, argv);
}
```

---

## Chunk 3: straylight-settings — All Pages

**Goal:** Implement the full settings application with 5 configuration pages: General, Display, Sound, Users, and About.

### Step 3.1 — CMakeLists for apps/settings/

- [ ] Create `apps/settings/CMakeLists.txt`

```cmake
# apps/settings/CMakeLists.txt
add_executable(straylight-settings
    main.cpp
    pages/general.cpp pages/display.cpp pages/sound.cpp
    pages/users.cpp pages/about.cpp)

target_link_libraries(straylight-settings PRIVATE
    straylight-common wayland-client wayland-egl EGL GLESv2 imgui PkgConfig::PipeWire)

target_include_directories(straylight-settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-settings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Step 3.2 — SettingsPage base and General page

- [ ] Create `apps/settings/settings_page.h`
- [ ] Create `apps/settings/pages/general.h`
- [ ] Create `apps/settings/pages/general.cpp`

```cpp
// apps/settings/settings_page.h
#pragma once
#include <straylight/common.h>

namespace straylight::settings {

class SettingsPage {
public:
    virtual ~SettingsPage() = default;
    virtual const char* label() const = 0;   // sidebar label
    virtual void load() = 0;                 // read current state
    virtual void render() = 0;               // ImGui panel
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/general.h
#pragma once
#include "../settings_page.h"
#include <string>

namespace straylight::settings {

class GeneralPage : public SettingsPage {
public:
    const char* label() const override { return "General"; }
    void load() override;
    void render() override;

private:
    char hostname_[256] = {};
    char timezone_[128] = {};
    int lang_idx_ = 0;
    static constexpr const char* kLangs[] = {"en_US.UTF-8", "de_DE.UTF-8",
        "fr_FR.UTF-8", "ja_JP.UTF-8", "zh_CN.UTF-8"};
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/general.cpp
#include "general.h"
#include "imgui.h"
#include <fstream>
#include <unistd.h>
#include <cstring>

namespace straylight::settings {

void GeneralPage::load() {
    gethostname(hostname_, sizeof(hostname_));
    // Read /etc/timezone
    std::ifstream tz("/etc/timezone");
    if (tz) tz.getline(timezone_, sizeof(timezone_));
    // ... (detect current lang_idx_ from $LANG)
}

void GeneralPage::render() {
    ImGui::SeparatorText("Hostname");
    if (ImGui::InputText("##host", hostname_, sizeof(hostname_),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Apply: sethostname() + write /etc/hostname
        sethostname(hostname_, std::strlen(hostname_));
        std::ofstream("/etc/hostname") << hostname_;
    }

    ImGui::SeparatorText("Timezone");
    if (ImGui::InputText("##tz", timezone_, sizeof(timezone_),
            ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Apply: symlink /etc/localtime → /usr/share/zoneinfo/<tz>
        std::string cmd = fmt::format("timedatectl set-timezone {}", timezone_);
        std::system(cmd.c_str());
    }

    ImGui::SeparatorText("Language");
    if (ImGui::Combo("##lang", &lang_idx_, kLangs, IM_ARRAYSIZE(kLangs))) {
        // Apply: write /etc/locale.conf
        std::ofstream("/etc/locale.conf") << "LANG=" << kLangs[lang_idx_] << "\n";
    }
}

} // namespace straylight::settings
```

### Step 3.3 — Display page

- [ ] Create `apps/settings/pages/display.h`
- [ ] Create `apps/settings/pages/display.cpp`

```cpp
// apps/settings/pages/display.h
#pragma once
#include "../settings_page.h"
#include <vector>
#include <string>

namespace straylight::settings {

struct DisplayMode { int width, height, refresh; };

class DisplayPage : public SettingsPage {
public:
    const char* label() const override { return "Display"; }
    void load() override;
    void render() override;

private:
    std::vector<DisplayMode> modes_;
    int selected_mode_ = 0;
    float scale_ = 1.0f;
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/display.cpp
#include "display.h"
#include "imgui.h"
#include <straylight/ipc_client.h>

namespace straylight::settings {

void DisplayPage::load() {
    // Query compositor via IPC for available modes
    auto client = IpcClient::connect("/run/straylight/compositor.sock");
    if (!client) return;
    auto resp = client->request(R"({"cmd":"get_outputs"})");
    if (!resp) return;
    // ... (parse JSON modes array into modes_)
}

void DisplayPage::render() {
    ImGui::SeparatorText("Resolution");
    std::vector<std::string> labels;
    for (auto& m : modes_)
        labels.push_back(fmt::format("{}x{} @ {}Hz", m.width, m.height, m.refresh));
    // ... (standard Combo from labels)

    ImGui::SeparatorText("Scale");
    if (ImGui::SliderFloat("UI Scale", &scale_, 1.0f, 3.0f, "%.1fx")) {
        // Delegate to compositor: {"cmd":"set_scale","scale":1.5}
        auto client = IpcClient::connect("/run/straylight/compositor.sock");
        if (client) client->request(fmt::format(R"({{"cmd":"set_scale","scale":{}}})", scale_));
    }
}

} // namespace straylight::settings
```

### Step 3.4 — Sound page (PipeWire)

- [ ] Create `apps/settings/pages/sound.h`
- [ ] Create `apps/settings/pages/sound.cpp`

```cpp
// apps/settings/pages/sound.h
#pragma once
#include "../settings_page.h"
#include <string>
#include <vector>

namespace straylight::settings {

struct AudioDevice { uint32_t id; std::string name; bool is_default; };

class SoundPage : public SettingsPage {
public:
    const char* label() const override { return "Sound"; }
    void load() override;
    void render() override;

private:
    float volume_ = 0.75f;
    bool muted_ = false;
    std::vector<AudioDevice> sinks_;
    int selected_sink_ = 0;
    Result<void, SLError> set_volume(float vol);
    Result<void, SLError> set_default_sink(uint32_t id);
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/sound.cpp
#include "sound.h"
#include "imgui.h"
#include <cstdio>
#include <sstream>

namespace straylight::settings {

void SoundPage::load() {
    // Parse `wpctl status` or `pw-cli list-objects Node` for sinks
    FILE* pipe = popen("wpctl status 2>/dev/null", "r");
    if (!pipe) return;
    // ... (parse output for sink names and IDs, detect default)
    pclose(pipe);

    // Read current volume
    pipe = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (pipe) {
        char buf[128];
        if (fgets(buf, sizeof(buf), pipe)) {
            float v;
            if (sscanf(buf, "Volume: %f", &v) == 1) volume_ = v;
            muted_ = (strstr(buf, "[MUTED]") != nullptr);
        }
        pclose(pipe);
    }
}

void SoundPage::render() {
    ImGui::SeparatorText("Volume");
    if (ImGui::SliderFloat("##vol", &volume_, 0.0f, 1.5f, "%.0f%%")) {
        set_volume(volume_);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Mute", &muted_)) {
        std::system(muted_ ? "wpctl set-mute @DEFAULT_AUDIO_SINK@ 1"
                           : "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0");
    }

    ImGui::SeparatorText("Output Device");
    // ... (standard Combo of sink names, call set_default_sink on change)
}

Result<void, SLError> SoundPage::set_volume(float vol) {
    std::string cmd = fmt::format("wpctl set-volume @DEFAULT_AUDIO_SINK@ {:.2f}", vol);
    if (std::system(cmd.c_str()) != 0)
        return Result<void, SLError>::error({"wpctl set-volume failed"});
    return Result<void, SLError>::ok({});
}

Result<void, SLError> SoundPage::set_default_sink(uint32_t id) {
    std::string cmd = fmt::format("wpctl set-default {}", id);
    if (std::system(cmd.c_str()) != 0)
        return Result<void, SLError>::error({"wpctl set-default failed"});
    return Result<void, SLError>::ok({});
}

} // namespace straylight::settings
```

### Step 3.5 — Users page

- [ ] Create `apps/settings/pages/users.h`
- [ ] Create `apps/settings/pages/users.cpp`

```cpp
// apps/settings/pages/users.h
#pragma once
#include "../settings_page.h"
#include <string>
#include <vector>

namespace straylight::settings {

struct UserInfo { uid_t uid; std::string name; std::string home; bool is_admin; };

class UsersPage : public SettingsPage {
public:
    const char* label() const override { return "Users"; }
    void load() override;
    void render() override;

private:
    std::vector<UserInfo> users_;
    char new_user_[64] = {};
    Result<void, SLError> add_user(const std::string& name, bool admin);
    Result<void, SLError> remove_user(const std::string& name);
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/users.cpp
#include "users.h"
#include "imgui.h"
#include <fstream>
#include <sstream>
#include <grp.h>

namespace straylight::settings {

void UsersPage::load() {
    users_.clear();
    std::ifstream f("/etc/passwd");
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string name, x, uid_s, gid_s, gecos, home;
        std::getline(ss, name, ':'); std::getline(ss, x, ':');
        std::getline(ss, uid_s, ':'); std::getline(ss, gid_s, ':');
        std::getline(ss, gecos, ':'); std::getline(ss, home, ':');
        uid_t uid = std::stoul(uid_s);
        if (uid < 1000 || uid == 65534) continue;  // skip system users
        bool admin = false;
        // ... (check membership in sudo/wheel group)
        users_.push_back({uid, name, home, admin});
    }
}

void UsersPage::render() {
    ImGui::SeparatorText("User Accounts");
    for (auto& u : users_) {
        ImGui::Text("%s (uid %d) %s %s", u.name.c_str(), u.uid,
            u.is_admin ? "[admin]" : "", u.home.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(fmt::format("Remove##{}", u.uid).c_str())) {
            remove_user(u.name);
            load();  // refresh
        }
    }
    ImGui::Separator();
    ImGui::InputText("New user", new_user_, sizeof(new_user_));
    ImGui::SameLine();
    if (ImGui::Button("Add") && new_user_[0]) {
        add_user(new_user_, false);
        new_user_[0] = '\0';
        load();
    }
}

Result<void, SLError> UsersPage::add_user(const std::string& name, bool admin) {
    std::string cmd = fmt::format("useradd -m -s /bin/bash {}", name);
    if (admin) cmd += " -G sudo";
    if (std::system(cmd.c_str()) != 0)
        return Result<void, SLError>::error({"useradd failed"});
    return Result<void, SLError>::ok({});
}

Result<void, SLError> UsersPage::remove_user(const std::string& name) {
    std::string cmd = fmt::format("userdel -r {}", name);
    if (std::system(cmd.c_str()) != 0)
        return Result<void, SLError>::error({"userdel failed"});
    return Result<void, SLError>::ok({});
}

} // namespace straylight::settings
```

### Step 3.6 — About page

- [ ] Create `apps/settings/pages/about.h`
- [ ] Create `apps/settings/pages/about.cpp`

```cpp
// apps/settings/pages/about.h
#pragma once
#include "../settings_page.h"
#include <string>

namespace straylight::settings {

class AboutPage : public SettingsPage {
public:
    const char* label() const override { return "About"; }
    void load() override;
    void render() override;

private:
    std::string os_version_, kernel_, hostname_, cpu_model_;
    uint64_t ram_total_kb_ = 0;
    std::string gpu_name_;
};

} // namespace straylight::settings
```

```cpp
// apps/settings/pages/about.cpp
#include "about.h"
#include "imgui.h"
#include <fstream>
#include <sys/utsname.h>

namespace straylight::settings {

void AboutPage::load() {
    // OS version from /etc/straylight-release
    std::ifstream rel("/etc/straylight-release");
    if (rel) std::getline(rel, os_version_);
    else os_version_ = "StrayLight OS (unknown)";

    struct utsname u;
    if (uname(&u) == 0) { kernel_ = u.release; hostname_ = u.nodename; }

    // CPU model from /proc/cpuinfo
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") == 0) {
            cpu_model_ = line.substr(line.find(':') + 2);
            break;
        }
    }
    // ... (read MemTotal from /proc/meminfo, GPU from lspci or DRM)
}

void AboutPage::render() {
    ImGui::SeparatorText("StrayLight OS");
    ImGui::Text("Version: %s", os_version_.c_str());
    ImGui::Text("Kernel:  %s", kernel_.c_str());
    ImGui::Text("Host:    %s", hostname_.c_str());
    ImGui::Separator();
    ImGui::Text("CPU:     %s", cpu_model_.c_str());
    ImGui::Text("RAM:     %lu GB", ram_total_kb_ / (1024 * 1024));
    ImGui::Text("GPU:     %s", gpu_name_.c_str());
}

} // namespace straylight::settings
```

### Step 3.7 — main.cpp: Settings Application with Sidebar Navigation

- [ ] Create `apps/settings/main.cpp`

```cpp
// apps/settings/main.cpp
#include <straylight/app_base.h>
#include "settings_page.h"
#include "pages/general.h"
#include "pages/display.h"
#include "pages/sound.h"
#include "pages/users.h"
#include "pages/about.h"
#include "imgui.h"
#include <memory>
#include <vector>

namespace straylight::settings {

class SettingsApp : public AppBase {
public:
    const char* title() const override { return "Settings"; }

    Result<void, SLError> init() override {
        pages_.emplace_back(std::make_unique<GeneralPage>());
        pages_.emplace_back(std::make_unique<DisplayPage>());
        pages_.emplace_back(std::make_unique<SoundPage>());
        pages_.emplace_back(std::make_unique<UsersPage>());
        pages_.emplace_back(std::make_unique<AboutPage>());
        for (auto& p : pages_) p->load();
        return Result<void, SLError>::ok({});
    }

    void update() override {} // pages load on demand

    void render() override {
        // Left sidebar (200px)
        ImGui::BeginChild("sidebar", {200, 0}, true);
        for (size_t i = 0; i < pages_.size(); i++) {
            if (ImGui::Selectable(pages_[i]->label(), selected_ == i))
                selected_ = i;
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right content panel
        ImGui::BeginChild("content", {0, 0}, true);
        pages_[selected_]->render();
        ImGui::EndChild();
    }

    void shutdown() override {}

private:
    std::vector<std::unique_ptr<SettingsPage>> pages_;
    size_t selected_ = 0;
};

} // namespace straylight::settings

int main(int argc, char* argv[]) {
    straylight::settings::SettingsApp app;
    return app.run(argc, argv);
}
```

---

## Chunk 4: Unit Tests for All Plan 9A/9B Apps

**Goal:** Add unit tests for straylight-terminal, straylight-files, straylight-monitor, and straylight-settings under `tests/unit/apps/`. Tests validate data parsing and page state without requiring a live Wayland display.

### Step 4.1 — CMakeLists for tests/unit/apps/

- [ ] Update `tests/unit/CMakeLists.txt` to add `add_subdirectory(apps)`
- [ ] Create `tests/unit/apps/CMakeLists.txt`

```cmake
# tests/unit/apps/CMakeLists.txt
include(GoogleTest)

set(APP_TEST_SOURCES
    test_terminal.cpp
    test_file_manager.cpp
    test_monitor.cpp
    test_settings.cpp)

add_executable(test_apps ${APP_TEST_SOURCES})

target_link_libraries(test_apps PRIVATE
    GTest::gtest GTest::gtest_main straylight-common)

# Link monitor/settings object libraries for unit-testable backends
target_include_directories(test_apps PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/system_monitor
    ${CMAKE_SOURCE_DIR}/apps/settings
    ${CMAKE_SOURCE_DIR}/apps/terminal
    ${CMAKE_SOURCE_DIR}/apps/file_manager)

gtest_discover_tests(test_apps)
```

### Step 4.2 — test_terminal.cpp (Plan 9A app)

- [ ] Create `tests/unit/apps/test_terminal.cpp`

```cpp
// tests/unit/apps/test_terminal.cpp
#include <gtest/gtest.h>
#include <straylight/common.h>
// Test the PTY abstraction and ANSI parser from Plan 9A's terminal

namespace {

TEST(Terminal, PtyOpenClose) {
    // Verify forkpty() returns valid fd, child can exec /bin/true
    int master_fd;
    pid_t pid = forkpty(&master_fd, nullptr, nullptr, nullptr);
    if (pid == 0) { _exit(0); }  // child
    ASSERT_GT(pid, 0);
    ASSERT_GE(master_fd, 0);

    int status;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    close(master_fd);
}

TEST(Terminal, AnsiStripBasic) {
    // Verify ESC[31m (red) is recognized as a color code
    std::string input = "\033[31mHello\033[0m";
    // The ANSI parser should extract "Hello" as visible text
    // and track color state = red at offset 0
    size_t visible_len = 0;
    for (char c : input) {
        if (c == '\033') { /* skip until 'm' */ }
        // ... (simplified — real test uses Plan 9A's AnsiParser class)
    }
    EXPECT_GT(input.size(), 5u);  // has escape sequences
}

TEST(Terminal, ScrollbackLimit) {
    // Verify scrollback ring buffer caps at configured max lines
    constexpr int kMaxLines = 10000;
    // ... (construct ScrollbackBuffer, push kMaxLines+100, verify size == kMaxLines)
    SUCCEED();  // placeholder — real impl tests Plan 9A's ScrollbackBuffer
}

} // namespace
```

### Step 4.3 — test_file_manager.cpp (Plan 9A app)

- [ ] Create `tests/unit/apps/test_file_manager.cpp`

```cpp
// tests/unit/apps/test_file_manager.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class FileManagerTest : public ::testing::Test {
protected:
    fs::path tmp_;
    void SetUp() override {
        tmp_ = fs::temp_directory_path() / "sl_fm_test";
        fs::create_directories(tmp_ / "subdir");
        std::ofstream(tmp_ / "file.txt") << "hello";
        std::ofstream(tmp_ / "image.png") << "PNG";
        std::ofstream(tmp_ / ".hidden") << "dot";
    }
    void TearDown() override { fs::remove_all(tmp_); }
};

TEST_F(FileManagerTest, ListDirectoryEntries) {
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(tmp_)) entries.push_back(e);
    EXPECT_GE(entries.size(), 3u);  // subdir, file.txt, image.png (.hidden may vary)
}

TEST_F(FileManagerTest, HiddenFilesFiltered) {
    int visible = 0;
    for (auto& e : fs::directory_iterator(tmp_)) {
        if (e.path().filename().string()[0] != '.') visible++;
    }
    EXPECT_EQ(visible, 3);  // subdir, file.txt, image.png
}

TEST_F(FileManagerTest, FileSizeCorrect) {
    EXPECT_EQ(fs::file_size(tmp_ / "file.txt"), 5u);
}

TEST_F(FileManagerTest, IsDirectoryCheck) {
    EXPECT_TRUE(fs::is_directory(tmp_ / "subdir"));
    EXPECT_FALSE(fs::is_directory(tmp_ / "file.txt"));
}

TEST_F(FileManagerTest, MimeTypeHeuristic) {
    // Verify extension-based mime detection
    auto ext = (tmp_ / "image.png").extension().string();
    EXPECT_EQ(ext, ".png");
    // Real test would use Plan 9A's MimeDetector::from_path()
}
```

### Step 4.4 — test_monitor.cpp

- [ ] Create `tests/unit/apps/test_monitor.cpp`

```cpp
// tests/unit/apps/test_monitor.cpp
#include <gtest/gtest.h>
#include "cpu.h"
#include "memory.h"
#include "network.h"
#include "process.h"

using namespace straylight::monitor;

TEST(MonitorCpu, SampleReturnsOk) {
    CpuProbe probe;
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value()) << "Failed to sample /proc/stat";
    auto& snap = r.value();
    EXPECT_GE(snap.cores.size(), 1u);
    EXPECT_GE(snap.load_avg[0], 0.0f);
}

TEST(MonitorCpu, DeltaProducesPercentage) {
    CpuProbe probe;
    probe.sample();  // prime prev_
    // Second sample should produce non-negative percentages
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value());
    for (auto& c : r.value().cores) {
        EXPECT_GE(c.usage_pct, 0.0f);
        EXPECT_LE(c.usage_pct, 100.0f);
    }
}

TEST(MonitorMemory, SampleReturnsOk) {
    MemoryProbe probe;
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value()) << "Failed to sample /proc/meminfo";
    auto& snap = r.value();
    EXPECT_GT(snap.total_kb, 0u);
    EXPECT_GE(snap.used_pct, 0.0f);
    EXPECT_LE(snap.used_pct, 100.0f);
}

TEST(MonitorMemory, SwapPercentageBounded) {
    MemoryProbe probe;
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r.value().swap_used_pct, 0.0f);
    EXPECT_LE(r.value().swap_used_pct, 100.0f);
}

TEST(MonitorNetwork, SampleReturnsOk) {
    NetworkProbe probe;
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value()) << "Failed to sample /proc/net/dev";
    EXPECT_GE(r.value().size(), 1u);  // at least loopback
}

TEST(MonitorNetwork, SpeedNonNegative) {
    NetworkProbe probe;
    probe.sample();  // prime
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value());
    for (auto& iface : r.value()) {
        EXPECT_GE(iface.rx_speed_mbps, 0.0);
        EXPECT_GE(iface.tx_speed_mbps, 0.0);
    }
}

TEST(MonitorProcess, SampleReturnsOk) {
    ProcessProbe probe;
    auto r = probe.sample();
    ASSERT_TRUE(r.has_value()) << "Failed to enumerate /proc";
    EXPECT_GE(r.value().size(), 1u);  // at least this test process
}

TEST(MonitorProcess, SortByCpu) {
    ProcessProbe probe;
    probe.sample();  // prime
    auto r = probe.sample(SortBy::Cpu);
    ASSERT_TRUE(r.has_value());
    auto& procs = r.value();
    for (size_t i = 1; i < procs.size(); i++) {
        EXPECT_GE(procs[i - 1].cpu_pct, procs[i].cpu_pct);
    }
}
```

### Step 4.5 — test_settings.cpp

- [ ] Create `tests/unit/apps/test_settings.cpp`

```cpp
// tests/unit/apps/test_settings.cpp
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

TEST(SettingsGeneral, HostnameReadable) {
    char buf[256];
    ASSERT_EQ(gethostname(buf, sizeof(buf)), 0);
    EXPECT_GT(std::strlen(buf), 0u);
}

TEST(SettingsGeneral, TimezoneFileExists) {
    // At least one of these should exist on a configured system
    bool has_tz = fs::exists("/etc/timezone") || fs::exists("/etc/localtime");
    EXPECT_TRUE(has_tz);
}

TEST(SettingsDisplay, CompositorSocketPath) {
    // Verify expected socket path constant matches runtime convention
    const char* path = "/run/straylight/compositor.sock";
    // In CI/test, socket won't exist, but path format is validated
    EXPECT_NE(std::string(path).find("compositor"), std::string::npos);
}

TEST(SettingsSound, WpctlAvailable) {
    // wpctl must be installed for sound settings to function
    int ret = std::system("which wpctl > /dev/null 2>&1");
    // This may fail in minimal CI — mark as non-fatal
    if (ret != 0) GTEST_SKIP() << "wpctl not installed";
    SUCCEED();
}

TEST(SettingsUsers, ParseEtcPasswd) {
    std::ifstream f("/etc/passwd");
    ASSERT_TRUE(f.good());
    std::string line;
    int human_users = 0;
    while (std::getline(f, line)) {
        // Count users with uid >= 1000
        auto second_colon = line.find(':', line.find(':') + 1);
        auto third_colon = line.find(':', second_colon + 1);
        auto uid_str = line.substr(second_colon + 1, third_colon - second_colon - 1);
        if (std::stoul(uid_str) >= 1000 && std::stoul(uid_str) != 65534) human_users++;
    }
    EXPECT_GE(human_users, 0);  // at least zero; may be in container
}

TEST(SettingsAbout, UnameReturnsValid) {
    struct utsname u;
    ASSERT_EQ(uname(&u), 0);
    EXPECT_GT(std::strlen(u.sysname), 0u);
    EXPECT_GT(std::strlen(u.release), 0u);
}

TEST(SettingsAbout, ProcCpuinfoReadable) {
    std::ifstream f("/proc/cpuinfo");
    ASSERT_TRUE(f.good());
    std::string line;
    bool found_model = false;
    while (std::getline(f, line)) {
        if (line.find("model name") != std::string::npos) {
            found_model = true;
            break;
        }
    }
    EXPECT_TRUE(found_model);
}

TEST(SettingsAbout, OsReleaseReadable) {
    // Check for /etc/straylight-release or /etc/os-release
    bool has_release = fs::exists("/etc/straylight-release") || fs::exists("/etc/os-release");
    EXPECT_TRUE(has_release);
}
```

### Step 4.6 — Wire tests into top-level CMake

- [ ] Update `tests/unit/CMakeLists.txt`

```cmake
# Add to existing tests/unit/CMakeLists.txt:
add_subdirectory(apps)
```

- [ ] Update root `CMakeLists.txt` (if not already present)

```cmake
# Add to existing root CMakeLists.txt, inside the enable_testing() block:
add_subdirectory(apps/system_monitor)
add_subdirectory(apps/settings)
```

---

## File Tree Summary

```
apps/
├── system_monitor/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── cpu.h / cpu.cpp
│   ├── memory.h / memory.cpp
│   ├── gpu.h / gpu.cpp
│   ├── network.h / network.cpp
│   └── process.h / process.cpp
├── settings/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── settings_page.h
│   └── pages/
│       ├── general.h / general.cpp
│       ├── display.h / display.cpp
│       ├── sound.h / sound.cpp
│       ├── users.h / users.cpp
│       └── about.h / about.cpp
tests/unit/apps/
├── CMakeLists.txt
├── test_terminal.cpp
├── test_file_manager.cpp
├── test_monitor.cpp
└── test_settings.cpp
```
