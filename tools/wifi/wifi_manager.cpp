// tools/wifi/wifi_manager.cpp
// Full WiFi management implementation for StrayLight OS.

#include "wifi_manager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace straylight {

WifiManager::WifiManager() {
    fs::create_directories(config_dir());
}

WifiManager::~WifiManager() = default;

std::string WifiManager::config_dir() const {
    const char* home = std::getenv("HOME");
    if (!home) home = "/root";
    return std::string(home) + "/.config/straylight/wifi";
}

Result<std::string, std::string> WifiManager::run_cmd(const std::string& cmd) const {
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
    if (rc != 0 && output.empty()) {
        return Result<std::string, std::string>::error(
            "command failed (rc=" + std::to_string(rc) + "): " + cmd);
    }
    return Result<std::string, std::string>::ok(output);
}

bool WifiManager::has_nmcli() const {
    return run_cmd("which nmcli 2>/dev/null").has_value();
}

std::string WifiManager::find_wifi_interface() const {
    auto res = run_cmd("iw dev 2>/dev/null");
    if (res.has_value()) {
        std::regex iface_re(R"(Interface\s+(\S+))");
        std::smatch m;
        std::string out = res.value();
        if (std::regex_search(out, m, iface_re)) {
            return m[1].str();
        }
    }
    // Fallback: check /sys/class/net for wireless devices
    std::string net_dir = "/sys/class/net";
    if (fs::exists(net_dir)) {
        for (const auto& entry : fs::directory_iterator(net_dir)) {
            std::string name = entry.path().filename().string();
            if (fs::exists(entry.path().string() + "/wireless")) {
                return name;
            }
        }
    }
    return "wlan0";
}

int WifiManager::freq_to_channel(int freq_mhz) const {
    if (freq_mhz >= 2412 && freq_mhz <= 2484) {
        if (freq_mhz == 2484) return 14;
        return (freq_mhz - 2407) / 5;
    }
    if (freq_mhz >= 5180 && freq_mhz <= 5825) {
        return (freq_mhz - 5000) / 5;
    }
    if (freq_mhz >= 5955 && freq_mhz <= 7115) {
        return (freq_mhz - 5950) / 5;
    }
    return 0;
}

std::string WifiManager::signal_bar(int dbm) const {
    int quality;
    if (dbm >= -50) quality = 4;
    else if (dbm >= -60) quality = 3;
    else if (dbm >= -70) quality = 2;
    else if (dbm >= -80) quality = 1;
    else quality = 0;

    std::string bar = "[";
    for (int i = 0; i < 4; ++i) {
        bar += (i < quality) ? "#" : ".";
    }
    bar += "]";
    return bar;
}

// ---------------------------------------------------------------------------
// Scan parsing
// ---------------------------------------------------------------------------

std::vector<WifiScanResult> WifiManager::parse_iw_scan(const std::string& output) const {
    std::vector<WifiScanResult> networks;
    std::istringstream stream(output);
    std::string line;
    WifiScanResult* current = nullptr;

    while (std::getline(stream, line)) {
        auto pos = line.find_first_not_of(" \t");
        if (pos != std::string::npos) line = line.substr(pos);

        if (line.rfind("BSS ", 0) == 0) {
            networks.emplace_back();
            current = &networks.back();
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
                    current->signal_dbm = static_cast<int>(std::stod(m[1].str()));
                }
            } else if (line.rfind("freq:", 0) == 0) {
                std::regex freq_re(R"(\d+)");
                std::smatch m;
                if (std::regex_search(line, m, freq_re)) {
                    current->frequency_mhz = std::stoi(m[0].str());
                    current->channel = freq_to_channel(current->frequency_mhz);
                }
            } else if (line.find("WPA") != std::string::npos) {
                if (current->security.empty() || current->security == "Open") {
                    current->security = "WPA";
                }
            } else if (line.find("RSN") != std::string::npos) {
                current->security = "WPA2";
            } else if (line.find("SAE") != std::string::npos) {
                current->security = "WPA3";
            }
        }
    }

    for (auto& net : networks) {
        if (net.security.empty()) net.security = "Open";
    }

    std::sort(networks.begin(), networks.end(),
              [](const auto& a, const auto& b) {
                  return a.signal_dbm > b.signal_dbm;
              });

    return networks;
}

