// tools/wifi/wifi_manager.h
// WiFi management for StrayLight OS — scanning, connecting, signal analysis, channel optimization.
#pragma once

#include <straylight/result.h>

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace straylight {

/// WiFi network discovered during scan.
struct WifiScanResult {
    std::string ssid;
    std::string bssid;
    int         signal_dbm = 0;
    int         frequency_mhz = 0;
    int         channel = 0;
    std::string security;       // "WPA2", "WPA3", "WEP", "Open"
    double      rate_mbps = 0;
    bool        connected = false;
};

/// Channel usage analysis.
struct ChannelInfo {
    int         channel = 0;
    int         frequency_mhz = 0;
    int         network_count = 0;      // how many APs on this channel
    int         avg_signal_dbm = 0;
    int         interference_score = 0; // 0=clear, 100=congested
    std::string band;                   // "2.4GHz" or "5GHz"
};

/// Saved WiFi network profile.
struct SavedWifi {
    std::string ssid;
    std::string security;
    bool        auto_connect = true;
    std::string last_connected;     // ISO-8601
    int         priority = 0;
};

/// Signal quality snapshot.
struct SignalQuality {
    std::string ssid;
    std::string bssid;
    int         signal_dbm = 0;
    int         noise_dbm = 0;
    int         link_quality = 0;   // 0-100
    double      tx_rate_mbps = 0;
    double      rx_rate_mbps = 0;
    int         channel = 0;
    int         frequency_mhz = 0;
};

class WifiManager {
public:
    WifiManager();
    ~WifiManager();

    /// Scan for available WiFi networks.
    Result<std::vector<WifiScanResult>, std::string> scan() const;

    /// Connect to a WiFi network.
    Result<void, std::string> connect(const std::string& ssid,
                                       const std::string& password = "");

    /// Disconnect from current WiFi.
    Result<void, std::string> disconnect();

    /// List saved WiFi networks.
    Result<std::vector<SavedWifi>, std::string> saved() const;

    /// Forget a saved network.
    Result<void, std::string> forget(const std::string& ssid);

    /// Analyze channel usage and find best channel.
    Result<std::vector<ChannelInfo>, std::string> channels() const;

    /// Get current signal quality.
    Result<SignalQuality, std::string> signal() const;

    /// Generate text-based QR code for a network.
    Result<std::string, std::string> qr(const std::string& ssid) const;

private:
    std::string config_dir() const;
    Result<std::string, std::string> run_cmd(const std::string& cmd) const;
    bool has_nmcli() const;
    std::string find_wifi_interface() const;
    int freq_to_channel(int freq_mhz) const;
    std::vector<WifiScanResult> parse_iw_scan(const std::string& output) const;
    std::vector<WifiScanResult> parse_nmcli_scan(const std::string& output) const;
    void save_network(const std::string& ssid, const std::string& security) const;
    std::string signal_bar(int dbm) const;
};

} // namespace straylight
