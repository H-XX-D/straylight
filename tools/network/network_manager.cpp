// tools/network/network_manager.cpp
// Full network management implementation for StrayLight OS.

#include "network_manager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace straylight {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NetworkManager::NetworkManager() {
    fs::create_directories(config_dir());
}

NetworkManager::~NetworkManager() = default;

std::string NetworkManager::config_dir() const {
    const char* home = std::getenv("HOME");
    if (!home) home = "/root";
    return std::string(home) + "/.config/straylight/networks";
}

Result<std::string, std::string> NetworkManager::run_cmd(const std::string& cmd) const {
    std::array<char, 8192> buffer{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return Result<std::string, std::string>::error(
            "popen failed: " + std::string(strerror(errno)));
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        return Result<std::string, std::string>::error(
            "command failed (rc=" + std::to_string(rc) + "): " + cmd +
            "\noutput: " + output);
    }
    return Result<std::string, std::string>::ok(output);
}

bool NetworkManager::has_nmcli() const {
    return run_cmd("which nmcli 2>/dev/null").has_value();
}

bool NetworkManager::has_iw() const {
    return run_cmd("which iw 2>/dev/null").has_value();
}

// ---------------------------------------------------------------------------
// Interface status
// ---------------------------------------------------------------------------

InterfaceStatus NetworkManager::read_sysfs_interface(const std::string& name) const {
    InterfaceStatus iface;
    iface.name = name;
    std::string base = "/sys/class/net/" + name;

    // Type
    std::ifstream type_f(base + "/type");
    if (type_f.is_open()) {
        int type_num = 0;
        type_f >> type_num;
        if (type_num == 1) {
            // Check if wireless
            if (fs::exists(base + "/wireless")) {
                iface.type = "wifi";
            } else {
                iface.type = "ethernet";
            }
        } else if (type_num == 772) {
            iface.type = "loopback";
        }
    }

    // Check for bridge/bond
    if (fs::exists(base + "/bridge")) iface.type = "bridge";
    if (fs::exists(base + "/bonding")) iface.type = "bond";

    // Operstate
    std::ifstream state_f(base + "/operstate");
    if (state_f.is_open()) std::getline(state_f, iface.state);

    // MAC address
    std::ifstream mac_f(base + "/address");
    if (mac_f.is_open()) std::getline(mac_f, iface.mac);

    // MTU
    std::ifstream mtu_f(base + "/mtu");
    if (mtu_f.is_open()) mtu_f >> iface.mtu;

    // RX/TX bytes
    std::ifstream rx_f(base + "/statistics/rx_bytes");
    if (rx_f.is_open()) rx_f >> iface.rx_bytes;
    std::ifstream tx_f(base + "/statistics/tx_bytes");
    if (tx_f.is_open()) tx_f >> iface.tx_bytes;

    return iface;
}

std::vector<InterfaceStatus> NetworkManager::parse_ip_addr(const std::string& output) const {
    std::vector<InterfaceStatus> interfaces;
    std::istringstream stream(output);
    std::string line;
    InterfaceStatus* current = nullptr;

    while (std::getline(stream, line)) {
        // Interface line: "2: eth0: <BROADCAST,..."
        std::regex iface_re(R"(^\d+:\s+(\S+):\s+<([^>]*)>)");
        std::smatch m;
        if (std::regex_search(line, m, iface_re)) {
            std::string name = m[1].str();
            // Remove trailing @... from veth/bridge interfaces
            auto at = name.find('@');
            if (at != std::string::npos) name = name.substr(0, at);

            interfaces.push_back(read_sysfs_interface(name));
            current = &interfaces.back();

            std::string flags = m[2].str();
            if (flags.find("UP") != std::string::npos) {
                if (current->state.empty()) current->state = "up";
            }

            // Parse MTU from the same line
            std::regex mtu_re(R"(mtu\s+(\d+))");
            if (std::regex_search(line, m, mtu_re)) {
                current->mtu = std::stoi(m[1].str());
            }
        } else if (current) {
            // IPv4: "    inet 192.168.1.100/24 ..."
            std::regex inet_re(R"(inet\s+(\S+))");
            if (std::regex_search(line, m, inet_re)) {
                current->ipv4 = m[1].str();
            }
            // IPv6: "    inet6 fe80::... "
            std::regex inet6_re(R"(inet6\s+(\S+))");
            if (std::regex_search(line, m, inet6_re)) {
                current->ipv6 = m[1].str();
            }
        }
    }

    // Get default gateway
    auto gw_res = run_cmd("ip route show default 2>/dev/null");
    if (gw_res.has_value()) {
        std::regex gw_re(R"(default via (\S+) dev (\S+))");
        std::smatch m;
        std::string gw_out = gw_res.value();
        if (std::regex_search(gw_out, m, gw_re)) {
            std::string gw = m[1].str();
            std::string dev = m[2].str();
            for (auto& iface : interfaces) {
                if (iface.name == dev) {
                    iface.gateway = gw;
                    break;
                }
            }
        }
    }

    return interfaces;
}

