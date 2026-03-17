// apps/audio-gui/audio_panel.h
// StrayLight Audio Control panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace straylight::audio {

struct AudioApp {
    char name[64];
    float volume;
    bool  muted;
    float level_l;  // simulated VU meter
    float level_r;
};

struct AudioRoute {
    int source;
    int sink;
    bool active;
};

struct AudioState {
    float master_volume = 0.75f;
    bool  master_mute = false;

    int output_device = 0;
    int input_device = 0;

    std::vector<AudioApp> apps;
    std::vector<AudioRoute> routes;

    static constexpr const char* output_devices[] = {
        "Built-in Speakers", "HDMI Audio", "USB DAC", "Bluetooth Headphones"
    };
    static constexpr int num_outputs = 4;

    static constexpr const char* input_devices[] = {
        "Built-in Microphone", "USB Microphone", "Line In", "Bluetooth Mic"
    };
    static constexpr int num_inputs = 4;

    static constexpr const char* sources[] = {
        "System", "Firefox", "Terminal", "Media Player", "Voice Chat"
    };
    static constexpr int num_sources = 5;

    static constexpr const char* sinks[] = {
        "Speakers", "HDMI", "USB DAC", "Headphones"
    };
    static constexpr int num_sinks = 4;

    float frame_counter = 0;

    void init() {
        AudioApp a1{}; snprintf(a1.name, 64, "Firefox"); a1.volume = 0.8f; a1.muted = false; a1.level_l = 0.4f; a1.level_r = 0.35f;
        AudioApp a2{}; snprintf(a2.name, 64, "Terminal"); a2.volume = 0.5f; a2.muted = false; a2.level_l = 0.1f; a2.level_r = 0.1f;
        AudioApp a3{}; snprintf(a3.name, 64, "Media Player"); a3.volume = 0.9f; a3.muted = false; a3.level_l = 0.7f; a3.level_r = 0.65f;
        AudioApp a4{}; snprintf(a4.name, 64, "Voice Chat"); a4.volume = 0.7f; a4.muted = false; a4.level_l = 0.3f; a4.level_r = 0.25f;
        AudioApp a5{}; snprintf(a5.name, 64, "System Sounds"); a5.volume = 0.6f; a5.muted = false; a5.level_l = 0.05f; a5.level_r = 0.05f;
        apps = {a1, a2, a3, a4, a5};

        routes.push_back({0, 0, true});
        routes.push_back({1, 0, true});
        routes.push_back({2, 2, true});
        routes.push_back({3, 3, true});
        routes.push_back({4, 0, true});
    }
};

