// bin/bus/bus_daemon.h
#pragma once

#include <straylight/common.h>
#include <straylight/daemon.h>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight {

using SignalHandler = std::function<void(const std::string&)>;

class BusDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    // Service registry (name -> owner pid)
    Result<void, SLError> register_service(const std::string& name, pid_t owner);
    void unregister_service(const std::string& name);
    std::optional<pid_t> lookup_owner(const std::string& name) const;

    // Signal forwarding
    void subscribe(const std::string& service, const std::string& signal,
                   SignalHandler handler);
    void emit(const std::string& service, const std::string& signal,
              const std::string& payload);

private:
    std::unordered_map<std::string, pid_t> service_registry_;
    std::unordered_map<std::string, std::vector<SignalHandler>> subscriptions_;
    mutable std::mutex mutex_;
};

} // namespace straylight
