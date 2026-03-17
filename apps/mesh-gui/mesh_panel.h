// apps/mesh-gui/mesh_panel.h
// StrayLight Mesh GUI — GPU Mesh Dashboard panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::mesh {

struct GpuCard {
    std::string name;
    float       temperature;
    float       utilization; // 0-100
    float       vram_used;   // GB
    float       vram_total;  // GB
    float       power_watts;
};

struct MeshNode {
    std::string hostname;
    std::string ip;
    std::string status; // "online", "offline", "degraded"
    std::vector<GpuCard> gpus;
    float interconnect_bw; // GB/s to other nodes
};

struct MeshJob {
    int         id;
    std::string name;
    std::string command;
    std::string status; // "running", "queued", "completed", "failed"
    float       vram_required; // GB
    std::string placement;
    std::string node;
    float       progress;
};

struct MeshState {
    std::vector<MeshNode> nodes;
    std::vector<MeshJob> jobs;
    int selected_node = -1;

    // Submit job dialog
    bool show_submit_dialog = false;
    char job_name[128] = {};
    char job_command[512] = {};
    float job_vram = 8.0f;
    int  job_placement_idx = 0;
    int  next_job_id = 100;

    void init() {
        {
            MeshNode n;
            n.hostname = "gpu-node-01";
            n.ip = "192.168.1.20";
            n.status = "online";
            n.interconnect_bw = 200.0f;
            n.gpus.push_back({"NVIDIA RTX 4090", 58.0f, 45.0f, 12.5f, 24.0f, 280.0f});
            n.gpus.push_back({"NVIDIA RTX 4090", 62.0f, 78.0f, 18.2f, 24.0f, 320.0f});
            nodes.push_back(n);
        }
        {
            MeshNode n;
            n.hostname = "gpu-node-02";
            n.ip = "192.168.1.21";
            n.status = "online";
            n.interconnect_bw = 200.0f;
            n.gpus.push_back({"NVIDIA A100 80GB", 55.0f, 92.0f, 72.0f, 80.0f, 350.0f});
            n.gpus.push_back({"NVIDIA A100 80GB", 53.0f, 15.0f, 8.0f, 80.0f, 180.0f});
            nodes.push_back(n);
        }
        {
            MeshNode n;
            n.hostname = "gpu-node-03";
            n.ip = "192.168.1.22";
            n.status = "online";
            n.interconnect_bw = 100.0f;
            n.gpus.push_back({"NVIDIA RTX 4080", 48.0f, 0.0f, 0.5f, 16.0f, 50.0f});
            nodes.push_back(n);
        }
        {
            MeshNode n;
            n.hostname = "gpu-node-04";
            n.ip = "192.168.1.23";
            n.status = "offline";
            n.interconnect_bw = 0.0f;
            n.gpus.push_back({"NVIDIA RTX 3090", 0.0f, 0.0f, 0.0f, 24.0f, 0.0f});
            n.gpus.push_back({"NVIDIA RTX 3090", 0.0f, 0.0f, 0.0f, 24.0f, 0.0f});
            nodes.push_back(n);
        }

        jobs.push_back({1, "llama-70b-finetune", "torchrun --nproc_per_node=2 finetune.py --model llama-70b",
                        "running", 140.0f, "spread", "gpu-node-01, gpu-node-02", 0.45f});
        jobs.push_back({2, "resnet50-training", "python train.py --model resnet50 --epochs 100",
                        "running", 8.0f, "pack", "gpu-node-02", 0.72f});
        jobs.push_back({3, "bert-inference-bench", "python bench.py --model bert-large --batch 32",
                        "queued", 4.0f, "any", "", 0.0f});
        jobs.push_back({4, "image-gen-batch", "python generate.py --model sdxl --count 1000",
                        "completed", 12.0f, "any", "gpu-node-01", 1.0f});
        jobs.push_back({5, "data-preprocess", "python preprocess.py --dataset imagenet --gpu",
                        "failed", 2.0f, "any", "gpu-node-03", 0.15f});
    }
};

