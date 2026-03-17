// apps/policy-gui/policy_panel.h
// StrayLight System Policy panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::policy {

struct ComplianceCheck {
    char description[128];
    bool passed;
    char detail[128];
};

struct PolicyRule {
    char name[64];
    char value[128];
    bool enforced;
};

struct PolicyState {
    int  current_role = 1;
    int  preview_role = -1;
    std::vector<ComplianceCheck> checks;
    std::vector<PolicyRule> custom_rules;

    bool show_custom_editor = false;
    char new_rule_name[64] = {};
    char new_rule_value[128] = {};

    static constexpr const char* roles[] = {
        "Workstation", "Server", "Development", "Kiosk", "Minimal", "Custom"
    };
    static constexpr int num_roles = 6;

    static constexpr const char* role_descriptions[] = {
        "General purpose desktop with full GUI, multimedia, and productivity tools",
        "Headless server with hardened security, no GUI, minimal attack surface",
        "Development workstation with compilers, debuggers, containers, and dev tools",
        "Locked-down single-application kiosk mode with restricted access",
        "Bare minimum system with essential services only",
        "User-defined policy with manual rule configuration"
    };

    void init() {
        update_checks();

        custom_rules.push_back({"ssh.permit_root", "no", true});
        custom_rules.push_back({"firewall.default_policy", "deny", true});
        custom_rules.push_back({"password.min_length", "12", true});
        custom_rules.push_back({"session.idle_timeout", "900", true});
        custom_rules.push_back({"audit.enabled", "true", true});
        custom_rules.push_back({"update.auto_security", "true", true});
        custom_rules.push_back({"usb.mass_storage", "allow", false});
    }

    void update_checks() {
        checks.clear();
        checks.push_back({"Firewall enabled", true, "iptables active with default deny"});
        checks.push_back({"SSH root login disabled", true, "PermitRootLogin no"});
        checks.push_back({"Audit logging active", true, "auditd running"});
        checks.push_back({"Password complexity enforced", true, "minlen=12, mixedcase"});
        checks.push_back({"Disk encryption enabled", true, "LUKS active on /dev/nvme0n1p3"});
        checks.push_back({"Automatic security updates", true, "Enabled"});
        checks.push_back({"SELinux/AppArmor active", current_role == 1 || current_role == 3,
                           current_role == 1 || current_role == 3 ? "Enforcing" : "Disabled"});
        checks.push_back({"USB mass storage restricted", current_role == 1 || current_role == 3,
                           current_role == 1 || current_role == 3 ? "Blocked" : "Allowed"});
        checks.push_back({"Network services minimized", current_role != 0,
                           current_role != 0 ? "Only essential ports" : "Multiple services exposed"});
        checks.push_back({"Core dump disabled", current_role == 1,
                           current_role == 1 ? "Disabled" : "Enabled"});
    }
};

