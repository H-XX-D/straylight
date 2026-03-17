// tools/probe/scanner.cpp
// Network scanning implementation using POSIX sockets.
#include "scanner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <sstream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace straylight {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

std::string NetworkScanner::guess_os_from_ttl(int ttl) {
    if (ttl <= 0) return "unknown";
    if (ttl <= 64) return "Linux/macOS/Unix";
    if (ttl <= 128) return "Windows";
    if (ttl <= 255) return "Network device/Solaris";
    return "unknown";
}

std::string NetworkScanner::lookup_mac_vendor(const std::string& mac) {
    // OUI prefix -> vendor mapping for common vendors
    static const std::map<std::string, std::string> oui_table = {
        {"00:50:56", "VMware"},
        {"00:0C:29", "VMware"},
        {"08:00:27", "VirtualBox"},
        {"52:54:00", "QEMU/KVM"},
        {"DC:A6:32", "Raspberry Pi"},
        {"B8:27:EB", "Raspberry Pi"},
        {"E4:5F:01", "Raspberry Pi"},
        {"00:1A:79", "Dell"},
        {"F8:BC:12", "Dell"},
        {"00:25:B5", "Dell"},
        {"3C:D9:2B", "HP"},
        {"00:17:A4", "HP"},
        {"9C:B6:D0", "HP"},
        {"00:1E:67", "Intel"},
        {"00:1B:21", "Intel"},
        {"A4:BF:01", "Intel"},
        {"00:1C:42", "Parallels"},
        {"AC:DE:48", "Apple"},
        {"00:1B:63", "Apple"},
        {"F0:18:98", "Apple"},
        {"3C:22:FB", "Apple"},
        {"14:7D:DA", "Apple"},
        {"A8:66:7F", "Apple"},
        {"00:23:14", "Apple"},
        {"10:DD:B1", "Apple"},
        {"7C:D1:C3", "Apple"},
        {"78:4F:43", "Apple"},
        {"2C:F0:A2", "TP-Link"},
        {"50:C7:BF", "TP-Link"},
        {"B0:4E:26", "TP-Link"},
        {"00:14:6C", "Netgear"},
        {"20:E5:2A", "Netgear"},
        {"C4:04:15", "Netgear"},
        {"00:18:E7", "Linksys"},
        {"C0:56:27", "Linksys"},
        {"00:26:F2", "Netgear"},
        {"00:1A:2F", "Cisco"},
        {"00:1E:14", "Cisco"},
        {"00:50:F1", "NVIDIA"},
        {"48:B0:2D", "NVIDIA"},
    };

    if (mac.size() < 8) return "unknown";
    std::string prefix = mac.substr(0, 8);
    // Normalize to uppercase
    for (auto& c : prefix) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

    auto it = oui_table.find(prefix);
    if (it != oui_table.end()) return it->second;
    return "unknown";
}

std::string NetworkScanner::service_name(uint16_t port) {
    static const std::map<uint16_t, std::string> services = {
        {20, "ftp-data"}, {21, "ftp"}, {22, "ssh"}, {23, "telnet"},
        {25, "smtp"}, {53, "dns"}, {67, "dhcp"}, {68, "dhcp"},
        {69, "tftp"}, {80, "http"}, {110, "pop3"}, {111, "rpcbind"},
        {123, "ntp"}, {135, "msrpc"}, {137, "netbios-ns"}, {138, "netbios-dgm"},
        {139, "netbios-ssn"}, {143, "imap"}, {161, "snmp"}, {162, "snmptrap"},
        {179, "bgp"}, {389, "ldap"}, {443, "https"}, {445, "microsoft-ds"},
        {465, "smtps"}, {514, "syslog"}, {515, "printer"}, {543, "klogin"},
        {544, "kshell"}, {548, "afp"}, {554, "rtsp"}, {587, "submission"},
        {631, "ipp"}, {636, "ldaps"}, {873, "rsync"}, {902, "vmware"},
        {993, "imaps"}, {995, "pop3s"}, {1080, "socks"}, {1433, "mssql"},
        {1434, "mssql-m"}, {1521, "oracle"}, {1723, "pptp"}, {2049, "nfs"},
        {2082, "cpanel"}, {2083, "cpanel-ssl"}, {2181, "zookeeper"},
        {3306, "mysql"}, {3389, "rdp"}, {3690, "svn"},
        {4443, "pharos"}, {5060, "sip"}, {5061, "sip-tls"},
        {5201, "iperf"}, {5432, "postgresql"}, {5900, "vnc"},
        {5984, "couchdb"}, {6379, "redis"}, {6443, "kubernetes"},
        {8080, "http-proxy"}, {8443, "https-alt"}, {8888, "http-alt"},
        {9090, "prometheus"}, {9200, "elasticsearch"}, {9300, "elasticsearch"},
        {11211, "memcached"}, {27017, "mongodb"}, {50000, "db2"},
    };

    auto it = services.find(port);
    return (it != services.end()) ? it->second : "unknown";
}

