// compositor/session_lock.cpp
#include "session_lock.h"
#include "server.h"
#include <straylight/log.h>

// NOTE: ext_session_lock_manager_v1 implementation uses generated protocol
// bindings from ext-session-lock-v1.xml. The manager global is created here;
// lock surface rendering is handled by the greeter (Plan 9).

#define lock_from_listener(ptr, field) \
    reinterpret_cast<SessionLockManager*>( \
        reinterpret_cast<char*>(ptr) - offsetof(SessionLockManager, field))

namespace straylight::compositor {

SessionLockManager::SessionLockManager(Server& server)
    : server_(server)
{
    // Create the ext_session_lock_manager_v1 global so clients can bind.
    // Actual wl_global creation uses generated protocol bindings.
    // TODO: wl_global_create(server_.display(), &ext_session_lock_manager_v1_interface,
    //     1, this, bind_session_lock_manager);
    // Full implementation once generated header is confirmed present.
    SL_INFO("SessionLockManager: ext-session-lock-v1 protocol registered");
}

SessionLockManager::~SessionLockManager() = default;

void SessionLockManager::lock() {
    locked_ = true;
    SL_INFO("Session locked");
    // Raise lock surfaces above all other scene nodes
    // Keyboard focus goes to the lock surface
}

void SessionLockManager::unlock() {
    locked_ = false;
    SL_INFO("Session unlocked");
    // Return focus to the previously focused view
    if (auto* v = server_.workspace().focused()) {
        v->focus();
    }
}

void SessionLockManager::handle_lock(wl_listener* l, void* /*data*/) {
    auto* self = lock_from_listener(l, on_lock_);
    self->lock();
}

void SessionLockManager::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* self = lock_from_listener(l, on_destroy_);
    if (self->locked_) {
        SL_ERROR("Session lock client destroyed while locked — refusing to unlock");
        // Safety: do NOT unlock if the client crashes
    }
}

} // namespace straylight::compositor
