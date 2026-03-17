// apps/replay-gui/timeline_panel.h
// StrayLight Replay GUI — Timeline and event viewer panel
#pragma once

#include "event_detail.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace straylight::replay {

struct TimelineState {
    std::vector<SystemEvent> events;
    std::vector<SystemEvent> filtered_events;
    int selected_index = -1;

    // Filters
    bool filter_process = true;
    bool filter_network = true;
    bool filter_filesystem = true;
    bool filter_crash = true;
    bool filter_security = true;
    bool filter_system = true;
    char filter_pid[32] = {};
    char filter_time_start[32] = {};
    char filter_time_end[32] = {};
    char search_text[256] = {};

    // Live mode
    bool live_mode = false;
    float live_timer = 0.0f;
    int next_event_id = 100;

    // Crash analysis
    bool crash_analysis = false;

    void init() {
        events.push_back({1, "2026-03-16 10:00:01.234", "process", "info", 1, "systemd",
            "System boot completed",
            "{\n  \"event\": \"boot_complete\",\n  \"uptime_ms\": 4521,\n  \"services_started\": 47\n}",
            {2, 3}});
        events.push_back({2, "2026-03-16 10:00:01.456", "system", "info", 1, "systemd",
            "All target units reached",
            "{\n  \"target\": \"multi-user.target\",\n  \"state\": \"active\"\n}",
            {1}});
        events.push_back({3, "2026-03-16 10:00:02.100", "network", "info", 850, "networkd",
            "Network interface eth0 configured via DHCP",
            "{\n  \"interface\": \"eth0\",\n  \"ip\": \"192.168.1.100\",\n  \"gateway\": \"192.168.1.1\",\n  \"dns\": [\"8.8.8.8\"]\n}",
            {1}});
        events.push_back({4, "2026-03-16 10:01:15.789", "filesystem", "info", 1200, "btrfs-mount",
            "NVMe root filesystem mounted",
            "{\n  \"device\": \"/dev/nvme0n1p2\",\n  \"mount\": \"/\",\n  \"fs\": \"btrfs\",\n  \"options\": \"compress=zstd:3\"\n}",
            {}});
        events.push_back({5, "2026-03-16 10:05:30.001", "security", "warning", 2100, "shield-audit",
            "Open port 22 detected without fail2ban",
            "{\n  \"check\": \"ssh_protection\",\n  \"port\": 22,\n  \"recommendation\": \"enable fail2ban or ufw rule\"\n}",
            {}});
        events.push_back({6, "2026-03-16 10:10:45.123", "process", "info", 3500, "straylight-compositor",
            "Wayland compositor started on GPU 0",
            "{\n  \"gpu\": \"NVIDIA RTX 4090\",\n  \"drm_device\": \"/dev/dri/card0\",\n  \"resolution\": \"3840x2160\"\n}",
            {}});
        events.push_back({7, "2026-03-16 10:15:00.000", "process", "error", 4200, "ml-inference",
            "CUDA out of memory during model load",
            "{\n  \"model\": \"llama-70b\",\n  \"required_vram\": 42000,\n  \"available_vram\": 24000,\n  \"error\": \"CUDA_ERROR_OUT_OF_MEMORY\"\n}",
            {8}});
        events.push_back({8, "2026-03-16 10:15:00.050", "crash", "critical", 4200, "ml-inference",
            "Process terminated with SIGSEGV after OOM",
            "{\n  \"signal\": \"SIGSEGV\",\n  \"address\": \"0x7f3a4c000000\",\n  \"backtrace\": [\n    \"libcuda.so+0x1a2b3c\",\n    \"ml-inference+0x45678\",\n    \"libc.so+0x29d90\"\n  ],\n  \"core_dump\": \"/var/crash/ml-inference.4200.core\"\n}",
            {7}});
        events.push_back({9, "2026-03-16 10:20:00.000", "network", "warning", 850, "networkd",
            "High packet loss detected on eth0",
            "{\n  \"interface\": \"eth0\",\n  \"loss_percent\": 2.3,\n  \"rtt_avg_ms\": 45.2\n}",
            {}});
        events.push_back({10, "2026-03-16 10:25:00.000", "filesystem", "warning", 1200, "btrfs-scrub",
            "Data checksum mismatch detected and corrected",
            "{\n  \"device\": \"/dev/nvme0n1p2\",\n  \"errors_corrected\": 1,\n  \"errors_uncorrectable\": 0\n}",
            {}});

        apply_filter();
    }

    void apply_filter() {
        filtered_events.clear();
        std::string search(search_text);
        std::string pid_filter(filter_pid);

        for (auto& e : events) {
            // Type filter
            if (e.type == "process" && !filter_process) continue;
            if (e.type == "network" && !filter_network) continue;
            if (e.type == "filesystem" && !filter_filesystem) continue;
            if (e.type == "crash" && !filter_crash) continue;
            if (e.type == "security" && !filter_security) continue;
            if (e.type == "system" && !filter_system) continue;

            // PID filter
            if (!pid_filter.empty()) {
                char pid_str[32];
                snprintf(pid_str, sizeof(pid_str), "%d", e.pid);
                if (std::string(pid_str) != pid_filter) continue;
            }

            // Search filter
            if (!search.empty()) {
                if (e.summary.find(search) == std::string::npos &&
                    e.json_payload.find(search) == std::string::npos &&
                    e.process_name.find(search) == std::string::npos) continue;
            }

            // Crash analysis mode: only show errors/crashes
            if (crash_analysis && e.severity != "error" && e.severity != "critical") continue;

            filtered_events.push_back(e);
        }
    }

