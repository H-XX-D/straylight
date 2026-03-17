// apps/migrate-gui/migrate_panel.h
// StrayLight Migrate GUI — Migration panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::migrate {

struct MigrateState {
    // Export options
    bool export_configs = true;
    bool export_themes = true;
    bool export_packages = true;
    bool export_dotfiles = true;
    bool export_ssh_keys = false;
    bool export_cron_jobs = true;
    bool export_desktop_settings = true;
    bool export_shell_history = false;
    char export_path[512] = "/tmp/straylight-export.tar.gz";
    bool exporting = false;
    float export_progress = 0.0f;

    // Import
    char import_path[512] = {};
    bool importing = false;
    float import_progress = 0.0f;
    bool show_import_preview = false;
    struct ImportItem {
        std::string name;
        std::string type;
        std::string size;
        bool        selected;
    };
    std::vector<ImportItem> import_preview;

    // Sync
    char sync_host[256] = {};
    char sync_user[128] = "root";
    int  sync_port = 22;
    bool syncing = false;
    float sync_progress = 0.0f;
    bool show_sync_diff = false;
    struct DiffEntry {
        std::string path;
        std::string status; // "added", "modified", "deleted"
        std::string size;
    };
    std::vector<DiffEntry> sync_diff;

    int active_tab = 0;

    void init() {
        // Sample import preview
        import_preview.push_back({"/etc/straylight/", "configs", "24 KB", true});
        import_preview.push_back({"~/.config/straylight/theme.json", "themes", "2 KB", true});
        import_preview.push_back({"package-list.txt (142 packages)", "packages", "4 KB", true});
        import_preview.push_back({"~/.bashrc, ~/.zshrc, ~/.profile", "dotfiles", "8 KB", true});
        import_preview.push_back({"crontab entries (7 jobs)", "cron", "1 KB", true});
        import_preview.push_back({"~/.config/straylight/desktop/", "desktop", "12 KB", true});

        // Sample sync diff
        sync_diff.push_back({"/etc/straylight/flux.conf", "modified", "+12 -3 lines"});
        sync_diff.push_back({"/etc/straylight/shield.conf", "modified", "+5 -2 lines"});
        sync_diff.push_back({"/etc/straylight/mesh.conf", "added", "2.1 KB"});
        sync_diff.push_back({"/etc/straylight/deprecated.conf", "deleted", "800 B"});
        sync_diff.push_back({"~/.config/straylight/theme.json", "modified", "+28 -14 lines"});
        sync_diff.push_back({"~/.local/bin/custom-tool", "added", "4.5 KB"});
    }
};