inline void render_mesh_panel(MeshState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT MESH");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Network topology view (top)
    float topo_h = 280.0f;
    if (ImGui::BeginChild("##topology", ImVec2(0, topo_h), true)) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "GPU Mesh Topology");
        ImGui::Separator();

        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float avail_w = ImGui::GetContentRegionAvail().x;
        float node_w = 220.0f;
        float node_h = 140.0f;
        float spacing = (avail_w - node_w * (float)st.nodes.size()) / (float)(st.nodes.size() + 1);

        // Draw interconnect lines between online nodes
        std::vector<ImVec2> node_centers;
        for (int i = 0; i < (int)st.nodes.size(); ++i) {
            float x = origin.x + spacing + (node_w + spacing) * (float)i;
            float y = origin.y + 20;
            node_centers.push_back(ImVec2(x + node_w * 0.5f, y + node_h * 0.5f));
        }
        for (int i = 0; i < (int)st.nodes.size(); ++i) {
            for (int j = i + 1; j < (int)st.nodes.size(); ++j) {
                if (st.nodes[i].status != "offline" && st.nodes[j].status != "offline") {
                    float bw = std::min(st.nodes[i].interconnect_bw, st.nodes[j].interconnect_bw);
                    ImU32 line_col = bw >= 200 ? IM_COL32(0, 200, 130, 100) : IM_COL32(100, 150, 200, 80);
                    draw->AddLine(node_centers[i], node_centers[j], line_col, 2.0f);
                    // BW label at midpoint
                    ImVec2 mid = ImVec2((node_centers[i].x + node_centers[j].x) * 0.5f,
                                        (node_centers[i].y + node_centers[j].y) * 0.5f - 10);
                    char bw_label[32];
                    snprintf(bw_label, sizeof(bw_label), "%.0f GB/s", bw);
                    draw->AddText(mid, IM_COL32(140, 140, 180, 200), bw_label);
                }
            }
        }

        // Draw node boxes
        for (int ni = 0; ni < (int)st.nodes.size(); ++ni) {
            auto& n = st.nodes[ni];
            float x = origin.x + spacing + (node_w + spacing) * (float)ni;
            float y = origin.y + 20;

            ImU32 border_col = n.status == "online"   ? IM_COL32(0, 200, 130, 200)
                             : n.status == "degraded" ? IM_COL32(200, 180, 40, 200)
                             : IM_COL32(120, 120, 120, 100);
            ImU32 bg_col = IM_COL32(20, 20, 35, 200);

            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + node_w, y + node_h), bg_col, 4.0f);
            draw->AddRect(ImVec2(x, y), ImVec2(x + node_w, y + node_h), border_col, 4.0f, 0, ni == st.selected_node ? 3.0f : 1.0f);

            // Node name
            draw->AddText(ImVec2(x + 8, y + 4), IM_COL32(220, 220, 220, 255), n.hostname.c_str());
            char ip_status[64];
            snprintf(ip_status, sizeof(ip_status), "%s [%s]", n.ip.c_str(), n.status.c_str());
            draw->AddText(ImVec2(x + 8, y + 20), IM_COL32(140, 140, 140, 200), ip_status);

            // GPU cards inside
            float gpu_y = y + 40;
            for (int gi = 0; gi < (int)n.gpus.size(); ++gi) {
                auto& g = n.gpus[gi];
                float gy = gpu_y + gi * 48.0f;

                // GPU name + temp
                char gpu_label[128];
                snprintf(gpu_label, sizeof(gpu_label), "%s", g.name.c_str());
                draw->AddText(ImVec2(x + 12, gy), IM_COL32(180, 180, 200, 255), gpu_label);

                if (n.status != "offline") {
                    char temp_label[32];
                    snprintf(temp_label, sizeof(temp_label), "%.0fC", g.temperature);
                    ImU32 temp_col = g.temperature < 60 ? IM_COL32(0, 200, 130, 255)
                                   : g.temperature < 80 ? IM_COL32(200, 180, 40, 255)
                                   : IM_COL32(200, 60, 60, 255);
                    draw->AddText(ImVec2(x + node_w - 40, gy), temp_col, temp_label);

                    // Utilization bar
                    float bar_x = x + 12;
                    float bar_y = gy + 16;
                    float bar_w = node_w - 24;
                    float bar_h = 6;
                    draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), IM_COL32(30, 30, 50, 255));
                    float util_w = bar_w * (g.utilization / 100.0f);
                    ImU32 util_col = g.utilization < 50 ? IM_COL32(0, 180, 120, 255)
                                   : g.utilization < 80 ? IM_COL32(200, 180, 40, 255)
                                   : IM_COL32(200, 60, 60, 255);
                    draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + util_w, bar_y + bar_h), util_col);

                    // VRAM bar
                    bar_y += bar_h + 2;
                    draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), IM_COL32(30, 30, 50, 255));
                    float vram_frac = g.vram_used / g.vram_total;
                    float vram_w = bar_w * vram_frac;
                    ImU32 vram_col = vram_frac < 0.5f ? IM_COL32(60, 120, 200, 255)
                                   : vram_frac < 0.8f ? IM_COL32(200, 180, 40, 255)
                                   : IM_COL32(200, 60, 60, 255);
                    draw->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + vram_w, bar_y + bar_h), vram_col);

                    // VRAM text
                    char vram_text[32];
                    snprintf(vram_text, sizeof(vram_text), "%.0f/%.0fGB", g.vram_used, g.vram_total);
                    draw->AddText(ImVec2(bar_x + bar_w + 2 - 70, bar_y - 2), IM_COL32(140, 140, 160, 200), vram_text);
                }
            }

            // Clickable area
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            char btn_id[32];
            snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
            if (ImGui::InvisibleButton(btn_id, ImVec2(node_w, node_h))) {
                st.selected_node = ni;
            }
        }

        ImGui::Dummy(ImVec2(0, node_h + 30));
    }
    ImGui::EndChild();

    ImGui::Spacing();

    // Bottom: Job list + Submit
    if (ImGui::BeginChild("##job_section", ImVec2(0, -1), false)) {
        // Toolbar
        if (ImGui::Button("Submit Job", ImVec2(120, 28))) {
            st.show_submit_dialog = true;
            memset(st.job_name, 0, sizeof(st.job_name));
            memset(st.job_command, 0, sizeof(st.job_command));
            st.job_vram = 8.0f;
            st.job_placement_idx = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Total VRAM: %.0f GB free across mesh",
                           []( const std::vector<MeshNode>& nodes) {
                               float total = 0;
                               for (auto& n : nodes) {
                                   if (n.status == "offline") continue;
                                   for (auto& g : n.gpus) total += g.vram_total - g.vram_used;
                               }
                               return total;
                           }(st.nodes));
        ImGui::Spacing();

        // Job table
        if (ImGui::BeginTable("##jobs", 7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("VRAM", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Placement", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Node(s)", ImGuiTableColumnFlags_WidthFixed, 180);
            ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableHeadersRow();

            for (auto& j : st.jobs) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", j.id);
                ImGui::TableNextColumn();
                ImGui::Text("%s", j.name.c_str());
                ImGui::TableNextColumn();
                ImVec4 status_col = j.status == "running"   ? ImVec4(0.2f, 1.0f, 0.5f, 1.0f)
                                  : j.status == "queued"    ? ImVec4(0.5f, 0.7f, 1.0f, 1.0f)
                                  : j.status == "completed" ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                                  : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                ImGui::TextColored(status_col, "%s", j.status.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.0f GB", j.vram_required);
                ImGui::TableNextColumn();
                ImGui::Text("%s", j.placement.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", j.node.empty() ? "pending" : j.node.c_str());
                ImGui::TableNextColumn();
                if (j.status == "running") {
                    ImGui::ProgressBar(j.progress, ImVec2(-1, 16));
                    j.progress += ImGui::GetIO().DeltaTime * 0.005f;
                    if (j.progress >= 1.0f) { j.status = "completed"; j.progress = 1.0f; }
                } else if (j.status == "completed") {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "100%%");
                } else if (j.status == "failed") {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%.0f%%", j.progress * 100.0f);
                } else {
                    ImGui::TextDisabled("--");
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // Submit Job dialog
    if (st.show_submit_dialog) {
        ImGui::OpenPopup("Submit Job");
        st.show_submit_dialog = false;
    }
    if (ImGui::BeginPopupModal("Submit Job", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Job Name:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputText("##job_name", st.job_name, sizeof(st.job_name));

        ImGui::Text("Command:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextMultiline("##job_cmd", st.job_command, sizeof(st.job_command), ImVec2(400, 60));

        ImGui::Text("VRAM Required (GB):");
        ImGui::SetNextItemWidth(400);
        ImGui::SliderFloat("##vram_req", &st.job_vram, 1.0f, 160.0f, "%.0f GB", ImGuiSliderFlags_Logarithmic);

        ImGui::Text("Placement Strategy:");
        const char* placements[] = {"Any - first available", "Pack - minimize nodes", "Spread - maximize distribution"};
        ImGui::SetNextItemWidth(400);
        ImGui::Combo("##placement", &st.job_placement_idx, placements, 3);

        ImGui::Spacing();
        if (ImGui::Button("Submit", ImVec2(120, 30))) {
            if (strlen(st.job_name) > 0 && strlen(st.job_command) > 0) {
                MeshJob j;
                j.id = st.next_job_id++;
                j.name = st.job_name;
                j.command = st.job_command;
                j.status = "queued";
                j.vram_required = st.job_vram;
                const char* strats[] = {"any", "pack", "spread"};
                j.placement = strats[st.job_placement_idx];
                j.progress = 0.0f;
                st.jobs.push_back(j);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

} // namespace straylight::mesh
