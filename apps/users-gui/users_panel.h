// apps/users-gui/users_panel.h
// StrayLight User Management panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::users {

struct UserInfo {
    char username[64];
    char display_name[64];
    char groups[128];
    char last_login[32];
    char shell[32];
    char home[128];
    char ssh_key[512];
    bool is_admin;
    bool locked;
};

struct GroupInfo {
    char name[64];
    int  gid;
    int  member_count;
};

struct UsersState {
    std::vector<UserInfo> user_list;
    std::vector<GroupInfo> group_list;
    int selected_user = 0;

    bool show_add_user = false;
    bool show_edit_user = false;
    bool show_add_group = false;

    char new_username[64] = {};
    char new_display[64] = {};
    char new_groups[128] = {};
    char new_shell[32] = {};
    char new_password[128] = {};
    bool new_is_admin = false;

    char new_group_name[64] = {};

    void init() {
        user_list.push_back({"root", "System Root", "root,wheel", "2026-03-16 08:00", "/bin/zsh", "/root",
            "ssh-ed25519 AAAAC3Nza...root@straylight", true, false});
        user_list.push_back({"straylight", "StrayLight Admin", "wheel,docker,audio,video", "2026-03-16 10:15", "/bin/zsh", "/home/straylight",
            "ssh-ed25519 AAAAC3Nza...admin@straylight", true, false});
        user_list.push_back({"deploy", "Deploy Service", "docker,deploy", "2026-03-15 22:00", "/bin/bash", "/home/deploy",
            "ssh-ed25519 AAAAC3Nza...deploy@straylight", false, false});
        user_list.push_back({"guest", "Guest User", "users", "2026-03-10 14:00", "/bin/bash", "/home/guest",
            "", false, true});

        group_list.push_back({"wheel", 10, 2});
        group_list.push_back({"docker", 998, 2});
        group_list.push_back({"audio", 92, 1});
        group_list.push_back({"video", 93, 1});
        group_list.push_back({"users", 100, 4});
        group_list.push_back({"deploy", 1001, 1});
    }
};

