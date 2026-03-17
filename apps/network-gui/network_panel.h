// apps/network-gui/network_panel.h
// StrayLight Network Manager panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::network {

struct WiFiNetwork {
    char ssid[64];
    int  signal;      // 0-100
    bool secured;
    bool connected;
    char band[16];    // "2.4 GHz" or "5 GHz"
};

struct VPNConnection {
    char name[64];
    char protocol[32];
    char server[128];
    bool connected;
};

struct FirewallRule {
    int  id;
    char action[16];  // ALLOW/DENY
    char protocol[16];
    char source[64];
    char dest[64];
    int  port;
    bool enabled;
};

struct NetworkState {
    std::vector<WiFiNetwork> networks;
    std::vector<VPNConnection> vpns;
    std::vector<FirewallRule> fw_rules;

    int  selected_network = 0;
    bool show_password_dialog = false;
    char wifi_password[128] = {};

    // Connected info
    char connected_ip[32] = "192.168.1.105";
    char connected_dns[32] = "1.1.1.1";
    char connected_gateway[32] = "192.168.1.1";
    char connected_speed[32] = "866 Mbps";
    char connected_ssid[64] = "StrayLight-5G";

    // New firewall rule
    char new_fw_source[64] = {};
    char new_fw_dest[64] = {};
    int  new_fw_port = 0;
    int  new_fw_action = 0;
    int  new_fw_protocol = 0;
    bool show_add_fw = false;

    static constexpr const char* fw_actions[] = { "ALLOW", "DENY" };
    static constexpr const char* fw_protocols[] = { "TCP", "UDP", "ICMP", "ANY" };

    void init() {
        WiFiNetwork n1{}; snprintf(n1.ssid, 64, "StrayLight-5G"); n1.signal = 92; n1.secured = true; n1.connected = true; snprintf(n1.band, 16, "5 GHz");
        WiFiNetwork n2{}; snprintf(n2.ssid, 64, "StrayLight-2G"); n2.signal = 78; n2.secured = true; n2.connected = false; snprintf(n2.band, 16, "2.4 GHz");
        WiFiNetwork n3{}; snprintf(n3.ssid, 64, "Neighbor-AP"); n3.signal = 45; n3.secured = true; n3.connected = false; snprintf(n3.band, 16, "2.4 GHz");
        WiFiNetwork n4{}; snprintf(n4.ssid, 64, "CoffeeShop"); n4.signal = 30; n4.secured = false; n4.connected = false; snprintf(n4.band, 16, "2.4 GHz");
        WiFiNetwork n5{}; snprintf(n5.ssid, 64, "IoT-Network"); n5.signal = 65; n5.secured = true; n5.connected = false; snprintf(n5.band, 16, "5 GHz");
        networks = {n1, n2, n3, n4, n5};

        VPNConnection v1{}; snprintf(v1.name, 64, "Work VPN"); snprintf(v1.protocol, 32, "WireGuard"); snprintf(v1.server, 128, "vpn.corp.io:51820"); v1.connected = true;
        VPNConnection v2{}; snprintf(v2.name, 64, "Privacy VPN"); snprintf(v2.protocol, 32, "OpenVPN"); snprintf(v2.server, 128, "nl.vpn.net:1194"); v2.connected = false;
        vpns = {v1, v2};

        fw_rules.push_back({1, "ALLOW", "TCP", "any", "any", 22, true});
        fw_rules.push_back({2, "ALLOW", "TCP", "any", "any", 80, true});
        fw_rules.push_back({3, "ALLOW", "TCP", "any", "any", 443, true});
        fw_rules.push_back({4, "DENY", "ANY", "any", "10.0.0.0/8", 0, true});
        fw_rules.push_back({5, "ALLOW", "UDP", "any", "any", 51820, true});
    }
};

