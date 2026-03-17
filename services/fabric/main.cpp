/**
 * StrayLight Fabric — Daemon
 *
 * Unified device topology graph daemon. Scans the system hardware,
 * builds a graph of all devices with bandwidth/latency edges,
 * monitors udev for hotplug events, and serves topology queries.
 *
 * Usage:
 *   straylight-fabric                    # run as daemon
 *   straylight-fabric --dump-json        # dump topology and exit
 */

#include "query_engine.h"
#include "topology.h"
#include "udev_monitor.h"
#include "straylight/daemon_base.h"
#include "straylight/result.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace straylight::fabric {

// ── Daemon ──────────────────────────────────────────────────────────

class FabricDaemon : public DaemonBase {
public:
    FabricDaemon()
        : DaemonBase("straylight-fabric")
        , query_(topo_)
        , udev_(topo_)
    {}

protected:
    VoidResult<> init() override {
        fprintf(stdout, "[fabric] scanning system topology...\n");

        auto r = topo_.build_topology();
        if (!r) return r;

        fprintf(stdout, "[fabric] found %zu devices, %zu links\n",
                topo_.node_count(), topo_.edge_count());

        // Save initial topology to JSON
        save_topology_json();

        // Start udev monitor
        auto udev_res = udev_.start();
        if (udev_res) {
            fprintf(stdout, "[fabric] udev monitor started\n");
        } else {
            fprintf(stderr, "[fabric] udev monitor failed: %s (non-fatal)\n",
                    udev_res.err().c_str());
        }

        // Register for topology change notifications
        udev_.on_change([this](const UdevEvent& event) {
            fprintf(stdout, "[fabric] topology changed: %s %s\n",
                    udev_action_str(event.action).c_str(),
                    event.devpath.c_str());
            save_topology_json();
        });

        // Set up command interface
        setup_command_socket();

        set_tick_interval_ms(1000);
        return VoidResult<>::ok();
    }

    void tick() override {
        // Process CLI commands
        process_commands();

        // Periodically save topology (every 60 seconds)
        tick_count_++;
        if (tick_count_ % 60 == 0) {
            save_topology_json();
        }
    }

    void shutdown() override {
        udev_.stop();
        save_topology_json();
        cleanup_command_socket();
        fprintf(stdout, "[fabric] udev events processed: %lu\n",
                static_cast<unsigned long>(udev_.events_processed()));
    }

    void on_reload() override {
        fprintf(stdout, "[fabric] rebuilding topology...\n");
        topo_.build_topology();
        save_topology_json();
        fprintf(stdout, "[fabric] topology rebuilt: %zu devices, %zu links\n",
                topo_.node_count(), topo_.edge_count());
    }

private:
    Topology topo_;
    QueryEngine query_;
    UdevMonitor udev_;
    uint64_t tick_count_ = 0;
    std::string socket_path_ = "/var/run/straylight/fabric.sock";
    std::string json_path_ = "/var/lib/straylight/fabric/topology.json";

