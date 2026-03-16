// compositor/layer_shell.cpp
#include "layer_shell.h"
#include "server.h"
#include <straylight/log.h>

#define ls_from_listener(ptr, field) \
    reinterpret_cast<LayerSurface*>(reinterpret_cast<char*>(ptr) - offsetof(LayerSurface, field))

namespace straylight::compositor {

LayerShellManager::LayerShellManager(Server& server, wlr_layer_shell_v1* layer_shell)
    : server_(server), layer_shell_(layer_shell)
{}

LayerShellManager::~LayerShellManager() = default;

void LayerShellManager::on_new_surface(wlr_layer_surface_v1* surface) {
    // Assign surface to its requested output (or primary output if null)
    if (!surface->output) {
        surface->output = wlr_output_layout_get_center_output(server_.output_layout());
    }

    // Create scene_layer_surface in the appropriate scene layer tree.
    // We use the root scene tree; wlr_scene_layer_surface_v1_create handles
    // layer-level ordering internally.
    wlr_scene_tree* layer_tree = &server_.scene()->tree;

    auto ls = std::make_unique<LayerSurface>();
    ls->server  = &server_;
    ls->surface = surface;
    ls->scene   = wlr_scene_layer_surface_v1_create(layer_tree, surface);

    ls->on_map.notify     = handle_map;
    ls->on_unmap.notify   = handle_unmap;
    ls->on_commit.notify  = handle_commit;
    ls->on_destroy.notify = handle_destroy;

    wl_signal_add(&surface->surface->events.map,    &ls->on_map);
    wl_signal_add(&surface->surface->events.unmap,  &ls->on_unmap);
    wl_signal_add(&surface->surface->events.commit, &ls->on_commit);
    wl_signal_add(&surface->events.destroy,          &ls->on_destroy);

    // Configure initial geometry
    wlr_box output_box{};
    wlr_output_layout_get_box(server_.output_layout(), surface->output, &output_box);
    wlr_scene_layer_surface_v1_configure(ls->scene, &output_box, &output_box);

    surfaces_.push_back(std::move(ls));
    SL_INFO("LayerSurface created: layer={}", (int)surface->pending.layer);
}

LayerShellManager::Margins
LayerShellManager::reserved_margins(wlr_output* output) const {
    Margins m{};
    for (const auto& ls : surfaces_) {
        if (!ls->surface->mapped || ls->surface->output != output) continue;
        const auto& state = ls->surface->current;
        m.top    += state.exclusive_zone > 0 &&
                    state.anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
                    ? state.exclusive_zone : 0;
        m.bottom += state.exclusive_zone > 0 &&
                    state.anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
                    ? state.exclusive_zone : 0;
        m.left   += state.exclusive_zone > 0 &&
                    state.anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                    ? state.exclusive_zone : 0;
        m.right  += state.exclusive_zone > 0 &&
                    state.anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
                    ? state.exclusive_zone : 0;
    }
    return m;
}

void LayerShellManager::handle_map(wl_listener* l, void* /*data*/) {
    auto* ls = ls_from_listener(l, on_map);
    ls->server->workspace().retile(); // Recalculate usable area
}

void LayerShellManager::handle_unmap(wl_listener* l, void* /*data*/) {
    auto* ls = ls_from_listener(l, on_unmap);
    ls->server->workspace().retile();
}

void LayerShellManager::handle_commit(wl_listener* l, void* /*data*/) {
    auto* ls = ls_from_listener(l, on_commit);
    wlr_box output_box{};
    wlr_output_layout_get_box(ls->server->output_layout(),
        ls->surface->output, &output_box);
    wlr_scene_layer_surface_v1_configure(ls->scene, &output_box, &output_box);
}

void LayerShellManager::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* ls = ls_from_listener(l, on_destroy);
    auto& surfaces = ls->server->layer_shell().surfaces_;
    surfaces.erase(
        std::remove_if(surfaces.begin(), surfaces.end(),
            [ls](const auto& up){ return up.get() == ls; }),
        surfaces.end());
}

} // namespace straylight::compositor
