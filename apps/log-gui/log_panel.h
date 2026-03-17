// apps/log-gui/log_panel.h
// StrayLight Log Viewer panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace straylight::logview {

struct LogEntry {
    char timestamp[32];
    char service[32];
    int  level;  // 0=debug, 1=info, 2=warn, 3=error, 4=critical
    char message[256];
};

struct ServiceErrorCount {
    char name[32];
    int  errors;
};

struct LogState {
    std::vector<LogEntry> entries;
    std::vector<LogEntry> filtered;
    std::vector<ServiceErrorCount> error_stats;

    int  filter_service = 0;
    int  filter_level = 0;
    char search_text[256] = {};
    bool follow_mode = true;
    bool auto_scroll = true;
    float sim_timer = 0;

    static constexpr const char* services[] = {
        "All", "compositor", "networking", "audio", "session",
        "vault", "scheduler", "kernel", "display"
    };
    static constexpr int num_services = 9;

    static constexpr const char* levels[] = {
        "All", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"
    };
    static constexpr int num_levels = 6;

    static constexpr const char* level_names[] = { "DEBUG", "INFO", "WARN", "ERROR", "CRIT" };

    void init() {
        entries.push_back({"2026-03-16 10:00:01", "compositor", 1, "Frame rendered in 6.2ms"});
        entries.push_back({"2026-03-16 10:00:01", "networking", 1, "WiFi link stable, signal -42dBm"});
        entries.push_back({"2026-03-16 10:00:02", "audio", 1, "PipeWire graph cycle: 1024 samples"});
        entries.push_back({"2026-03-16 10:00:03", "session", 2, "User idle timeout approaching (4m)"});
        entries.push_back({"2026-03-16 10:00:04", "vault", 3, "Failed auth attempt from 10.0.0.5"});
        entries.push_back({"2026-03-16 10:00:05", "scheduler", 1, "Cron job backup.daily completed"});
        entries.push_back({"2026-03-16 10:00:06", "kernel", 0, "Page fault handled: 0x7fff2a3b"});
        entries.push_back({"2026-03-16 10:00:07", "networking", 3, "DNS resolution timeout for api.example.com"});
        entries.push_back({"2026-03-16 10:00:08", "display", 1, "VRR active on DP-1 @ 144Hz"});
        entries.push_back({"2026-03-16 10:00:09", "compositor", 2, "GPU memory usage at 78%"});
        entries.push_back({"2026-03-16 10:00:10", "audio", 3, "Buffer underrun on output sink"});
        entries.push_back({"2026-03-16 10:00:11", "kernel", 4, "OOM killer triggered: process 4521"});
        entries.push_back({"2026-03-16 10:00:12", "session", 1, "Screen locked after idle timeout"});
        entries.push_back({"2026-03-16 10:00:13", "vault", 1, "Secret rotated: system/api/token"});
        entries.push_back({"2026-03-16 10:00:14", "networking", 1, "VPN tunnel established to 10.8.0.1"});

        apply_filter();
        compute_stats();
    }

    void apply_filter() {
        filtered.clear();
        std::string search(search_text);
        for (auto& e : entries) {
            if (filter_service > 0 && strcmp(e.service, services[filter_service]) != 0) continue;
            if (filter_level > 0 && e.level < (filter_level - 1)) continue;
            if (!search.empty()) {
                std::string msg(e.message);
                if (msg.find(search) == std::string::npos &&
                    std::string(e.service).find(search) == std::string::npos) continue;
            }
            filtered.push_back(e);
        }
    }

    void compute_stats() {
        error_stats.clear();
        // Count errors per service
        for (int s = 1; s < num_services; ++s) {
            ServiceErrorCount sc{};
            snprintf(sc.name, 32, "%s", services[s]);
            sc.errors = 0;
            for (auto& e : entries) {
                if (strcmp(e.service, services[s]) == 0 && e.level >= 3) sc.errors++;
            }
            error_stats.push_back(sc);
        }
    }
};

