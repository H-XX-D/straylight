// apps/notify-gui/notify_panel.h
// StrayLight Notification Settings panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::notify {

struct NotifEntry {
    char app[64];
    char title[128];
    char body[256];
    char time[32];
    int  urgency;  // 0=low, 1=normal, 2=critical
    bool read;
};

struct NotifRule {
    char app[64];
    bool allow_sound;
    bool allow_popup;
    bool allow_badge;
    int  priority;  // 0=default, 1=silent, 2=important
};

struct NotifyState {
    std::vector<NotifEntry> history;
    std::vector<NotifRule> rules;

    bool dnd_enabled = false;
    int  dnd_start_hour = 22;
    int  dnd_start_min = 0;
    int  dnd_end_hour = 7;
    int  dnd_end_min = 0;

    bool show_add_rule = false;
    char new_rule_app[64] = {};

    static constexpr const char* urgency_labels[] = { "Low", "Normal", "Critical" };
    static constexpr const char* priority_labels[] = { "Default", "Silent", "Important" };

    void init() {
        history.push_back({"Firefox", "Download Complete", "linux-straylight-6.8.2.tar.xz saved", "10:14", 0, true});
        history.push_back({"System", "Update Available", "12 packages ready to upgrade", "10:10", 1, false});
        history.push_back({"Vault", "Auth Failure", "Failed login attempt from 10.0.0.5", "10:04", 2, false});
        history.push_back({"Email", "New Message", "Re: StrayLight deployment schedule", "09:55", 1, true});
        history.push_back({"Calendar", "Meeting in 15 min", "Team standup - Room B", "09:45", 1, true});
        history.push_back({"System", "Low Battery", "Battery at 15%, connect charger", "09:30", 2, false});
        history.push_back({"Terminal", "Task Complete", "Build finished successfully", "09:22", 0, true});
        history.push_back({"Chat", "Message from Alex", "Hey, are you available?", "09:15", 1, false});

        rules.push_back({"Firefox", true, true, true, 0});
        rules.push_back({"System", true, true, true, 2});
        rules.push_back({"Vault", true, true, false, 2});
        rules.push_back({"Email", true, true, true, 0});
        rules.push_back({"Calendar", true, true, false, 2});
        rules.push_back({"Terminal", false, false, true, 1});
        rules.push_back({"Chat", true, true, true, 0});
    }
};

inline void render_notify_panel(NotifyState& st) {
    if (st.history.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("NOTIFICATION SETTINGS");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // DND toggle
    ImGui::BeginChild("##dnd", ImVec2(-1, 80), true);
    ImGui::Checkbox("Do Not Disturb", &st.dnd_enabled);
    if (st.dnd_enabled) {
        ImGui::SameLine(200);
        ImGui::Text("Schedule:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##dnd_sh", &st.dnd_start_hour, 0); st.dnd_start_hour = std::clamp(st.dnd_start_hour, 0, 23);
        ImGui::SameLine(); ImGui::Text(":"); ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##dnd_sm", &st.dnd_start_min, 0); st.dnd_start_min = std::clamp(st.dnd_start_min, 0, 59);
        ImGui::SameLine(); ImGui::Text(" to "); ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##dnd_eh", &st.dnd_end_hour, 0); st.dnd_end_hour = std::clamp(st.dnd_end_hour, 0, 23);
        ImGui::SameLine(); ImGui::Text(":"); ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##dnd_em", &st.dnd_end_min, 0); st.dnd_end_min = std::clamp(st.dnd_end_min, 0, 59);

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                           "DND active: %02d:%02d - %02d:%02d", st.dnd_start_hour, st.dnd_start_min, st.dnd_end_hour, st.dnd_end_min);
    }
    ImGui::EndChild();
    ImGui::Spacing();

    float half_w = ImGui::GetContentRegionAvail().x * 0.5f;

    // Notification history
    ImGui::BeginChild("##notif_history", ImVec2(half_w, 0), true);
    ImGui::TextColored(accent, "Notification History");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Clear All")) st.history.clear();
    ImGui::Separator();

    ImVec4 urgency_colors[] = {
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
        ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
        ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
    };

    for (int i = 0; i < (int)st.history.size(); ++i) {
        auto& n = st.history[i];
        ImGui::PushID(i);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        float item_h = 52;

        // Background for unread
        if (!n.read) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + item_h),
                IM_COL32(0, 40, 25, 100), 3.0f);
        }

        // Urgency dot
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImU32 dot_col = (n.urgency == 2) ? IM_COL32(255, 60, 60, 255) :
                        (n.urgency == 1) ? IM_COL32(0, 200, 130, 255) : IM_COL32(100, 100, 100, 255);
        draw->AddCircleFilled(ImVec2(pos.x + 8, pos.y + 14), 4, dot_col);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 2));
        ImGui::TextColored(accent, "%s", n.app);
        ImGui::SameLine(200);
        ImGui::TextDisabled("%s", n.time);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 18));
        ImGui::Text("%s", n.title);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 20, pos.y + 34));
        ImGui::TextDisabled("%s", n.body);

        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + item_h));

        if (ImGui::InvisibleButton("##notif", ImVec2(ImGui::GetContentRegionAvail().x, 1))) {
            n.read = true;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Per-app rules
    ImGui::BeginChild("##rules", ImVec2(0, 0), true);
    ImGui::TextColored(accent, "Per-App Settings");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Add Rule")) {
        st.show_add_rule = true;
        memset(st.new_rule_app, 0, sizeof(st.new_rule_app));
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##rule_table", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("App", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Sound", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Popup", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Badge", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)st.rules.size(); ++i) {
            auto& r = st.rules[i];
            ImGui::TableNextRow();
            ImGui::PushID(500 + i);

            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", r.app);
            ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##snd", &r.allow_sound);
            ImGui::TableSetColumnIndex(2); ImGui::Checkbox("##pop", &r.allow_popup);
            ImGui::TableSetColumnIndex(3); ImGui::Checkbox("##bdg", &r.allow_badge);
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##pri", &r.priority, NotifyState::priority_labels, 3);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Add Rule dialog
    if (st.show_add_rule) {
        ImGui::OpenPopup("Add Notification Rule");
        st.show_add_rule = false;
    }
    if (ImGui::BeginPopupModal("Add Notification Rule", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("App Name", st.new_rule_app, 64);
        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(120, 30)) && strlen(st.new_rule_app) > 0) {
            NotifRule nr{};
            snprintf(nr.app, 64, "%s", st.new_rule_app);
            nr.allow_sound = true; nr.allow_popup = true; nr.allow_badge = true; nr.priority = 0;
            st.rules.push_back(nr);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::notify