std::vector<uint16_t> NetworkScanner::default_ports() {
    return {
        21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443, 445, 993, 995,
        1433, 1521, 2049, 3306, 3389, 5432, 5900, 6379, 6443, 8080, 8443,
        9090, 9200, 27017
    };
}

Result<std::pair<uint32_t, uint32_t>, std::string> NetworkScanner::parse_cidr(
    const std::string& cidr)
{
    auto slash = cidr.find('/');
    if (slash == std::string::npos) {
        return Result<std::pair<uint32_t, uint32_t>, std::string>::error(
            "Invalid CIDR: missing /prefix");
    }

    std::string ip_str = cidr.substr(0, slash);
    int prefix = 0;
    try {
        prefix = std::stoi(cidr.substr(slash + 1));
    } catch (...) {
        return Result<std::pair<uint32_t, uint32_t>, std::string>::error(
            "Invalid CIDR prefix");
    }

    if (prefix < 0 || prefix > 32) {
        return Result<std::pair<uint32_t, uint32_t>, std::string>::error(
            "CIDR prefix out of range");
    }

    auto ip_r = string_to_ip(ip_str);
    if (!ip_r.has_value()) return Result<std::pair<uint32_t, uint32_t>, std::string>::error(ip_r.error());

    uint32_t base = ip_r.value();
    uint32_t mask = (prefix == 0) ? 0 : (~uint32_t(0) << (32 - prefix));
    base &= mask;
    uint32_t count = (prefix == 32) ? 1 : (uint32_t(1) << (32 - prefix));

    return Result<std::pair<uint32_t, uint32_t>, std::string>::ok({base, count});
}

std::string NetworkScanner::ip_to_string(uint32_t ip) {
    struct in_addr addr{};
    addr.s_addr = htonl(ip);
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return buf;
}

Result<uint32_t, std::string> NetworkScanner::string_to_ip(const std::string& ip) {
    struct in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return Result<uint32_t, std::string>::error("Invalid IP address: " + ip);
    }
    return Result<uint32_t, std::string>::ok(ntohl(addr.s_addr));
}

// ---------------------------------------------------------------------------
// Subnet detection
// ---------------------------------------------------------------------------

Result<std::string, std::string> NetworkScanner::detect_subnet() const {
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        return Result<std::string, std::string>::error(
            std::string("getifaddrs failed: ") + strerror(errno));
    }

    struct IfGuard {
        struct ifaddrs* p;
        ~IfGuard() { freeifaddrs(p); }
    } guard{ifaddr};

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        // Skip loopback
        std::string name = ifa->ifa_name;
        if (name == "lo" || name == "lo0") continue;

        // Must be up and running
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (!(ifa->ifa_flags & IFF_RUNNING)) continue;

        auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        auto* mask_sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);

        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        uint32_t mask = ntohl(mask_sin->sin_addr.s_addr);
        uint32_t network = ip & mask;

        // Count prefix bits
        int prefix = 0;
        uint32_t m = mask;
        while (m & 0x80000000) {
            ++prefix;
            m <<= 1;
        }

        return Result<std::string, std::string>::ok(
            ip_to_string(network) + "/" + std::to_string(prefix));
    }

    return Result<std::string, std::string>::error("No suitable network interface found");
}

