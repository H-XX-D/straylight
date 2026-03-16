// compositor/view.cpp
#include "view.h"
#include "server.h"
#include "workspace.h"
#include <straylight/log.h>

#define view_from_listener(ptr, field) \
    reinterpret_cast<View*>(reinterpret_cast<char*>(ptr) - offsetof(View, field))

namespace straylight::compositor {

void View::create(Server& server, wlr_xdg_toplevel* toplevel) {
    new View(server, toplevel); // Workspace takes ownership on map
}

View::View(Server& server, wlr_xdg_toplevel* toplevel)
    : server_(server), toplevel_(toplevel)
{
    scene_tree_ = wlr_scene_xdg_surface_create(
        &server_.scene()->tree, toplevel->base);

    // Store back-pointer for input hit-testing
    toplevel->base->data = this;

    on_map_.notify            = handle_map;
    on_unmap_.notify          = handle_unmap;
    on_commit_.notify         = handle_commit;
    on_destroy_.notify        = handle_destroy;
    on_request_move_.notify   = handle_request_move;
    on_request_resize_.notify = handle_request_resize;
    on_request_maximize_.notify   = handle_request_maximize;
    on_request_fullscreen_.notify = handle_request_fullscreen;
    on_set_title_.notify      = handle_set_title;

    wl_signal_add(&toplevel->base->surface->events.map,           &on_map_);
    wl_signal_add(&toplevel->base->surface->events.unmap,         &on_unmap_);
    wl_signal_add(&toplevel->base->surface->events.commit,        &on_commit_);
    wl_signal_add(&toplevel->base->events.destroy,                &on_destroy_);
    wl_signal_add(&toplevel->events.request_move,                 &on_request_move_);
    wl_signal_add(&toplevel->events.request_resize,               &on_request_resize_);
    wl_signal_add(&toplevel->events.request_maximize,             &on_request_maximize_);
    wl_signal_add(&toplevel->events.request_fullscreen,           &on_request_fullscreen_);
    wl_signal_add(&toplevel->events.set_title,                    &on_set_title_);
}

View::~View() {
    wl_list_remove(&on_map_.link);
    wl_list_remove(&on_unmap_.link);
    wl_list_remove(&on_commit_.link);
    wl_list_remove(&on_destroy_.link);
    wl_list_remove(&on_request_move_.link);
    wl_list_remove(&on_request_resize_.link);
    wl_list_remove(&on_request_maximize_.link);
    wl_list_remove(&on_request_fullscreen_.link);
    wl_list_remove(&on_set_title_.link);
}

std::string View::title()  const { return toplevel_->title  ? toplevel_->title  : ""; }
std::string View::app_id() const { return toplevel_->app_id ? toplevel_->app_id : ""; }

void View::focus(wlr_surface* surface) {
    if (!surface) surface = toplevel_->base->surface;
    wlr_seat* seat = server_.seat();
    wlr_surface* prev = seat->keyboard_state.focused_surface;
    if (prev == surface) return;

    if (prev) {
        // Deactivate previous toplevel
        wlr_xdg_toplevel* prev_top = wlr_xdg_surface_try_from_wlr_surface(prev);
        if (prev_top) wlr_xdg_toplevel_set_activated(prev_top, false);
    }

    // Raise in scene
    wlr_scene_node_raise_to_top(&scene_tree_->node);
    wlr_xdg_toplevel_set_activated(toplevel_, true);
    focused_ = true;

    // Deliver keyboard focus
    wlr_keyboard* kb = wlr_seat_get_keyboard(seat);
    if (kb) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

void View::set_position(int x, int y) {
    wlr_scene_node_set_position(&scene_tree_->node, x, y);
}

void View::set_size(int w, int h) {
    wlr_xdg_toplevel_set_size(toplevel_, w, h);
}

void View::close() {
    wlr_xdg_toplevel_send_close(toplevel_);
}

// --- Listener callbacks ---

void View::handle_map(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_map_);
    self->mapped_ = true;
    self->server_.workspace().on_view_map(self);
    self->focus();
    SL_DEBUG("View mapped: app_id={} title={}", self->app_id(), self->title());
}

void View::handle_unmap(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_unmap_);
    self->mapped_ = false;
    self->server_.workspace().on_view_unmap(self);
    SL_DEBUG("View unmapped: app_id={}", self->app_id());
}

void View::handle_commit(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_commit_);
    // Notify workspace to re-tile if pending geometry change
    if (self->mapped_) self->server_.workspace().on_view_commit(self);
}

void View::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_destroy_);
    delete self;
}

void View::handle_request_move(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_request_move_);
    self->server_.workspace().on_view_request_move(self);
}

void View::handle_request_resize(wl_listener* l, void* data) {
    auto* self  = view_from_listener(l, on_request_resize_);
    auto* event = static_cast<wlr_xdg_toplevel_resize_event*>(data);
    self->server_.workspace().on_view_request_resize(self, event->edges);
}

void View::handle_request_maximize(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_request_maximize_);
    self->server_.workspace().on_view_request_maximize(self);
}

void View::handle_request_fullscreen(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_request_fullscreen_);
    self->server_.workspace().on_view_request_fullscreen(self);
}

void View::handle_set_title(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_set_title_);
    SL_DEBUG("View title changed: {}", self->title());
}

} // namespace straylight::compositor
