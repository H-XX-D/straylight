// apps/health-gui/health_panel.h
// StrayLight Health GUI — System Health Dashboard panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::health {

struct HealthCheck {
    std::string name;
    std::string icon;   // text icon
    float       score;  // 0-100
    int         status; // 0=ok, 1=warn, 2=critical
    std::string detail;
    std::string category;
};

struct Alert {
    std::string message;
    std::string timestamp;
    int         severity; // 0=info, 1=warn, 2=critical
};

struct HealthState {
    float overall_score = 0.0f;
    std::vector<HealthCheck> checks;
    std::vector<Alert> alerts;
    float score_history[60] = {};
    int   history_offset = 0;
    bool  generating_report = false;
    float report_progress = 0.0f;

    void init() {
        checks.push_back({"CPU Temperature", "[T]", 92.0f, 0, "62C - Within normal operating range", "Hardware"});
        checks.push_back({"CPU Utilization", "[C]", 85.0f, 0, "Average 23% across 32 threads", "Hardware"});
        checks.push_back({"Memory Usage", "[M]", 78.0f, 0, "48.2 GB / 128 GB (37.6%) used", "Hardware"});
        checks.push_back({"GPU Temperature", "[G]", 88.0f, 0, "58C - Nominal under idle load", "Hardware"});
        checks.push_back({"GPU VRAM", "[V]", 70.0f, 1, "18.2 GB / 24 GB (75.8%) allocated", "Hardware"});
        checks.push_back({"Disk Usage (root)", "[D]", 82.0f, 0, "420 GB / 2 TB (21%) used", "Storage"});
        checks.push_back({"Disk Health (SMART)", "[S]", 95.0f, 0, "No errors, 98% life remaining", "Storage"});
        checks.push_back({"Network Latency", "[N]", 90.0f, 0, "Gateway RTT: 0.8ms", "Network"});
        checks.push_back({"DNS Resolution", "[R]", 95.0f, 0, "Resolving in < 5ms", "Network"});
        checks.push_back({"Firewall Status", "[F]", 100.0f, 0, "Active with 12 rules loaded", "Security"});
        checks.push_back({"Service Health", "[H]", 72.0f, 1, "2 of 47 services degraded", "Services"});
        checks.push_back({"Kernel Errors", "[K]", 60.0f, 1, "3 non-critical warnings in dmesg", "System"});
        checks.push_back({"Swap Usage", "[W]", 98.0f, 0, "0 MB / 16 GB used", "System"});
        checks.push_back({"Uptime", "[U]", 85.0f, 0, "6 days, 4 hours, 22 minutes", "System"});
        checks.push_back({"Package Updates", "[P]", 55.0f, 1, "14 security updates available", "System"});
        checks.push_back({"Backup Status", "[B]", 40.0f, 2, "Last backup: 8 days ago (overdue)", "System"});

        // Calculate overall score
        float total = 0;
        for (auto& c : checks) total += c.score;
        overall_score = total / (float)checks.size();

        // Alerts
        alerts.push_back({"Backup overdue: last backup was 8 days ago", "2026-03-16 10:00", 2});
        alerts.push_back({"14 security updates available", "2026-03-16 09:00", 1});
        alerts.push_back({"GPU VRAM usage above 75%", "2026-03-16 10:05", 1});
        alerts.push_back({"Service 'ml-scheduler' degraded", "2026-03-16 08:30", 1});
        alerts.push_back({"3 kernel warnings in last 24h", "2026-03-16 07:15", 1});
        alerts.push_back({"System boot completed successfully", "2026-03-10 10:00", 0});

        // History (simulate declining score)
        for (int i = 0; i < 60; ++i) {
            score_history[i] = 82.0f + 5.0f * sinf(i * 0.2f) - (float)i * 0.05f;
        }
    }
};