inline void render_audio_panel(AudioState& st) {
    if (st.apps.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);
    st.frame_counter += ImGui::GetIO().DeltaTime;

    // Simulate fluctuating VU levels
    for (auto& a : st.apps) {
        if (!a.muted) {
            a.level_l = a.volume * (0.3f + 0.4f * (float)(sin(st.frame_counter * 3.0 + (double)(a.name[0])) * 0.5 + 0.5));
            a.level_r = a.volume * (0.3f + 0.4f * (float)(sin(st.frame_counter * 3.5 + (double)(a.name[1])) * 0.5 + 0.5));
        } else {
            a.level_l = 0; a.level_r = 0;
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("AUDIO CONTROL");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Master Volume
    ImGui::BeginChild("##master", ImVec2(-1, 70), true);
    ImGui::TextColored(accent, "Master Volume");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::Checkbox("Mute##master_mute", &st.master_mute)) {}

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    ImGui::SliderFloat("##master_vol", &st.master_volume, 0.0f, 1.0f, "%.0f%%");
    ImGui::SameLine();
    ImGui::Text("%.0f%%", st.master_volume * 100);
    ImGui::EndChild();
    ImGui::Spacing();

    // Device selection
    ImGui::BeginChild("##devices", ImVec2(-1, 60), true);
    ImGui::Text("Output:");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(250);
    ImGui::Combo("##out_dev", &st.output_device, AudioState::output_devices, AudioState::num_outputs);
    ImGui::SameLine(400);
    ImGui::Text("Input:");
    ImGui::SameLine(450);
    ImGui::SetNextItemWidth(250);
    ImGui::Combo("##in_dev", &st.input_device, AudioState::input_devices, AudioState::num_inputs);
    ImGui::EndChild();
    ImGui::Spacing();

    float half_w = ImGui::GetContentRegionAvail().x * 0.55f;

    // Per-app volume sliders
    ImGui::BeginChild("##per_app", ImVec2(half_w, 0), true);
    ImGui::TextColored(accent, "Per-Application Volume");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < (int)st.apps.size(); ++i) {
        auto& a = st.apps[i];
        ImGui::PushID(i);

        ImGui::Text("%s", a.name);
        ImGui::SameLine(130);
        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("##vol", &a.volume, 0.0f, 1.0f, "%.0f%%");
        ImGui::SameLine();
        ImGui::Checkbox("Mute", &a.muted);

        // VU meter bars
        ImVec2 bar_pos = ImGui::GetCursorScreenPos();
        float bar_w = ImGui::GetContentRegionAvail().x - 20;
        float bar_h = 6.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Left channel
        draw->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w, bar_pos.y + bar_h),
                            IM_COL32(30, 30, 50, 255));
        float fill_l = a.level_l * st.master_volume * bar_w;
        ImU32 color_l = (a.level_l > 0.8f) ? IM_COL32(255, 60, 60, 255) :
                        (a.level_l > 0.5f) ? IM_COL32(255, 200, 0, 255) : IM_COL32(0, 255, 136, 255);
        draw->AddRectFilled(bar_pos, ImVec2(bar_pos.x + fill_l, bar_pos.y + bar_h), color_l);
        ImGui::Dummy(ImVec2(0, bar_h + 1));

        // Right channel
        ImVec2 bar_pos2 = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(bar_pos2, ImVec2(bar_pos2.x + bar_w, bar_pos2.y + bar_h),
                            IM_COL32(30, 30, 50, 255));
        float fill_r = a.level_r * st.master_volume * bar_w;
        ImU32 color_r = (a.level_r > 0.8f) ? IM_COL32(255, 60, 60, 255) :
                        (a.level_r > 0.5f) ? IM_COL32(255, 200, 0, 255) : IM_COL32(0, 255, 136, 255);
        draw->AddRectFilled(bar_pos2, ImVec2(bar_pos2.x + fill_r, bar_pos2.y + bar_h), color_r);
        ImGui::Dummy(ImVec2(0, bar_h + 6));

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Audio routing matrix
    ImGui::BeginChild("##routing", ImVec2(0, 0), true);
    ImGui::TextColored(accent, "Audio Routing Matrix");
    ImGui::Separator();
    ImGui::Spacing();

    // Header row
    ImGui::Text("          ");
    for (int s = 0; s < AudioState::num_sinks; ++s) {
        ImGui::SameLine(100 + s * 80);
        ImGui::Text("%s", AudioState::sinks[s]);
    }

    for (int src = 0; src < AudioState::num_sources; ++src) {
        ImGui::Text("%-10s", AudioState::sources[src]);
        for (int snk = 0; snk < AudioState::num_sinks; ++snk) {
            ImGui::SameLine(100 + snk * 80);
            ImGui::PushID(src * 100 + snk);
            bool connected = false;
            for (auto& r : st.routes) {
                if (r.source == src && r.sink == snk) { connected = r.active; break; }
            }
            if (ImGui::Checkbox("##route", &connected)) {
                bool found = false;
                for (auto& r : st.routes) {
                    if (r.source == src && r.sink == snk) { r.active = connected; found = true; break; }
                }
                if (!found && connected) {
                    st.routes.push_back({src, snk, true});
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

} // namespace straylight::audio
