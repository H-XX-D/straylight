// tools/wifi/main.cpp
// CLI front-end for straylight-wifi — WiFi management.

#include "wifi_manager.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

static void print_usage() {
    std::cerr
        << "straylight-wifi — WiFi management CLI\n"
        << "\n"
        << "Usage:\n"
        << "  straylight-wifi scan                         Scan for WiFi networks\n"
        << "  straylight-wifi connect <ssid> [--pass=X]    Connect to network\n"
        << "  straylight-wifi disconnect                   Disconnect from WiFi\n"
        << "  straylight-wifi saved                        List saved networks\n"
        << "  straylight-wifi forget <ssid>                Forget saved network\n"
        << "  straylight-wifi channels                     Channel analysis\n"
        << "  straylight-wifi signal                       Current signal quality\n"
        << "  straylight-wifi qr <ssid>                    QR code for network\n";
}

static std::string get_arg(int argc, char* argv[], const std::string& prefix, int start = 2) {
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
    }
    return "";
}

static std::string signal_bar(int dbm) {
    int quality;
    if (dbm >= -50) quality = 4;
    else if (dbm >= -60) quality = 3;
    else if (dbm >= -70) quality = 2;
    else if (dbm >= -80) quality = 1;
    else quality = 0;

    std::string bar;
    for (int i = 0; i < 4; ++i) bar += (i < quality) ? "|" : ".";
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

    straylight::WifiManager mgr;

    // -----------------------------------------------------------------------
    // scan
    // -----------------------------------------------------------------------
    if (command == "scan") {
        auto res = mgr.scan();
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
                  << std::setw(6) << "SIG"
                  << std::setw(32) << "SSID"
                  << std::setw(10) << "SECURITY"
                  << std::setw(6) << "CH"
                  << std::setw(20) << "BSSID"
                  << "\n";
        std::cout << std::string(74, '-') << "\n";

        for (const auto& net : networks) {
            std::string ssid = net.ssid.empty() ? "(hidden)" : net.ssid;
            if (net.connected) ssid += " *";

            std::cout << std::left
                      << std::setw(6) << signal_bar(net.signal_dbm)
                      << std::setw(32) << ssid
                      << std::setw(10) << net.security
                      << std::setw(6) << net.channel
                      << std::setw(20) << net.bssid
                      << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // connect <ssid> [--pass=X]
    // -----------------------------------------------------------------------
    if (command == "connect") {
        if (argc < 3) {
            std::cerr << "Error: 'connect' requires an SSID\n";
            return 1;
        }
        std::string ssid = argv[2];
        std::string pass = get_arg(argc, argv, "--pass=", 3);

        auto res = mgr.connect(ssid, pass);
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << "Connected to '" << ssid << "'\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // disconnect
    // -----------------------------------------------------------------------
    if (command == "disconnect") {
        auto res = mgr.disconnect();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << "Disconnected from WiFi.\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // saved
    // -----------------------------------------------------------------------
    if (command == "saved") {
        auto res = mgr.saved();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        const auto& networks = res.value();
        if (networks.empty()) {
            std::cout << "No saved WiFi networks.\n";
            return 0;
        }

        std::cout << std::left
                  << std::setw(28) << "SSID"
                  << std::setw(12) << "SECURITY"
                  << std::setw(8) << "AUTO"
                  << "LAST CONNECTED\n";
        std::cout << std::string(68, '-') << "\n";

        for (const auto& net : networks) {
            std::cout << std::left
                      << std::setw(28) << net.ssid
                      << std::setw(12) << net.security
                      << std::setw(8) << (net.auto_connect ? "yes" : "no")
                      << net.last_connected << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // forget <ssid>
    // -----------------------------------------------------------------------
    if (command == "forget") {
        if (argc < 3) {
            std::cerr << "Error: 'forget' requires an SSID\n";
            return 1;
        }
        auto res = mgr.forget(argv[2]);
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << "Forgot '" << argv[2] << "'\n";
        return 0;
    }

    // -----------------------------------------------------------------------
    // channels
    // -----------------------------------------------------------------------
    if (command == "channels") {
        auto res = mgr.channels();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        const auto& channels = res.value();

        // Find best channel per band
        int best_24 = 0, best_24_score = 101;
        int best_5 = 0, best_5_score = 101;

        std::cout << "=== 2.4 GHz Channels ===\n";
        std::cout << std::left
                  << std::setw(6) << "CH"
                  << std::setw(10) << "FREQ"
                  << std::setw(10) << "NETS"
                  << std::setw(12) << "AVG SIG"
                  << "INTERFERENCE\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& ch : channels) {
            if (ch.band != "2.4GHz") continue;

            // Interference bar
            int bars = ch.interference_score / 10;
            std::string ibar;
            for (int i = 0; i < 10; ++i) ibar += (i < bars) ? "#" : ".";

            std::cout << std::left
                      << std::setw(6) << ch.channel
                      << std::setw(10) << ch.frequency_mhz
                      << std::setw(10) << ch.network_count
                      << std::setw(12) << (ch.avg_signal_dbm != 0 ?
                         std::to_string(ch.avg_signal_dbm) + " dBm" : "-")
                      << "[" << ibar << "] " << ch.interference_score << "%\n";

            if (ch.interference_score < best_24_score) {
                best_24_score = ch.interference_score;
                best_24 = ch.channel;
            }
        }

        std::cout << "\nBest 2.4GHz channel: " << best_24 << "\n\n";

        std::cout << "=== 5 GHz Channels ===\n";
        std::cout << std::left
                  << std::setw(6) << "CH"
                  << std::setw(10) << "FREQ"
                  << std::setw(10) << "NETS"
                  << std::setw(12) << "AVG SIG"
                  << "INTERFERENCE\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& ch : channels) {
            if (ch.band != "5GHz") continue;
            if (ch.network_count == 0 && ch.interference_score == 0) continue;

            int bars = ch.interference_score / 10;
            std::string ibar;
            for (int i = 0; i < 10; ++i) ibar += (i < bars) ? "#" : ".";

            std::cout << std::left
                      << std::setw(6) << ch.channel
                      << std::setw(10) << ch.frequency_mhz
                      << std::setw(10) << ch.network_count
                      << std::setw(12) << (ch.avg_signal_dbm != 0 ?
                         std::to_string(ch.avg_signal_dbm) + " dBm" : "-")
                      << "[" << ibar << "] " << ch.interference_score << "%\n";

            if (ch.interference_score < best_5_score) {
                best_5_score = ch.interference_score;
                best_5 = ch.channel;
            }
        }

        if (best_5 > 0) {
            std::cout << "\nBest 5GHz channel: " << best_5 << "\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // signal
    // -----------------------------------------------------------------------
    if (command == "signal") {
        auto res = mgr.signal();
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }

        const auto& sq = res.value();
        std::cout << "WiFi Signal Quality\n"
                  << "  Network:      " << sq.ssid << "\n"
                  << "  Signal:       " << sq.signal_dbm << " dBm "
                  << signal_bar(sq.signal_dbm) << "\n"
                  << "  Link Quality: " << sq.link_quality << "%\n"
                  << "  Channel:      " << sq.channel << " ("
                  << sq.frequency_mhz << " MHz)\n";

        if (sq.noise_dbm != 0) {
            std::cout << "  Noise:        " << sq.noise_dbm << " dBm\n"
                      << "  SNR:          " << (sq.signal_dbm - sq.noise_dbm) << " dB\n";
        }
        if (sq.tx_rate_mbps > 0) {
            std::cout << "  TX Rate:      " << sq.tx_rate_mbps << " Mbit/s\n";
        }
        if (sq.rx_rate_mbps > 0) {
            std::cout << "  RX Rate:      " << sq.rx_rate_mbps << " Mbit/s\n";
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // qr <ssid>
    // -----------------------------------------------------------------------
    if (command == "qr") {
        if (argc < 3) {
            std::cerr << "Error: 'qr' requires an SSID\n";
            return 1;
        }
        auto res = mgr.qr(argv[2]);
        if (!res.has_value()) {
            std::cerr << "Error: " << res.error() << "\n";
            return 1;
        }
        std::cout << res.value();
        return 0;
    }

    std::cerr << "Error: unknown command '" << command << "'\n";
    print_usage();
    return 1;
}
