// tools/probe/diagnostics.cpp
// Human-readable formatting for network diagnostic results.
#include "diagnostics.h"

#include <iomanip>
#include <sstream>

namespace straylight {

std::string ProbeDiagnostics::pad(const std::string& s, size_t width) {
    if (s.size() >= width) return s;
    return s + std::string(width - s.size(), ' ');
}

std::string ProbeDiagnostics::format_ms(double ms) {
    std::ostringstream oss;
    if (ms < 1.0) {
        oss << std::fixed << std::setprecision(2) << ms << "ms";
    } else if (ms < 100.0) {
        oss << std::fixed << std::setprecision(1) << ms << "ms";
    } else {
        oss << std::fixed << std::setprecision(0) << ms << "ms";
    }
    return oss.str();
}

std::string ProbeDiagnostics::format_hosts(const std::vector<DiscoveredHost>& hosts) {
    if (hosts.empty()) return "No hosts found.\n";

    std::ostringstream oss;
    oss << "\n";
    oss << pad("IP ADDRESS", 18)
        << pad("HOSTNAME", 30)
        << pad("MAC", 20)
        << pad("VENDOR", 16)
        << pad("RTT", 12)
        << pad("OS", 20) << "\n";
    oss << std::string(116, '-') << "\n";

    for (const auto& h : hosts) {
        oss << pad(h.ip, 18)
            << pad(h.hostname.empty() ? "-" : h.hostname, 30)
            << pad(h.mac.empty() ? "-" : h.mac, 20)
            << pad(h.vendor.empty() ? "-" : h.vendor, 16)
            << pad(format_ms(h.rtt_ms), 12)
            << pad(h.os_guess.empty() ? "-" : h.os_guess, 20) << "\n";
    }

    oss << "\n" << hosts.size() << " host(s) found\n";
    return oss.str();
}

std::string ProbeDiagnostics::format_ports(const std::string& host,
                                           const std::vector<PortResult>& ports) {
    std::ostringstream oss;
    oss << "\nOpen ports on " << host << ":\n\n";

    if (ports.empty()) {
        oss << "No open ports found.\n";
        return oss.str();
    }

    oss << pad("PORT", 10) << pad("STATE", 12) << pad("SERVICE", 20) << pad("RTT", 12) << "\n";
    oss << std::string(54, '-') << "\n";

    for (const auto& p : ports) {
        oss << pad(std::to_string(p.port) + "/tcp", 10)
            << pad(p.state, 12)
            << pad(p.service, 20)
            << pad(format_ms(p.rtt_ms), 12) << "\n";
    }

    oss << "\n" << ports.size() << " open port(s)\n";
    return oss.str();
}

std::string ProbeDiagnostics::format_trace(const std::string& host,
                                           const std::vector<TraceHop>& hops) {
    std::ostringstream oss;
    oss << "\nTraceroute to " << host << " (" << hops.size() << " hops max):\n\n";

    oss << pad("HOP", 6) << pad("IP ADDRESS", 18) << pad("HOSTNAME", 40) << pad("RTT", 12) << "\n";
    oss << std::string(76, '-') << "\n";

    for (const auto& h : hops) {
        oss << pad(std::to_string(h.hop), 6);
        if (h.timeout) {
            oss << pad("*", 18) << pad("*", 40) << pad("*", 12);
        } else {
            oss << pad(h.ip.empty() ? "*" : h.ip, 18)
                << pad(h.hostname.empty() ? h.ip : h.hostname, 40)
                << pad(format_ms(h.rtt_ms), 12);
        }
        oss << "\n";
    }

    return oss.str();
}

std::string ProbeDiagnostics::format_dns(const std::string& domain,
                                         const std::vector<DnsRecord>& records) {
    std::ostringstream oss;
    oss << "\nDNS records for " << domain << ":\n\n";

    oss << pad("TYPE", 8) << pad("NAME", 30) << pad("VALUE", 50) << pad("TTL", 8) << "\n";
    oss << std::string(96, '-') << "\n";

    for (const auto& r : records) {
        oss << pad(r.type, 8)
            << pad(r.name, 30)
            << pad(r.value, 50)
            << pad(r.ttl > 0 ? std::to_string(r.ttl) : "-", 8) << "\n";
    }

    oss << "\n" << records.size() << " record(s)\n";
    return oss.str();
}

std::string ProbeDiagnostics::format_bandwidth(const std::string& host, double mbps) {
    std::ostringstream oss;
    oss << "\nBandwidth test to " << host << ":\n\n";
    oss << std::fixed << std::setprecision(2);

    if (mbps >= 1000.0) {
        oss << "  Throughput: " << (mbps / 1000.0) << " Gbps\n";
    } else {
        oss << "  Throughput: " << mbps << " Mbps\n";
    }

    return oss.str();
}

std::string ProbeDiagnostics::format_wifi(const std::vector<WifiAP>& aps) {
    if (aps.empty()) return "No WiFi networks found.\n";

    std::ostringstream oss;
    oss << "\nWiFi Networks:\n\n";
    oss << pad("SSID", 32) << pad("BSSID", 20) << pad("SIGNAL", 10)
        << pad("CH", 6) << pad("FREQ", 8) << pad("SECURITY", 20) << "\n";
    oss << std::string(96, '-') << "\n";

    for (const auto& ap : aps) {
        oss << pad(ap.ssid.empty() ? "(hidden)" : ap.ssid, 32)
            << pad(ap.bssid, 20)
            << pad(std::to_string(ap.signal_dbm) + " dBm", 10)
            << pad(std::to_string(ap.channel), 6)
            << pad(std::to_string(ap.frequency_ghz).substr(0, 3) + " GHz", 8)
            << pad(ap.security, 20) << "\n";
    }

    oss << "\n" << aps.size() << " network(s) found\n";
    return oss.str();
}

std::string ProbeDiagnostics::format_health(const std::vector<HealthCheck>& checks) {
    std::ostringstream oss;
    oss << "\nNetwork Health Check:\n\n";

    int passed = 0;
    int total = static_cast<int>(checks.size());

    for (const auto& c : checks) {
        std::string status = c.passed ? "[PASS]" : "[FAIL]";
        oss << "  " << pad(status, 8) << pad(c.name, 16) << c.detail << "\n";
        if (c.passed) ++passed;
    }

    oss << "\n  Score: " << passed << "/" << total;
    if (passed == total) {
        oss << " — Network healthy";
    } else if (passed >= total / 2) {
        oss << " — Partial connectivity";
    } else {
        oss << " — Network issues detected";
    }
    oss << "\n";

    return oss.str();
}

} // namespace straylight