inline void render_migrate_panel(MigrateState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT MIGRATE");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##migrate_tabs")) {
        // --- Export Tab ---
        if (ImGui::BeginTabItem("Export")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Export System Configuration");
            ImGui::TextDisabled("Select what to include in the export bundle:");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Columns(2, "##export_opts", false);

            ImGui::Checkbox("System Configs (/etc/straylight/)", &st.export_configs);
            ImGui::Checkbox("Theme & Appearance", &st.export_themes);
            ImGui::Checkbox("Installed Packages List", &st.export_packages);
            ImGui::Checkbox("Dotfiles (.bashrc, .zshrc, etc.)", &st.export_dotfiles);

            ImGui::NextColumn();

            ImGui::Checkbox("SSH Keys & Config", &st.export_ssh_keys);
            if (st.export_ssh_keys) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(sensitive!)");
            }
            ImGui::Checkbox("Cron Jobs", &st.export_cron_jobs);
            ImGui::Checkbox("Desktop Settings", &st.export_desktop_settings);
            ImGui::Checkbox("Shell History", &st.export_shell_history);

            ImGui::Columns(1);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Export Path:");
            ImGui::SetNextItemWidth(-120);
            ImGui::InputText("##export_path", st.export_path, sizeof(st.export_path));
            ImGui::SameLine();

            if (st.exporting) {
                ImGui::BeginDisabled();
                ImGui::Button("Exporting...");
                ImGui::EndDisabled();
                ImGui::Spacing();
                ImGui::ProgressBar(st.export_progress, ImVec2(-1, 25));
                st.export_progress += ImGui::GetIO().DeltaTime * 0.12f;
                if (st.export_progress >= 1.0f) { st.exporting = false; st.export_progress = 1.0f; }
            } else {
                if (ImGui::Button("Export", ImVec2(100, 0))) {
                    st.exporting = true;
                    st.export_progress = 0.0f;
                }
                if (st.export_progress >= 1.0f) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Export completed successfully!");
                    ImGui::Text("Saved to: %s", st.export_path);
                }
            }

            // Size estimate
            ImGui::Spacing();
            int items = 0;
            if (st.export_configs) items++;
            if (st.export_themes) items++;
            if (st.export_packages) items++;
            if (st.export_dotfiles) items++;
            if (st.export_ssh_keys) items++;
            if (st.export_cron_jobs) items++;
            if (st.export_desktop_settings) items++;
            if (st.export_shell_history) items++;
            ImGui::TextDisabled("Estimated size: ~%d KB (%d categories selected)", items * 15, items);

            ImGui::EndTabItem();
        }

        // --- Import Tab ---
        if (ImGui::BeginTabItem("Import")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Import Configuration Bundle");
            ImGui::Spacing();

            ImGui::Text("Import From:");
            ImGui::SetNextItemWidth(-120);
            ImGui::InputTextWithHint("##import_path", "/path/to/export.tar.gz", st.import_path, sizeof(st.import_path));
            ImGui::SameLine();
            if (ImGui::Button("Preview", ImVec2(100, 0))) {
                st.show_import_preview = true;
            }

            ImGui::Spacing();

            if (st.show_import_preview) {
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Bundle Contents:");
                ImGui::Spacing();

                if (ImGui::BeginTable("##import_preview", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Include", ImGuiTableColumnFlags_WidthFixed, 60);
                    ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)st.import_preview.size(); ++i) {
                        auto& item = st.import_preview[i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID(i);
                        ImGui::Checkbox("##sel", &item.selected);
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", item.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", item.type.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", item.size.c_str());
                    }
                    ImGui::EndTable();
                }

                ImGui::Spacing();
                if (st.importing) {
                    ImGui::ProgressBar(st.import_progress, ImVec2(-1, 25));
                    st.import_progress += ImGui::GetIO().DeltaTime * 0.12f;
                    if (st.import_progress >= 1.0f) { st.importing = false; st.import_progress = 1.0f; }
                } else {
                    if (ImGui::Button("Import Selected", ImVec2(160, 30))) {
                        st.importing = true;
                        st.import_progress = 0.0f;
                    }
                    if (st.import_progress >= 1.0f) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Import completed!");
                    }
                }
            }

            ImGui::EndTabItem();
        }

        // --- Sync Tab ---
        if (ImGui::BeginTabItem("Sync")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Sync Configuration with Remote Host");
            ImGui::Spacing();

            ImGui::Text("Remote Host:");
            ImGui::SetNextItemWidth(300);
            ImGui::InputTextWithHint("##sync_host", "hostname or IP", st.sync_host, sizeof(st.sync_host));
            ImGui::SameLine();
            ImGui::Text("User:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##sync_user", st.sync_user, sizeof(st.sync_user));
            ImGui::SameLine();
            ImGui::Text("Port:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(60);
            ImGui::InputInt("##sync_port", &st.sync_port, 0);

            ImGui::Spacing();
            if (ImGui::Button("Show Diff", ImVec2(120, 30))) { st.show_sync_diff = true; }
            ImGui::SameLine();
            if (st.syncing) {
                ImGui::BeginDisabled();
                ImGui::Button("Syncing...", ImVec2(120, 30));
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::ProgressBar(st.sync_progress, ImVec2(200, 30));
                st.sync_progress += ImGui::GetIO().DeltaTime * 0.08f;
                if (st.sync_progress >= 1.0f) { st.syncing = false; st.sync_progress = 1.0f; }
            } else {
                if (ImGui::Button("Sync", ImVec2(120, 30))) {
                    st.syncing = true;
                    st.sync_progress = 0.0f;
                }
                if (st.sync_progress >= 1.0f) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "Sync completed!");
                }
            }

            ImGui::Spacing();

            if (st.show_sync_diff) {
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::Text("Configuration Diff (local vs remote):");
                ImGui::Spacing();

                if (ImGui::BeginTable("##sync_diff", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                        ImVec2(0, -1))) {
                    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 100);
                    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 140);
                    ImGui::TableHeadersRow();

                    for (auto& d : st.sync_diff) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", d.path.c_str());
                        ImGui::TableNextColumn();
                        ImVec4 col = d.status == "added"    ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                                   : d.status == "modified" ? ImVec4(1.0f, 0.8f, 0.2f, 1.0f)
                                   : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                        ImGui::TextColored(col, "%s", d.status.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", d.size.c_str());
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace straylight::migrate