std::vector<WifiScanResult> WifiManager::parse_nmcli_scan(const std::string& output) const {
    std::vector<WifiScanResult> networks;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // nmcli -t fields: ACTIVE:BSSID:SSID:MODE:CHAN:RATE:SIGNAL:BARS:SECURITY
        // Split on ':'  but BSSID has colons...
        // Use nmcli with --fields and escape
        WifiScanResult net;

        // Simple field extraction — nmcli -t uses \: for literal colons
        std::vector<std::string> fields;
        std::string field;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == ':') {
                field += ':';
                ++i;
            } else if (line[i] == ':') {
                fields.push_back(field);
                field.clear();
            } else {
                field += line[i];
            }
        }
        fields.push_back(field);

        if (fields.size() >= 8) {
            net.connected = (fields[0] == "*" || fields[0] == "yes");
            net.bssid = fields[1];
            net.ssid = fields[2];
            // fields[3] = mode
            try { net.channel = std::stoi(fields[4]); } catch (...) {}
            // fields[5] = rate string
            std::regex rate_re(R"((\d+)\s*Mbit)");
            std::smatch m;
            std::string rate_str = fields[5];
            if (std::regex_search(rate_str, m, rate_re)) {
                net.rate_mbps = std::stod(m[1].str());
            }
            try { net.signal_dbm = std::stoi(fields[6]); } catch (...) {}
            // Convert percentage to dBm approximation if positive
            if (net.signal_dbm > 0) {
                net.signal_dbm = -100 + net.signal_dbm;
            }
            if (fields.size() > 8) net.security = fields[8];
            else net.security = "Open";
            networks.push_back(net);
        }
    }

    std::sort(networks.begin(), networks.end(),
              [](const auto& a, const auto& b) {
                  return a.signal_dbm > b.signal_dbm;
              });

    return networks;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<std::vector<WifiScanResult>, std::string> WifiManager::scan() const {
    if (has_nmcli()) {
        auto res = run_cmd(
            "nmcli -t -f ACTIVE,BSSID,SSID,MODE,CHAN,RATE,SIGNAL,BARS,SECURITY "
            "device wifi list --rescan yes 2>/dev/null");
        if (res.has_value()) {
            return Result<std::vector<WifiScanResult>, std::string>::ok(
                parse_nmcli_scan(res.value()));
        }
    }

    std::string iface = find_wifi_interface();
    auto res = run_cmd("iw dev " + iface + " scan 2>/dev/null");
    if (!res.has_value()) {
        return Result<std::vector<WifiScanResult>, std::string>::error(
            "WiFi scan failed: " + res.error());
    }

    return Result<std::vector<WifiScanResult>, std::string>::ok(
        parse_iw_scan(res.value()));
}

Result<void, std::string> WifiManager::connect(const std::string& ssid,
                                                 const std::string& password) {
    if (has_nmcli()) {
        std::string cmd = "nmcli device wifi connect '" + ssid + "'";
        if (!password.empty()) cmd += " password '" + password + "'";
        cmd += " 2>&1";
        auto res = run_cmd(cmd);
        if (!res.has_value()) {
            return Result<void, std::string>::error("connection failed: " + res.error());
        }
        save_network(ssid, "WPA2");
        return Result<void, std::string>::ok();
    }

    // wpa_supplicant fallback
    std::string iface = find_wifi_interface();
    if (password.empty()) {
        auto res = run_cmd("iw dev " + iface + " connect '" + ssid + "' 2>&1");
        if (!res.has_value()) {
            return Result<void, std::string>::error("connection failed: " + res.error());
        }
    } else {
        auto psk_res = run_cmd("wpa_passphrase '" + ssid + "' '" + password + "' 2>/dev/null");
        if (!psk_res.has_value()) {
            return Result<void, std::string>::error("PSK generation failed: " + psk_res.error());
        }

        std::string conf_path = "/tmp/straylight_wpa_" + ssid + ".conf";
        std::ofstream conf(conf_path);
        if (!conf.is_open()) {
            return Result<void, std::string>::error("cannot write wpa_supplicant config");
        }
        conf << psk_res.value();
        conf.close();

        run_cmd("wpa_supplicant -B -i " + iface + " -c " + conf_path + " 2>/dev/null");
        run_cmd("dhclient " + iface + " 2>/dev/null");
    }

    save_network(ssid, password.empty() ? "Open" : "WPA2");
    return Result<void, std::string>::ok();
}

Result<void, std::string> WifiManager::disconnect() {
    if (has_nmcli()) {
        std::string iface = find_wifi_interface();
        auto res = run_cmd("nmcli device disconnect " + iface + " 2>&1");
        if (!res.has_value()) {
            return Result<void, std::string>::error("disconnect failed: " + res.error());
        }
        return Result<void, std::string>::ok();
    }

    std::string iface = find_wifi_interface();
    run_cmd("ip link set " + iface + " down 2>/dev/null");
    return Result<void, std::string>::ok();
}