// ---------------------------------------------------------------------------
// Ping host
// ---------------------------------------------------------------------------

Result<DiscoveredHost, std::string> NetworkScanner::ping_host(const std::string& host) const {
    // Resolve hostname
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (gai != 0) {
        return Result<DiscoveredHost, std::string>::error(
            std::string("Cannot resolve ") + host + ": " + gai_strerror(gai));
    }

    struct AddrGuard {
        struct addrinfo* p;
        ~AddrGuard() { freeaddrinfo(p); }
    } ag{res};

    auto* sin = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    std::string resolved_ip;
    {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
        resolved_ip = buf;
    }

    // Use UDP-based ping (connect + ICMP unreachable detection) for non-root
    // Fall back to simple TCP connect to port 80/443 for RTT measurement
    DiscoveredHost dh;
    dh.ip = resolved_ip;
    dh.hostname = host;

    // Try TCP connect to get RTT
    uint16_t probe_ports[] = {80, 443, 22};
    for (uint16_t port : probe_ports) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        // Set non-blocking
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr = sin->sin_addr;

        auto start = std::chrono::steady_clock::now();
        int rc = connect(sock, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

        if (rc == 0) {
            auto end = std::chrono::steady_clock::now();
            dh.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
            dh.alive = true;
            close(sock);
            break;
        }

        if (errno == EINPROGRESS) {
            struct pollfd pfd{};
            pfd.fd = sock;
            pfd.events = POLLOUT;
            int pr = poll(&pfd, 1, 2000);
            auto end = std::chrono::steady_clock::now();

            if (pr > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                // Even "connection refused" means host is alive
                if (so_error == 0 || so_error == ECONNREFUSED) {
                    dh.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
                    dh.alive = true;
                    close(sock);
                    break;
                }
            }
        }

        close(sock);
    }

    // TTL-based OS guess (from ICMP if we had root, estimate from common defaults)
    if (dh.alive) {
        // Try to get TTL from a raw socket if available
        int raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (raw >= 0) {
            struct sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(33434);
            sa.sin_addr = sin->sin_addr;

            int optval = 1;
            setsockopt(raw, IPPROTO_IP, IP_RECVTTL, &optval, sizeof(optval));

            // Send a UDP probe and check the TTL on the response
            // This is best-effort; without raw ICMP, we estimate
            close(raw);
        }

        // Reverse DNS
        char hbuf[NI_MAXHOST];
        struct sockaddr_in rev_sa{};
        rev_sa.sin_family = AF_INET;
        rev_sa.sin_addr = sin->sin_addr;
        if (getnameinfo(reinterpret_cast<struct sockaddr*>(&rev_sa), sizeof(rev_sa),
                        hbuf, sizeof(hbuf), nullptr, 0, NI_NAMEREQD) == 0) {
            dh.hostname = hbuf;
        }
    }

    if (!dh.alive) {
        return Result<DiscoveredHost, std::string>::error("Host " + host + " is not responding");
    }

    return Result<DiscoveredHost, std::string>::ok(std::move(dh));
}

// ---------------------------------------------------------------------------
// Subnet scan
// ---------------------------------------------------------------------------

