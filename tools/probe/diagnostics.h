// tools/probe/diagnostics.h
// Network diagnostics formatting and reporting for StrayLight Probe.
#pragma once

#include "scanner.h"

#include <string>
#include <vector>

namespace straylight {

/// Formats network scan results for human-readable terminal output.
class ProbeDiagnostics {
public:
    /// Format a list of discovered hosts as a table.
    static std::string format_hosts(const std::vector<DiscoveredHost>& hosts);

    /// Format port scan results.
    static std::string format_ports(const std::string& host,
                                    const std::vector<PortResult>& ports);

    /// Format traceroute results.
    static std::string format_trace(const std::string& host,
                                    const std::vector<TraceHop>& hops);

    /// Format DNS records.
    static std::string format_dns(const std::string& domain,
                                  const std::vector<DnsRecord>& records);

    /// Format bandwidth test result.
    static std::string format_bandwidth(const std::string& host, double mbps);

    /// Format WiFi scan results.
    static std::string format_wifi(const std::vector<WifiAP>& aps);

    /// Format health check results.
    static std::string format_health(const std::vector<HealthCheck>& checks);

private:
    /// Pad a string to a fixed width.
    static std::string pad(const std::string& s, size_t width);

    /// Format milliseconds with appropriate precision.
    static std::string format_ms(double ms);
};

} // namespace straylight
