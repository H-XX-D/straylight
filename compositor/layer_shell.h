// compositor/layer_shell.h
#pragma once
#include <vector>
#include <memory>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

class Server;

// LayerSurface wraps a single wlr_layer_surface_v1 (e.g., a panel or dock).
struct LayerSurface {
    Server*               server     = nullptr;
    wlr_layer_surface_v1* surface    = nullptr;
    wlr_scene_layer_surface_v1* scene = nullptr;
    wl_listener on_map{};
    wl_listener on_unmap{};
    wl_listener on_commit{};
    wl_listener on_destroy{};
};

class LayerShellManager {
public:
    LayerShellManager(Server& server, wlr_layer_shell_v1* layer_shell);
    ~LayerShellManager();

    LayerShellManager(const LayerShellManager&) = delete;
    LayerShellManager& operator=(const LayerShellManager&) = delete;

    // Called by Server::handle_new_layer_surface
    void on_new_surface(wlr_layer_surface_v1* surface);

    // Returns the combined reserved margin for a given layer on the given output
    struct Margins { int top, bottom, left, right; };
    Margins reserved_margins(wlr_output* output) const;

private:
    Server&              server_;
    wlr_layer_shell_v1*  layer_shell_;

    std::vector<std::unique_ptr<LayerSurface>> surfaces_;

    static void handle_map(wl_listener* l, void*);
    static void handle_unmap(wl_listener* l, void*);
    static void handle_commit(wl_listener* l, void*);
    static void handle_destroy(wl_listener* l, void*);
};

} // namespace straylight::compositor
