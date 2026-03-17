// services/backup-scheduler/main.cpp
// straylight-backup daemon — automated backup scheduler.
#include "backup_engine.h"

#include <straylight/config.h>
#include <straylight/daemon.h>
#include <straylight/ipc.h>
#include <straylight/log.h>

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace straylight {

class BackupDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override {
        SL_INFO("backup: initializing daemon");

        tick_interval_s_ = cfg.get<int>("tick_interval_seconds", 300);
        socket_path_ = cfg.get<std::string>(
            "ipc.socket_path", "/run/straylight/backup.sock");

        auto init_res = engine_.init("/etc/straylight/backup.conf");
        if (!init_res.has_value()) {
            SL_WARN("backup: engine init issue: {}", init_res.error().message());
        }

        auto bind_res = ipc_server_.bind(socket_path_);
        if (!bind_res.has_value()) {
            SL_WARN("backup: IPC bind failed: {}", bind_res.error());
        }

        SL_INFO("backup: daemon initialized (tick={}s)", tick_interval_s_);
        return Result<void, SLError>::ok();
    }

    Result<void, SLError> tick() override {
        // Check scheduled backups
        engine_.tick();

        // Handle IPC
        handle_ipc();

        std::this_thread::sleep_for(std::chrono::seconds(tick_interval_s_));
        return Result<void, SLError>::ok();
    }

    void shutdown() override {
        SL_INFO("backup: shutting down");
    }

private:
    void handle_ipc() {
        auto conn_res = ipc_server_.accept(100);
        if (!conn_res.has_value()) return;

        auto& conn = conn_res.value();
        auto msg_res = conn->receive();
        if (!msg_res.has_value()) return;

        try {
            auto req = nlohmann::json::parse(msg_res.value());
            std::string cmd = req.value("cmd", "");
            nlohmann::json response;

            if (cmd == "now") {
                std::string schedule = "";
                if (req.contains("params")) {
                    schedule = req["params"].value("schedule", "");
                }
                auto res = engine_.run_backup(schedule);
                if (res.has_value()) {
                    response["status"] = "ok";
                    response["backup_id"] = res.value().id;
                    response["size"] = res.value().size_bytes;
                } else {
                    response["status"] = "error";
                    response["message"] = res.error().message();
                }

            } else if (cmd == "list") {
                auto records = engine_.list_backups();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& r : records) {
                    nlohmann::json rj;
                    rj["id"] = r.id;
                    rj["type"] = r.type;
                    rj["source"] = r.source;
                    rj["status"] = r.status;
                    rj["size_bytes"] = r.size_bytes;
                    rj["encrypted"] = r.encrypted;
                    rj["verified"] = r.verified;
                    arr.push_back(rj);
                }
                response["status"] = "ok";
                response["backups"] = arr;

            } else if (cmd == "restore") {
                std::string id = req["params"].value("id", "");
                std::string target = req["params"].value("target", "");
                auto res = engine_.restore(id, target);
                response["status"] = res.has_value() ? "ok" : "error";
                if (!res.has_value()) response["message"] = res.error().message();

            } else if (cmd == "verify") {
                std::string id = req["params"].value("id", "");
                auto res = engine_.verify(id);
                if (res.has_value()) {
                    response["status"] = "ok";
                    response["valid"] = res.value();
                } else {
                    response["status"] = "error";
                    response["message"] = res.error().message();
                }

            } else if (cmd == "schedule") {
                auto schedules = engine_.list_schedules();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& s : schedules) {
                    nlohmann::json sj;
                    sj["name"] = s.name;
                    sj["source"] = s.source;
                    sj["schedule"] = s.cron_expression;
                    sj["encrypt"] = s.encrypt;
                    arr.push_back(sj);
                }
                response["status"] = "ok";
                response["schedules"] = arr;

            } else if (cmd == "remote-add") {
                RemoteTarget rt;
                auto& p = req["params"];
                rt.name = p.value("name", "");
                rt.host = p.value("host", "");
                rt.path = p.value("path", "/backups");
                rt.port = p.value("port", 22);
                rt.bandwidth_limit_kbps = p.value("bandwidth_limit_kbps", 0);
                auto res = engine_.add_remote(rt);
                response["status"] = res.has_value() ? "ok" : "error";
                if (!res.has_value()) response["message"] = res.error().message();

            } else if (cmd == "remote-remove") {
                std::string name = req["params"].value("name", "");
                auto res = engine_.remove_remote(name);
                response["status"] = res.has_value() ? "ok" : "error";
                if (!res.has_value()) response["message"] = res.error().message();

            } else if (cmd == "remote-list") {
                auto remotes = engine_.list_remotes();
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& r : remotes) {
                    nlohmann::json rj;
                    rj["name"] = r.name;
                    rj["host"] = r.host;
                    rj["path"] = r.path;
                    rj["port"] = r.port;
                    arr.push_back(rj);
                }
                response["status"] = "ok";
                response["remotes"] = arr;

            } else {
                response["status"] = "error";
                response["message"] = "Unknown command: " + cmd;
            }

            conn->send(response.dump());
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["status"] = "error";
            err["message"] = e.what();
            conn->send(err.dump());
        }
    }

    BackupEngine engine_;
    IpcServer ipc_server_;
    std::string socket_path_;
    int tick_interval_s_ = 300;
};

} // namespace straylight

int main(int /*argc*/, char* /*argv*/[]) {
    straylight::Log::init("straylight-backup");

    auto cfg_result = straylight::Config::load("/etc/straylight/backup.conf");
    if (!cfg_result.has_value()) {
        SL_ERROR("backup: failed to load config: {}", cfg_result.error());
        return 1;
    }

    straylight::BackupDaemon daemon;
    return daemon.run(cfg_result.value());
}
