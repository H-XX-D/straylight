// shell/extensions/builtin_sysmon.cpp
// Built-in shell extension: mini system monitor (CPU/RAM/GPU bars).

#include "extension_api.h"

#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CpuSnapshot {
    long long user = 0, nice = 0, system = 0, idle = 0;
    long long iowait = 0, irq = 0, softirq = 0, steal = 0;

    long long total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
    long long active() const { return total() - idle - iowait; }
};

struct SysmonState {
    SlExtensionContext* ctx = nullptr;

    float cpu_usage = 0.0f;
    float mem_usage = 0.0f;
    float gpu_usage = 0.0f;
    float gpu_mem_usage = 0.0f;
    long long mem_total_mb = 0;
    long long mem_used_mb = 0;

    CpuSnapshot prev_cpu;
    CpuSnapshot curr_cpu;

    float update_interval = 1.0f;
    float time_since_update = 0.0f;

    // History for sparklines
    static constexpr int HISTORY_LEN = 60;
    float cpu_history[HISTORY_LEN]{};
    float mem_history[HISTORY_LEN]{};
    float gpu_history[HISTORY_LEN]{};
    int history_idx = 0;
};

SysmonState g_sysmon;

CpuSnapshot read_cpu_stat() {
    CpuSnapshot snap;
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return snap;

    std::string line;
    if (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string cpu_label;
        ss >> cpu_label >> snap.user >> snap.nice >> snap.system >> snap.idle
           >> snap.iowait >> snap.irq >> snap.softirq >> snap.steal;
    }
    return snap;
}

void read_memory_info() {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return;

    long long total = 0, available = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("MemTotal:") == 0) {
            std::sscanf(line.c_str(), "MemTotal: %lld", &total);
        } else if (line.find("MemAvailable:") == 0) {
            std::sscanf(line.c_str(), "MemAvailable: %lld", &available);
        }
    }

    g_sysmon.mem_total_mb = total / 1024;
    g_sysmon.mem_used_mb = (total - available) / 1024;
    if (total > 0) {
        g_sysmon.mem_usage = static_cast<float>(total - available) / static_cast<float>(total);
    }
}

void read_gpu_usage() {
    // Try NVIDIA nvidia-smi via sysfs
    std::ifstream gpu_util("/sys/class/drm/card0/device/gpu_busy_percent");
    if (gpu_util.is_open()) {
        int pct = 0;
        gpu_util >> pct;
        g_sysmon.gpu_usage = static_cast<float>(pct) / 100.0f;
    } else {
        // Fallback: try NVIDIA specific path
        std::ifstream nv("/proc/driver/nvidia/gpus/0000:01:00.0/information");
        if (nv.is_open()) {
            g_sysmon.gpu_usage = 0.0f; // Can't get utilization from info file
        }
        // On systems without GPU sysfs, report 0
    }

    // GPU memory from AMDGPU/Intel sysfs
    std::ifstream vram_used("/sys/class/drm/card0/device/mem_info_vram_used");
    std::ifstream vram_total("/sys/class/drm/card0/device/mem_info_vram_total");
    if (vram_used.is_open() && vram_total.is_open()) {
        long long used = 0, total = 0;
        vram_used >> used;
        vram_total >> total;
        if (total > 0) {
            g_sysmon.gpu_mem_usage = static_cast<float>(used) / static_cast<float>(total);
        }
    }
}

void update_readings() {
    // CPU
    g_sysmon.curr_cpu = read_cpu_stat();
    long long total_diff = g_sysmon.curr_cpu.total() - g_sysmon.prev_cpu.total();
    long long active_diff = g_sysmon.curr_cpu.active() - g_sysmon.prev_cpu.active();
    if (total_diff > 0) {
        g_sysmon.cpu_usage = static_cast<float>(active_diff) / static_cast<float>(total_diff);
    }
    g_sysmon.prev_cpu = g_sysmon.curr_cpu;

    // Memory
    read_memory_info();

    // GPU
    read_gpu_usage();

    // Update history
    int idx = g_sysmon.history_idx % SysmonState::HISTORY_LEN;
    g_sysmon.cpu_history[idx] = g_sysmon.cpu_usage;
    g_sysmon.mem_history[idx] = g_sysmon.mem_usage;
    g_sysmon.gpu_history[idx] = g_sysmon.gpu_usage;
    g_sysmon.history_idx++;
}