Result<std::vector<DiscoveredHost>, std::string> NetworkScanner::scan_subnet(
    const std::string& subnet) const
{
    auto cidr_r = parse_cidr(subnet);
    if (!cidr_r.has_value()) {
        return Result<std::vector<DiscoveredHost>, std::string>::error(cidr_r.error());
    }

    auto [base, count] = cidr_r.value();

    // Cap scan size to /20 (4096 hosts) to prevent accidental massive scans
    if (count > 4096) {
        return Result<std::vector<DiscoveredHost>, std::string>::error(
            "Subnet too large (max /20 = 4096 hosts). Specify a smaller range.");
    }

    std::vector<DiscoveredHost> results;

    // Skip network address (first) and broadcast (last)
    uint32_t start = base + 1;
    uint32_t end = base + count - 1;

    for (uint32_t ip = start; ip < end; ++ip) {
        std::string ip_str = ip_to_string(ip);

        // Quick TCP connect probe
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(80);
        inet_pton(AF_INET, ip_str.c_str(), &sa.sin_addr);

        auto probe_start = std::chrono::steady_clock::now();
        int rc = connect(sock, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

        bool alive = false;
        double rtt = 0.0;

        if (rc == 0) {
            auto now = std::chrono::steady_clock::now();
            rtt = std::chrono::duration<double, std::milli>(now - probe_start).count();
            alive = true;
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd{};
            pfd.fd = sock;
            pfd.events = POLLOUT;
            int pr = poll(&pfd, 1, 200); // 200ms timeout per host
            if (pr > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0 || so_error == ECONNREFUSED) {
                    auto now = std::chrono::steady_clock::now();
                    rtt = std::chrono::duration<double, std::milli>(now - probe_start).count();
                    alive = true;
                }
            }
        }

        close(sock);

        if (alive) {
            DiscoveredHost dh;
            dh.ip = ip_str;
            dh.alive = true;
            dh.rtt_ms = rtt;
            dh.os_guess = "unknown"; // Would need ICMP raw socket for TTL

            // Reverse DNS
            char hbuf[NI_MAXHOST];
            struct sockaddr_in rev{};
            rev.sin_family = AF_INET;
            inet_pton(AF_INET, ip_str.c_str(), &rev.sin_addr);
            if (getnameinfo(reinterpret_cast<struct sockaddr*>(&rev), sizeof(rev),
                            hbuf, sizeof(hbuf), nullptr, 0, NI_NAMEREQD) == 0) {
                dh.hostname = hbuf;
            }

            results.push_back(std::move(dh));
        }
    }

    return Result<std::vector<DiscoveredHost>, std::string>::ok(std::move(results));
}

// ---------------------------------------------------------------------------
// Port scan
// ---------------------------------------------------------------------------

Result<std::vector<PortResult>, std::string> NetworkScanner::scan_ports(
    const std::string& host,
    const std::vector<uint16_t>& ports) const
{
    const auto& target_ports = ports.empty() ? default_ports() : ports;

    // Resolve host
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (gai != 0) {
        return Result<std::vector<PortResult>, std::string>::error(
            std::string("Cannot resolve ") + host + ": " + gai_strerror(gai));
    }

    struct AddrGuard {
        struct addrinfo* p;
        ~AddrGuard() { freeaddrinfo(p); }
    } ag{res};

    auto* sin = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);

    std::vector<PortResult> results;

    for (uint16_t port : target_ports) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) continue;

        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr = sin->sin_addr;

        auto start = std::chrono::steady_clock::now();
        int rc = connect(sock, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa));

        PortResult pr;
        pr.port = port;
        pr.service = service_name(port);

        if (rc == 0) {
            auto end = std::chrono::steady_clock::now();
            pr.state = "open";
            pr.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd{};
            pfd.fd = sock;
            pfd.events = POLLOUT;
            int poll_rc = poll(&pfd, 1, 1000); // 1s timeout per port
            auto end = std::chrono::steady_clock::now();

            if (poll_rc > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    pr.state = "open";
                    pr.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
                } else if (so_error == ECONNREFUSED) {
                    pr.state = "closed";
                    pr.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();
                } else {
                    pr.state = "filtered";
                }
            } else {
                pr.state = "filtered";
            }
        } else if (errno == ECONNREFUSED) {
            pr.state = "closed";
        } else {
            pr.state = "filtered";
        }

        close(sock);

        // Only report open ports by default
        if (pr.state == "open") {
            results.push_back(std::move(pr));
        }
    }

    return Result<std::vector<PortResult>, std::string>::ok(std::move(results));
}

// ---------------------------------------------------------------------------
// Traceroute
// ---------------------------------------------------------------------------

