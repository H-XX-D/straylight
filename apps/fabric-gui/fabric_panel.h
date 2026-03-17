// apps/fabric-gui/fabric_panel.h
// StrayLight Device Topology (Fabric) panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::fabric {

struct DeviceNode {
    char name[64];
    char type[32];     // "CPU", "GPU", "NVMe", "USB", "Network", "Memory", "PCIe Switch"
    float x, y;        // position in graph
    int  parent;       // -1 for root
    float bandwidth;   // GB/s
    float latency;     // ns
    int   affinity;    // NUMA node
};

struct PathResult {
    std::vector<int> nodes;
    float total_bandwidth;
    float total_latency;
    bool  has_bottleneck;
    int   bottleneck_node;
};

struct FabricState {
    std::vector<DeviceNode> devices;
    int selected_device = -1;
    int path_source = -1;
    int path_dest = -1;
    PathResult path_result;
    bool path_computed = false;

    void init() {
        devices.push_back({"Root Complex", "PCIe Switch", 400, 50, -1, 64.0f, 0, 0});
        devices.push_back({"CPU 0 (Zen 4)", "CPU", 150, 150, 0, 51.2f, 10, 0});
        devices.push_back({"CPU 1 (Zen 4)", "CPU", 650, 150, 0, 51.2f, 10, 1});
        devices.push_back({"DDR5-A (32GB)", "Memory", 50, 280, 1, 76.8f, 80, 0});
        devices.push_back({"DDR5-B (32GB)", "Memory", 250, 280, 1, 76.8f, 80, 0});
        devices.push_back({"DDR5-C (32GB)", "Memory", 550, 280, 2, 76.8f, 80, 1});
        devices.push_back({"DDR5-D (32GB)", "Memory", 750, 280, 2, 76.8f, 80, 1});
        devices.push_back({"RTX 4090", "GPU", 100, 400, 1, 16.0f, 150, 0});
        devices.push_back({"Samsung 980 PRO", "NVMe", 300, 400, 1, 7.0f, 200, 0});
        devices.push_back({"Intel AX210", "Network", 500, 400, 2, 2.4f, 500, 1});
        devices.push_back({"USB 3.2 Hub", "USB", 700, 400, 2, 1.25f, 1000, 1});
        devices.push_back({"WD Blue HDD", "USB", 750, 520, 10, 0.16f, 5000, 1});
    }

    void compute_path() {
        path_result.nodes.clear();
        path_computed = true;

        if (path_source < 0 || path_dest < 0) return;

        // Simple path: walk up from source to root, then down to dest
        std::vector<int> src_path, dst_path;
        int n = path_source;
        while (n >= 0) { src_path.push_back(n); n = devices[n].parent; }
        n = path_dest;
        while (n >= 0) { dst_path.push_back(n); n = devices[n].parent; }

        // Find common ancestor
        int lca = 0;
        for (int s : src_path) {
            for (int d : dst_path) {
                if (s == d) { lca = s; goto found; }
            }
        }
        found:

        // Build path
        for (int s : src_path) {
            path_result.nodes.push_back(s);
            if (s == lca) break;
        }
        std::vector<int> tail;
        for (int d : dst_path) {
            if (d == lca) break;
            tail.push_back(d);
        }
        for (int i = (int)tail.size() - 1; i >= 0; --i)
            path_result.nodes.push_back(tail[i]);

        // Compute metrics
        path_result.total_bandwidth = 999.0f;
        path_result.total_latency = 0;
        path_result.has_bottleneck = false;
        path_result.bottleneck_node = -1;
        for (int nid : path_result.nodes) {
            if (devices[nid].bandwidth < path_result.total_bandwidth) {
                path_result.total_bandwidth = devices[nid].bandwidth;
                if (path_result.total_bandwidth < 2.0f) {
                    path_result.has_bottleneck = true;
                    path_result.bottleneck_node = nid;
                }
            }
            path_result.total_latency += devices[nid].latency;
        }
    }
};

