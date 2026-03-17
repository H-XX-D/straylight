// apps/rewind-gui/rewind_panel.h
// StrayLight Process Rewind panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace straylight::rewind {

struct Checkpoint {
    char timestamp[32];
    char type[16];     // "auto", "manual", "signal"
    float size_mb;
    int  id;
};

struct TrackedProcess {
    int  pid;
    char name[64];
    char status[16];
    int  checkpoint_count;
    std::vector<Checkpoint> checkpoints;
};

struct RewindState {
    std::vector<TrackedProcess> processes;
    int selected_process = 0;
    int selected_checkpoint = -1;
    float timeline_pos = 1.0f;  // 0..1

    bool show_restore_confirm = false;
    bool restoring = false;
    float restore_progress = 0;

    void init() {
        TrackedProcess p1;
        p1.pid = 1234; snprintf(p1.name, 64, "firefox"); snprintf(p1.status, 16, "running");
        p1.checkpoint_count = 5;
        p1.checkpoints.push_back({"10:00:01", "auto", 128.5f, 1});
        p1.checkpoints.push_back({"10:05:00", "auto", 132.1f, 2});
        p1.checkpoints.push_back({"10:10:00", "auto", 140.3f, 3});
        p1.checkpoints.push_back({"10:12:30", "manual", 138.8f, 4});
        p1.checkpoints.push_back({"10:15:00", "auto", 145.2f, 5});
        processes.push_back(p1);

        TrackedProcess p2;
        p2.pid = 2345; snprintf(p2.name, 64, "code-editor"); snprintf(p2.status, 16, "running");
        p2.checkpoint_count = 3;
        p2.checkpoints.push_back({"09:30:00", "auto", 85.4f, 1});
        p2.checkpoints.push_back({"09:45:00", "auto", 92.1f, 2});
        p2.checkpoints.push_back({"10:00:00", "signal", 88.7f, 3});
        processes.push_back(p2);

        TrackedProcess p3;
        p3.pid = 3456; snprintf(p3.name, 64, "database-server"); snprintf(p3.status, 16, "running");
        p3.checkpoint_count = 8;
        p3.checkpoints.push_back({"08:00:00", "auto", 256.0f, 1});
        p3.checkpoints.push_back({"08:30:00", "auto", 258.3f, 2});
        p3.checkpoints.push_back({"09:00:00", "auto", 262.1f, 3});
        p3.checkpoints.push_back({"09:30:00", "auto", 265.8f, 4});
        p3.checkpoints.push_back({"10:00:00", "auto", 270.5f, 5});
        p3.checkpoints.push_back({"10:02:15", "signal", 268.2f, 6});
        p3.checkpoints.push_back({"10:10:00", "manual", 272.0f, 7});
        p3.checkpoints.push_back({"10:14:00", "auto", 274.1f, 8});
        processes.push_back(p3);

        TrackedProcess p4;
        p4.pid = 4567; snprintf(p4.name, 64, "build-daemon"); snprintf(p4.status, 16, "stopped");
        p4.checkpoint_count = 2;
        p4.checkpoints.push_back({"09:00:00", "auto", 45.2f, 1});
        p4.checkpoints.push_back({"09:15:00", "auto", 52.8f, 2});
        processes.push_back(p4);
    }
};