Result<std::vector<TraceHop>, std::string> NetworkScanner::traceroute(
    const std::string& host, int max_hops) const
{
    // Resolve host
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (gai != 0) {
        return Result<std::vector<TraceHop>, std::string>::error(
            std::string("Cannot resolve ") + host + ": " + gai_strerror(gai));
    }

    struct AddrGuard {
        struct addrinfo* p;
        ~AddrGuard() { freeaddrinfo(p); }
    } ag{res};

    auto* target_sin = reinterpret_cast<struct sockaddr_in*>(res->ai_addr);
    uint32_t target_ip = target_sin->sin_addr.s_addr;

    std::vector<TraceHop> hops;
    uint16_t dest_port = 33434;

    for (int ttl = 1; ttl <= max_hops; ++ttl) {
        int send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (send_sock < 0) {
            return Result<std::vector<TraceHop>, std::string>::error(
                std::string("socket() failed: ") + strerror(errno));
        }

        // Set TTL
        if (setsockopt(send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
            close(send_sock);
            continue;
        }

        struct sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(static_cast<uint16_t>(dest_port + ttl));
        dest.sin_addr = target_sin->sin_addr;

        auto start = std::chrono::steady_clock::now();

        // Send a UDP probe
        char probe_data[] = "STRAYLIGHT";
        ssize_t sent = sendto(send_sock, probe_data, sizeof(probe_data), 0,
                              reinterpret_cast<struct sockaddr*>(&dest), sizeof(dest));
        close(send_sock);

        if (sent < 0) {
            TraceHop hop;
            hop.hop = ttl;
            hop.timeout = true;
            hops.push_back(hop);
            continue;
        }

        // Wait for ICMP time-exceeded or destination-unreachable using a raw socket
        // This requires root; if unavailable, use connect-based estimation
        // For non-root, we use a simulated approach with decreasing TTL via connect
        TraceHop hop;
        hop.hop = ttl;

        // Try to receive ICMP reply via raw socket
        int recv_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (recv_sock >= 0) {
            struct pollfd pfd{};
            pfd.fd = recv_sock;
            pfd.events = POLLIN;
            int pr = poll(&pfd, 1, 2000);

            if (pr > 0) {
                char buf[512];
                struct sockaddr_in from{};
                socklen_t from_len = sizeof(from);
                ssize_t n = recvfrom(recv_sock, buf, sizeof(buf), 0,
                                     reinterpret_cast<struct sockaddr*>(&from), &from_len);

                auto end = std::chrono::steady_clock::now();
                hop.rtt_ms = std::chrono::duration<double, std::milli>(end - start).count();

                if (n > 0) {
                    char ip_buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, ip_buf, sizeof(ip_buf));
                    hop.ip = ip_buf;

                    // Reverse DNS
                    char hbuf[NI_MAXHOST];
                    if (getnameinfo(reinterpret_cast<struct sockaddr*>(&from), from_len,
                                    hbuf, sizeof(hbuf), nullptr, 0, NI_NAMEREQD) == 0) {
                        hop.hostname = hbuf;
                    }
                }
            } else {
                hop.timeout = true;
            }
            close(recv_sock);
        } else {
            // No raw socket access — mark as timeout but record hop
            hop.timeout = true;
        }

        hops.push_back(hop);

        // Check if we reached the target
        if (!hop.ip.empty()) {
            struct in_addr hop_addr{};
            inet_pton(AF_INET, hop.ip.c_str(), &hop_addr);
            if (hop_addr.s_addr == target_ip) {
                break;
            }
        }
    }

    return Result<std::vector<TraceHop>, std::string>::ok(std::move(hops));
}

// ---------------------------------------------------------------------------
// DNS lookup
// ---------------------------------------------------------------------------