inline void render_fabric_panel(FabricState& st) {
    if (st.devices.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("DEVICE TOPOLOGY");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Path query bar
    ImGui::Text("Path Query:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##src", st.path_source >= 0 ? st.devices[st.path_source].name : "Source")) {
        for (int i = 0; i < (int)st.devices.size(); ++i) {
            if (ImGui::Selectable(st.devices[i].name, st.path_source == i)) {
                st.path_source = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("->");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##dst", st.path_dest >= 0 ? st.devices[st.path_dest].name : "Destination")) {
        for (int i = 0; i < (int)st.devices.size(); ++i) {
            if (ImGui::Selectable(st.devices[i].name, st.path_dest == i)) {
                st.path_dest = i;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Find Path", ImVec2(100, 0))) {
        st.compute_path();
    }

    // Path result
    if (st.path_computed && !st.path_result.nodes.empty()) {
        ImGui::SameLine(0, 20);
        ImGui::Text("BW: %.1f GB/s  Latency: %.0f ns", st.path_result.total_bandwidth, st.path_result.total_latency);
        if (st.path_result.has_bottleneck) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "BOTTLENECK: %s",
                               st.devices[st.path_result.bottleneck_node].name);
        }
    }
    ImGui::Spacing();

    float detail_w = 280;

    // Topology graph
    ImGui::BeginChild("##graph", ImVec2(ImGui::GetContentRegionAvail().x - detail_w - 8, 0), true);
    ImGui::TextDisabled("Device Tree (click to inspect)");

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Draw edges
    for (int i = 0; i < (int)st.devices.size(); ++i) {
        auto& d = st.devices[i];
        if (d.parent >= 0) {
            auto& p = st.devices[d.parent];
            ImVec2 from(origin.x + p.x + 40, origin.y + p.y + 20);
            ImVec2 to(origin.x + d.x + 40, origin.y + d.y);

            // Check if this edge is on the path
            bool on_path = false;
            if (st.path_computed) {
                for (int j = 0; j < (int)st.path_result.nodes.size() - 1; ++j) {
                    int a = st.path_result.nodes[j], b = st.path_result.nodes[j+1];
                    if ((a == i && b == d.parent) || (a == d.parent && b == i)) {
                        on_path = true; break;
                    }
                }
            }

            ImU32 edge_col = on_path ? IM_COL32(0, 255, 136, 255) : IM_COL32(60, 60, 80, 200);
            float thickness = on_path ? 3.0f : 1.5f;
            draw->AddLine(from, to, edge_col, thickness);
        }
    }

    // Draw nodes
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < (int)st.devices.size(); ++i) {
        auto& d = st.devices[i];
        ImVec2 tl(origin.x + d.x, origin.y + d.y);
        ImVec2 br(tl.x + 80, tl.y + 40);

        bool hovered = io.MousePos.x >= tl.x && io.MousePos.x <= br.x &&
                       io.MousePos.y >= tl.y && io.MousePos.y <= br.y;

        // Node color by type
        ImU32 node_col;
        if (strcmp(d.type, "CPU") == 0) node_col = IM_COL32(0, 100, 180, 200);
        else if (strcmp(d.type, "GPU") == 0) node_col = IM_COL32(0, 160, 80, 200);
        else if (strcmp(d.type, "Memory") == 0) node_col = IM_COL32(120, 80, 160, 200);
        else if (strcmp(d.type, "NVMe") == 0) node_col = IM_COL32(180, 100, 0, 200);
        else if (strcmp(d.type, "Network") == 0) node_col = IM_COL32(0, 120, 160, 200);
        else if (strcmp(d.type, "USB") == 0) node_col = IM_COL32(100, 100, 100, 200);
        else node_col = IM_COL32(60, 60, 100, 200);

        // Bottleneck highlight
        bool is_bottleneck = st.path_result.has_bottleneck && st.path_result.bottleneck_node == i;
        if (is_bottleneck) node_col = IM_COL32(255, 50, 50, 220);

        bool selected = (st.selected_device == i);
        ImU32 border = selected ? IM_COL32(0, 255, 136, 255) :
                       hovered ? IM_COL32(200, 200, 200, 255) :
                       IM_COL32(80, 80, 100, 255);

        draw->AddRectFilled(tl, br, node_col, 6.0f);
        draw->AddRect(tl, br, border, 6.0f, 0, selected ? 3.0f : 1.5f);

        // Truncated name
        char short_name[20];
        snprintf(short_name, 20, "%s", d.name);
        draw->AddText(ImVec2(tl.x + 4, tl.y + 4), IM_COL32(255, 255, 255, 255), short_name);
        draw->AddText(ImVec2(tl.x + 4, tl.y + 22), IM_COL32(180, 180, 180, 200), d.type);

        if (hovered && ImGui::IsMouseClicked(0)) {
            st.selected_device = i;
        }
    }

    ImGui::Dummy(ImVec2(0, 560));
    ImGui::EndChild();

    ImGui::SameLine();

    // Device detail panel
    ImGui::BeginChild("##detail", ImVec2(detail_w, 0), true);
    if (st.selected_device >= 0 && st.selected_device < (int)st.devices.size()) {
        auto& d = st.devices[st.selected_device];

        ImGui::TextColored(accent, "%s", d.name);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Type:");       ImGui::SameLine(100); ImGui::Text("%s", d.type);
        ImGui::Text("Bandwidth:");  ImGui::SameLine(100); ImGui::Text("%.1f GB/s", d.bandwidth);
        ImGui::Text("Latency:");    ImGui::SameLine(100); ImGui::Text("%.0f ns", d.latency);
        ImGui::Text("NUMA Node:");  ImGui::SameLine(100); ImGui::Text("%d", d.affinity);

        if (d.parent >= 0) {
            ImGui::Text("Parent:");
            ImGui::SameLine(100);
            ImGui::TextColored(accent, "%s", st.devices[d.parent].name);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(accent, "Performance");
        ImGui::Spacing();

        // Bandwidth bar
        float max_bw = 76.8f;
        float bw_frac = d.bandwidth / max_bw;
        char bw_str[32]; snprintf(bw_str, 32, "%.1f GB/s", d.bandwidth);
        ImGui::Text("Bandwidth:");
        ImGui::ProgressBar(bw_frac, ImVec2(-1, 16), bw_str);

        // Latency bar (inverse - lower is better)
        float max_lat = 5000.0f;
        float lat_frac = d.latency / max_lat;
        char lat_str[32]; snprintf(lat_str, 32, "%.0f ns", d.latency);
        ImGui::Text("Latency:");
        ImGui::ProgressBar(lat_frac, ImVec2(-1, 16), lat_str);

        ImGui::Spacing();
        ImGui::Separator();

        // Quick path buttons
        ImGui::TextColored(accent, "Quick Path");
        if (ImGui::Button("Set as Source", ImVec2(-1, 24))) {
            st.path_source = st.selected_device;
        }
        if (ImGui::Button("Set as Destination", ImVec2(-1, 24))) {
            st.path_dest = st.selected_device;
        }
    } else {
        ImGui::TextDisabled("Click a device in the graph");
    }
    ImGui::EndChild();
}

} // namespace straylight::fabric
