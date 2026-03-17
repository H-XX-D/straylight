// apps/update-gui/update_panel.h
// StrayLight System Updater panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::update {

struct PackageUpdate {
    char name[64];
    char current_ver[32];
    char new_ver[32];
    char size[16];
    bool selected;
    bool held;
};

struct UpdateHistory {
    char date[32];
    char action[64];
    int  package_count;
    bool success;
};

struct UpdateState {
    std::vector<PackageUpdate> available;
    std::vector<UpdateHistory> history;

    bool  updating = false;
    float progress = 0.0f;
    char  progress_text[128] = {};
    bool  auto_snapshot = true;
    int   current_pkg = 0;

    void init() {
        available.push_back({"linux-straylight", "6.8.1-sl1", "6.8.2-sl1", "128 MB", true, false});
        available.push_back({"mesa-vulkan", "24.0.2", "24.0.4", "45 MB", true, false});
        available.push_back({"straylight-compositor", "1.2.0", "1.2.1", "12 MB", true, false});
        available.push_back({"imgui", "1.90.1", "1.90.3", "2.4 MB", true, false});
        available.push_back({"wayland-protocols", "1.34", "1.35", "800 KB", true, false});
        available.push_back({"llvm-runtime", "18.0.1", "18.1.0", "95 MB", true, false});
        available.push_back({"python3", "3.12.2", "3.12.3", "22 MB", true, false});
        available.push_back({"curl", "8.6.0", "8.7.1", "3.1 MB", false, true});

        history.push_back({"2026-03-15 14:30", "System upgrade (12 packages)", 12, true});
        history.push_back({"2026-03-10 09:00", "Security update (3 packages)", 3, true});
        history.push_back({"2026-03-05 16:45", "Kernel update", 1, true});
        history.push_back({"2026-02-28 11:20", "Full system upgrade (28 packages)", 28, true});
        history.push_back({"2026-02-20 08:00", "Failed upgrade attempt", 5, false});
    }
};

inline void render_update_panel(UpdateState& st) {
    if (st.available.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("SYSTEM UPDATER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Summary
    int update_count = 0;
    float total_size = 0;
    for (auto& p : st.available) {
        if (p.selected && !p.held) {
            update_count++;
            float sz = 0;
            sscanf(p.size, "%f", &sz);
            total_size += sz;
        }
    }
    ImGui::Text("Available updates: %d", (int)st.available.size());
    ImGui::SameLine(250);
    ImGui::Text("Selected: %d", update_count);
    ImGui::SameLine(420);
    ImGui::Checkbox("Auto-snapshot before update", &st.auto_snapshot);
    ImGui::Spacing();

    // Progress bar (shown during update)
    if (st.updating) {
        st.progress += ImGui::GetIO().DeltaTime * 0.05f;
        if (st.progress >= 1.0f) {
            st.progress = 1.0f;
            st.updating = false;
            snprintf(st.progress_text, 128, "Update complete!");
        } else {
            int idx = (int)(st.progress * update_count);
            snprintf(st.progress_text, 128, "Upgrading package %d of %d...", idx + 1, update_count);
        }
        ImGui::ProgressBar(st.progress, ImVec2(-1, 24), st.progress_text);
        ImGui::Spacing();
    }

    // Available updates table
    ImGui::BeginChild("##updates", ImVec2(-1, ImGui::GetContentRegionAvail().y * 0.55f), true);
    ImGui::TextColored(accent, "Available Updates");
    ImGui::Separator();

    if (ImGui::BeginTable("##pkg_table", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Install", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("New", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)st.available.size(); ++i) {
            auto& p = st.available[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            if (p.held) {
                ImGui::BeginDisabled();
                ImGui::Checkbox("##sel", &p.selected);
                ImGui::EndDisabled();
            } else {
                ImGui::Checkbox("##sel", &p.selected);
            }

            ImGui::TableSetColumnIndex(1);
            if (p.held) ImGui::TextDisabled("%s", p.name);
            else ImGui::Text("%s", p.name);

            ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%s", p.current_ver);
            ImGui::TableSetColumnIndex(3); ImGui::TextColored(accent, "%s", p.new_ver);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%s", p.size);
            ImGui::TableSetColumnIndex(5); ImGui::Checkbox("##hold", &p.held);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (!st.updating) {
        if (ImGui::Button("Upgrade Selected", ImVec2(180, 34))) {
            st.updating = true;
            st.progress = 0.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Select All", ImVec2(120, 34))) {
            for (auto& p : st.available) if (!p.held) p.selected = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect All", ImVec2(120, 34))) {
            for (auto& p : st.available) p.selected = false;
        }
    }
    ImGui::EndChild();

    // Update history
    ImGui::BeginChild("##history", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "Update History");
    ImGui::Separator();

    if (ImGui::BeginTable("##hist_table", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Packages", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (auto& h : st.history) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", h.date);
            ImGui::TableSetColumnIndex(1); ImGui::Text("%s", h.action);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", h.package_count);
            ImGui::TableSetColumnIndex(3);
            if (h.success) ImGui::TextColored(accent, "Success");
            else ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Failed");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace straylight::update
