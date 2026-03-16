// compositor/session_lock.cpp
// Stub — full implementation in Plan 3, Chunk 7
#include "session_lock.h"
#include "server.h"

namespace straylight::compositor {

SessionLockManager::SessionLockManager(Server& server)
    : server_(server)
{
}

SessionLockManager::~SessionLockManager() = default;

} // namespace straylight::compositor