inline void render_network_panel(NetworkState& st) {
    if (st.networks.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("NETWORK MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    float left_w = ImGui::GetContentRegionAvail().x * 0.4f;

    // Left: WiFi list
    ImGui::BeginChild("##wifi_list", ImVec2(left_w, ImGui::GetContentRegionAvail().y * 0.55f), true);
    ImGui::TextColored(accent, "WiFi Networks");
    ImGui::Separator();

    for (int i = 0; i < (int)st.networks.size(); ++i) {
        auto& n = st.networks[i];
        ImGui::PushID(i);

        bool selected = (i == st.selected_network);
        char label[128];
        snprintf(label, 128, "%s %s%s", n.ssid, n.band, n.connected ? " [Connected]" : "");

        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 36))) {
            st.selected_network = i;
        }

        // Signal strength bars
        ImVec2 pos = ImGui::GetItemRectMax();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float bx = pos.x - 60;
        float by = pos.y - 28;
        int bars = n.signal > 75 ? 4 : n.signal > 50 ? 3 : n.signal > 25 ? 2 : 1;
        for (int b = 0; b < 4; ++b) {
            float h = 6 + b * 5;
            ImU32 col = (b < bars) ? IM_COL32(0, 255, 136, 255) : IM_COL32(60, 60, 80, 255);
            draw->AddRectFilled(ImVec2(bx + b * 10, by + 20 - h),
                                ImVec2(bx + b * 10 + 6, by + 20), col, 1.0f);
        }

        // Lock icon for secured
        if (n.secured) {
            draw->AddText(ImVec2(bx + 45, by + 4), IM_COL32(180, 180, 180, 255), "L");
        }

        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Scan", ImVec2(-1, 28))) {
        // Trigger rescan
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: Connected info + VPN
    ImGui::BeginChild("##right_panel", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f));

    // Connection info card
    ImGui::BeginChild("##conn_info", ImVec2(-1, 160), true);
    ImGui::TextColored(accent, "Connected: %s", st.connected_ssid);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("IP Address:"); ImGui::SameLine(140); ImGui::Text("%s", st.connected_ip);
    ImGui::Text("DNS Server:"); ImGui::SameLine(140); ImGui::Text("%s", st.connected_dns);
    ImGui::Text("Gateway:");    ImGui::SameLine(140); ImGui::Text("%s", st.connected_gateway);
    ImGui::Text("Link Speed:"); ImGui::SameLine(140); ImGui::Text("%s", st.connected_speed);
    ImGui::Spacing();
    if (ImGui::Button("Disconnect", ImVec2(120, 28))) {
        // disconnect
    }
    ImGui::SameLine();
    if (ImGui::Button("Forget", ImVec2(120, 28))) {
        // forget
    }
    ImGui::EndChild();

    // VPN
    ImGui::BeginChild("##vpn", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "VPN Connections");
    ImGui::Separator();
    for (int i = 0; i < (int)st.vpns.size(); ++i) {
        auto& v = st.vpns[i];
        ImGui::PushID(1000 + i);
        ImGui::Text("%s", v.name);
        ImGui::SameLine(200);
        ImGui::TextDisabled("(%s)", v.protocol);
        ImGui::SameLine(350);
        bool conn = v.connected;
        if (ImGui::Checkbox("##vpn_toggle", &conn)) {
            v.connected = conn;
        }
        ImGui::SameLine();
        ImGui::Text(v.connected ? "Connected" : "Disconnected");
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::EndChild();

    // Bottom: Firewall rules
    ImGui::BeginChild("##firewall", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "Firewall Rules");
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    if (ImGui::SmallButton("Add Rule")) {
        st.show_add_fw = true;
        memset(st.new_fw_source, 0, sizeof(st.new_fw_source));
        memset(st.new_fw_dest, 0, sizeof(st.new_fw_dest));
        st.new_fw_port = 0;
    }
    ImGui::Separator();

    if (ImGui::BeginTable("##fw_table", 7,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Destination");
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();

        int delete_idx = -1;
        for (int i = 0; i < (int)st.fw_rules.size(); ++i) {
            auto& r = st.fw_rules[i];
            ImGui::TableNextRow();
            ImGui::PushID(2000 + i);

            ImGui::TableSetColumnIndex(0); ImGui::Text("%d", r.id);
            ImGui::TableSetColumnIndex(1);
            ImVec4 ac = (strcmp(r.action, "ALLOW") == 0) ? ImVec4(0, 1, 0.67f, 1) : ImVec4(1, 0.3f, 0.3f, 1);
            ImGui::TextColored(ac, "%s", r.action);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", r.protocol);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%s", r.source);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%s", r.dest);
            ImGui::TableSetColumnIndex(5);
            if (r.port > 0) ImGui::Text("%d", r.port); else ImGui::TextDisabled("*");
            ImGui::TableSetColumnIndex(6);
            ImGui::Checkbox("##en", &r.enabled);

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // Add Firewall Rule Dialog
    if (st.show_add_fw) {
        ImGui::OpenPopup("Add Firewall Rule");
        st.show_add_fw = false;
    }
    if (ImGui::BeginPopupModal("Add Firewall Rule", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Combo("Action", &st.new_fw_action, NetworkState::fw_actions, 2);
        ImGui::Combo("Protocol", &st.new_fw_protocol, NetworkState::fw_protocols, 4);
        ImGui::InputText("Source", st.new_fw_source, sizeof(st.new_fw_source));
        ImGui::InputText("Destination", st.new_fw_dest, sizeof(st.new_fw_dest));
        ImGui::InputInt("Port", &st.new_fw_port);
        ImGui::Spacing();
        if (ImGui::Button("Add", ImVec2(120, 30))) {
            FirewallRule nr;
            nr.id = (int)st.fw_rules.size() + 1;
            snprintf(nr.action, 16, "%s", NetworkState::fw_actions[st.new_fw_action]);
            snprintf(nr.protocol, 16, "%s", NetworkState::fw_protocols[st.new_fw_protocol]);
            snprintf(nr.source, 64, "%s", strlen(st.new_fw_source) > 0 ? st.new_fw_source : "any");
            snprintf(nr.dest, 64, "%s", strlen(st.new_fw_dest) > 0 ? st.new_fw_dest : "any");
            nr.port = st.new_fw_port;
            nr.enabled = true;
            st.fw_rules.push_back(nr);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::network