inline void render_health_panel(HealthState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT HEALTH");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Top section: Overall gauge + alerts
    float gauge_section_h = 220.0f;
    if (ImGui::BeginChild("##gauge_section", ImVec2(0, gauge_section_h), false)) {
        float gauge_w = 250.0f;

        // Circular gauge (drawn with ImDrawList)
        if (ImGui::BeginChild("##gauge", ImVec2(gauge_w, -1), false)) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 center = ImGui::GetCursorScreenPos();
            center.x += gauge_w * 0.5f;
            center.y += 100.0f;
            float radius = 80.0f;

            // Background arc
            for (float a = 3.14159f; a < 3.14159f * 2.0f; a += 0.02f) {
                float x1 = center.x + cosf(a) * radius;
                float y1 = center.y + sinf(a) * radius;
                float x2 = center.x + cosf(a) * (radius - 12.0f);
                float y2 = center.y + sinf(a) * (radius - 12.0f);
                draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(40, 40, 60, 255), 2.0f);
            }

            // Score arc
            float score_frac = st.overall_score / 100.0f;
            float score_angle = 3.14159f + 3.14159f * score_frac;
            ImU32 score_col = st.overall_score >= 80 ? IM_COL32(0, 200, 130, 255)
                            : st.overall_score >= 60 ? IM_COL32(200, 180, 40, 255)
                            : IM_COL32(200, 60, 60, 255);
            for (float a = 3.14159f; a < score_angle; a += 0.02f) {
                float x1 = center.x + cosf(a) * radius;
                float y1 = center.y + sinf(a) * radius;
                float x2 = center.x + cosf(a) * (radius - 12.0f);
                float y2 = center.y + sinf(a) * (radius - 12.0f);
                draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), score_col, 3.0f);
            }

            // Score text
            char score_text[16];
            snprintf(score_text, sizeof(score_text), "%.0f", st.overall_score);
            ImVec2 text_size = ImGui::CalcTextSize(score_text);
            draw->AddText(nullptr, 36.0f, ImVec2(center.x - text_size.x * 1.5f, center.y - 25.0f),
                          score_col, score_text);
            draw->AddText(ImVec2(center.x - 30, center.y + 15.0f), IM_COL32(160, 160, 160, 255), "Health Score");

            ImGui::Dummy(ImVec2(0, 200));
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Score history sparkline
        if (ImGui::BeginChild("##history_section", ImVec2(300, -1), false)) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Health Over Time");
            ImGui::Spacing();
            ImGui::PlotLines("##history", st.score_history, 60, st.history_offset,
                             nullptr, 0.0f, 100.0f, ImVec2(-1, 120));
            ImGui::TextDisabled("Last 60 readings");

            // Update history
            st.score_history[st.history_offset] = st.overall_score + (float)((int)(ImGui::GetTime() * 10) % 5) - 2.0f;
            st.history_offset = (st.history_offset + 1) % 60;
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Alert list
        if (ImGui::BeginChild("##alerts", ImVec2(0, -1), true)) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Alerts");
            ImGui::Separator();
            for (auto& a : st.alerts) {
                ImVec4 col = a.severity == 2 ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                           : a.severity == 1 ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                           : ImVec4(0.4f, 0.7f, 0.4f, 1.0f);
                const char* sev_str = a.severity == 2 ? "[CRIT]" : a.severity == 1 ? "[WARN]" : "[INFO]";
                ImGui::TextColored(col, "%s", sev_str);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", a.message.c_str());
                ImGui::TextDisabled("  %s", a.timestamp.c_str());
                ImGui::Spacing();
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // Generate Report button
    ImGui::Spacing();
    if (st.generating_report) {
        ImGui::BeginDisabled();
        ImGui::Button("Generating Report...", ImVec2(180, 30));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::ProgressBar(st.report_progress, ImVec2(200, 30));
        st.report_progress += ImGui::GetIO().DeltaTime * 0.2f;
        if (st.report_progress >= 1.0f) { st.generating_report = false; st.report_progress = 0.0f; }
    } else {
        if (ImGui::Button("Generate Report", ImVec2(160, 30))) {
            st.generating_report = true;
            st.report_progress = 0.0f;
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Per-check cards
    if (ImGui::BeginChild("##check_cards", ImVec2(0, -1), false)) {
        std::string current_cat;
        for (auto& c : st.checks) {
            if (c.category != current_cat) {
                current_cat = c.category;
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "%s", current_cat.c_str());
                ImGui::Separator();
            }

            ImVec4 status_col = c.status == 0 ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f)
                              : c.status == 1 ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                              : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            const char* status_str = c.status == 0 ? "OK" : c.status == 1 ? "WARN" : "CRIT";

            // Icon
            ImGui::TextColored(status_col, "%s", c.icon.c_str());
            ImGui::SameLine();

            // Name
            ImGui::Text("%s", c.name.c_str());
            ImGui::SameLine(220);

            // Score bar
            float frac = c.score / 100.0f;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, status_col);
            char score_label[32];
            snprintf(score_label, sizeof(score_label), "%.0f/100", c.score);
            ImGui::ProgressBar(frac, ImVec2(200, 18), score_label);
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextColored(status_col, "[%s]", status_str);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", c.detail.c_str());
        }
    }
    ImGui::EndChild();
}

} // namespace straylight::health