Result<std::vector<DnsRecord>, std::string> NetworkScanner::dns_lookup(
    const std::string& domain,
    const std::string& type) const
{
    std::vector<DnsRecord> records;

    // Use getaddrinfo for A/AAAA records
    bool want_a = (type == "ANY" || type == "A");
    bool want_aaaa = (type == "ANY" || type == "AAAA");

    if (want_a) {
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        int gai = getaddrinfo(domain.c_str(), nullptr, &hints, &res);
        if (gai == 0) {
            for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
                auto* sin = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
                char buf[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
                DnsRecord rec;
                rec.type = "A";
                rec.name = domain;
                rec.value = buf;
                records.push_back(std::move(rec));
            }
            freeaddrinfo(res);
        }
    }

    if (want_aaaa) {
        struct addrinfo hints{};
        hints.ai_family = AF_INET6;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* res = nullptr;
        int gai = getaddrinfo(domain.c_str(), nullptr, &hints, &res);
        if (gai == 0) {
            for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
                auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
                char buf[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
                DnsRecord rec;
                rec.type = "AAAA";
                rec.name = domain;
                rec.value = buf;
                records.push_back(std::move(rec));
            }
            freeaddrinfo(res);
        }
    }

    // For CNAME, MX, NS, TXT, SOA, PTR — use res_query if available
    // These require <resolv.h> which is platform-specific.
    // On macOS, we can use the DNS framework or res_query.
    // For portability, we report what getaddrinfo gives us and note
    // that advanced records require the daemon's DNS resolver.

    if (records.empty()) {
        return Result<std::vector<DnsRecord>, std::string>::error(
            "No DNS records found for " + domain);
    }

    return Result<std::vector<DnsRecord>, std::string>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// Bandwidth test
// ---------------------------------------------------------------------------

Result<double, std::string> NetworkScanner::bandwidth_test(
    const std::string& host, uint16_t port) const
{
    // Connect to target
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
    if (gai != 0) {
        return Result<double, std::string>::error(
            std::string("Cannot resolve ") + host + ": " + gai_strerror(gai));
    }

    struct AddrGuard {
        struct addrinfo* p;
        ~AddrGuard() { freeaddrinfo(p); }
    } ag{res};

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        return Result<double, std::string>::error(
            std::string("socket() failed: ") + strerror(errno));
    }

    // Set send buffer size
    int sndbuf = 1024 * 1024; // 1MB
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        close(sock);
        return Result<double, std::string>::error(
            std::string("connect() failed: ") + strerror(errno) +
            " — is an iperf3 server running on " + host + ":" + port_str + "?");
    }

    // Send data for ~3 seconds and measure throughput
    static constexpr size_t kBufSize = 128 * 1024; // 128KB chunks
    std::vector<char> buf(kBufSize, 'X');

    auto start = std::chrono::steady_clock::now();
    size_t total_bytes = 0;
    auto deadline = start + std::chrono::seconds(3);

    while (std::chrono::steady_clock::now() < deadline) {
        ssize_t n = write(sock, buf.data(), buf.size());
        if (n <= 0) break;
        total_bytes += static_cast<size_t>(n);
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed_s = std::chrono::duration<double>(end - start).count();

    close(sock);

    if (total_bytes == 0 || elapsed_s < 0.001) {
        return Result<double, std::string>::error("Bandwidth test produced no data");
    }

    double mbps = (static_cast<double>(total_bytes) * 8.0) / (elapsed_s * 1e6);
    return Result<double, std::string>::ok(mbps);
}

// ---------------------------------------------------------------------------
// WiFi scan
// ---------------------------------------------------------------------------

