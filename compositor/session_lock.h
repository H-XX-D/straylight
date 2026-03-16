// compositor/session_lock.h
// Stub — full implementation in Plan 3, Chunk 7
#pragma once

namespace straylight::compositor {

class Server;

class SessionLockManager {
public:
    explicit SessionLockManager(Server& server);
    ~SessionLockManager();

    SessionLockManager(const SessionLockManager&) = delete;
    SessionLockManager& operator=(const SessionLockManager&) = delete;

private:
    Server& server_;
};

} // namespace straylight::compositor
