// apps/display-gui/display_panel.h
// StrayLight Display Settings panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace straylight::display {

struct MonitorInfo {
    char name[64];
    float x, y;          // position in layout (normalized 0-1)
    float w, h;          // size in layout
    int   res_index;     // current resolution index
    int   rate_index;    // current refresh rate index
    bool  primary;
    bool  dragging;
};

struct DisplayState {
    std::vector<MonitorInfo> monitors;
    int selected_monitor = 0;

    float color_temp = 6500.0f;  // Kelvin
    bool  night_mode = false;

    int   profile_index = 0;
    char  profile_name[128] = {};
    bool  show_save_dialog = false;

    static constexpr const char* resolutions[] = {
        "3840x2160", "2560x1440", "1920x1080", "1680x1050",
        "1600x900", "1440x900", "1366x768", "1280x720"
    };
    static constexpr int num_resolutions = 8;

    static constexpr const char* refresh_rates[] = {
        "144 Hz", "120 Hz", "90 Hz", "60 Hz", "50 Hz", "30 Hz"
    };
    static constexpr int num_rates = 6;

    static constexpr const char* profiles[] = {
        "Default", "Dual Landscape", "Portrait + Landscape", "Presentation"
    };
    static constexpr int num_profiles = 4;

    void init() {
        MonitorInfo m1{};
        snprintf(m1.name, sizeof(m1.name), "DP-1 (Primary)");
        m1.x = 0.05f; m1.y = 0.15f; m1.w = 0.4f; m1.h = 0.55f;
        m1.res_index = 2; m1.rate_index = 3; m1.primary = true; m1.dragging = false;
        monitors.push_back(m1);

        MonitorInfo m2{};
        snprintf(m2.name, sizeof(m2.name), "HDMI-1");
        m2.x = 0.5f; m2.y = 0.2f; m2.w = 0.35f; m2.h = 0.45f;
        m2.res_index = 2; m2.rate_index = 3; m2.primary = false; m2.dragging = false;
        monitors.push_back(m2);
    }
};