inline void render_log_panel(LogState& st) {
    if (st.entries.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    // Simulate new log entries
    st.sim_timer += ImGui::GetIO().DeltaTime;
    if (st.follow_mode && st.sim_timer > 2.0f) {
        st.sim_timer = 0;
        LogEntry ne;
        int svc = 1 + (rand() % (LogState::num_services - 1));
        snprintf(ne.timestamp, 32, "2026-03-16 10:00:%02d", 15 + (int)st.entries.size());
        snprintf(ne.service, 32, "%s", LogState::services[svc]);
        ne.level = rand() % 5;
        const char* msgs[] = {
            "Heartbeat OK", "Processing request", "Warning: high latency",
            "Error: connection refused", "Critical: service unresponsive"
        };
        snprintf(ne.message, 256, "%s", msgs[ne.level]);
        st.entries.push_back(ne);
        st.apply_filter();
        st.compute_stats();
        st.auto_scroll = true;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("LOG VIEWER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Filter bar
    ImGui::Text("Service:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::Combo("##svc_filter", &st.filter_service, LogState::services, LogState::num_services)) {
        st.apply_filter();
    }
    ImGui::SameLine(230);
    ImGui::Text("Level:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("##lvl_filter", &st.filter_level, LogState::levels, LogState::num_levels)) {
        st.apply_filter();
    }
    ImGui::SameLine(440);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputTextWithHint("##search", "Search...", st.search_text, sizeof(st.search_text))) {
        st.apply_filter();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &st.follow_mode);
    ImGui::SameLine();
    ImGui::Text("(%zu entries)", st.filtered.size());
    ImGui::Spacing();

    float stats_w = 250.0f;

    // Log view
    ImGui::BeginChild("##log_view", ImVec2(ImGui::GetContentRegionAvail().x - stats_w - 8, 0), true);

    ImVec4 level_colors[] = {
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),  // DEBUG - gray
        ImVec4(0.7f, 0.9f, 1.0f, 1.0f),  // INFO - light blue
        ImVec4(1.0f, 0.85f, 0.0f, 1.0f), // WARN - yellow
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f),  // ERROR - red
        ImVec4(1.0f, 0.0f, 0.5f, 1.0f),  // CRITICAL - magenta
    };

    ImGuiListClipper clipper;
    clipper.Begin((int)st.filtered.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            auto& e = st.filtered[i];
            ImVec4 col = level_colors[std::clamp(e.level, 0, 4)];

            ImGui::TextDisabled("%s", e.timestamp);
            ImGui::SameLine(170);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::Text("[%-5s]", LogState::level_names[std::clamp(e.level, 0, 4)]);
            ImGui::PopStyleColor();
            ImGui::SameLine(240);
            ImGui::TextColored(accent, "%-12s", e.service);
            ImGui::SameLine(350);
            ImGui::TextWrapped("%s", e.message);
        }
    }

    if (st.auto_scroll && st.follow_mode) {
        ImGui::SetScrollHereY(1.0f);
        st.auto_scroll = false;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Statistics panel
    ImGui::BeginChild("##stats", ImVec2(stats_w, 0), true);
    ImGui::TextColored(accent, "Error Statistics");
    ImGui::Separator();
    ImGui::Spacing();

    int max_errors = 1;
    for (auto& s : st.error_stats) max_errors = std::max(max_errors, s.errors);

    for (auto& s : st.error_stats) {
        ImGui::Text("%-12s", s.name);
        ImGui::SameLine(110);

        float frac = (float)s.errors / (float)max_errors;
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        float bar_w = ImGui::GetContentRegionAvail().x - 30;
        float bar_h = 14;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w, bar_pos.y + bar_h),
                            IM_COL32(30, 30, 50, 255), 2.0f);
        ImU32 bar_col = s.errors > 2 ? IM_COL32(255, 60, 60, 255) :
                        s.errors > 0 ? IM_COL32(255, 200, 0, 255) : IM_COL32(0, 255, 136, 255);
        draw->AddRectFilled(bar_pos, ImVec2(bar_pos.x + frac * bar_w, bar_pos.y + bar_h),
                            bar_col, 2.0f);
        ImGui::Dummy(ImVec2(bar_w, bar_h));
        ImGui::SameLine();
        ImGui::Text("%d", s.errors);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Total entries: %zu", st.entries.size());
    ImGui::Text("Filtered: %zu", st.filtered.size());
    int total_errors = 0;
    for (auto& e : st.entries) if (e.level >= 3) total_errors++;
    ImGui::Text("Total errors: %d", total_errors);

    ImGui::EndChild();
}

} // namespace straylight::logview
