// compositor/ipc.h
// Stub — full implementation in Plan 3, Chunk 9
#pragma once

namespace straylight::compositor {

class Server;

class CompositorIpc {
public:
    explicit CompositorIpc(Server& server);
    ~CompositorIpc();

    CompositorIpc(const CompositorIpc&) = delete;
    CompositorIpc& operator=(const CompositorIpc&) = delete;

private:
    Server& server_;
};

} // namespace straylight::compositor