inline void render_users_panel(UsersState& st) {
    if (st.user_list.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("USER MANAGEMENT");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar
    if (ImGui::Button("Add User", ImVec2(120, 30))) {
        st.show_add_user = true;
        memset(st.new_username, 0, sizeof(st.new_username));
        memset(st.new_display, 0, sizeof(st.new_display));
        memset(st.new_groups, 0, sizeof(st.new_groups));
        snprintf(st.new_shell, 32, "/bin/bash");
        st.new_is_admin = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Group", ImVec2(120, 30))) {
        st.show_add_group = true;
        memset(st.new_group_name, 0, sizeof(st.new_group_name));
    }
    ImGui::Spacing();

    float list_w = ImGui::GetContentRegionAvail().x * 0.4f;

    // User list
    ImGui::BeginChild("##user_list", ImVec2(list_w, ImGui::GetContentRegionAvail().y * 0.65f), true);
    ImGui::TextColored(accent, "Users (%zu)", st.user_list.size());
    ImGui::Separator();

    for (int i = 0; i < (int)st.user_list.size(); ++i) {
        auto& u = st.user_list[i];
        ImGui::PushID(i);

        bool selected = (i == st.selected_user);

        // Avatar placeholder (colored circle with initial)
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float rad = 14;
        ImVec2 center(pos.x + rad + 4, pos.y + rad + 4);
        ImU32 avatar_col = u.is_admin ? IM_COL32(0, 200, 100, 255) : IM_COL32(80, 80, 140, 255);
        if (u.locked) avatar_col = IM_COL32(120, 60, 60, 255);
        draw->AddCircleFilled(center, rad, avatar_col);
        char initial[2] = {u.username[0], 0};
        if (initial[0] >= 'a') initial[0] -= 32; // uppercase
        ImVec2 ts = ImGui::CalcTextSize(initial);
        draw->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
                      IM_COL32(255, 255, 255, 255), initial);

        ImGui::Dummy(ImVec2(rad * 2 + 8, 0));
        ImGui::SameLine();

        char label[128];
        snprintf(label, 128, "%s (%s)%s", u.display_name, u.username,
                 u.locked ? " [LOCKED]" : "");
        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 32))) {
            st.selected_user = i;
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // User detail panel
    ImGui::BeginChild("##user_detail", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.65f), true);
    if (st.selected_user >= 0 && st.selected_user < (int)st.user_list.size()) {
        auto& u = st.user_list[st.selected_user];
        ImGui::TextColored(accent, "%s", u.display_name);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Username:");    ImGui::SameLine(130); ImGui::Text("%s", u.username);
        ImGui::Text("Home:");        ImGui::SameLine(130); ImGui::Text("%s", u.home);
        ImGui::Text("Shell:");       ImGui::SameLine(130); ImGui::Text("%s", u.shell);
        ImGui::Text("Groups:");      ImGui::SameLine(130); ImGui::TextWrapped("%s", u.groups);
        ImGui::Text("Last Login:");  ImGui::SameLine(130); ImGui::Text("%s", u.last_login);
        ImGui::Text("Admin:");       ImGui::SameLine(130); ImGui::Text("%s", u.is_admin ? "Yes" : "No");
        ImGui::Text("Status:");      ImGui::SameLine(130);
        if (u.locked) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Locked");
        else ImGui::TextColored(accent, "Active");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(accent, "SSH Keys");
        ImGui::Spacing();
        if (strlen(u.ssh_key) > 0) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
            char key_buf[512];
            snprintf(key_buf, 512, "%s", u.ssh_key);
            ImGui::InputTextMultiline("##ssh", key_buf, 512, ImVec2(-1, 60), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("No SSH key configured");
        }

        ImGui::Spacing();
        if (ImGui::Button("Edit User", ImVec2(120, 28))) {
            st.show_edit_user = true;
            snprintf(st.new_username, 64, "%s", u.username);
            snprintf(st.new_display, 64, "%s", u.display_name);
            snprintf(st.new_groups, 128, "%s", u.groups);
            snprintf(st.new_shell, 32, "%s", u.shell);
            st.new_is_admin = u.is_admin;
        }
        ImGui::SameLine();
        if (u.locked) {
            if (ImGui::Button("Unlock", ImVec2(100, 28))) u.locked = false;
        } else {
            if (ImGui::Button("Lock", ImVec2(100, 28))) u.locked = true;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
        if (ImGui::Button("Delete", ImVec2(100, 28))) {
            // delete user
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();

    // Group management
    ImGui::BeginChild("##groups", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "Groups");
    ImGui::Separator();

    if (ImGui::BeginTable("##grp_table", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Group Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("GID", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Members", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (auto& g : st.group_list) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", g.name);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", g.gid);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", g.member_count);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Add User Dialog
    if (st.show_add_user) {
        ImGui::OpenPopup("Add User");
        st.show_add_user = false;
    }
    if (ImGui::BeginPopupModal("Add User", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Username", st.new_username, 64);
        ImGui::InputText("Display Name", st.new_display, 64);
        ImGui::InputText("Groups", st.new_groups, 128);
        ImGui::InputText("Shell", st.new_shell, 32);
        ImGui::InputText("Password", st.new_password, 128, ImGuiInputTextFlags_Password);
        ImGui::Checkbox("Administrator", &st.new_is_admin);
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 30)) && strlen(st.new_username) > 0) {
            UserInfo nu{};
            snprintf(nu.username, 64, "%s", st.new_username);
            snprintf(nu.display_name, 64, "%s", st.new_display);
            snprintf(nu.groups, 128, "%s", st.new_groups);
            snprintf(nu.shell, 32, "%s", st.new_shell);
            snprintf(nu.last_login, 32, "Never");
            snprintf(nu.home, 128, "/home/%s", st.new_username);
            nu.is_admin = st.new_is_admin;
            nu.locked = false;
            st.user_list.push_back(nu);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Edit User Dialog
    if (st.show_edit_user) {
        ImGui::OpenPopup("Edit User");
        st.show_edit_user = false;
    }
    if (ImGui::BeginPopupModal("Edit User", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Display Name", st.new_display, 64);
        ImGui::InputText("Groups", st.new_groups, 128);
        ImGui::InputText("Shell", st.new_shell, 32);
        ImGui::Checkbox("Administrator", &st.new_is_admin);
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(120, 30))) {
            if (st.selected_user >= 0 && st.selected_user < (int)st.user_list.size()) {
                auto& u = st.user_list[st.selected_user];
                snprintf(u.display_name, 64, "%s", st.new_display);
                snprintf(u.groups, 128, "%s", st.new_groups);
                snprintf(u.shell, 32, "%s", st.new_shell);
                u.is_admin = st.new_is_admin;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Add Group Dialog
    if (st.show_add_group) {
        ImGui::OpenPopup("Add Group");
        st.show_add_group = false;
    }
    if (ImGui::BeginPopupModal("Add Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Group Name", st.new_group_name, 64);
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 30)) && strlen(st.new_group_name) > 0) {
            GroupInfo ng{};
            snprintf(ng.name, 64, "%s", st.new_group_name);
            ng.gid = 2000 + (int)st.group_list.size();
            ng.member_count = 0;
            st.group_list.push_back(ng);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::users
