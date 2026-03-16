// compositor/session_lock.h
#pragma once
#include <memory>

extern "C" {
#include <wayland-server-core.h>
}

namespace straylight::compositor {

class Server;

class SessionLockManager {
public:
    explicit SessionLockManager(Server& server);
    ~SessionLockManager();

    SessionLockManager(const SessionLockManager&) = delete;
    SessionLockManager& operator=(const SessionLockManager&) = delete;

    bool is_locked() const { return locked_; }

    // Request lock (called by greeter on startup)
    void lock();
    // Unlock (called after successful PAM authentication)
    void unlock();

private:
    Server& server_;
    bool    locked_ = false;

    wl_listener on_lock_{};
    wl_listener on_destroy_{};

    static void handle_lock(wl_listener* l, void* data);
    static void handle_destroy(wl_listener* l, void* data);
};

} // namespace straylight::compositor