    void save_topology_json() {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::path(json_path_).parent_path(), ec);
        std::ofstream out(json_path_, std::ios::trunc);
        if (out) {
            out << topo_.to_json();
        }
    }

    void setup_command_socket() {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(fs::path(socket_path_).parent_path(), ec);
    }

    void cleanup_command_socket() {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(socket_path_ + ".cmd", ec);
        fs::remove(socket_path_ + ".resp", ec);
    }

    void process_commands() {
        auto cmd_path = socket_path_ + ".cmd";
        std::ifstream in(cmd_path);
        if (!in) return;

        std::string line;
        while (std::getline(in, line)) {
            handle_command(line);
        }
        in.close();
        std::ofstream clear(cmd_path, std::ios::trunc);
    }

    void handle_command(const std::string& line) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        auto response_path = socket_path_ + ".resp";
        std::ofstream resp(response_path, std::ios::app);

        if (cmd == "topology") {
            resp << topo_.to_json();
        }
        else if (cmd == "path") {
            std::string from, to;
            iss >> from >> to;
            auto path_res = topo_.find_path(from, to);
            if (path_res) {
                auto& path = path_res.value();
                resp << "PATH: " << path.hops.size() << " hops, "
                     << path.total_latency_ns << " ns total latency, "
                     << path.bottleneck_bw_gbps << " Gbps bottleneck\n";
                for (auto& hop : path.hops) {
                    resp << "  " << hop.from << " -> " << hop.to
                         << " [" << link_type_str(hop.link_type)
                         << " " << hop.bandwidth_gbps << " Gbps, "
                         << hop.latency_ns << " ns]\n";
                }
            } else {
                resp << "ERROR: " << path_res.err() << "\n";
            }
        }
        else if (cmd == "fastest") {
            std::string from, to;
            iss >> from >> to;
            auto path_res = topo_.find_fastest_path(from, to);
            if (path_res) {
                auto& path = path_res.value();
                resp << "FASTEST: " << path.hops.size() << " hops, "
                     << path.bottleneck_bw_gbps << " Gbps bandwidth\n";
                for (auto& hop : path.hops) {
                    resp << "  " << hop.from << " -> " << hop.to
                         << " [" << link_type_str(hop.link_type)
                         << " " << hop.bandwidth_gbps << " Gbps]\n";
                }
            } else {
                resp << "ERROR: " << path_res.err() << "\n";
            }
        }
        else if (cmd == "bottleneck") {
            std::string from, to;
            iss >> from >> to;
            auto path_res = topo_.find_fastest_path(from, to);
            if (path_res) {
                auto bn_res = query_.get_bottleneck(path_res.value());
                if (bn_res) {
                    auto& bn = bn_res.value();
                    resp << "BOTTLENECK: " << bn.from << " -> " << bn.to
                         << " [" << link_type_str(bn.link_type)
                         << " " << bn.bandwidth_gbps << " Gbps, "
                         << bn.latency_ns << " ns] (hop " << bn.hop_index << ")\n";
                } else {
                    resp << "ERROR: " << bn_res.err() << "\n";
                }
            } else {
                resp << "ERROR: " << path_res.err() << "\n";
            }
        }
        else if (cmd == "affinity") {
            std::string device;
            iss >> device;
            auto aff_res = query_.get_affinity(device);
            if (aff_res) {
                auto& aff = aff_res.value();
                resp << "AFFINITY: device=" << aff.device_id
                     << " cpu=" << aff.closest_cpu
                     << " numa=" << aff.closest_numa
                     << " node=" << aff.numa_node
                     << " latency=" << aff.latency_ns << " ns\n";
            } else {
                resp << "ERROR: " << aff_res.err() << "\n";
            }
        }
        else if (cmd == "devices") {
            std::string type;
            iss >> type;
            std::vector<DeviceNode> devs;
            if (type.empty()) {
                devs = topo_.get_all_nodes();
            } else {
                devs = query_.get_devices(type);
            }
            resp << "DEVICES: " << devs.size() << "\n";
            for (auto& d : devs) {
                resp << "  " << d.id << " [" << device_type_str(d.type) << "] "
                     << d.name;
                if (d.bandwidth_gbps > 0) resp << " " << d.bandwidth_gbps << " Gbps";
                if (d.numa_node >= 0) resp << " numa=" << d.numa_node;
                resp << "\n";
            }
        }
        else if (cmd == "bandwidth") {
            std::string from, to;
            iss >> from >> to;
            auto bw_res = query_.get_bandwidth(from, to);
            if (bw_res) {
                resp << "BANDWIDTH: " << bw_res.value() << " Gbps\n";
            } else {
                resp << "ERROR: " << bw_res.err() << "\n";
            }
        }
        else if (cmd == "query") {
            std::string rest;
            std::getline(iss, rest);
            auto path_res = query_.query(rest);
            if (path_res) {
                auto& path = path_res.value();
                resp << "QUERY RESULT: " << path.hops.size() << " hops\n";
                for (auto& hop : path.hops) {
                    resp << "  " << hop.from << " -> " << hop.to
                         << " [" << hop.bandwidth_gbps << " Gbps, "
                         << hop.latency_ns << " ns]\n";
                }
            } else {
                resp << "ERROR: " << path_res.err() << "\n";
            }
        }
        else if (cmd == "transfer") {
            std::string from, to;
            uint64_t bytes;
            iss >> from >> to >> bytes;
            auto est_res = query_.estimate_transfer_time(from, to, bytes);
            if (est_res) {
                auto& est = est_res.value();
                resp << "TRANSFER: " << est.bytes << " bytes, "
                     << est.estimated_time_us << " us estimated, "
                     << est.bottleneck_bw_gbps << " Gbps bottleneck, "
                     << est.hop_count << " hops\n";
            } else {
                resp << "ERROR: " << est_res.err() << "\n";
            }
        }
        else {
            resp << "ERROR: unknown command: " << cmd << "\n";
        }
    }
};

} // namespace straylight::fabric

// ── main ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Check for --dump-json mode
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dump-json") == 0) {
            straylight::fabric::Topology topo;
            topo.build_topology();
            printf("%s", topo.to_json().c_str());
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            fprintf(stdout,
                "Usage: straylight-fabric [OPTIONS]\n"
                "\n"
                "Unified device topology graph daemon for StrayLight OS.\n"
                "\n"
                "Options:\n"
                "  --dump-json   Scan topology and dump JSON to stdout, then exit\n"
                "  -h, --help    Show this help\n");
            return 0;
        }
    }

    straylight::fabric::FabricDaemon daemon;
    return daemon.run();
}
