// compositor/ipc.cpp
// Stub — full implementation in Plan 3, Chunk 9
#include "ipc.h"
#include "server.h"

namespace straylight::compositor {

CompositorIpc::CompositorIpc(Server& server)
    : server_(server)
{
}

CompositorIpc::~CompositorIpc() = default;

} // namespace straylight::compositor
