// compositor/layer_shell.cpp
// Stub — full implementation in Plan 3, Chunk 6
#include "layer_shell.h"
#include "server.h"
#include <straylight/log.h>

namespace straylight::compositor {

LayerShellManager::LayerShellManager(Server& server, wlr_layer_shell_v1* shell)
    : server_(server), shell_(shell)
{
}

LayerShellManager::~LayerShellManager() = default;

void LayerShellManager::on_new_surface(wlr_layer_surface_v1* surface) {
    SL_DEBUG("Layer surface requested (stub) — namespace={}",
        surface->namespace_ ? surface->namespace_ : "(null)");
    // TODO: Implement in Chunk 6
}

} // namespace straylight::compositor
