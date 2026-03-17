// tools/network/main.cpp
// CLI front-end for straylight-network — network management.

#include "network_manager.h"

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

static void print_usage() {
    std::cerr
        << "straylight-network — network management CLI\n"
        << "\n"
        << "Usage:\n"
        << "  straylight-network status                                    Show interface status\n"
        << "  straylight-network scan-wifi                                 Scan for WiFi networks\n"
        << "  straylight-network connect <ssid> [--pass=X] [--hidden]     Connect to WiFi\n"
        << "  straylight-network disconnect [interface]                    Disconnect\n"
        << "  straylight-network vpn add <name> --type=wireguard|openvpn --config=FILE\n"
        << "  straylight-network vpn connect <name>                        Connect VPN\n"
        << "  straylight-network vpn disconnect <name>                     Disconnect VPN\n"
        << "  straylight-network vpn list                                  List VPNs\n"
        << "  straylight-network firewall allow [--port=N] [--proto=tcp] [--from=IP] [--comment=X]\n"
        << "  straylight-network firewall deny  [--port=N] [--proto=tcp] [--from=IP]\n"
        << "  straylight-network firewall list                             List firewall rules\n"
        << "  straylight-network firewall remove <rule-id>                 Remove rule\n"
        << "  straylight-network dns set <server1> [server2 ...]          Set DNS servers\n"
        << "  straylight-network dns list                                  Show DNS config\n"
        << "  straylight-network bond create <name> --mode=X --members=a,b [--ip=X]\n"
        << "  straylight-network bond destroy <name>                       Remove bond/bridge\n"
        << "  straylight-network bond list                                 List bonds/bridges\n";
}

static std::string get_arg(int argc, char* argv[], const std::string& prefix, int start = 2) {
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    }
    return "";
}

static bool has_flag(int argc, char* argv[], const std::string& flag, int start = 2) {
    for (int i = start; i < argc; ++i) {
        if (std::string(argv[i]) == flag) return true;
    }
    return false;
}

static std::string human_bytes(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int idx = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && idx < 4) { val /= 1024.0; ++idx; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", val, units[idx]);
    return buf;
}