inline void render_rewind_panel(RewindState& st) {
    if (st.processes.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("PROCESS REWIND");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Process table
    ImGui::BeginChild("##proc_table", ImVec2(-1, 180), true);
    ImGui::TextColored(accent, "Tracked Processes");
    ImGui::Separator();

    if (ImGui::BeginTable("##ptable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Checkpoints", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Total Size", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableHeadersRow();

        for (int i = 0; i < (int)st.processes.size(); ++i) {
            auto& p = st.processes[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", p.pid);
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Selectable(p.name, st.selected_process == i, ImGuiSelectableFlags_SpanAllColumns)) {
                st.selected_process = i;
                st.selected_checkpoint = -1;
                st.timeline_pos = 1.0f;
            }
            ImGui::TableSetColumnIndex(2);
            if (strcmp(p.status, "running") == 0)
                ImGui::TextColored(accent, "%s", p.status);
            else
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "%s", p.status);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", p.checkpoint_count);

            float total = 0;
            for (auto& c : p.checkpoints) total += c.size_mb;
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f MB", total);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::Spacing();

    if (st.selected_process < 0 || st.selected_process >= (int)st.processes.size()) return;
    auto& proc = st.processes[st.selected_process];

    // Timeline scrubber
    ImGui::BeginChild("##timeline", ImVec2(-1, 80), true);
    ImGui::TextColored(accent, "Timeline: %s (PID %d)", proc.name, proc.pid);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 tl_pos = ImGui::GetCursorScreenPos();
    float tl_w = ImGui::GetContentRegionAvail().x - 20;
    float tl_h = 30;
    float tl_y = tl_pos.y + 10;

    // Timeline bar
    draw->AddRectFilled(ImVec2(tl_pos.x, tl_y),
                        ImVec2(tl_pos.x + tl_w, tl_y + tl_h),
                        IM_COL32(30, 30, 50, 255), 4.0f);

    // Checkpoint markers
    int n_cp = (int)proc.checkpoints.size();
    for (int i = 0; i < n_cp; ++i) {
        float x = tl_pos.x + (tl_w * (float)(i) / (float)(std::max(n_cp - 1, 1)));
        ImU32 dot_col;
        if (strcmp(proc.checkpoints[i].type, "manual") == 0)
            dot_col = IM_COL32(0, 255, 136, 255);
        else if (strcmp(proc.checkpoints[i].type, "signal") == 0)
            dot_col = IM_COL32(255, 200, 0, 255);
        else
            dot_col = IM_COL32(100, 150, 220, 255);

        bool is_selected = (st.selected_checkpoint == i);
        float r = is_selected ? 8.0f : 5.0f;
        draw->AddCircleFilled(ImVec2(x, tl_y + tl_h * 0.5f), r, dot_col);

        // Label
        draw->AddText(ImVec2(x - 15, tl_y + tl_h + 2),
                      IM_COL32(150, 150, 150, 255), proc.checkpoints[i].timestamp);

        // Click detection
        ImGuiIO& io = ImGui::GetIO();
        float dx = io.MousePos.x - x;
        float dy = io.MousePos.y - (tl_y + tl_h * 0.5f);
        if (dx*dx + dy*dy < 100 && ImGui::IsMouseClicked(0)) {
            st.selected_checkpoint = i;
            st.timeline_pos = (float)i / (float)(std::max(n_cp - 1, 1));
        }
    }

    // Scrubber handle
    float sx = tl_pos.x + tl_w * st.timeline_pos;
    draw->AddLine(ImVec2(sx, tl_y - 4), ImVec2(sx, tl_y + tl_h + 4),
                  IM_COL32(255, 255, 255, 200), 2.0f);
    draw->AddTriangleFilled(
        ImVec2(sx - 6, tl_y - 4), ImVec2(sx + 6, tl_y - 4), ImVec2(sx, tl_y + 2),
        IM_COL32(0, 255, 136, 255));

    ImGui::Dummy(ImVec2(0, tl_h + 30));

    // Slider for timeline
    ImGui::SetNextItemWidth(tl_w);
    if (ImGui::SliderFloat("##tl_slider", &st.timeline_pos, 0.0f, 1.0f, "")) {
        int idx = (int)(st.timeline_pos * (n_cp - 1) + 0.5f);
        st.selected_checkpoint = std::clamp(idx, 0, n_cp - 1);
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // Checkpoint details + restore
    ImGui::BeginChild("##checkpoint_detail", ImVec2(-1, 0), true);
    if (st.selected_checkpoint >= 0 && st.selected_checkpoint < n_cp) {
        auto& cp = proc.checkpoints[st.selected_checkpoint];

        ImGui::TextColored(accent, "Checkpoint #%d", cp.id);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Timestamp:"); ImGui::SameLine(120); ImGui::Text("%s", cp.timestamp);
        ImGui::Text("Type:");      ImGui::SameLine(120);
        if (strcmp(cp.type, "manual") == 0)
            ImGui::TextColored(accent, "%s", cp.type);
        else if (strcmp(cp.type, "signal") == 0)
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "%s", cp.type);
        else
            ImGui::Text("%s", cp.type);
        ImGui::Text("Size:");      ImGui::SameLine(120); ImGui::Text("%.1f MB", cp.size_mb);

        ImGui::Spacing();
        if (!st.restoring) {
            if (ImGui::Button("Restore to This Checkpoint", ImVec2(260, 32))) {
                st.show_restore_confirm = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Create Checkpoint Now", ImVec2(200, 32))) {
                Checkpoint nc;
                snprintf(nc.timestamp, 32, "now");
                snprintf(nc.type, 16, "manual");
                nc.size_mb = cp.size_mb + 2.0f;
                nc.id = n_cp + 1;
                proc.checkpoints.push_back(nc);
                proc.checkpoint_count++;
            }
        } else {
            st.restore_progress += ImGui::GetIO().DeltaTime * 0.3f;
            if (st.restore_progress >= 1.0f) {
                st.restoring = false;
                st.restore_progress = 0;
            }
            ImGui::ProgressBar(st.restore_progress, ImVec2(-1, 24), "Restoring process state...");
        }
    } else {
        ImGui::TextDisabled("Click a checkpoint on the timeline to view details");
    }
    ImGui::EndChild();

    // Restore confirm
    if (st.show_restore_confirm) {
        ImGui::OpenPopup("Confirm Restore");
        st.show_restore_confirm = false;
    }
    if (ImGui::BeginPopupModal("Confirm Restore", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Restore %s (PID %d) to checkpoint #%d?",
                     proc.name, proc.pid,
                     st.selected_checkpoint >= 0 ? proc.checkpoints[st.selected_checkpoint].id : 0);
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Current process state will be replaced.");
        ImGui::Spacing();
        if (ImGui::Button("Restore", ImVec2(120, 30))) {
            st.restoring = true;
            st.restore_progress = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::rewind