Result<std::vector<InterfaceStatus>, std::string> NetworkManager::status() const {
    auto res = run_cmd("ip addr show 2>/dev/null");
    if (!res.has_value()) {
        return Result<std::vector<InterfaceStatus>, std::string>::error(
            "failed to get interface status: " + res.error());
    }

    auto interfaces = parse_ip_addr(res.value());

    // Enrich WiFi interfaces with connection info
    for (auto& iface : interfaces) {
        if (iface.type == "wifi") {
            auto iw_res = run_cmd("iw dev " + iface.name + " link 2>/dev/null");
            if (iw_res.has_value()) {
                std::string info = iw_res.value();
                std::regex ssid_re(R"(SSID:\s+(.+))");
                std::regex signal_re(R"(signal:\s+(-?\d+)\s+dBm)");
                std::smatch m;
                if (std::regex_search(info, m, ssid_re)) iface.ssid = m[1].str();
                if (std::regex_search(info, m, signal_re)) iface.signal_dbm = std::stoi(m[1].str());
            }
        }
    }

    return Result<std::vector<InterfaceStatus>, std::string>::ok(interfaces);
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

std::vector<WifiNetwork> NetworkManager::parse_iw_scan(const std::string& output) const {
    std::vector<WifiNetwork> networks;
    std::istringstream stream(output);
    std::string line;
    WifiNetwork* current = nullptr;

    while (std::getline(stream, line)) {
        auto pos = line.find_first_not_of(" \t");
        if (pos != std::string::npos) line = line.substr(pos);

        if (line.rfind("BSS ", 0) == 0) {
            networks.emplace_back();
            current = &networks.back();
            // "BSS aa:bb:cc:dd:ee:ff(on wlan0)"
            std::regex bss_re(R"(BSS\s+([0-9a-fA-F:]+))");
            std::smatch m;
            if (std::regex_search(line, m, bss_re)) {
                current->bssid = m[1].str();
            }
            current->connected = (line.find("associated") != std::string::npos);
        } else if (current) {
            if (line.rfind("SSID:", 0) == 0) {
                current->ssid = line.substr(6);
            } else if (line.rfind("signal:", 0) == 0) {
                std::regex sig_re(R"((-?\d+\.?\d*)\s+dBm)");
                std::smatch m;
                if (std::regex_search(line, m, sig_re)) {
                    current->signal_strength = static_cast<int>(std::stod(m[1].str()));
                }
            } else if (line.rfind("freq:", 0) == 0) {
                std::regex freq_re(R"(\d+)");
                std::smatch m;
                if (std::regex_search(line, m, freq_re)) {
                    current->frequency_mhz = std::stoi(m[0].str());
                    // Calculate channel
                    if (current->frequency_mhz >= 2412 && current->frequency_mhz <= 2484) {
                        current->channel = (current->frequency_mhz - 2407) / 5;
                    } else if (current->frequency_mhz >= 5180) {
                        current->channel = (current->frequency_mhz - 5000) / 5;
                    }
                }
            } else if (line.find("WPA") != std::string::npos) {
                if (line.find("Version: 2") != std::string::npos) {
                    current->security = "WPA2";
                } else {
                    current->security = "WPA";
                }
            } else if (line.find("RSN") != std::string::npos) {
                if (current->security != "WPA3") current->security = "WPA2";
            } else if (line.find("SAE") != std::string::npos) {
                current->security = "WPA3";
            }
        }
    }

    // Mark open networks
    for (auto& net : networks) {
        if (net.security.empty()) net.security = "Open";
    }

    // Sort by signal strength (strongest first)
    std::sort(networks.begin(), networks.end(),
              [](const auto& a, const auto& b) {
                  return a.signal_strength > b.signal_strength;
              });

    // Remove duplicates (keep strongest signal per SSID)
    std::vector<WifiNetwork> unique;
    for (const auto& net : networks) {
        bool found = false;
        for (const auto& u : unique) {
            if (u.ssid == net.ssid && !net.ssid.empty()) { found = true; break; }
        }
        if (!found) unique.push_back(net);
    }

    return unique;
}

Result<std::vector<WifiNetwork>, std::string> NetworkManager::scan_wifi() const {
    // Try nmcli first
    if (has_nmcli()) {
        auto res = run_cmd("nmcli -t -f SSID,BSSID,SIGNAL,FREQ,SECURITY,ACTIVE device wifi list 2>/dev/null");
        if (res.has_value()) {
            std::vector<WifiNetwork> networks;
            std::istringstream stream(res.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty()) continue;
                // Fields separated by ':'
                WifiNetwork net;
                std::vector<std::string> fields;
                size_t pos = 0;
                while (pos < line.size()) {
                    auto next = line.find(':', pos);
                    // Handle escaped colons in BSSID
                    if (next != std::string::npos && next + 1 < line.size() &&
                        line[next + 1] != ':' && (fields.size() != 1 || next - pos < 2)) {
                        fields.push_back(line.substr(pos, next - pos));
                        pos = next + 1;
                    } else if (next != std::string::npos) {
                        // BSSID contains colons; accumulate until field complete
                        auto field_end = next;
                        // Find the field boundary (BSSID is 17 chars: XX:XX:XX:XX:XX:XX)
                        if (fields.size() == 1 && pos + 17 <= line.size()) {
                            fields.push_back(line.substr(pos, 17));
                            pos += 18; // skip the trailing ':'
                        } else {
                            fields.push_back(line.substr(pos, next - pos));
                            pos = next + 1;
                        }
                    } else {
                        fields.push_back(line.substr(pos));
                        break;
                    }
                }

                if (fields.size() >= 5) {
                    net.ssid = fields[0];
                    net.bssid = fields[1];
                    if (fields.size() > 2) {
                        try { net.signal_strength = std::stoi(fields[2]); } catch (...) {}
                    }
                    if (fields.size() > 3) {
                        try { net.frequency_mhz = std::stoi(fields[3]); } catch (...) {}
                    }
                    if (fields.size() > 4) net.security = fields[4];
                    if (fields.size() > 5) net.connected = (fields[5] == "yes");
                    networks.push_back(net);
                }
            }
            return Result<std::vector<WifiNetwork>, std::string>::ok(networks);
        }
    }

    // Fallback to iw
    // Find wireless interface
    std::string wifi_iface;
    auto ifaces_res = run_cmd("iw dev 2>/dev/null");
    if (ifaces_res.has_value()) {
        std::regex iface_re(R"(Interface\s+(\S+))");
        std::smatch m;
        std::string ifaces = ifaces_res.value();
        if (std::regex_search(ifaces, m, iface_re)) {
            wifi_iface = m[1].str();
        }
    }

    if (wifi_iface.empty()) {
        return Result<std::vector<WifiNetwork>, std::string>::error(
            "no wireless interface found");
    }

    auto scan_res = run_cmd("iw dev " + wifi_iface + " scan 2>/dev/null");
    if (!scan_res.has_value()) {
        return Result<std::vector<WifiNetwork>, std::string>::error(
            "WiFi scan failed: " + scan_res.error());
    }

    return Result<std::vector<WifiNetwork>, std::string>::ok(
        parse_iw_scan(scan_res.value()));
}

