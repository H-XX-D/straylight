// bin/bus/bus_daemon.cpp
#include "bus_daemon.h"
#include <thread>
#include <chrono>

namespace straylight {

Result<void, SLError> BusDaemon::init(const Config& /*cfg*/) {
    SL_INFO("bus: initialized");
    return Result<void, SLError>::ok();
}

Result<void, SLError> BusDaemon::tick() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return Result<void, SLError>::ok();
}

void BusDaemon::shutdown() {
    SL_INFO("bus: shutting down");
}

Result<void, SLError> BusDaemon::register_service(const std::string& name, pid_t owner) {
    std::lock_guard lock(mutex_);
    if (service_registry_.contains(name))
        return Result<void, SLError>::error(
            SLError{SLErrorCode::AlreadyExists, "service already registered: " + name});
    service_registry_[name] = owner;
    SL_DEBUG("bus: registered service {} (pid={})", name, owner);
    return Result<void, SLError>::ok();
}

void BusDaemon::unregister_service(const std::string& name) {
    std::lock_guard lock(mutex_);
    service_registry_.erase(name);
    subscriptions_.erase(name);
}

std::optional<pid_t> BusDaemon::lookup_owner(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = service_registry_.find(name);
    if (it == service_registry_.end()) return std::nullopt;
    return it->second;
}

void BusDaemon::subscribe(const std::string& service, const std::string& signal,
                           SignalHandler handler) {
    std::lock_guard lock(mutex_);
    subscriptions_[service + "." + signal].push_back(std::move(handler));
}

void BusDaemon::emit(const std::string& service, const std::string& signal,
                     const std::string& payload) {
    // Copy handlers under lock, invoke outside to avoid deadlock
    // if a handler re-enters subscribe()/emit()/register_service().
    std::vector<SignalHandler> handlers;
    {
        std::lock_guard lock(mutex_);
        auto key = service + "." + signal;
        if (auto it = subscriptions_.find(key); it != subscriptions_.end())
            handlers = it->second;
    }
    for (auto& h : handlers) h(payload);
}

} // namespace straylight
