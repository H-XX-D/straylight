// tools/network/network_manager.h
// Network management for StrayLight OS — WiFi, VPN, firewall, DNS, bonding.
#pragma once

#include <straylight/result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace straylight {

/// WiFi network discovered by scanning.
struct WifiNetwork {
    std::string ssid;
    std::string bssid;
    int signal_strength = 0;   // dBm or percentage
    int frequency_mhz = 0;
    std::string security;      // "WPA2", "WPA3", "WEP", "Open"
    bool connected = false;
    int channel = 0;
    double rate_mbps = 0;
};

/// Network interface status.
struct InterfaceStatus {
    std::string name;          // e.g. "eth0", "wlan0"
    std::string type;          // "ethernet", "wifi", "loopback", "bridge", "bond"
    std::string state;         // "up", "down", "dormant"
    std::string ipv4;
    std::string ipv6;
    std::string gateway;
    std::string mac;
    int mtu = 1500;
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    std::string ssid;          // current WiFi SSID if applicable
    int signal_dbm = 0;
};

/// VPN connection configuration.
struct VpnConfig {
    std::string name;
    std::string type;          // "wireguard", "openvpn", "ipsec"
    std::string server;
    int port = 0;
    std::string config_file;
    bool connected = false;
    std::string local_ip;
    std::string public_key;    // wireguard
};

/// Firewall rule.
struct FirewallRule {
    uint32_t id = 0;
    std::string chain;         // "input", "output", "forward"
    std::string action;        // "accept", "drop", "reject"
    std::string protocol;      // "tcp", "udp", "icmp", "any"
    std::string source;        // IP or CIDR
    std::string destination;
    int port = 0;
    std::string interface;
    std::string comment;
};

/// DNS configuration.
struct DnsConfig {
    std::vector<std::string> servers;
    std::vector<std::string> search_domains;
    std::string mode;          // "systemd-resolved", "resolv.conf", "dnsmasq"
    bool dnssec = false;
    bool dns_over_tls = false;
};

/// Network bond/bridge configuration.
struct BondConfig {
    std::string name;
    std::string mode;          // "balance-rr", "active-backup", "802.3ad", etc.
    std::vector<std::string> members;
    std::string ip;
    bool is_bridge = false;
};

/// Saved network connection.
struct SavedConnection {
    std::string name;
    std::string type;          // "wifi", "vpn", "ethernet"
    std::string ssid;
    bool auto_connect = true;
    std::string last_used;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    /// Get status of all network interfaces.
    Result<std::vector<InterfaceStatus>, std::string> status() const;

    /// Scan for available WiFi networks.
    Result<std::vector<WifiNetwork>, std::string> scan_wifi() const;

    /// Connect to a WiFi network.
    Result<void, std::string> connect_wifi(const std::string& ssid,
                                            const std::string& password = "",
                                            bool hidden = false);

    /// Disconnect from current network.
    Result<void, std::string> disconnect(const std::string& interface = "");

    /// Add a VPN configuration.
    Result<void, std::string> vpn_add(const VpnConfig& config);

    /// Connect to a VPN.
    Result<void, std::string> vpn_connect(const std::string& name);

    /// Disconnect from a VPN.
    Result<void, std::string> vpn_disconnect(const std::string& name);

    /// List VPN configurations.
    Result<std::vector<VpnConfig>, std::string> vpn_list() const;

    /// Add a firewall rule.
    Result<void, std::string> firewall_add(const FirewallRule& rule);

    /// Remove a firewall rule.
    Result<void, std::string> firewall_remove(uint32_t rule_id);

    /// List firewall rules.
    Result<std::vector<FirewallRule>, std::string> firewall_list() const;

    /// Set DNS configuration.
    Result<void, std::string> dns_set(const DnsConfig& config);

    /// Get current DNS configuration.
    Result<DnsConfig, std::string> dns_get() const;

    /// Create a network bond or bridge.
    Result<void, std::string> bond_create(const BondConfig& config);

    /// Destroy a bond or bridge.
    Result<void, std::string> bond_destroy(const std::string& name);

    /// List bonds and bridges.
    Result<std::vector<BondConfig>, std::string> bond_list() const;

    /// List saved connections.
    std::vector<SavedConnection> saved_connections() const;

    /// Forget a saved connection.
    Result<void, std::string> forget_connection(const std::string& name);

private:
    /// Config directory for saved networks.
    std::string config_dir() const;

    /// Run a shell command and capture output.
    Result<std::string, std::string> run_cmd(const std::string& cmd) const;

    /// Check if nmcli is available.
    bool has_nmcli() const;

    /// Check if iw is available.
    bool has_iw() const;

    /// Parse nmcli output for interface status.
    std::vector<InterfaceStatus> parse_nmcli_status(const std::string& output) const;

    /// Parse iw scan output.
    std::vector<WifiNetwork> parse_iw_scan(const std::string& output) const;

    /// Parse ip addr output.
    std::vector<InterfaceStatus> parse_ip_addr(const std::string& output) const;

    /// Read /sys/class/net for interface info.
    InterfaceStatus read_sysfs_interface(const std::string& name) const;
};

} // namespace straylight