    void add_live_event() {
        const char* types[] = {"process", "network", "filesystem", "system"};
        const char* sevs[] = {"info", "info", "warning", "info"};
        const char* summaries[] = {
            "Periodic health check completed",
            "TCP connection established to 10.0.0.5:443",
            "Temporary file cleanup: removed 23 files",
            "CPU temperature: 62C (nominal)"
        };
        int idx = next_event_id % 4;
        SystemEvent e;
        e.id = next_event_id++;
        e.timestamp = "2026-03-16 10:30:00.000";
        e.type = types[idx];
        e.severity = sevs[idx];
        e.pid = 1000 + (next_event_id % 20);
        e.process_name = idx == 0 ? "healthd" : idx == 1 ? "networkd" : idx == 2 ? "cleanup" : "tempmon";
        e.summary = summaries[idx];
        e.json_payload = "{\n  \"auto_generated\": true\n}";
        events.push_back(e);
        apply_filter();
    }
};

inline void render_timeline_panel(TimelineState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT REPLAY");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Filter bar
    ImGui::Text("Filters:");
    ImGui::SameLine();
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.7f, 0.5f, 1.0f, 1.0f));
    changed |= ImGui::Checkbox("Process", &st.filter_process);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
    changed |= ImGui::Checkbox("Network", &st.filter_network);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.2f, 0.8f, 0.5f, 1.0f));
    changed |= ImGui::Checkbox("Filesystem", &st.filter_filesystem);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    changed |= ImGui::Checkbox("Crash", &st.filter_crash);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    changed |= ImGui::Checkbox("Security", &st.filter_security);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.6f, 0.6f, 0.8f, 1.0f));
    changed |= ImGui::Checkbox("System", &st.filter_system);
    ImGui::PopStyleColor();

    // PID filter
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    changed |= ImGui::InputTextWithHint("##pid_filter", "PID", st.filter_pid, sizeof(st.filter_pid));

    // Search
    ImGui::SetNextItemWidth(300);
    changed |= ImGui::InputTextWithHint("##search", "Search events...", st.search_text, sizeof(st.search_text));
    ImGui::SameLine();

    // Crash analysis toggle
    ImGui::PushStyleColor(ImGuiCol_Button, st.crash_analysis ? ImVec4(0.7f, 0.1f, 0.1f, 0.8f) : ImVec4(0.0f, 0.55f, 0.38f, 0.8f));
    if (ImGui::Button(st.crash_analysis ? "Crash Analysis: ON" : "Crash Analysis", ImVec2(160, 0))) {
        st.crash_analysis = !st.crash_analysis;
        changed = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Live mode toggle
    ImGui::PushStyleColor(ImGuiCol_Button, st.live_mode ? ImVec4(0.8f, 0.4f, 0.0f, 0.8f) : ImVec4(0.0f, 0.55f, 0.38f, 0.8f));
    if (ImGui::Button(st.live_mode ? "Live: ON" : "Live Mode", ImVec2(100, 0))) {
        st.live_mode = !st.live_mode;
    }
    ImGui::PopStyleColor();

    if (changed) st.apply_filter();

    // Live event injection
    if (st.live_mode) {
        st.live_timer += ImGui::GetIO().DeltaTime;
        if (st.live_timer > 2.0f) {
            st.live_timer = 0.0f;
            st.add_live_event();
        }
    }

    ImGui::Spacing();

    // Timeline visualization
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = 50.0f;

        draw->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + h), IM_COL32(15, 15, 25, 255));
        float y_center = p.y + h * 0.5f;
        draw->AddLine(ImVec2(p.x + 10, y_center), ImVec2(p.x + w - 10, y_center), IM_COL32(60, 60, 100, 200), 1.0f);

        int n = (int)st.filtered_events.size();
        for (int i = 0; i < n && i < 200; ++i) {
            float x = p.x + 10 + (w - 20) * (float)i / (float)std::max(n - 1, 1);
            ImU32 col = type_color_u32(st.filtered_events[i].type);
            float r = (i == st.selected_index) ? 6.0f : 4.0f;
            draw->AddCircleFilled(ImVec2(x, y_center), r, col);
            if (st.filtered_events[i].severity == "critical" || st.filtered_events[i].severity == "error") {
                draw->AddCircle(ImVec2(x, y_center), r + 3.0f, IM_COL32(255, 80, 80, 180), 0, 1.5f);
            }
        }
        ImGui::Dummy(ImVec2(0, h));
    }

    ImGui::Spacing();

    // Event list + detail split
    float left_w = ImGui::GetContentRegionAvail().x * 0.5f;
    if (ImGui::BeginChild("##event_list", ImVec2(left_w, -1), true)) {
        ImGui::TextDisabled("%zu events", st.filtered_events.size());
        ImGui::Separator();

        for (int i = 0; i < (int)st.filtered_events.size(); ++i) {
            auto& e = st.filtered_events[i];
            ImGui::PushID(i);

            bool sel = (i == st.selected_index);
            ImVec4 sev_col = severity_color(e.severity);

            // Severity indicator
            ImGui::TextColored(sev_col, "[%s]", e.severity.c_str());
            ImGui::SameLine();

            char label[256];
            snprintf(label, sizeof(label), "%s | %s [%d] %s##ev%d",
                     e.timestamp.c_str(), e.type.c_str(), e.pid, e.summary.c_str(), e.id);
            if (ImGui::Selectable(label, sel)) {
                st.selected_index = i;
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Detail panel
    if (ImGui::BeginChild("##event_detail", ImVec2(0, -1), true)) {
        if (st.selected_index >= 0 && st.selected_index < (int)st.filtered_events.size()) {
            render_event_detail(st.filtered_events[st.selected_index]);
        } else {
            ImGui::TextDisabled("Select an event to view details");
        }
    }
    ImGui::EndChild();
}

} // namespace straylight::replay
