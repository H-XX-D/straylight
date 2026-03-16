// compositor/layer_shell.h
// Stub — full implementation in Plan 3, Chunk 6
#pragma once

extern "C" {
#include <wlr/types/wlr_layer_shell_v1.h>
}

namespace straylight::compositor {

class Server;

class LayerShellManager {
public:
    LayerShellManager(Server& server, wlr_layer_shell_v1* shell);
    ~LayerShellManager();

    LayerShellManager(const LayerShellManager&) = delete;
    LayerShellManager& operator=(const LayerShellManager&) = delete;

    void on_new_surface(wlr_layer_surface_v1* surface);

private:
    Server& server_;
    wlr_layer_shell_v1* shell_;
};

} // namespace straylight::compositor