inline void render_display_panel(DisplayState& st) {
    if (st.monitors.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("DISPLAY SETTINGS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Profile bar
    ImGui::Text("Profile:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    ImGui::Combo("##profile", &st.profile_index, DisplayState::profiles, DisplayState::num_profiles);
    ImGui::SameLine();
    if (ImGui::Button("Save Profile")) {
        st.show_save_dialog = true;
        memset(st.profile_name, 0, sizeof(st.profile_name));
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Profile")) {
        // Simulate load
    }
    ImGui::Spacing();

    // Monitor layout area
    float layout_h = 260.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 layout_pos = ImGui::GetCursorScreenPos();
    ImVec2 layout_size(avail.x, layout_h);

    ImGui::BeginChild("##layout", ImVec2(avail.x, layout_h), true);
    ImGui::TextDisabled("Monitor Layout (drag to reposition)");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float canvas_w = avail.x - 24.0f;
    float canvas_h = layout_h - 40.0f;

    // Draw grid
    for (int i = 0; i <= 10; ++i) {
        float x = origin.x + (canvas_w * i / 10.0f);
        float y = origin.y + (canvas_h * i / 10.0f);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + canvas_h),
                      IM_COL32(40, 40, 60, 100));
        if (i <= 10)
            draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + canvas_w, y),
                          IM_COL32(40, 40, 60, 100));
    }

    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < (int)st.monitors.size(); ++i) {
        auto& m = st.monitors[i];
        ImVec2 tl(origin.x + m.x * canvas_w, origin.y + m.y * canvas_h);
        ImVec2 br(tl.x + m.w * canvas_w, tl.y + m.h * canvas_h);

        bool hovered = io.MousePos.x >= tl.x && io.MousePos.x <= br.x &&
                       io.MousePos.y >= tl.y && io.MousePos.y <= br.y;

        ImU32 fill = (i == st.selected_monitor)
            ? IM_COL32(0, 100, 55, 180) : IM_COL32(30, 30, 50, 200);
        ImU32 border = hovered ? IM_COL32(0, 255, 136, 255) : IM_COL32(80, 80, 120, 255);

        draw->AddRectFilled(tl, br, fill, 4.0f);
        draw->AddRect(tl, br, border, 4.0f, 0, 2.0f);

        // Label
        char label[128];
        snprintf(label, sizeof(label), "%s\n%s @ %s",
                 m.name, DisplayState::resolutions[m.res_index],
                 DisplayState::refresh_rates[m.rate_index]);
        ImVec2 text_size = ImGui::CalcTextSize(m.name);
        draw->AddText(ImVec2(tl.x + 6, tl.y + 4), IM_COL32(220, 220, 220, 255), m.name);
        draw->AddText(ImVec2(tl.x + 6, tl.y + 22),
                      IM_COL32(150, 150, 150, 255),
                      DisplayState::resolutions[m.res_index]);

        if (m.primary) {
            draw->AddText(ImVec2(tl.x + 6, br.y - 18), IM_COL32(0, 255, 136, 255), "[PRIMARY]");
        }

        // Click to select
        if (hovered && ImGui::IsMouseClicked(0)) {
            st.selected_monitor = i;
            m.dragging = true;
        }
        if (m.dragging) {
            if (ImGui::IsMouseDown(0)) {
                m.x += io.MouseDelta.x / canvas_w;
                m.y += io.MouseDelta.y / canvas_h;
                m.x = std::clamp(m.x, 0.0f, 1.0f - m.w);
                m.y = std::clamp(m.y, 0.0f, 1.0f - m.h);
            } else {
                m.dragging = false;
            }
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // Per-monitor settings
    if (st.selected_monitor >= 0 && st.selected_monitor < (int)st.monitors.size()) {
        auto& m = st.monitors[st.selected_monitor];

        ImGui::BeginChild("##monitor_settings", ImVec2(avail.x * 0.55f, 0), true);
        ImGui::TextColored(accent, "Monitor: %s", m.name);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Resolution:");
        ImGui::SameLine(120);
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##res", &m.res_index, DisplayState::resolutions, DisplayState::num_resolutions);

        ImGui::Text("Refresh Rate:");
        ImGui::SameLine(120);
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##rate", &m.rate_index, DisplayState::refresh_rates, DisplayState::num_rates);

        ImGui::Spacing();
        bool was_primary = m.primary;
        ImGui::Checkbox("Set as Primary", &m.primary);
        if (m.primary && !was_primary) {
            for (int j = 0; j < (int)st.monitors.size(); ++j) {
                if (j != st.selected_monitor) st.monitors[j].primary = false;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Apply Changes", ImVec2(160, 32))) {
            // Apply monitor configuration
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Color / Night Mode
        ImGui::BeginChild("##color_settings", ImVec2(0, 0), true);
        ImGui::TextColored(accent, "Color & Night Mode");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Night Mode", &st.night_mode);
        ImGui::Spacing();

        ImGui::Text("Color Temperature:");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##temp", &st.color_temp, 2700.0f, 6500.0f, "%.0f K");

        // Visual indicator
        float t = (st.color_temp - 2700.0f) / (6500.0f - 2700.0f);
        ImVec4 warm(1.0f, 0.6f, 0.2f, 1.0f);
        ImVec4 cool(0.8f, 0.9f, 1.0f, 1.0f);
        ImVec4 preview(warm.x + t * (cool.x - warm.x),
                       warm.y + t * (cool.y - warm.y),
                       warm.z + t * (cool.z - warm.z), 1.0f);

        ImGui::Spacing();
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            bar_pos, ImVec2(bar_pos.x + ImGui::GetContentRegionAvail().x, bar_pos.y + 24),
            ImGui::ColorConvertFloat4ToU32(preview), 3.0f);
        ImGui::Dummy(ImVec2(0, 30));

        ImGui::Text(st.night_mode ? "Night mode: Active" : "Night mode: Off");
        if (st.night_mode) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Blue light reduced");
        }

        ImGui::EndChild();
    }

    // Save Profile Dialog
    if (st.show_save_dialog) {
        ImGui::OpenPopup("Save Profile");
        st.show_save_dialog = false;
    }
    if (ImGui::BeginPopupModal("Save Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Profile Name:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##prof_name", st.profile_name, sizeof(st.profile_name));
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 30))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace straylight::display
