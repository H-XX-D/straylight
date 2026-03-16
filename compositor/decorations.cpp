// compositor/decorations.cpp
// Stub — full implementation in Plan 3, Chunk 8
#include "decorations.h"
#include "server.h"

namespace straylight::compositor {

Decorations::Decorations(Server& server)
    : server_(server)
{
}

Decorations::~Decorations() = default;

} // namespace straylight::compositor