ImVec4 usage_color(float usage) {
    if (usage >= 0.9f) return ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    if (usage >= 0.7f) return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    return ImVec4(0.0f, 0.9f, 0.6f, 1.0f);
}

void draw_usage_bar(const char* label, float usage, const char* detail) {
    ImGui::Text("%s", label);
    ImGui::SameLine(80.0f);

    ImVec4 color = usage_color(usage);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%.0f%%", usage * 100.0f);
    ImGui::ProgressBar(usage, ImVec2(-1.0f, 18.0f), overlay);
    ImGui::PopStyleColor();

    if (detail && detail[0]) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), " %s", detail);
    }
}

} // anonymous namespace

extern "C" {

SlExtensionInfo sl_extension_info() {
    SlExtensionInfo info{};
    info.api_version = SL_EXTENSION_API_VERSION;
    std::strncpy(info.name, "sysmon", SL_EXT_NAME_MAX - 1);
    std::strncpy(info.version, "1.0.0", SL_EXT_VERSION_MAX - 1);
    std::strncpy(info.author, "StrayLight OS", SL_EXT_AUTHOR_MAX - 1);
    std::strncpy(info.description,
                 "Mini system monitor - CPU/RAM/GPU usage bars",
                 SL_EXT_DESCRIPTION_MAX - 1);
    return info;
}

int sl_extension_init(SlExtensionContext* ctx) {
    g_sysmon = SysmonState{};
    g_sysmon.ctx = ctx;
    g_sysmon.prev_cpu = read_cpu_stat();
    update_readings();

    if (ctx && ctx->log_info) {
        ctx->log_info("Sysmon extension initialized");
    }
    return 0;
}

void sl_extension_render(float dt) {
    g_sysmon.time_since_update += dt;
    if (g_sysmon.time_since_update >= g_sysmon.update_interval) {
        g_sysmon.time_since_update = 0.0f;
        update_readings();
    }

    ImGui::SetNextWindowSize(ImVec2(280.0f, 200.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("System Monitor", nullptr, ImGuiWindowFlags_NoCollapse)) {
        // CPU bar
        char cpu_detail[32];
        std::snprintf(cpu_detail, sizeof(cpu_detail), "");
        draw_usage_bar("CPU", g_sysmon.cpu_usage, cpu_detail);

        // RAM bar
        char mem_detail[64];
        std::snprintf(mem_detail, sizeof(mem_detail), "%lld / %lld MB",
                      g_sysmon.mem_used_mb, g_sysmon.mem_total_mb);
        draw_usage_bar("RAM", g_sysmon.mem_usage, mem_detail);

        // GPU bar
        char gpu_detail[32];
        std::snprintf(gpu_detail, sizeof(gpu_detail), "");
        draw_usage_bar("GPU", g_sysmon.gpu_usage, gpu_detail);

        // GPU VRAM bar (if available)
        if (g_sysmon.gpu_mem_usage > 0.0f) {
            draw_usage_bar("VRAM", g_sysmon.gpu_mem_usage, "");
        }

        ImGui::Spacing();

        // Sparkline graphs
        if (ImGui::CollapsingHeader("History (60s)")) {
            int len = std::min(g_sysmon.history_idx, SysmonState::HISTORY_LEN);
            int offset = g_sysmon.history_idx % SysmonState::HISTORY_LEN;

            // Reorder history for plot
            float ordered_cpu[SysmonState::HISTORY_LEN]{};
            float ordered_mem[SysmonState::HISTORY_LEN]{};
            float ordered_gpu[SysmonState::HISTORY_LEN]{};
            for (int i = 0; i < len; ++i) {
                int src = (offset - len + i + SysmonState::HISTORY_LEN) % SysmonState::HISTORY_LEN;
                ordered_cpu[i] = g_sysmon.cpu_history[src];
                ordered_mem[i] = g_sysmon.mem_history[src];
                ordered_gpu[i] = g_sysmon.gpu_history[src];
            }

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.0f, 0.9f, 0.6f, 1.0f));
            ImGui::PlotLines("CPU", ordered_cpu, len, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
            ImGui::PlotLines("RAM", ordered_mem, len, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            ImGui::PlotLines("GPU", ordered_gpu, len, 0, nullptr, 0.0f, 1.0f, ImVec2(0, 40));
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
}

void sl_extension_shutdown() {
    if (g_sysmon.ctx && g_sysmon.ctx->log_info) {
        g_sysmon.ctx->log_info("Sysmon extension shut down");
    }
    g_sysmon = SysmonState{};
}

} // extern "C"