inline void render_policy_panel(PolicyState& st) {
    if (st.checks.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("SYSTEM POLICY");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Current role display
    ImGui::BeginChild("##role_display", ImVec2(-1, 80), true);
    ImGui::Text("Current Role:");
    ImGui::SameLine(120);

    // Large role label
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 label_pos = ImGui::GetCursorScreenPos();
    ImVec2 label_size(ImGui::CalcTextSize(PolicyState::roles[st.current_role]).x * 2.0f + 20, 40);

    draw->AddRectFilled(label_pos, ImVec2(label_pos.x + label_size.x, label_pos.y + label_size.y),
                        IM_COL32(0, 80, 55, 200), 6.0f);
    draw->AddRect(label_pos, ImVec2(label_pos.x + label_size.x, label_pos.y + label_size.y),
                  IM_COL32(0, 255, 136, 255), 6.0f, 0, 2.0f);

    ImGui::SetWindowFontScale(2.0f);
    draw->AddText(ImVec2(label_pos.x + 10, label_pos.y + 6),
                  IM_COL32(0, 255, 136, 255), PolicyState::roles[st.current_role]);
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Dummy(ImVec2(0, 40));
    ImGui::TextDisabled("%s", PolicyState::role_descriptions[st.current_role]);
    ImGui::EndChild();
    ImGui::Spacing();

    float left_w = ImGui::GetContentRegionAvail().x * 0.45f;

    // Role selector with preview
    ImGui::BeginChild("##role_select", ImVec2(left_w, ImGui::GetContentRegionAvail().y * 0.5f), true);
    ImGui::TextColored(accent, "Role Selector");
    ImGui::Separator();
    ImGui::Spacing();

    for (int i = 0; i < PolicyState::num_roles; ++i) {
        ImGui::PushID(i);
        bool is_current = (i == st.current_role);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImU32 bg = is_current ? IM_COL32(0, 60, 40, 200) : IM_COL32(25, 25, 40, 200);

        if (ImGui::Selectable("##role", is_current || st.preview_role == i, 0, ImVec2(0, 50))) {
            st.preview_role = i;
        }

        // Overlay content
        ImDrawList* rdraw = ImGui::GetWindowDrawList();
        rdraw->AddText(ImVec2(pos.x + 8, pos.y + 4), IM_COL32(220, 220, 220, 255),
                       PolicyState::roles[i]);
        rdraw->AddText(ImVec2(pos.x + 8, pos.y + 22), IM_COL32(140, 140, 140, 200),
                       PolicyState::role_descriptions[i]);

        if (is_current) {
            rdraw->AddText(ImVec2(pos.x + ImGui::GetContentRegionAvail().x - 70, pos.y + 4),
                           IM_COL32(0, 255, 136, 255), "ACTIVE");
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (st.preview_role >= 0 && st.preview_role != st.current_role) {
        if (ImGui::Button("Apply Role", ImVec2(-1, 30))) {
            st.current_role = st.preview_role;
            st.update_checks();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Compliance checklist
    ImGui::BeginChild("##compliance", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.5f), true);
    ImGui::TextColored(accent, "Compliance Checklist");
    ImGui::Separator();
    ImGui::Spacing();

    int passed = 0, total = (int)st.checks.size();
    for (auto& c : st.checks) if (c.passed) passed++;
    ImGui::Text("Score: %d / %d", passed, total);

    // Progress
    ImGui::ProgressBar((float)passed / std::max(total, 1), ImVec2(-1, 16));
    ImGui::Spacing();

    for (int i = 0; i < (int)st.checks.size(); ++i) {
        auto& c = st.checks[i];
        ImGui::PushID(500 + i);

        if (c.passed) {
            ImGui::TextColored(accent, "[PASS]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[FAIL]");
        }
        ImGui::SameLine(60);
        ImGui::Text("%s", c.description);
        ImGui::SameLine(300);
        ImGui::TextDisabled("%s", c.detail);

        ImGui::PopID();
    }
    ImGui::EndChild();

    // Custom role editor
    ImGui::BeginChild("##custom_rules", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "Policy Rules (Custom Role Editor)");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Add Rule")) {
        st.show_custom_editor = true;
        memset(st.new_rule_name, 0, sizeof(st.new_rule_name));
        memset(st.new_rule_value, 0, sizeof(st.new_rule_value));
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##rules", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Rule", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Enforced", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();

        int delete_idx = -1;
        for (int i = 0; i < (int)st.custom_rules.size(); ++i) {
            auto& r = st.custom_rules[i];
            ImGui::TableNextRow();
            ImGui::PushID(1000 + i);

            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", r.name);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##val", r.value, sizeof(r.value));
            ImGui::TableSetColumnIndex(2); ImGui::Checkbox("##enf", &r.enforced);
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
            if (ImGui::SmallButton("Del")) delete_idx = i;
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
        ImGui::EndTable();
        if (delete_idx >= 0) st.custom_rules.erase(st.custom_rules.begin() + delete_idx);
    }

    ImGui::Spacing();
    if (ImGui::Button("Apply All Rules", ImVec2(160, 28))) {
        st.update_checks();
    }
    ImGui::EndChild();

    // Add Rule dialog
    if (st.show_custom_editor) {
        ImGui::OpenPopup("Add Policy Rule");
        st.show_custom_editor = false;
    }
    if (ImGui::BeginPopupModal("Add Policy Rule", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Rule Name", st.new_rule_name, 64);
        ImGui::InputText("Value", st.new_rule_value, 128);
        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(120, 30)) && strlen(st.new_rule_name) > 0) {
            PolicyRule nr{};
            snprintf(nr.name, 64, "%s", st.new_rule_name);
            snprintf(nr.value, 128, "%s", st.new_rule_value);
            nr.enforced = true;
            st.custom_rules.push_back(nr);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::policy