Result<void, std::string> NetworkManager::connect_wifi(const std::string& ssid,
                                                         const std::string& password,
                                                         bool hidden) {
    if (has_nmcli()) {
        std::string cmd = "nmcli device wifi connect '" + ssid + "'";
        if (!password.empty()) cmd += " password '" + password + "'";
        if (hidden) cmd += " hidden yes";
        cmd += " 2>/dev/null";
        auto res = run_cmd(cmd);
        if (!res.has_value()) {
            return Result<void, std::string>::error("WiFi connection failed: " + res.error());
        }

        // Save connection
        std::ostringstream json;
        json << "{\n"
             << "  \"type\": \"wifi\",\n"
             << "  \"ssid\": \"" << ssid << "\",\n"
             << "  \"auto_connect\": true\n"
             << "}\n";
        std::string path = config_dir() + "/" + ssid + ".json";
        std::ofstream out(path);
        if (out.is_open()) out << json.str();

        return Result<void, std::string>::ok();
    }

    // wpa_supplicant fallback
    if (password.empty()) {
        auto res = run_cmd("iw dev wlan0 connect '" + ssid + "' 2>/dev/null");
        if (!res.has_value()) {
            return Result<void, std::string>::error("WiFi connection failed: " + res.error());
        }
        return Result<void, std::string>::ok();
    }

    // Generate wpa_supplicant config
    auto psk_res = run_cmd("wpa_passphrase '" + ssid + "' '" + password + "' 2>/dev/null");
    if (!psk_res.has_value()) {
        return Result<void, std::string>::error("failed to generate PSK: " + psk_res.error());
    }

    std::string conf_path = "/tmp/straylight_wpa_" + ssid + ".conf";
    std::ofstream conf(conf_path);
    if (!conf.is_open()) {
        return Result<void, std::string>::error("cannot write wpa_supplicant config");
    }
    conf << psk_res.value();
    conf.close();

    auto connect_res = run_cmd("wpa_supplicant -B -i wlan0 -c " + conf_path + " 2>/dev/null");
    if (!connect_res.has_value()) {
        return Result<void, std::string>::error("wpa_supplicant failed: " + connect_res.error());
    }

    // Request DHCP
    run_cmd("dhclient wlan0 2>/dev/null");

    return Result<void, std::string>::ok();
}

