// compositor/decorations.h
// Stub — full implementation in Plan 3, Chunk 8
#pragma once

namespace straylight::compositor {

class Server;

class Decorations {
public:
    explicit Decorations(Server& server);
    ~Decorations();

private:
    Server& server_;
};

} // namespace straylight::compositor