Result<std::vector<WifiAP>, std::string> NetworkScanner::wifi_scan() const {
    std::vector<WifiAP> aps;

#ifdef __APPLE__
    // macOS: use system_profiler to get WiFi info
    FILE* pipe = popen(
        "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/"
        "Resources/airport -s 2>/dev/null || "
        "system_profiler SPAirPortDataType 2>/dev/null", "r");
    if (!pipe) {
        return Result<std::vector<WifiAP>, std::string>::error(
            "Cannot query WiFi information");
    }

    char line[512];
    bool header_skipped = false;
    while (fgets(line, sizeof(line), pipe)) {
        std::string l = line;
        // airport -s output: SSID BSSID RSSI CHANNEL HT CC SECURITY
        if (!header_skipped) {
            if (l.find("SSID") != std::string::npos && l.find("BSSID") != std::string::npos) {
                header_skipped = true;
            }
            continue;
        }

        // Parse the airport -s output
        // Columns are whitespace-separated but SSID can contain spaces
        // BSSID is always XX:XX:XX:XX:XX:XX format
        // We find the BSSID position and work from there
        auto bssid_start = l.find_first_of("0123456789abcdef");
        if (bssid_start == std::string::npos) continue;

        // Look for MAC address pattern
        size_t pos = 0;
        while (pos < l.size()) {
            size_t colon1 = l.find(':', pos);
            if (colon1 == std::string::npos || colon1 < 2) break;

            // Check if this is a MAC address (xx:xx:xx:xx:xx:xx)
            if (colon1 + 15 <= l.size() &&
                l[colon1 + 3] == ':' && l[colon1 + 6] == ':' &&
                l[colon1 + 9] == ':' && l[colon1 + 12] == ':')
            {
                std::string bssid = l.substr(colon1 - 2, 17);
                std::string ssid = l.substr(0, colon1 - 2);
                // Trim SSID
                while (!ssid.empty() && ssid.back() == ' ') ssid.pop_back();
                while (!ssid.empty() && ssid.front() == ' ') ssid.erase(ssid.begin());

                // Parse remaining fields after BSSID
                std::string rest = l.substr(colon1 + 15);
                std::istringstream iss(rest);
                int rssi = 0;
                int channel = 0;
                iss >> rssi >> channel;

                WifiAP ap;
                ap.ssid = ssid;
                ap.bssid = bssid;
                ap.signal_dbm = rssi;
                ap.channel = channel;
                if (channel <= 14) {
                    ap.frequency_ghz = 2.4;
                } else {
                    ap.frequency_ghz = 5.0;
                }

                // Read remaining as security
                std::string sec;
                std::getline(iss, sec);
                while (!sec.empty() && sec.front() == ' ') sec.erase(sec.begin());
                ap.security = sec.empty() ? "unknown" : sec;

                aps.push_back(std::move(ap));
                break;
            }
            pos = colon1 + 1;
        }
    }
    pclose(pipe);
#else
    // Linux: parse /proc/net/wireless and iwconfig
    FILE* pipe = popen("iwlist wlan0 scan 2>/dev/null", "r");
    if (!pipe) {
        return Result<std::vector<WifiAP>, std::string>::error(
            "Cannot scan WiFi (iwlist not available or no wlan0)");
    }

    WifiAP current;
    bool have_ap = false;
    char line[512];
    while (fgets(line, sizeof(line), pipe)) {
        std::string l = line;

        if (l.find("Cell ") != std::string::npos && l.find("Address:") != std::string::npos) {
            if (have_ap) aps.push_back(current);
            current = WifiAP{};
            have_ap = true;
            auto addr_pos = l.find("Address:");
            if (addr_pos != std::string::npos) {
                current.bssid = l.substr(addr_pos + 9);
                while (!current.bssid.empty() &&
                       (current.bssid.back() == '\n' || current.bssid.back() == ' ')) {
                    current.bssid.pop_back();
                }
            }
        } else if (l.find("ESSID:") != std::string::npos) {
            auto q1 = l.find('"');
            auto q2 = l.rfind('"');
            if (q1 != std::string::npos && q2 > q1) {
                current.ssid = l.substr(q1 + 1, q2 - q1 - 1);
            }
        } else if (l.find("Channel:") != std::string::npos) {
            auto pos2 = l.find("Channel:");
            try {
                current.channel = std::stoi(l.substr(pos2 + 8));
                current.frequency_ghz = (current.channel <= 14) ? 2.4 : 5.0;
            } catch (...) {}
        } else if (l.find("Signal level=") != std::string::npos) {
            auto pos2 = l.find("Signal level=");
            try {
                current.signal_dbm = std::stoi(l.substr(pos2 + 13));
            } catch (...) {}
        } else if (l.find("IE:") != std::string::npos) {
            if (l.find("WPA2") != std::string::npos) current.security = "WPA2";
            else if (l.find("WPA") != std::string::npos) current.security = "WPA";
        }
    }
    if (have_ap) aps.push_back(current);
    pclose(pipe);
#endif

    return Result<std::vector<WifiAP>, std::string>::ok(std::move(aps));
}