Result<void, std::string> NetworkManager::disconnect(const std::string& interface) {
    std::string iface = interface.empty() ? "" : interface;

    if (has_nmcli()) {
        std::string cmd = "nmcli device disconnect";
        if (!iface.empty()) cmd += " " + iface;
        cmd += " 2>/dev/null";
        auto res = run_cmd(cmd);
        if (!res.has_value()) {
            return Result<void, std::string>::error("disconnect failed: " + res.error());
        }
        return Result<void, std::string>::ok();
    }

    if (iface.empty()) iface = "wlan0";
    run_cmd("ip link set " + iface + " down 2>/dev/null");
    return Result<void, std::string>::ok();
}

// ---------------------------------------------------------------------------
// VPN
// ---------------------------------------------------------------------------

Result<void, std::string> NetworkManager::vpn_add(const VpnConfig& config) {
    if (config.type == "wireguard") {
        if (config.config_file.empty()) {
            return Result<void, std::string>::error("WireGuard requires a config file");
        }

        // Copy config to /etc/wireguard/
        std::string dest = "/etc/wireguard/" + config.name + ".conf";
        auto res = run_cmd("cp '" + config.config_file + "' '" + dest + "' 2>/dev/null");
        if (!res.has_value()) {
            return Result<void, std::string>::error("failed to install WireGuard config: " + res.error());
        }
        run_cmd("chmod 600 '" + dest + "' 2>/dev/null");

    } else if (config.type == "openvpn") {
        if (config.config_file.empty()) {
            return Result<void, std::string>::error("OpenVPN requires a config file");
        }

        if (has_nmcli()) {
            auto res = run_cmd("nmcli connection import type openvpn file '" +
                               config.config_file + "' 2>/dev/null");
            if (!res.has_value()) {
                return Result<void, std::string>::error("failed to import OpenVPN config: " + res.error());
            }
        } else {
            std::string dest = "/etc/openvpn/client/" + config.name + ".conf";
            auto res = run_cmd("cp '" + config.config_file + "' '" + dest + "' 2>/dev/null");
            if (!res.has_value()) {
                return Result<void, std::string>::error("failed to install OpenVPN config: " + res.error());
            }
        }
    } else {
        return Result<void, std::string>::error("unsupported VPN type: " + config.type);
    }

    // Save to our config
    std::ostringstream json;
    json << "{\n"
         << "  \"name\": \"" << config.name << "\",\n"
         << "  \"type\": \"" << config.type << "\",\n"
         << "  \"server\": \"" << config.server << "\",\n"
         << "  \"port\": " << config.port << "\n"
         << "}\n";
    std::string path = config_dir() + "/vpn-" + config.name + ".json";
    std::ofstream out(path);
    if (out.is_open()) out << json.str();

    return Result<void, std::string>::ok();
}

Result<void, std::string> NetworkManager::vpn_connect(const std::string& name) {
    // Try WireGuard
    auto wg_res = run_cmd("wg-quick up " + name + " 2>/dev/null");
    if (wg_res.has_value()) {
        return Result<void, std::string>::ok();
    }

    // Try nmcli
    if (has_nmcli()) {
        auto res = run_cmd("nmcli connection up '" + name + "' 2>/dev/null");
        if (res.has_value()) {
            return Result<void, std::string>::ok();
        }
    }

    // Try OpenVPN
    auto ovpn_res = run_cmd("systemctl start openvpn-client@" + name + " 2>/dev/null");
    if (ovpn_res.has_value()) {
        return Result<void, std::string>::ok();
    }

    return Result<void, std::string>::error("failed to connect VPN '" + name + "'");
}

Result<void, std::string> NetworkManager::vpn_disconnect(const std::string& name) {
    // Try all backends
    run_cmd("wg-quick down " + name + " 2>/dev/null");
    if (has_nmcli()) {
        run_cmd("nmcli connection down '" + name + "' 2>/dev/null");
    }
    run_cmd("systemctl stop openvpn-client@" + name + " 2>/dev/null");

    return Result<void, std::string>::ok();
}