Result<std::vector<SavedWifi>, std::string> WifiManager::saved() const {
    std::vector<SavedWifi> networks;
    std::string dir = config_dir();
    if (!fs::exists(dir)) {
        return Result<std::vector<SavedWifi>, std::string>::ok(networks);
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string fname = entry.path().filename().string();
        if (fname.size() <= 5 || fname.substr(fname.size() - 5) != ".conf") continue;

        std::ifstream f(entry.path().string());
        if (!f.is_open()) continue;

        SavedWifi wifi;
        wifi.ssid = fname.substr(0, fname.size() - 5);

        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("security=", 0) == 0) wifi.security = line.substr(9);
            else if (line.rfind("auto_connect=", 0) == 0) wifi.auto_connect = (line.substr(13) == "true");
            else if (line.rfind("last_connected=", 0) == 0) wifi.last_connected = line.substr(15);
            else if (line.rfind("priority=", 0) == 0) {
                try { wifi.priority = std::stoi(line.substr(9)); } catch (...) {}
            }
        }

        networks.push_back(wifi);
    }

    // Also check nmcli saved connections
    if (has_nmcli()) {
        auto res = run_cmd("nmcli -t -f NAME,TYPE connection show 2>/dev/null");
        if (res.has_value()) {
            std::istringstream stream(res.value());
            std::string line;
            while (std::getline(stream, line)) {
                if (line.find("802-11-wireless") != std::string::npos) {
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string name = line.substr(0, colon);
                        bool already = false;
                        for (const auto& w : networks) {
                            if (w.ssid == name) { already = true; break; }
                        }
                        if (!already) {
                            SavedWifi wifi;
                            wifi.ssid = name;
                            wifi.security = "WPA2";
                            networks.push_back(wifi);
                        }
                    }
                }
            }
        }
    }

    return Result<std::vector<SavedWifi>, std::string>::ok(networks);
}

Result<void, std::string> WifiManager::forget(const std::string& ssid) {
    std::string path = config_dir() + "/" + ssid + ".conf";
    if (fs::exists(path)) {
        fs::remove(path);
    }

    if (has_nmcli()) {
        run_cmd("nmcli connection delete '" + ssid + "' 2>/dev/null");
    }

    return Result<void, std::string>::ok();
}

Result<std::vector<ChannelInfo>, std::string> WifiManager::channels() const {
    auto scan_res = scan();
    if (!scan_res.has_value()) {
        return Result<std::vector<ChannelInfo>, std::string>::error(scan_res.error());
    }

    const auto& networks = scan_res.value();
    std::map<int, std::vector<const WifiScanResult*>> by_channel;

    for (const auto& net : networks) {
        if (net.channel > 0) {
            by_channel[net.channel].push_back(&net);
        }
    }

    // Build full channel list (2.4GHz: 1-14, 5GHz common: 36,40,44,48,52,56,60,64,100-144,149-165)
    std::vector<int> all_channels_24 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    std::vector<int> all_channels_5 = {36, 40, 44, 48, 52, 56, 60, 64,
                                        100, 104, 108, 112, 116, 120, 124, 128,
                                        132, 136, 140, 144, 149, 153, 157, 161, 165};

    std::vector<ChannelInfo> result;

    auto process_channel = [&](int ch, const std::string& band) {
        ChannelInfo info;
        info.channel = ch;
        info.band = band;

        if (band == "2.4GHz") {
            info.frequency_mhz = 2407 + ch * 5;
        } else {
            info.frequency_mhz = 5000 + ch * 5;
        }

        auto it = by_channel.find(ch);
        if (it != by_channel.end()) {
            info.network_count = static_cast<int>(it->second.size());
            int total_signal = 0;
            for (const auto* net : it->second) {
                total_signal += net->signal_dbm;
            }
            info.avg_signal_dbm = info.network_count > 0 ? total_signal / info.network_count : 0;
        }

        // Calculate interference: overlapping channels in 2.4GHz
        if (band == "2.4GHz") {
            int overlap_count = 0;
            for (int adj = ch - 2; adj <= ch + 2; ++adj) {
                if (adj != ch) {
                    auto adj_it = by_channel.find(adj);
                    if (adj_it != by_channel.end()) {
                        overlap_count += static_cast<int>(adj_it->second.size());
                    }
                }
            }
            info.interference_score = std::min(100,
                info.network_count * 20 + overlap_count * 10);
        } else {
            // 5GHz channels don't overlap
            info.interference_score = std::min(100, info.network_count * 25);
        }

        result.push_back(info);
    };

    for (int ch : all_channels_24) process_channel(ch, "2.4GHz");
    for (int ch : all_channels_5) process_channel(ch, "5GHz");

    return Result<std::vector<ChannelInfo>, std::string>::ok(result);
}

