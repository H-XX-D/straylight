// tools/probe/scanner.h
// Network scanning and device discovery for StrayLight Probe.
#pragma once

#include <straylight/result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace straylight {

/// A discovered host on the network.
struct DiscoveredHost {
    std::string ip;
    std::string mac;
    std::string vendor;      // MAC vendor from OUI lookup
    std::string hostname;    // reverse DNS if available
    int ttl = 0;             // for OS fingerprinting
    std::string os_guess;    // based on TTL
    double rtt_ms = 0.0;     // round-trip time
    bool alive = false;
};

/// An open port on a host.
struct PortResult {
    uint16_t port = 0;
    std::string state;       // "open", "closed", "filtered"
    std::string service;     // well-known service name
    double rtt_ms = 0.0;
};

/// A traceroute hop.
struct TraceHop {
    int hop = 0;
    std::string ip;
    std::string hostname;
    double rtt_ms = 0.0;     // average of probes
    bool timeout = false;
};

/// A DNS record.
struct DnsRecord {
    std::string type;    // A, AAAA, CNAME, MX, NS, TXT, SOA, PTR
    std::string name;
    std::string value;
    int ttl = 0;
};

/// WiFi access point info.
struct WifiAP {
    std::string ssid;
    std::string bssid;
    int channel = 0;
    int signal_dbm = 0;
    std::string security;
    double frequency_ghz = 0.0;
};

/// Network health check result.
struct HealthCheck {
    std::string name;
    bool passed = false;
    std::string detail;
    double latency_ms = 0.0;
};

/// Network scanner with ARP sweep, ICMP ping, port scanning, and diagnostics.
class NetworkScanner {
public:
    /// Detect the local subnet (e.g. "192.168.1.0/24") from the default interface.
    Result<std::string, std::string> detect_subnet() const;

    /// ARP + ICMP sweep of a subnet.  Returns all responding hosts.
    Result<std::vector<DiscoveredHost>, std::string> scan_subnet(
        const std::string& subnet) const;

    /// Ping a single host and return discovery info.
    Result<DiscoveredHost, std::string> ping_host(const std::string& host) const;

    /// TCP connect scan of common or specified ports on a host.
    Result<std::vector<PortResult>, std::string> scan_ports(
        const std::string& host,
        const std::vector<uint16_t>& ports = {}) const;

    /// Traceroute to a host with latency per hop.
    Result<std::vector<TraceHop>, std::string> traceroute(
        const std::string& host,
        int max_hops = 30) const;

    /// DNS resolution for all record types.
    Result<std::vector<DnsRecord>, std::string> dns_lookup(
        const std::string& domain,
        const std::string& type = "ANY") const;

    /// TCP bandwidth test (simple throughput estimation).
    Result<double, std::string> bandwidth_test(
        const std::string& host,
        uint16_t port = 5201) const;

    /// WiFi information: current connection and nearby APs.
    Result<std::vector<WifiAP>, std::string> wifi_scan() const;

    /// Full network health check.
    Result<std::vector<HealthCheck>, std::string> health_check() const;

private:
    /// Guess OS from ICMP TTL value.
    static std::string guess_os_from_ttl(int ttl);

    /// Look up MAC vendor from OUI prefix.
    static std::string lookup_mac_vendor(const std::string& mac);

    /// Get well-known service name for a port.
    static std::string service_name(uint16_t port);

    /// Default ports for scanning.
    static std::vector<uint16_t> default_ports();

    /// Parse a CIDR subnet into base IP and host count.
    static Result<std::pair<uint32_t, uint32_t>, std::string> parse_cidr(
        const std::string& cidr);

    /// Convert uint32 IP to dotted-quad string.
    static std::string ip_to_string(uint32_t ip);

    /// Convert dotted-quad string to uint32.
    static Result<uint32_t, std::string> string_to_ip(const std::string& ip);
};

} // namespace straylight