Result<std::vector<VpnConfig>, std::string> NetworkManager::vpn_list() const {
    std::vector<VpnConfig> vpns;

    // WireGuard interfaces
    auto wg_res = run_cmd("wg show interfaces 2>/dev/null");
    if (wg_res.has_value() && !wg_res.value().empty()) {
        std::istringstream stream(wg_res.value());
        std::string name;
        while (stream >> name) {
            VpnConfig vpn;
            vpn.name = name;
            vpn.type = "wireguard";
            vpn.connected = true;

            // Get details
            auto detail = run_cmd("wg show " + name + " 2>/dev/null");
            if (detail.has_value()) {
                std::regex endpoint_re(R"(endpoint:\s+(\S+))");
                std::regex pk_re(R"(public key:\s+(\S+))");
                std::smatch m;
                std::string info = detail.value();
                if (std::regex_search(info, m, endpoint_re)) vpn.server = m[1].str();
                if (std::regex_search(info, m, pk_re)) vpn.public_key = m[1].str();
            }
            vpns.push_back(vpn);
        }
    }

    // Check WireGuard configs not currently up
    if (fs::exists("/etc/wireguard")) {
        for (const auto& entry : fs::directory_iterator("/etc/wireguard")) {
            std::string fname = entry.path().filename().string();
            if (fname.size() > 5 && fname.substr(fname.size() - 5) == ".conf") {
                std::string name = fname.substr(0, fname.size() - 5);
                bool already_listed = false;
                for (const auto& v : vpns) {
                    if (v.name == name) { already_listed = true; break; }
                }
                if (!already_listed) {
                    VpnConfig vpn;
                    vpn.name = name;
                    vpn.type = "wireguard";
                    vpn.connected = false;
                    vpns.push_back(vpn);
                }
            }
        }
    }

    // nmcli VPN connections
    if (has_nmcli()) {
        auto res = run_cmd("nmcli -t -f NAME,TYPE,ACTIVE connection show 2>/dev/null");
        if (res.has_value()) {
            std::istringstream stream(res.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (line.find("vpn") != std::string::npos) {
                    auto first_colon = line.find(':');
                    auto second_colon = line.find(':', first_colon + 1);
                    if (first_colon != std::string::npos) {
                        VpnConfig vpn;
                        vpn.name = line.substr(0, first_colon);
                        vpn.type = "openvpn";
                        if (second_colon != std::string::npos) {
                            vpn.connected = (line.substr(second_colon + 1) == "yes");
                        }
                        vpns.push_back(vpn);
                    }
                }
            }
        }
    }

    return Result<std::vector<VpnConfig>, std::string>::ok(vpns);
}

// ---------------------------------------------------------------------------
// Firewall
// ---------------------------------------------------------------------------

Result<void, std::string> NetworkManager::firewall_add(const FirewallRule& rule) {
    // Build nftables rule
    std::ostringstream cmd;

    // Check if nftables is available
    auto nft_check = run_cmd("which nft 2>/dev/null");
    if (nft_check.has_value()) {
        cmd << "nft add rule inet filter " << rule.chain;
        if (!rule.protocol.empty() && rule.protocol != "any") {
            cmd << " " << rule.protocol;
        }
        if (!rule.source.empty()) {
            cmd << " ip saddr " << rule.source;
        }
        if (!rule.destination.empty()) {
            cmd << " ip daddr " << rule.destination;
        }
        if (rule.port > 0) {
            cmd << " dport " << rule.port;
        }
        if (!rule.interface.empty()) {
            if (rule.chain == "input") cmd << " iifname \"" << rule.interface << "\"";
            else cmd << " oifname \"" << rule.interface << "\"";
        }
        cmd << " " << rule.action;
        if (!rule.comment.empty()) {
            cmd << " comment \"" << rule.comment << "\"";
        }
        cmd << " 2>/dev/null";
    } else {
        // ufw fallback
        cmd << "ufw ";
        if (rule.action == "accept") cmd << "allow";
        else if (rule.action == "drop") cmd << "deny";
        else cmd << rule.action;

        if (!rule.source.empty()) cmd << " from " << rule.source;
        if (!rule.destination.empty()) cmd << " to " << rule.destination;
        if (rule.port > 0) cmd << " port " << rule.port;
        if (!rule.protocol.empty() && rule.protocol != "any") {
            cmd << " proto " << rule.protocol;
        }
        if (!rule.comment.empty()) cmd << " comment '" << rule.comment << "'";
        cmd << " 2>/dev/null";
    }

    auto res = run_cmd(cmd.str());
    if (!res.has_value()) {
        return Result<void, std::string>::error("failed to add firewall rule: " + res.error());
    }
    return Result<void, std::string>::ok();
}