Result<SignalQuality, std::string> WifiManager::signal() const {
    std::string iface = find_wifi_interface();
    SignalQuality sq;

    // Try iw link
    auto link_res = run_cmd("iw dev " + iface + " link 2>/dev/null");
    if (!link_res.has_value() || link_res.value().find("Not connected") != std::string::npos) {
        return Result<SignalQuality, std::string>::error("not connected to any WiFi network");
    }

    std::string info = link_res.value();
    std::smatch m;

    std::regex ssid_re(R"(SSID:\s+(.+))");
    if (std::regex_search(info, m, ssid_re)) sq.ssid = m[1].str();

    std::regex signal_re(R"(signal:\s+(-?\d+)\s+dBm)");
    if (std::regex_search(info, m, signal_re)) sq.signal_dbm = std::stoi(m[1].str());

    std::regex freq_re(R"(freq:\s+(\d+))");
    if (std::regex_search(info, m, freq_re)) {
        sq.frequency_mhz = std::stoi(m[1].str());
        sq.channel = freq_to_channel(sq.frequency_mhz);
    }

    std::regex tx_re(R"(tx bitrate:\s+([\d.]+)\s+MBit)");
    if (std::regex_search(info, m, tx_re)) sq.tx_rate_mbps = std::stod(m[1].str());

    std::regex rx_re(R"(rx bitrate:\s+([\d.]+)\s+MBit)");
    if (std::regex_search(info, m, rx_re)) sq.rx_rate_mbps = std::stod(m[1].str());

    // Read noise from survey
    auto survey_res = run_cmd("iw dev " + iface + " survey dump 2>/dev/null");
    if (survey_res.has_value()) {
        std::regex noise_re(R"(noise:\s+(-?\d+)\s+dBm)");
        std::string survey = survey_res.value();
        if (std::regex_search(survey, m, noise_re)) {
            sq.noise_dbm = std::stoi(m[1].str());
        }
    }

    // Calculate link quality (0-100) from signal
    if (sq.signal_dbm >= -50) sq.link_quality = 100;
    else if (sq.signal_dbm <= -100) sq.link_quality = 0;
    else sq.link_quality = 2 * (sq.signal_dbm + 100);

    return Result<SignalQuality, std::string>::ok(sq);
}

Result<std::string, std::string> WifiManager::qr(const std::string& ssid) const {
    // Look up saved network security type
    std::string security = "WPA";
    std::string password;

    std::string conf_path = config_dir() + "/" + ssid + ".conf";
    if (fs::exists(conf_path)) {
        std::ifstream f(conf_path);
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("security=", 0) == 0) security = line.substr(9);
            if (line.rfind("password=", 0) == 0) password = line.substr(9);
        }
    }

    // Also try nmcli for the password
    if (password.empty() && has_nmcli()) {
        auto res = run_cmd("nmcli -s -g 802-11-wireless-security.psk connection show '"
                           + ssid + "' 2>/dev/null");
        if (res.has_value()) {
            password = res.value();
            if (!password.empty() && password.back() == '\n') password.pop_back();
        }
    }

    // Build WiFi QR code string: WIFI:T:<type>;S:<ssid>;P:<password>;;
    std::string wifi_str = "WIFI:T:" + security + ";S:" + ssid + ";";
    if (!password.empty()) wifi_str += "P:" + password + ";";
    wifi_str += ";";

    // Try qrencode for text QR
    auto qr_res = run_cmd("echo -n '" + wifi_str + "' | qrencode -t UTF8 2>/dev/null");
    if (qr_res.has_value() && !qr_res.value().empty()) {
        return Result<std::string, std::string>::ok(qr_res.value());
    }

    // Fallback: generate a simple ASCII representation
    std::ostringstream out;
    out << "WiFi Network QR Data:\n"
        << "  SSID:     " << ssid << "\n"
        << "  Security: " << security << "\n"
        << "  String:   " << wifi_str << "\n"
        << "\n"
        << "  (Install 'qrencode' for visual QR code)\n"
        << "\n"
        << "  Manual connect:\n"
        << "    straylight-wifi connect " << ssid << "\n";

    return Result<std::string, std::string>::ok(out.str());
}

void WifiManager::save_network(const std::string& ssid, const std::string& security) const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

    std::string path = config_dir() + "/" + ssid + ".conf";
    std::ofstream out(path);
    if (out.is_open()) {
        out << "ssid=" << ssid << "\n"
            << "security=" << security << "\n"
            << "auto_connect=true\n"
            << "last_connected=" << buf << "\n"
            << "priority=0\n";
    }
}

} // namespace straylight
