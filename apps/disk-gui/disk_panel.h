// apps/disk-gui/disk_panel.h
// StrayLight Disk Manager panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::disk {

struct Partition {
    char name[32];
    char mount[64];
    char fs_type[16];
    float size_gb;
    float used_gb;
    ImU32 color;
};

struct DiskInfo {
    char device[32];
    char model[64];
    float total_gb;
    int  smart_status;  // 0=good, 1=warning, 2=failing
    int  temp_c;
    int  power_on_hours;
    std::vector<Partition> partitions;
};

struct DiskState {
    std::vector<DiskInfo> disks;
    int selected_disk = 0;
    int selected_partition = -1;

    bool show_format_confirm = false;
    bool show_mount_dialog = false;
    char mount_point[128] = {};

    void init() {
        DiskInfo d1{};
        snprintf(d1.device, 32, "/dev/nvme0n1");
        snprintf(d1.model, 64, "Samsung 980 PRO 1TB");
        d1.total_gb = 953.87f;
        d1.smart_status = 0;
        d1.temp_c = 38;
        d1.power_on_hours = 4520;
        d1.partitions.push_back({"nvme0n1p1", "/boot/efi", "vfat", 0.5f, 0.1f, IM_COL32(200, 100, 50, 255)});
        d1.partitions.push_back({"nvme0n1p2", "/boot", "ext4", 1.0f, 0.3f, IM_COL32(100, 150, 200, 255)});
        d1.partitions.push_back({"nvme0n1p3", "/", "btrfs", 200.0f, 45.0f, IM_COL32(0, 200, 100, 255)});
        d1.partitions.push_back({"nvme0n1p4", "/home", "btrfs", 700.0f, 312.0f, IM_COL32(100, 100, 220, 255)});
        d1.partitions.push_back({"nvme0n1p5", "[swap]", "swap", 16.0f, 2.1f, IM_COL32(180, 80, 180, 255)});
        disks.push_back(d1);

        DiskInfo d2{};
        snprintf(d2.device, 32, "/dev/sda");
        snprintf(d2.model, 64, "WD Blue 2TB HDD");
        d2.total_gb = 1863.02f;
        d2.smart_status = 1;
        d2.temp_c = 42;
        d2.power_on_hours = 18200;
        d2.partitions.push_back({"sda1", "/data", "ext4", 1800.0f, 890.0f, IM_COL32(220, 180, 50, 255)});
        d2.partitions.push_back({"sda2", "", "ext4", 63.0f, 0.0f, IM_COL32(120, 120, 120, 255)});
        disks.push_back(d2);
    }
};