// ---------------------------------------------------------------------------
// Network health check
// ---------------------------------------------------------------------------

Result<std::vector<HealthCheck>, std::string> NetworkScanner::health_check() const {
    std::vector<HealthCheck> checks;

    // 1. Check gateway connectivity
    {
        HealthCheck hc;
        hc.name = "Gateway";

        // Detect gateway IP
        std::string gateway_ip;
#ifdef __APPLE__
        FILE* pipe = popen("route -n get default 2>/dev/null | grep gateway", "r");
#else
        FILE* pipe = popen("ip route show default 2>/dev/null | awk '/default/ {print $3}'", "r");
#endif
        if (pipe) {
            char buf[256];
            if (fgets(buf, sizeof(buf), pipe)) {
                std::string line = buf;
                // Extract IP from the line
                auto pos = line.find_first_of("0123456789");
                if (pos != std::string::npos) {
                    auto end_pos = line.find_first_not_of("0123456789.", pos);
                    gateway_ip = line.substr(pos, end_pos - pos);
                }
            }
            pclose(pipe);
        }

        if (gateway_ip.empty()) {
            hc.passed = false;
            hc.detail = "No default gateway found";
        } else {
            auto ping_r = ping_host(gateway_ip);
            if (ping_r.has_value()) {
                hc.passed = true;
                hc.latency_ms = ping_r.value().rtt_ms;
                hc.detail = "Gateway " + gateway_ip + " reachable (" +
                           std::to_string(static_cast<int>(hc.latency_ms)) + "ms)";
            } else {
                hc.passed = false;
                hc.detail = "Gateway " + gateway_ip + " unreachable";
            }
        }
        checks.push_back(std::move(hc));
    }

    // 2. Check DNS resolution
    {
        HealthCheck hc;
        hc.name = "DNS";
        auto start = std::chrono::steady_clock::now();
        auto dns_r = dns_lookup("google.com", "A");
        auto end = std::chrono::steady_clock::now();
        hc.latency_ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (dns_r.has_value() && !dns_r.value().empty()) {
            hc.passed = true;
            hc.detail = "DNS resolving correctly (" +
                       std::to_string(static_cast<int>(hc.latency_ms)) + "ms)";
        } else {
            hc.passed = false;
            hc.detail = "DNS resolution failed";
        }
        checks.push_back(std::move(hc));
    }

    // 3. Check internet connectivity
    {
        HealthCheck hc;
        hc.name = "Internet";
        // Try to connect to well-known hosts
        std::string test_hosts[] = {"1.1.1.1", "8.8.8.8", "9.9.9.9"};
        bool reached = false;
        for (const auto& th : test_hosts) {
            auto ping_r = ping_host(th);
            if (ping_r.has_value()) {
                hc.passed = true;
                hc.latency_ms = ping_r.value().rtt_ms;
                hc.detail = "Internet reachable via " + th + " (" +
                           std::to_string(static_cast<int>(hc.latency_ms)) + "ms)";
                reached = true;
                break;
            }
        }
        if (!reached) {
            hc.passed = false;
            hc.detail = "Cannot reach any public DNS servers";
        }
        checks.push_back(std::move(hc));
    }

    // 4. Check for packet loss (quick 5-ping test)
    {
        HealthCheck hc;
        hc.name = "Packet Loss";
        int success = 0;
        int total = 5;
        for (int i = 0; i < total; ++i) {
            auto r = ping_host("8.8.8.8");
            if (r.has_value()) ++success;
        }
        double loss_pct = 100.0 * (1.0 - static_cast<double>(success) / total);
        hc.passed = (loss_pct < 20.0);
        std::ostringstream oss;
        oss << std::fixed;
        oss.precision(0);
        oss << loss_pct << "% packet loss (" << success << "/" << total << " probes succeeded)";
        hc.detail = oss.str();
        checks.push_back(std::move(hc));
    }

    return Result<std::vector<HealthCheck>, std::string>::ok(std::move(checks));
}

} // namespace straylight
