// tools/probe/main.cpp
// straylight-probe — Network discovery and diagnostics for StrayLight OS.
#include "scanner.h"
#include "diagnostics.h"

#include <straylight/log.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using straylight::NetworkScanner;
using straylight::ProbeDiagnostics;

static void print_usage() {
    std::cerr
        << "straylight-probe — Network discovery & diagnostics\n\n"
        << "Usage:\n"
        << "  straylight-probe scan [subnet]         Discover hosts on network\n"
        << "  straylight-probe ports <host> [ports]   TCP port scan\n"
        << "  straylight-probe trace <host>           Traceroute with latency\n"
        << "  straylight-probe dns <domain> [type]    DNS resolution\n"
        << "  straylight-probe bandwidth <host>       TCP bandwidth test\n"
        << "  straylight-probe wifi                   WiFi scan\n"
        << "  straylight-probe health                 Full network health check\n"
        << "  straylight-probe ping <host>            Ping a single host\n"
        << "\nExamples:\n"
        << "  straylight-probe scan 192.168.1.0/24\n"
        << "  straylight-probe ports example.com 22,80,443\n"
        << "  straylight-probe dns example.com A\n";
}

static std::vector<uint16_t> parse_ports(const std::string& spec) {
    std::vector<uint16_t> ports;
    std::istringstream iss(spec);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Handle ranges like 80-100
        auto dash = token.find('-');
        if (dash != std::string::npos) {
            try {
                uint16_t start = static_cast<uint16_t>(std::stoi(token.substr(0, dash)));
                uint16_t end = static_cast<uint16_t>(std::stoi(token.substr(dash + 1)));
                for (uint16_t p = start; p <= end; ++p) {
                    ports.push_back(p);
                }
            } catch (...) {}
        } else {
            try {
                ports.push_back(static_cast<uint16_t>(std::stoi(token)));
            } catch (...) {}
        }
    }
    return ports;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    NetworkScanner scanner;

    if (cmd == "scan") {
        std::string subnet;
        if (argc >= 3) {
            subnet = argv[2];
        } else {
            auto det = scanner.detect_subnet();
            if (!det.has_value()) {
                std::cerr << "Error: " << det.error() << "\n";
                std::cerr << "Specify a subnet: straylight-probe scan 192.168.1.0/24\n";
                return 1;
            }
            subnet = det.value();
            std::cerr << "Auto-detected subnet: " << subnet << "\n";
        }

        std::cerr << "Scanning " << subnet << " ...\n";
        auto r = scanner.scan_subnet(subnet);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_hosts(r.value());

    } else if (cmd == "ports") {
        if (argc < 3) {
            std::cerr << "Usage: straylight-probe ports <host> [ports]\n";
            return 1;
        }
        std::string host = argv[2];
        std::vector<uint16_t> ports;
        if (argc >= 4) {
            ports = parse_ports(argv[3]);
        }

        std::cerr << "Scanning ports on " << host << " ...\n";
        auto r = scanner.scan_ports(host, ports);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_ports(host, r.value());

    } else if (cmd == "trace") {
        if (argc < 3) {
            std::cerr << "Usage: straylight-probe trace <host>\n";
            return 1;
        }
        std::string host = argv[2];
        int max_hops = 30;
        if (argc >= 4) {
            try { max_hops = std::stoi(argv[3]); } catch (...) {}
        }

        std::cerr << "Tracing route to " << host << " ...\n";
        auto r = scanner.traceroute(host, max_hops);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_trace(host, r.value());

    } else if (cmd == "dns") {
        if (argc < 3) {
            std::cerr << "Usage: straylight-probe dns <domain> [type]\n";
            return 1;
        }
        std::string domain = argv[2];
        std::string type = (argc >= 4) ? argv[3] : "ANY";

        auto r = scanner.dns_lookup(domain, type);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_dns(domain, r.value());

    } else if (cmd == "bandwidth") {
        if (argc < 3) {
            std::cerr << "Usage: straylight-probe bandwidth <host> [port]\n";
            return 1;
        }
        std::string host = argv[2];
        uint16_t port = 5201;
        if (argc >= 4) {
            try { port = static_cast<uint16_t>(std::stoi(argv[3])); } catch (...) {}
        }

        std::cerr << "Testing bandwidth to " << host << ":" << port << " ...\n";
        auto r = scanner.bandwidth_test(host, port);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_bandwidth(host, r.value());

    } else if (cmd == "wifi") {
        auto r = scanner.wifi_scan();
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_wifi(r.value());

    } else if (cmd == "health") {
        std::cerr << "Running network health checks ...\n";
        auto r = scanner.health_check();
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        std::cout << ProbeDiagnostics::format_health(r.value());

    } else if (cmd == "ping") {
        if (argc < 3) {
            std::cerr << "Usage: straylight-probe ping <host>\n";
            return 1;
        }
        auto r = scanner.ping_host(argv[2]);
        if (!r.has_value()) {
            std::cerr << "Error: " << r.error() << "\n";
            return 1;
        }
        const auto& h = r.value();
        std::cout << "Host: " << h.ip;
        if (!h.hostname.empty() && h.hostname != h.ip) std::cout << " (" << h.hostname << ")";
        std::cout << "\n";
        std::cout << "RTT:  " << h.rtt_ms << " ms\n";
        if (!h.os_guess.empty()) std::cout << "OS:   " << h.os_guess << "\n";

    } else {
        std::cerr << "Error: unknown command '" << cmd << "'\n";
        print_usage();
        return 1;
    }

    return 0;
}