inline void render_disk_panel(DiskState& st) {
    if (st.disks.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("DISK MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Disk selector
    ImGui::Text("Disk:");
    ImGui::SameLine();
    for (int i = 0; i < (int)st.disks.size(); ++i) {
        ImGui::SameLine();
        char label[128];
        snprintf(label, 128, "%s (%s)##disk%d", st.disks[i].device, st.disks[i].model, i);
        if (ImGui::RadioButton(label, st.selected_disk == i)) {
            st.selected_disk = i;
            st.selected_partition = -1;
        }
    }
    ImGui::Spacing();

    if (st.selected_disk < 0 || st.selected_disk >= (int)st.disks.size()) return;
    auto& disk = st.disks[st.selected_disk];

    // Partition map visualization
    ImGui::BeginChild("##part_map", ImVec2(-1, 100), true);
    ImGui::TextDisabled("Partition Map: %s (%.1f GB)", disk.model, disk.total_gb);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 bar_pos = ImGui::GetCursorScreenPos();
    float bar_w = ImGui::GetContentRegionAvail().x - 10;
    float bar_h = 40;

    // Background
    draw->AddRectFilled(bar_pos, ImVec2(bar_pos.x + bar_w, bar_pos.y + bar_h),
                        IM_COL32(30, 30, 50, 255), 4.0f);

    float x = bar_pos.x;
    for (int i = 0; i < (int)disk.partitions.size(); ++i) {
        auto& p = disk.partitions[i];
        float pw = (p.size_gb / disk.total_gb) * bar_w;
        if (pw < 2) pw = 2;

        ImVec2 tl(x, bar_pos.y);
        ImVec2 br(x + pw - 1, bar_pos.y + bar_h);

        bool hovered = ImGui::GetIO().MousePos.x >= tl.x && ImGui::GetIO().MousePos.x <= br.x &&
                       ImGui::GetIO().MousePos.y >= tl.y && ImGui::GetIO().MousePos.y <= br.y;
        bool selected = (st.selected_partition == i);

        ImU32 col = p.color;
        if (selected) col = IM_COL32(255, 255, 255, 100);

        draw->AddRectFilled(tl, br, col, 2.0f);
        if (hovered || selected)
            draw->AddRect(tl, br, IM_COL32(255, 255, 255, 255), 2.0f, 0, 2.0f);

        if (pw > 40) {
            draw->AddText(ImVec2(tl.x + 4, tl.y + 4), IM_COL32(255, 255, 255, 255), p.name);
            char sz[32]; snprintf(sz, 32, "%.1f GB", p.size_gb);
            draw->AddText(ImVec2(tl.x + 4, tl.y + 20), IM_COL32(200, 200, 200, 200), sz);
        }

        if (hovered && ImGui::IsMouseClicked(0)) {
            st.selected_partition = i;
        }

        x += pw;
    }
    ImGui::Dummy(ImVec2(0, bar_h + 8));
    ImGui::EndChild();
    ImGui::Spacing();

    float left_w = ImGui::GetContentRegionAvail().x * 0.55f;

    // Partition details / usage
    ImGui::BeginChild("##part_detail", ImVec2(left_w, 0), true);
    ImGui::TextColored(accent, "Partitions");
    ImGui::Separator();

    if (ImGui::BeginTable("##ptable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Partition");
        ImGui::TableSetupColumn("Mount");
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)disk.partitions.size(); ++i) {
            auto& p = disk.partitions[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(p.name, st.selected_partition == i, ImGuiSelectableFlags_SpanAllColumns)) {
                st.selected_partition = i;
            }
            ImGui::TableSetColumnIndex(1);
            if (strlen(p.mount) > 0) ImGui::Text("%s", p.mount);
            else ImGui::TextDisabled("Not mounted");
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", p.fs_type);

            ImGui::TableSetColumnIndex(3);
            float usage = (p.size_gb > 0) ? p.used_gb / p.size_gb : 0;
            char overlay[64];
            snprintf(overlay, 64, "%.1f / %.1f GB", p.used_gb, p.size_gb);
            ImGui::ProgressBar(usage, ImVec2(-1, 16), overlay);

            ImGui::TableSetColumnIndex(4);
            if (strlen(p.mount) > 0) {
                if (ImGui::SmallButton("Unmount")) {
                    p.mount[0] = '\0';
                }
            } else {
                if (ImGui::SmallButton("Mount")) {
                    st.show_mount_dialog = true;
                    st.selected_partition = i;
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
            if (ImGui::SmallButton("Fmt")) {
                st.show_format_confirm = true;
                st.selected_partition = i;
            }
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // SMART / Health
    ImGui::BeginChild("##smart", ImVec2(0, 0), true);
    ImGui::TextColored(accent, "Disk Health (SMART)");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Model: %s", disk.model);
    ImGui::Text("Device: %s", disk.device);
    ImGui::Text("Capacity: %.1f GB", disk.total_gb);
    ImGui::Spacing();

    // SMART status indicator
    ImGui::Text("Status:");
    ImGui::SameLine();
    if (disk.smart_status == 0) {
        ImGui::TextColored(accent, "HEALTHY");
        ImDrawList* sdraw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        sdraw->AddCircleFilled(ImVec2(p.x + 20, p.y + 20), 15, IM_COL32(0, 200, 100, 255));
        ImGui::Dummy(ImVec2(0, 44));
    } else if (disk.smart_status == 1) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "WARNING");
        ImDrawList* sdraw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        sdraw->AddCircleFilled(ImVec2(p.x + 20, p.y + 20), 15, IM_COL32(255, 200, 0, 255));
        ImGui::Dummy(ImVec2(0, 44));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "FAILING");
        ImDrawList* sdraw = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        sdraw->AddCircleFilled(ImVec2(p.x + 20, p.y + 20), 15, IM_COL32(255, 50, 50, 255));
        ImGui::Dummy(ImVec2(0, 44));
    }

    ImGui::Text("Temperature: %d C", disk.temp_c);
    ImGui::Text("Power-On Hours: %d", disk.power_on_hours);

    // Total usage
    float total_used = 0, total_size = 0;
    for (auto& p : disk.partitions) { total_used += p.used_gb; total_size += p.size_gb; }
    ImGui::Spacing();
    ImGui::Text("Total Usage:");
    char usage_str[64];
    snprintf(usage_str, 64, "%.1f / %.1f GB", total_used, total_size);
    ImGui::ProgressBar(total_used / std::max(total_size, 0.01f), ImVec2(-1, 20), usage_str);

    ImGui::EndChild();

    // Format confirmation
    if (st.show_format_confirm) {
        ImGui::OpenPopup("Confirm Format");
        st.show_format_confirm = false;
    }
    if (ImGui::BeginPopupModal("Confirm Format", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "WARNING: This will erase all data!");
        if (st.selected_partition >= 0 && st.selected_partition < (int)disk.partitions.size()) {
            ImGui::Text("Partition: %s", disk.partitions[st.selected_partition].name);
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
        if (ImGui::Button("Format", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Mount dialog
    if (st.show_mount_dialog) {
        ImGui::OpenPopup("Mount Partition");
        st.show_mount_dialog = false;
    }
    if (ImGui::BeginPopupModal("Mount Partition", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Mount Point:");
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##mount", "/mnt/data", st.mount_point, sizeof(st.mount_point));
        ImGui::Spacing();
        if (ImGui::Button("Mount", ImVec2(120, 30)) && strlen(st.mount_point) > 0) {
            if (st.selected_partition >= 0 && st.selected_partition < (int)disk.partitions.size()) {
                snprintf(disk.partitions[st.selected_partition].mount, 64, "%s", st.mount_point);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::disk
