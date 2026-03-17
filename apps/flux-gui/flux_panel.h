// apps/flux-gui/flux_panel.h
// StrayLight Flux GUI — Stream Monitor panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::flux {

struct StreamEntry {
    std::string name;
    int         subscriber_count;
    float       message_rate;   // msgs/sec
    float       buffer_usage;   // 0-100
    int         total_messages;
    std::vector<std::string> recent_messages;
    float       rate_history[60] = {};
    int         rate_offset = 0;
};

struct FluxState {
    std::vector<StreamEntry> streams;
    int selected_index = -1;

    // Create dialog
    bool show_create_dialog = false;
    char new_name[128] = {};

    // Filter expression
    char filter_expr[256] = {};

    // Publish test message
    char publish_topic[128] = {};
    char publish_payload[1024] = {};

    // Delete
    bool show_delete_confirm = false;

    void init() {
        {
            StreamEntry s;
            s.name = "system.health";
            s.subscriber_count = 5;
            s.message_rate = 12.3f;
            s.buffer_usage = 15.0f;
            s.total_messages = 45230;
            s.recent_messages = {
                "{\"score\":82,\"ts\":\"2026-03-16T10:30:01Z\"}",
                "{\"score\":83,\"ts\":\"2026-03-16T10:29:56Z\"}",
                "{\"score\":81,\"ts\":\"2026-03-16T10:29:51Z\"}",
                "{\"score\":82,\"ts\":\"2026-03-16T10:29:46Z\"}",
                "{\"score\":84,\"ts\":\"2026-03-16T10:29:41Z\"}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 12.0f + 2.0f * sinf(i * 0.3f);
            streams.push_back(s);
        }
        {
            StreamEntry s;
            s.name = "gpu.metrics";
            s.subscriber_count = 3;
            s.message_rate = 60.0f;
            s.buffer_usage = 35.0f;
            s.total_messages = 1245000;
            s.recent_messages = {
                "{\"gpu\":0,\"temp\":58,\"util\":23,\"vram_pct\":75.8}",
                "{\"gpu\":0,\"temp\":57,\"util\":22,\"vram_pct\":75.6}",
                "{\"gpu\":0,\"temp\":58,\"util\":25,\"vram_pct\":75.9}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 58.0f + 5.0f * sinf(i * 0.5f);
            streams.push_back(s);
        }
        {
            StreamEntry s;
            s.name = "network.events";
            s.subscriber_count = 8;
            s.message_rate = 145.0f;
            s.buffer_usage = 62.0f;
            s.total_messages = 8920000;
            s.recent_messages = {
                "{\"type\":\"tcp_connect\",\"src\":\"192.168.1.100\",\"dst\":\"10.0.0.5\",\"port\":443}",
                "{\"type\":\"dns_query\",\"name\":\"api.straylight.local\",\"rtt_ms\":2.1}",
                "{\"type\":\"tcp_close\",\"src\":\"192.168.1.100\",\"dst\":\"10.0.0.5\",\"port\":443}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 140.0f + 20.0f * sinf(i * 0.4f);
            streams.push_back(s);
        }
        {
            StreamEntry s;
            s.name = "process.lifecycle";
            s.subscriber_count = 4;
            s.message_rate = 3.5f;
            s.buffer_usage = 5.0f;
            s.total_messages = 12400;
            s.recent_messages = {
                "{\"event\":\"start\",\"pid\":6200,\"name\":\"healthd\",\"ts\":\"2026-03-16T10:30:00Z\"}",
                "{\"event\":\"exit\",\"pid\":6198,\"name\":\"cleanup\",\"code\":0}",
                "{\"event\":\"start\",\"pid\":6199,\"name\":\"logrotate\"}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 3.0f + 1.5f * sinf(i * 0.2f);
            streams.push_back(s);
        }
        {
            StreamEntry s;
            s.name = "security.alerts";
            s.subscriber_count = 6;
            s.message_rate = 0.5f;
            s.buffer_usage = 2.0f;
            s.total_messages = 340;
            s.recent_messages = {
                "{\"level\":\"warn\",\"msg\":\"Failed SSH login from 10.0.0.99\",\"count\":3}",
                "{\"level\":\"info\",\"msg\":\"Firewall rule updated: allow 443/tcp\"}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 0.4f + 0.3f * sinf(i * 0.15f);
            streams.push_back(s);
        }
        {
            StreamEntry s;
            s.name = "ml.inference";
            s.subscriber_count = 2;
            s.message_rate = 25.0f;
            s.buffer_usage = 45.0f;
            s.total_messages = 890000;
            s.recent_messages = {
                "{\"model\":\"resnet50\",\"latency_ms\":4.2,\"batch\":32}",
                "{\"model\":\"resnet50\",\"latency_ms\":4.1,\"batch\":32}",
                "{\"model\":\"bert-large\",\"latency_ms\":12.8,\"batch\":8}"
            };
            for (int i = 0; i < 60; ++i) s.rate_history[i] = 24.0f + 3.0f * sinf(i * 0.35f);
            streams.push_back(s);
        }
    }
};

inline void render_flux_panel(FluxState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT FLUX");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar
    if (ImGui::Button("Create Stream", ImVec2(130, 28))) {
        st.show_create_dialog = true;
        memset(st.new_name, 0, sizeof(st.new_name));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##filter", "Filter expression (e.g. type=tcp_connect)", st.filter_expr, sizeof(st.filter_expr));

    ImGui::Spacing();

    // Stream list (left) + detail (right)
    float list_w = 300.0f;
    if (ImGui::BeginChild("##stream_list", ImVec2(list_w, -1), true)) {
        ImGui::TextDisabled("Streams (%zu)", st.streams.size());
        ImGui::Separator();

        for (int i = 0; i < (int)st.streams.size(); ++i) {
            auto& s = st.streams[i];
            ImGui::PushID(i);
            bool sel = (i == st.selected_index);

            if (ImGui::Selectable("##sel", sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 60))) {
                st.selected_index = i;
            }

            ImVec2 p = ImGui::GetItemRectMin();
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // Name
            draw->AddText(ImVec2(p.x + 8, p.y + 4), IM_COL32(220, 220, 220, 255), s.name.c_str());

            // Stats line
            char stats[128];
            snprintf(stats, sizeof(stats), "%d subs | %.1f msg/s | %d total",
                     s.subscriber_count, s.message_rate, s.total_messages);
            draw->AddText(ImVec2(p.x + 8, p.y + 22), IM_COL32(140, 140, 140, 255), stats);

            // Buffer usage bar
            float bar_x = p.x + 8;
            float bar_y = p.y + 42;
            float bar_w = list_w - 30;
            draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + 8), IM_COL32(30, 30, 50, 255));
            float fill_w = bar_w * (s.buffer_usage / 100.0f);
            ImU32 buf_col = s.buffer_usage < 50 ? IM_COL32(0, 180, 120, 255)
                          : s.buffer_usage < 80 ? IM_COL32(200, 180, 40, 255)
                          : IM_COL32(200, 60, 60, 255);
            draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + fill_w, bar_y + 8), buf_col);

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Detail panel
    if (ImGui::BeginChild("##stream_detail", ImVec2(0, -1), false)) {
        bool has_sel = st.selected_index >= 0 && st.selected_index < (int)st.streams.size();
        if (has_sel) {
            auto& s = st.streams[st.selected_index];

            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "%s", s.name.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // Stats
            ImGui::Columns(3, "##stream_stats", false);
            ImGui::Text("Subscribers: %d", s.subscriber_count);
            ImGui::NextColumn();
            ImGui::Text("Rate: %.1f msg/s", s.message_rate);
            ImGui::NextColumn();
            ImGui::Text("Buffer: %.0f%%", s.buffer_usage);
            ImGui::Columns(1);
            ImGui::Spacing();

            // Throughput graph
            ImGui::Text("Throughput (msgs/sec):");
            ImGui::PlotLines("##rate_graph", s.rate_history, 60, s.rate_offset,
                             nullptr, 0.0f, s.message_rate * 2.0f, ImVec2(-1, 100));
            // Animate
            s.rate_history[s.rate_offset] = s.message_rate + (float)((int)(ImGui::GetTime() * 100) % 10) - 5.0f;
            s.rate_offset = (s.rate_offset + 1) % 60;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Live message viewer
            ImGui::Text("Recent Messages:");
            if (ImGui::BeginChild("##messages", ImVec2(-1, 200), true)) {
                for (auto& msg : s.recent_messages) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 0.7f, 1.0f));
                    ImGui::TextWrapped("%s", msg.c_str());
                    ImGui::PopStyleColor();
                    ImGui::Separator();
                }
            }
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Publish test message
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Publish Test Message");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##publish_payload", st.publish_payload, sizeof(st.publish_payload),
                                       ImVec2(-1, 80));
            if (ImGui::Button("Publish", ImVec2(120, 28))) {
                if (strlen(st.publish_payload) > 0) {
                    s.recent_messages.insert(s.recent_messages.begin(), st.publish_payload);
                    if (s.recent_messages.size() > 20) s.recent_messages.pop_back();
                    s.total_messages++;
                    memset(st.publish_payload, 0, sizeof(st.publish_payload));
                }
            }

            // Delete button
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
            if (ImGui::Button("Delete Stream", ImVec2(0, 28))) { st.show_delete_confirm = true; }
            ImGui::PopStyleColor();

        } else {
            ImGui::TextDisabled("Select a stream to view details");
        }
    }
    ImGui::EndChild();

    // Create Stream dialog
    if (st.show_create_dialog) {
        ImGui::OpenPopup("Create Stream");
        st.show_create_dialog = false;
    }
    if (ImGui::BeginPopupModal("Create Stream", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Stream Name:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##stream_name", "e.g. my.custom.stream", st.new_name, sizeof(st.new_name));
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 30))) {
            if (strlen(st.new_name) > 0) {
                StreamEntry s;
                s.name = st.new_name;
                s.subscriber_count = 0;
                s.message_rate = 0;
                s.buffer_usage = 0;
                s.total_messages = 0;
                for (int i = 0; i < 60; ++i) s.rate_history[i] = 0;
                st.streams.push_back(s);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Delete confirmation
    if (st.show_delete_confirm) {
        ImGui::OpenPopup("Confirm Delete##flux");
        st.show_delete_confirm = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete##flux", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool has_sel2 = st.selected_index >= 0 && st.selected_index < (int)st.streams.size();
        if (has_sel2) {
            ImGui::Text("Delete stream '%s'?", st.streams[st.selected_index].name.c_str());
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "All buffered messages will be lost.");
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
        if (ImGui::Button("Delete", ImVec2(120, 30))) {
            if (has_sel2) {
                st.streams.erase(st.streams.begin() + st.selected_index);
                st.selected_index = -1;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace straylight::flux
