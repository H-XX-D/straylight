// apps/cron-gui/cron_panel.h
// StrayLight Cron GUI — Task Scheduler panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::cron {

struct TaskExecution {
    std::string timestamp;
    int         exit_code;
    float       duration_sec;
    std::string stdout_text;
    std::string stderr_text;
};

struct CronTask {
    std::string name;
    std::string command;
    std::string schedule;
    std::string last_run;
    std::string next_run;
    int         status; // 0=idle, 1=running, 2=failed, 3=disabled
    bool        enabled;
    std::string resource_constraints;
    std::vector<std::string> dependencies;
    std::vector<TaskExecution> history;
};

struct CronState {
    std::vector<CronTask> tasks;
    int selected_index = -1;

    // Add task dialog
    bool show_add_dialog = false;
    char new_name[128] = {};
    char new_command[512] = {};
    int  new_schedule_idx = 0;
    char new_custom_schedule[64] = {};
    char new_constraints[256] = {};

    // Delete
    bool show_delete_confirm = false;

    void init() {
        {
            CronTask t;
            t.name = "system-health-check";
            t.command = "/usr/bin/straylight-health --json > /var/log/straylight/health-latest.json";
            t.schedule = "*/5 * * * *";
            t.last_run = "2026-03-16 10:25:00";
            t.next_run = "2026-03-16 10:30:00";
            t.status = 0;
            t.enabled = true;
            t.resource_constraints = "max-cpu=10% max-mem=256M";
            t.history.push_back({"2026-03-16 10:25:00", 0, 2.3f, "Health score: 82/100\nAll checks passed.", ""});
            t.history.push_back({"2026-03-16 10:20:00", 0, 2.1f, "Health score: 83/100\nAll checks passed.", ""});
            t.history.push_back({"2026-03-16 10:15:00", 0, 2.4f, "Health score: 81/100\n1 warning: GPU VRAM elevated.", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "daily-backup";
            t.command = "/usr/bin/straylight-snapshot create --name daily-$(date +%Y%m%d)";
            t.schedule = "0 3 * * *";
            t.last_run = "2026-03-16 03:00:00";
            t.next_run = "2026-03-17 03:00:00";
            t.status = 0;
            t.enabled = true;
            t.resource_constraints = "max-cpu=50% max-mem=4G nice=19";
            t.dependencies = {"system-health-check"};
            t.history.push_back({"2026-03-16 03:00:00", 0, 245.0f, "Snapshot 'daily-20260316' created (1.8 GB)", ""});
            t.history.push_back({"2026-03-15 03:00:00", 0, 238.0f, "Snapshot 'daily-20260315' created (1.7 GB)", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "log-rotate";
            t.command = "/usr/sbin/logrotate /etc/logrotate.d/straylight";
            t.schedule = "0 0 * * *";
            t.last_run = "2026-03-16 00:00:00";
            t.next_run = "2026-03-17 00:00:00";
            t.status = 0;
            t.enabled = true;
            t.history.push_back({"2026-03-16 00:00:00", 0, 1.2f, "Rotated 8 log files, freed 420 MB", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "security-audit";
            t.command = "/usr/bin/straylight-shield audit --output /var/log/straylight/audit-latest.json";
            t.schedule = "0 6 * * 1";
            t.last_run = "2026-03-11 06:00:00";
            t.next_run = "2026-03-18 06:00:00";
            t.status = 0;
            t.enabled = true;
            t.resource_constraints = "max-cpu=25% max-mem=1G";
            t.history.push_back({"2026-03-11 06:00:00", 0, 45.0f, "Audit score: 78/100\n2 medium findings", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "ml-model-refresh";
            t.command = "/usr/bin/straylight-mesh sync-models --registry hub.straylight.local";
            t.schedule = "0 2 * * 0";
            t.last_run = "2026-03-10 02:00:00";
            t.next_run = "2026-03-17 02:00:00";
            t.status = 2;
            t.enabled = true;
            t.resource_constraints = "max-cpu=100% max-mem=8G gpu=required";
            t.history.push_back({"2026-03-10 02:00:00", 1, 180.0f, "", "ERROR: Registry unreachable at hub.straylight.local:5000\nConnection refused"});
            t.history.push_back({"2026-03-03 02:00:00", 0, 320.0f, "Synced 3 models (12.4 GB total)", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "package-update-check";
            t.command = "/usr/bin/straylight-migrate check-updates --quiet";
            t.schedule = "0 8 * * *";
            t.last_run = "2026-03-16 08:00:00";
            t.next_run = "2026-03-17 08:00:00";
            t.status = 0;
            t.enabled = true;
            t.history.push_back({"2026-03-16 08:00:00", 0, 8.5f, "14 updates available (3 security)", ""});
            tasks.push_back(t);
        }
        {
            CronTask t;
            t.name = "temp-cleanup";
            t.command = "find /tmp -type f -mtime +7 -delete && find /var/tmp -type f -mtime +30 -delete";
            t.schedule = "0 4 * * *";
            t.last_run = "2026-03-16 04:00:00";
            t.next_run = "2026-03-17 04:00:00";
            t.status = 3;
            t.enabled = false;
            t.history.push_back({"2026-03-15 04:00:00", 0, 0.8f, "Removed 156 files (2.3 GB)", ""});
            tasks.push_back(t);
        }
    }
};

inline void render_cron_panel(CronState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT CRON");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar
    if (ImGui::Button("Add Task", ImVec2(120, 30))) {
        st.show_add_dialog = true;
        memset(st.new_name, 0, sizeof(st.new_name));
        memset(st.new_command, 0, sizeof(st.new_command));
        memset(st.new_custom_schedule, 0, sizeof(st.new_custom_schedule));
        memset(st.new_constraints, 0, sizeof(st.new_constraints));
        st.new_schedule_idx = 0;
    }
    ImGui::SameLine();
    bool has_sel = st.selected_index >= 0 && st.selected_index < (int)st.tasks.size();
    if (!has_sel) ImGui::BeginDisabled();
    if (ImGui::Button("Run Now", ImVec2(100, 30))) {
        if (has_sel) {
            st.tasks[st.selected_index].status = 1;
            st.tasks[st.selected_index].last_run = "2026-03-16 10:30:00";
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
    if (ImGui::Button("Delete", ImVec2(80, 30))) { st.show_delete_confirm = true; }
    ImGui::PopStyleColor();
    if (!has_sel) ImGui::EndDisabled();

    ImGui::Spacing();

    // Task table
    float table_h = ImGui::GetContentRegionAvail().y * 0.45f;
    if (ImGui::BeginChild("##task_table_area", ImVec2(0, table_h), false)) {
        if (ImGui::BeginTable("##tasks", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Schedule", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Last Run", ImGuiTableColumnFlags_WidthFixed, 160);
            ImGui::TableSetupColumn("Next Run", ImGuiTableColumnFlags_WidthFixed, 160);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Toggle", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)st.tasks.size(); ++i) {
                auto& t = st.tasks[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                bool sel = (i == st.selected_index);
                if (ImGui::Selectable(t.name.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns)) {
                    st.selected_index = i;
                }

                ImGui::TableNextColumn();
                ImGui::Text("%s", t.schedule.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", t.last_run.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", t.next_run.c_str());

                ImGui::TableNextColumn();
                ImVec4 status_col;
                const char* status_str;
                switch (t.status) {
                    case 0: status_col = ImVec4(0.2f, 1.0f, 0.5f, 1.0f); status_str = "Idle"; break;
                    case 1: status_col = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); status_str = "Running"; break;
                    case 2: status_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); status_str = "Failed"; break;
                    default: status_col = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); status_str = "Disabled"; break;
                }
                ImGui::TextColored(status_col, "%s", status_str);

                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::Checkbox("##enable", &t.enabled)) {
                    t.status = t.enabled ? 0 : 3;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    // Task detail panel
    if (ImGui::BeginChild("##task_detail", ImVec2(0, -1), false)) {
        if (has_sel) {
            auto& t = st.tasks[st.selected_index];
            ImGui::Columns(2, "##detail_cols", true);

            // Left: task info
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "%s", t.name.c_str());
            ImGui::Separator();
            ImGui::Text("Command:");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.05f, 0.05f, 0.08f, 1.0f));
            char cmd[512];
            snprintf(cmd, sizeof(cmd), "%s", t.command.c_str());
            ImGui::InputTextMultiline("##cmd", cmd, sizeof(cmd), ImVec2(-1, 60), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
            ImGui::Text("Schedule: %s", t.schedule.c_str());
            if (!t.resource_constraints.empty())
                ImGui::Text("Constraints: %s", t.resource_constraints.c_str());
            if (!t.dependencies.empty()) {
                ImGui::Text("Dependencies:");
                for (auto& d : t.dependencies) {
                    ImGui::SameLine();
                    ImGui::SmallButton(d.c_str());
                }
            }

            ImGui::NextColumn();

            // Right: execution history
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Execution History");
            ImGui::Separator();
            for (auto& h : t.history) {
                ImVec4 col = h.exit_code == 0 ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(col, "[exit %d]", h.exit_code);
                ImGui::SameLine();
                ImGui::Text("%s (%.1fs)", h.timestamp.c_str(), h.duration_sec);

                if (!h.stdout_text.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                    ImGui::TextWrapped("  stdout: %s", h.stdout_text.c_str());
                    ImGui::PopStyleColor();
                }
                if (!h.stderr_text.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                    ImGui::TextWrapped("  stderr: %s", h.stderr_text.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::Spacing();
            }

            ImGui::Columns(1);
        } else {
            ImGui::TextDisabled("Select a task to view details");
        }
    }
    ImGui::EndChild();

    // Add Task dialog
    if (st.show_add_dialog) {
        ImGui::OpenPopup("Add Task");
        st.show_add_dialog = false;
    }
    if (ImGui::BeginPopupModal("Add Task", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Task Name:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##task_name", st.new_name, sizeof(st.new_name));

        ImGui::Text("Command:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextMultiline("##task_cmd", st.new_command, sizeof(st.new_command), ImVec2(400, 60));

        ImGui::Text("Schedule:");
        const char* schedules[] = {"Every 5 minutes", "Hourly", "Daily at 3 AM", "Weekly (Sunday)", "Custom..."};
        ImGui::SetNextItemWidth(400);
        ImGui::Combo("##schedule", &st.new_schedule_idx, schedules, 5);
        if (st.new_schedule_idx == 4) {
            ImGui::SetNextItemWidth(400);
            ImGui::InputTextWithHint("##custom_sched", "cron expression (e.g. */5 * * * *)",
                                     st.new_custom_schedule, sizeof(st.new_custom_schedule));
        }

        ImGui::Text("Resource Constraints:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextWithHint("##constraints", "e.g. max-cpu=50% max-mem=2G",
                                 st.new_constraints, sizeof(st.new_constraints));

        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(120, 30))) {
            if (strlen(st.new_name) > 0 && strlen(st.new_command) > 0) {
                CronTask t;
                t.name = st.new_name;
                t.command = st.new_command;
                const char* cron_exprs[] = {"*/5 * * * *", "0 * * * *", "0 3 * * *", "0 0 * * 0"};
                t.schedule = st.new_schedule_idx < 4 ? cron_exprs[st.new_schedule_idx] : st.new_custom_schedule;
                t.last_run = "never";
                t.next_run = "pending";
                t.status = 0;
                t.enabled = true;
                t.resource_constraints = st.new_constraints;
                st.tasks.push_back(t);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Delete confirmation
    if (st.show_delete_confirm) {
        ImGui::OpenPopup("Confirm Delete##cron");
        st.show_delete_confirm = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete##cron", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (has_sel) {
            ImGui::Text("Delete task '%s'?", st.tasks[st.selected_index].name.c_str());
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
        if (ImGui::Button("Delete", ImVec2(120, 30))) {
            if (has_sel) {
                st.tasks.erase(st.tasks.begin() + st.selected_index);
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

} // namespace straylight::cron