static std::string signal_bar(int dbm) {
    // Convert dBm to quality (approximate)
    int quality;
    if (dbm >= -50) quality = 4;
    else if (dbm >= -60) quality = 3;
    else if (dbm >= -70) quality = 2;
    else if (dbm >= -80) quality = 1;
    else quality = 0;

    std::string bar;
    for (int i = 0; i < 4; ++i) {
        bar += (i < quality) ? "|" : ".";
    }
    return bar;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        print_usage();
        return 0;
    }

    straylight::NetworkManager mgr;

    // -----------------------------------------------------------------------
    // status
    // -----------------------------------------------------------------------
    if (command == "status") {
        auto res = mgr.status();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        for (const auto& iface : res.value()) {
            if (iface.type == "loopback") continue;

            std::cout << iface.name << " (" << iface.type << ") — " << iface.state << "\n";
            if (!iface.mac.empty()) std::cout << "  MAC:     " << iface.mac << "\n";
            if (!iface.ipv4.empty()) std::cout << "  IPv4:    " << iface.ipv4 << "\n";
            if (!iface.ipv6.empty()) std::cout << "  IPv6:    " << iface.ipv6 << "\n";
            if (!iface.gateway.empty()) std::cout << "  Gateway: " << iface.gateway << "\n";
            if (!iface.ssid.empty()) {
                std::cout << "  WiFi:    " << iface.ssid
                          << " (" << iface.signal_dbm << " dBm)\n";
            }
            std::cout << "  Traffic: RX " << human_bytes(iface.rx_bytes)
                      << " / TX " << human_bytes(iface.tx_bytes) << "\n";
            std::cout << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // scan-wifi
    // -----------------------------------------------------------------------
    if (command == "scan-wifi") {
        auto res = mgr.scan_wifi();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        const auto& networks = res.value();
        if (networks.empty()) {
            std::cout << "No WiFi networks found.\n";
            return 0;
        }

        std::cout << std::left
                  << std::setw(6) << "SIGNAL"
                  << std::setw(32) << "SSID"
                  << std::setw(10) << "SECURITY"
                  << std::setw(8) << "CHAN"
                  << "BSSID\n";
        std::cout << std::string(72, '-') << "\n";

        for (const auto& net : networks) {
            std::string sig = signal_bar(net.signal_strength);
            std::string ssid = net.ssid.empty() ? "(hidden)" : net.ssid;
            if (net.connected) ssid += " *";

            std::cout << std::left
                      << std::setw(6) << sig
                      << std::setw(32) << ssid
                      << std::setw(10) << net.security
                      << std::setw(8) << net.channel
                      << net.bssid << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // connect <ssid> [--pass=X] [--hidden]
    // -----------------------------------------------------------------------
    if (command == "connect") {
        if (argc < 3) {
            std::cerr << "Error: 'connect' requires an SSID\n";
            return 1;
        }
        std::string ssid = argv[2];
        std::string pass = get_arg(argc, argv, "--pass=", 3);
        bool hidden = has_flag(argc, argv, "--hidden", 3);

        auto res = mgr.connect_wifi(ssid, pass, hidden);
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << "Connected to '" << ssid << "'\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // disconnect [interface]
    // -----------------------------------------------------------------------
    if (command == "disconnect") {
        std::string iface = (argc >= 3) ? argv[2] : "";
        auto res = mgr.disconnect(iface);
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << "Disconnected.\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // vpn <add|connect|disconnect|list>
    // -----------------------------------------------------------------------
    if (command == "vpn") {
        if (argc < 3) {
            std::cerr << "Error: 'vpn' requires a subcommand\n";
            return 1;
        }
        std::string sub = argv[2];

        if (sub == "add") {
            if (argc < 4) {
                std::cerr << "Error: 'vpn add' requires a name\n";
                return 1;
            }
            straylight::VpnConfig vpn;
            vpn.name = argv[3];
            vpn.type = get_arg(argc, argv, "--type=", 4);
            vpn.config_file = get_arg(argc, argv, "--config=", 4);
            vpn.server = get_arg(argc, argv, "--server=", 4);
            std::string port_str = get_arg(argc, argv, "--port=", 4);
            if (!port_str.empty()) vpn.port = std::atoi(port_str.c_str());

            if (vpn.type.empty()) vpn.type = "wireguard";

            auto res = mgr.vpn_add(vpn);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "VPN '" << vpn.name << "' (" << vpn.type << ") added.\n";
            return 0;
        }

        if (sub == "connect") {
            if (argc < 4) {
                std::cerr << "Error: 'vpn connect' requires a name\n";
                return 1;
            }
            auto res = mgr.vpn_connect(argv[3]);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "VPN '" << argv[3] << "' connected.\n";
            return 0;
        }

        if (sub == "disconnect") {
            if (argc < 4) {
                std::cerr << "Error: 'vpn disconnect' requires a name\n";
                return 1;
            }
            auto res = mgr.vpn_disconnect(argv[3]);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "VPN '" << argv[3] << "' disconnected.\n";
            return 0;
        }

        if (sub == "list") {
            auto res = mgr.vpn_list();
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            const auto& vpns = res.value();
            if (vpns.empty()) {
                std::cout << "No VPN configurations found.\n";
                return 0;
            }
            std::cout << std::left
                      << std::setw(20) << "NAME"
                      << std::setw(12) << "TYPE"
                      << std::setw(10) << "STATUS"
                      << "SERVER\n";
            std::cout << std::string(52, '-') << "\n";
            for (const auto& vpn : vpns) {
                std::cout << std::left
                          << std::setw(20) << vpn.name
                          << std::setw(12) << vpn.type
                          << std::setw(10) << (vpn.connected ? "connected" : "down")
                          << vpn.server << "\n";
            }
            return 0;
        }

        std::cerr << "Error: unknown vpn subcommand '" << sub << "'\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // firewall <allow|deny|list|remove>
    // -----------------------------------------------------------------------
    if (command == "firewall") {
        if (argc < 3) {
            std::cerr << "Error: 'firewall' requires a subcommand\n";
            return 1;
        }
        std::string sub = argv[2];

        if (sub == "allow" || sub == "deny") {
            straylight::FirewallRule rule;
            rule.action = (sub == "allow") ? "accept" : "drop";
            rule.chain = "input";

            std::string port_str = get_arg(argc, argv, "--port=", 3);
            if (!port_str.empty()) rule.port = std::atoi(port_str.c_str());
            rule.protocol = get_arg(argc, argv, "--proto=", 3);
            rule.source = get_arg(argc, argv, "--from=", 3);
            rule.destination = get_arg(argc, argv, "--to=", 3);
            rule.interface = get_arg(argc, argv, "--iface=", 3);
            rule.comment = get_arg(argc, argv, "--comment=", 3);

            if (rule.protocol.empty()) rule.protocol = "tcp";

            auto res = mgr.firewall_add(rule);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "Firewall rule added: " << sub;
            if (rule.port > 0) std::cout << " port " << rule.port << "/" << rule.protocol;
            if (!rule.source.empty()) std::cout << " from " << rule.source;
            std::cout << "\n";
            return 0;
        }

        if (sub == "list") {
            auto res = mgr.firewall_list();
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            const auto& rules = res.value();
            if (rules.empty()) {
                std::cout << "No firewall rules configured.\n";
                return 0;
            }
            std::cout << std::left
                      << std::setw(6) << "ID"
                      << std::setw(10) << "CHAIN"
                      << std::setw(10) << "ACTION"
                      << std::setw(8) << "PROTO"
                      << std::setw(18) << "SOURCE"
                      << std::setw(8) << "PORT"
                      << "COMMENT\n";
            std::cout << std::string(70, '-') << "\n";
            for (const auto& r : rules) {
                std::cout << std::left
                          << std::setw(6) << r.id
                          << std::setw(10) << r.chain
                          << std::setw(10) << r.action
                          << std::setw(8) << (r.protocol.empty() ? "any" : r.protocol)
                          << std::setw(18) << (r.source.empty() ? "*" : r.source)
                          << std::setw(8) << (r.port > 0 ? std::to_string(r.port) : "*")
                          << r.comment << "\n";
            }
            return 0;
        }

        if (sub == "remove") {
            if (argc < 4) {
                std::cerr << "Error: 'firewall remove' requires a rule ID\n";
                return 1;
            }
            auto res = mgr.firewall_remove(std::stoul(argv[3]));
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "Firewall rule " << argv[3] << " removed.\n";
            return 0;
        }

        std::cerr << "Error: unknown firewall subcommand '" << sub << "'\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // dns <set|list>
    // -----------------------------------------------------------------------
    if (command == "dns") {
        if (argc < 3) {
            std::cerr << "Error: 'dns' requires a subcommand\n";
            return 1;
        }
        std::string sub = argv[2];

        if (sub == "set") {
            if (argc < 4) {
                std::cerr << "Error: 'dns set' requires at least one server\n";
                return 1;
            }
            straylight::DnsConfig config;
            for (int i = 3; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg.rfind("--", 0) != 0) {
                    config.servers.push_back(arg);
                }
            }
            config.dnssec = has_flag(argc, argv, "--dnssec", 3);
            config.dns_over_tls = has_flag(argc, argv, "--dot", 3);

            std::string domains = get_arg(argc, argv, "--domains=", 3);
            if (!domains.empty()) {
                size_t pos = 0;
                while (pos < domains.size()) {
                    auto next = domains.find(',', pos);
                    config.search_domains.push_back(
                        domains.substr(pos, next == std::string::npos ? next : next - pos));
                    pos = (next == std::string::npos) ? domains.size() : next + 1;
                }
            }

            auto res = mgr.dns_set(config);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "DNS servers set to:";
            for (const auto& s : config.servers) std::cout << " " << s;
            std::cout << "\n";
            return 0;
        }

        if (sub == "list") {
            auto res = mgr.dns_get();
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            const auto& config = res.value();
            std::cout << "DNS Configuration (" << config.mode << "):\n";
            std::cout << "  Servers:";
            for (const auto& s : config.servers) std::cout << " " << s;
            std::cout << "\n";
            if (!config.search_domains.empty()) {
                std::cout << "  Search:";
                for (const auto& d : config.search_domains) std::cout << " " << d;
                std::cout << "\n";
            }
            std::cout << "  DNSSEC:       " << (config.dnssec ? "yes" : "no") << "\n"
                      << "  DNS-over-TLS: " << (config.dns_over_tls ? "yes" : "no") << "\n";
            return 0;
        }

        std::cerr << "Error: unknown dns subcommand '" << sub << "'\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // bond <create|destroy|list>
    // -----------------------------------------------------------------------
    if (command == "bond") {
        if (argc < 3) {
            std::cerr << "Error: 'bond' requires a subcommand\n";
            return 1;
        }
        std::string sub = argv[2];

        if (sub == "create") {
            if (argc < 4) {
                std::cerr << "Error: 'bond create' requires a name\n";
                return 1;
            }
            straylight::BondConfig config;
            config.name = argv[3];
            config.mode = get_arg(argc, argv, "--mode=", 4);
            config.ip = get_arg(argc, argv, "--ip=", 4);
            config.is_bridge = has_flag(argc, argv, "--bridge", 4);

            std::string members = get_arg(argc, argv, "--members=", 4);
            if (!members.empty()) {
                size_t pos = 0;
                while (pos < members.size()) {
                    auto next = members.find(',', pos);
                    config.members.push_back(
                        members.substr(pos, next == std::string::npos ? next : next - pos));
                    pos = (next == std::string::npos) ? members.size() : next + 1;
                }
            }

            auto res = mgr.bond_create(config);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << (config.is_bridge ? "Bridge" : "Bond") << " '"
                      << config.name << "' created.\n";
            return 0;
        }

        if (sub == "destroy") {
            if (argc < 4) {
                std::cerr << "Error: 'bond destroy' requires a name\n";
                return 1;
            }
            auto res = mgr.bond_destroy(argv[3]);
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            std::cout << "Bond/bridge '" << argv[3] << "' destroyed.\n";
            return 0;
        }

        if (sub == "list") {
            auto res = mgr.bond_list();
            if (!res.has_value()) {
                std::cerr << "Error: " << res.error() << "\n";
                return 1;
            }
            const auto& bonds = res.value();
            if (bonds.empty()) {
                std::cout << "No bonds or bridges configured.\n";
                return 0;
            }
            for (const auto& b : bonds) {
                std::cout << b.name << " (" << (b.is_bridge ? "bridge" : "bond") << ")";
                if (!b.mode.empty()) std::cout << " mode=" << b.mode;
                std::cout << "\n  Members:";
                for (const auto& m : b.members) std::cout << " " << m;
                std::cout << "\n";
            }
            return 0;
        }

        std::cerr << "Error: unknown bond subcommand '" << sub << "'\n";
        return 1;
    }

    std::cerr << "Error: unknown command '" << command << "'\n";
    print_usage();
    return 1;
}