Result<void, std::string> NetworkManager::firewall_remove(uint32_t rule_id) {
    auto nft_check = run_cmd("which nft 2>/dev/null");
    if (nft_check.has_value()) {
        // nftables: delete rule by handle
        std::string cmd = "nft delete rule inet filter input handle " +
                          std::to_string(rule_id) + " 2>/dev/null";
        auto res = run_cmd(cmd);
        if (!res.has_value()) {
            // Try other chains
            cmd = "nft delete rule inet filter output handle " +
                  std::to_string(rule_id) + " 2>/dev/null";
            res = run_cmd(cmd);
            if (!res.has_value()) {
                cmd = "nft delete rule inet filter forward handle " +
                      std::to_string(rule_id) + " 2>/dev/null";
                res = run_cmd(cmd);
            }
        }
        if (!res.has_value()) {
            return Result<void, std::string>::error("failed to remove rule: " + res.error());
        }
    } else {
        // ufw fallback
        std::string cmd = "ufw delete " + std::to_string(rule_id) + " 2>/dev/null";
        auto res = run_cmd(cmd);
        if (!res.has_value()) {
            return Result<void, std::string>::error("failed to remove rule: " + res.error());
        }
    }
    return Result<void, std::string>::ok();
}

Result<std::vector<FirewallRule>, std::string> NetworkManager::firewall_list() const {
    std::vector<FirewallRule> rules;

    auto nft_check = run_cmd("which nft 2>/dev/null");
    if (nft_check.has_value()) {
        auto res = run_cmd("nft -a list ruleset 2>/dev/null");
        if (res.has_value()) {
            std::istringstream stream(res.value());
            std::string line;
            std::string current_chain;

            while (std::getline(stream, line)) {
                auto pos = line.find_first_not_of(" \t");
                if (pos != std::string::npos) line = line.substr(pos);

                // Chain header
                std::regex chain_re(R"(chain\s+(\S+)\s*\{)");
                std::smatch m;
                if (std::regex_search(line, m, chain_re)) {
                    current_chain = m[1].str();
                    continue;
                }

                // Rule with handle
                std::regex handle_re(R"(#\s*handle\s+(\d+))");
                if (std::regex_search(line, m, handle_re)) {
                    FirewallRule rule;
                    rule.id = std::stoul(m[1].str());
                    rule.chain = current_chain;

                    // Parse action
                    if (line.find(" accept") != std::string::npos) rule.action = "accept";
                    else if (line.find(" drop") != std::string::npos) rule.action = "drop";
                    else if (line.find(" reject") != std::string::npos) rule.action = "reject";

                    // Parse protocol
                    std::regex proto_re(R"(\b(tcp|udp|icmp)\b)");
                    if (std::regex_search(line, m, proto_re)) rule.protocol = m[1].str();

                    // Parse source
                    std::regex saddr_re(R"(ip saddr (\S+))");
                    if (std::regex_search(line, m, saddr_re)) rule.source = m[1].str();

                    // Parse dest
                    std::regex daddr_re(R"(ip daddr (\S+))");
                    if (std::regex_search(line, m, daddr_re)) rule.destination = m[1].str();

                    // Parse port
                    std::regex port_re(R"(dport (\d+))");
                    if (std::regex_search(line, m, port_re)) rule.port = std::stoi(m[1].str());

                    // Parse comment
                    std::regex comment_re(R"(comment\s+"([^"]*)")");
                    if (std::regex_search(line, m, comment_re)) rule.comment = m[1].str();

                    rules.push_back(rule);
                }
            }
        }
    } else {
        // ufw fallback
        auto res = run_cmd("ufw status numbered 2>/dev/null");
        if (res.has_value()) {
            std::istringstream stream(res.value());
            std::string line;
            uint32_t idx = 0;
            while (std::getline(stream, line)) {
                std::regex rule_re(R"(\[\s*(\d+)\]\s+(.+))");
                std::smatch m;
                if (std::regex_search(line, m, rule_re)) {
                    FirewallRule rule;
                    rule.id = std::stoul(m[1].str());
                    std::string desc = m[2].str();

                    if (desc.find("ALLOW") != std::string::npos) rule.action = "accept";
                    else if (desc.find("DENY") != std::string::npos) rule.action = "drop";
                    else if (desc.find("REJECT") != std::string::npos) rule.action = "reject";

                    rule.comment = desc;
                    rules.push_back(rule);
                }
            }
        }
    }

    return Result<std::vector<FirewallRule>, std::string>::ok(rules);
}

// ---------------------------------------------------------------------------
// DNS
// ---------------------------------------------------------------------------

Result<void, std::string> NetworkManager::dns_set(const DnsConfig& config) {
    // Check if systemd-resolved is in use
    if (fs::exists("/etc/systemd/resolved.conf")) {
        std::string servers_str;
        for (size_t i = 0; i < config.servers.size(); ++i) {
            if (i > 0) servers_str += " ";
            servers_str += config.servers[i];
        }

        std::string domains_str;
        for (size_t i = 0; i < config.search_domains.size(); ++i) {
            if (i > 0) domains_str += " ";
            domains_str += config.search_domains[i];
        }

        // Write resolved.conf
        std::ofstream out("/etc/systemd/resolved.conf");
        if (!out.is_open()) {
            // Try resolvectl
            for (const auto& server : config.servers) {
                run_cmd("resolvectl dns 1 " + server + " 2>/dev/null");
            }
            return Result<void, std::string>::ok();
        }

        out << "[Resolve]\n"
            << "DNS=" << servers_str << "\n";
        if (!domains_str.empty()) {
            out << "Domains=" << domains_str << "\n";
        }
        out << "DNSSEC=" << (config.dnssec ? "yes" : "no") << "\n"
            << "DNSOverTLS=" << (config.dns_over_tls ? "yes" : "no") << "\n";
        out.close();

        run_cmd("systemctl restart systemd-resolved 2>/dev/null");
        return Result<void, std::string>::ok();
    }

    // Direct /etc/resolv.conf manipulation
    std::ofstream out("/etc/resolv.conf");
    if (!out.is_open()) {
        return Result<void, std::string>::error("cannot write to /etc/resolv.conf");
    }

    out << "# Generated by straylight-network\n";
    for (const auto& domain : config.search_domains) {
        out << "search " << domain << "\n";
    }
    for (const auto& server : config.servers) {
        out << "nameserver " << server << "\n";
    }
    out.close();

    return Result<void, std::string>::ok();
}

Result<DnsConfig, std::string> NetworkManager::dns_get() const {
    DnsConfig config;

    // Try resolvectl
    auto res = run_cmd("resolvectl status 2>/dev/null");
    if (res.has_value()) {
        config.mode = "systemd-resolved";
        std::istringstream stream(res.value());
        std::string line;
        while (std::getline(stream, line)) {
            auto pos = line.find_first_not_of(" \t");
            if (pos != std::string::npos) line = line.substr(pos);

            if (line.rfind("DNS Servers:", 0) == 0 || line.rfind("Current DNS Server:", 0) == 0) {
                std::regex ip_re(R"((\d+\.\d+\.\d+\.\d+|[0-9a-fA-F:]+))");
                auto it = std::sregex_iterator(line.begin(), line.end(), ip_re);
                for (; it != std::sregex_iterator(); ++it) {
                    std::string server = (*it)[1].str();
                    bool found = false;
                    for (const auto& s : config.servers) {
                        if (s == server) { found = true; break; }
                    }
                    if (!found) config.servers.push_back(server);
                }
            } else if (line.rfind("DNS Domain:", 0) == 0) {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string domains = line.substr(colon + 1);
                    std::istringstream ds(domains);
                    std::string domain;
                    while (ds >> domain) {
                        config.search_domains.push_back(domain);
                    }
                }
            } else if (line.find("DNSSEC") != std::string::npos) {
                config.dnssec = (line.find("yes") != std::string::npos);
            } else if (line.find("DNS over TLS") != std::string::npos) {
                config.dns_over_tls = (line.find("yes") != std::string::npos);
            }
        }
        return Result<DnsConfig, std::string>::ok(config);
    }

    // Read /etc/resolv.conf
    config.mode = "resolv.conf";
    std::ifstream resolv("/etc/resolv.conf");
    if (!resolv.is_open()) {
        return Result<DnsConfig, std::string>::error("cannot read DNS configuration");
    }

    std::string line;
    while (std::getline(resolv, line)) {
        if (line.rfind("nameserver", 0) == 0) {
            auto pos = line.find_first_not_of(" \t", 10);
            if (pos != std::string::npos) {
                config.servers.push_back(line.substr(pos));
            }
        } else if (line.rfind("search", 0) == 0) {
            std::istringstream ds(line.substr(7));
            std::string domain;
            while (ds >> domain) {
                config.search_domains.push_back(domain);
            }
        }
    }

    return Result<DnsConfig, std::string>::ok(config);
}

// ---------------------------------------------------------------------------
// Bond / Bridge
// ---------------------------------------------------------------------------

Result<void, std::string> NetworkManager::bond_create(const BondConfig& config) {
    if (config.is_bridge) {
        // Create bridge
        auto res = run_cmd("ip link add name " + config.name + " type bridge 2>/dev/null");
        if (!res.has_value()) {
            return Result<void, std::string>::error("failed to create bridge: " + res.error());
        }

        // Add members
        for (const auto& member : config.members) {
            run_cmd("ip link set " + member + " master " + config.name + " 2>/dev/null");
        }

        // Set IP if provided
        if (!config.ip.empty()) {
            run_cmd("ip addr add " + config.ip + " dev " + config.name + " 2>/dev/null");
        }

        // Bring up
        run_cmd("ip link set " + config.name + " up 2>/dev/null");
    } else {
        // Create bond
        std::string mode = config.mode.empty() ? "balance-rr" : config.mode;

        // Create bond device
        auto res = run_cmd("ip link add " + config.name + " type bond mode " + mode + " 2>/dev/null");
        if (!res.has_value()) {
            return Result<void, std::string>::error("failed to create bond: " + res.error());
        }

        // Add members
        for (const auto& member : config.members) {
            run_cmd("ip link set " + member + " down 2>/dev/null");
            run_cmd("ip link set " + member + " master " + config.name + " 2>/dev/null");
        }

        // Set IP if provided
        if (!config.ip.empty()) {
            run_cmd("ip addr add " + config.ip + " dev " + config.name + " 2>/dev/null");
        }

        // Bring up
        run_cmd("ip link set " + config.name + " up 2>/dev/null");
        for (const auto& member : config.members) {
            run_cmd("ip link set " + member + " up 2>/dev/null");
        }
    }

    return Result<void, std::string>::ok();
}

Result<void, std::string> NetworkManager::bond_destroy(const std::string& name) {
    run_cmd("ip link set " + name + " down 2>/dev/null");
    auto res = run_cmd("ip link delete " + name + " 2>/dev/null");
    if (!res.has_value()) {
        return Result<void, std::string>::error("failed to destroy: " + res.error());
    }
    return Result<void, std::string>::ok();
}

Result<std::vector<BondConfig>, std::string> NetworkManager::bond_list() const {
    std::vector<BondConfig> bonds;

    // Scan /sys/class/net for bonds
    std::string net_dir = "/sys/class/net";
    if (!fs::exists(net_dir)) {
        return Result<std::vector<BondConfig>, std::string>::ok(bonds);
    }

    for (const auto& entry : fs::directory_iterator(net_dir)) {
        std::string name = entry.path().filename().string();
        BondConfig config;
        config.name = name;

        if (fs::exists(entry.path().string() + "/bonding")) {
            config.is_bridge = false;
            // Read mode
            std::ifstream mode_f(entry.path().string() + "/bonding/mode");
            if (mode_f.is_open()) std::getline(mode_f, config.mode);

            // Read slaves
            std::ifstream slaves_f(entry.path().string() + "/bonding/slaves");
            if (slaves_f.is_open()) {
                std::string slaves;
                std::getline(slaves_f, slaves);
                std::istringstream ss(slaves);
                std::string member;
                while (ss >> member) config.members.push_back(member);
            }

            bonds.push_back(config);
        } else if (fs::exists(entry.path().string() + "/bridge")) {
            config.is_bridge = true;
            // Read bridge members from brif/
            std::string brif = entry.path().string() + "/brif";
            if (fs::exists(brif)) {
                for (const auto& brentry : fs::directory_iterator(brif)) {
                    config.members.push_back(brentry.path().filename().string());
                }
            }
            bonds.push_back(config);
        }
    }

    return Result<std::vector<BondConfig>, std::string>::ok(bonds);
}

// ---------------------------------------------------------------------------
// Saved connections
// ---------------------------------------------------------------------------

std::vector<SavedConnection> NetworkManager::saved_connections() const {
    std::vector<SavedConnection> connections;
    std::string dir = config_dir();
    if (!fs::exists(dir)) return connections;

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string fname = entry.path().filename().string();
        if (fname.size() <= 5 || fname.substr(fname.size() - 5) != ".json") continue;

        std::ifstream f(entry.path().string());
        if (!f.is_open()) continue;
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());

        SavedConnection conn;
        conn.name = fname.substr(0, fname.size() - 5);

        std::regex type_re(R"("type"\s*:\s*"([^"]*)")");
        std::regex ssid_re(R"("ssid"\s*:\s*"([^"]*)")");
        std::regex auto_re(R"("auto_connect"\s*:\s*(true|false))");

        std::smatch m;
        if (std::regex_search(content, m, type_re)) conn.type = m[1].str();
        if (std::regex_search(content, m, ssid_re)) conn.ssid = m[1].str();
        if (std::regex_search(content, m, auto_re)) conn.auto_connect = (m[1].str() == "true");

        connections.push_back(conn);
    }

    return connections;
}

Result<void, std::string> NetworkManager::forget_connection(const std::string& name) {
    std::string path = config_dir() + "/" + name + ".json";
    if (fs::exists(path)) {
        fs::remove(path);
    }

    // Also remove from nmcli if available
    if (has_nmcli()) {
        run_cmd("nmcli connection delete '" + name + "' 2>/dev/null");
    }

    return Result<void, std::string>::ok();
}

} // namespace straylight
