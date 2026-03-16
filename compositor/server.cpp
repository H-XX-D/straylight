// compositor/server.cpp
#include "server.h"
#include "output.h"
#include "view.h"
#include "workspace.h"
#include "input.h"
#include "layer_shell.h"
#include "session_lock.h"
#include "ipc.h"

#include <straylight/log.h>

#include <cstdlib>
#include <stdexcept>

extern "C" {
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

// Helper: get enclosing Server* from a wl_listener* via offsetof
#define server_from_listener(ptr, field) \
    reinterpret_cast<Server*>(reinterpret_cast<char*>(ptr) - offsetof(Server, field))

std::expected<std::unique_ptr<Server>, ServerError> Server::create() {
    auto s = std::unique_ptr<Server>(new Server());
    if (auto r = s->init(); !r) return std::unexpected(r.error());
    return s;
}

Server::~Server() {
    if (display_) wl_display_destroy_clients(display_);
    ipc_.reset();
    session_lock_.reset();
    layer_shell_.reset();
    input_.reset();
    workspace_.reset();
    if (display_) wl_display_destroy(display_);
}

std::expected<void, ServerError> Server::init() {
    // Redirect wlroots log to spdlog
    wlr_log_init(WLR_DEBUG, [](wlr_log_importance imp, const char* fmt, va_list args) {
        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, args);
        switch (imp) {
            case WLR_ERROR: SL_ERROR("[wlroots] {}", buf); break;
            case WLR_INFO:  SL_INFO ("[wlroots] {}", buf); break;
            default:        SL_DEBUG("[wlroots] {}", buf); break;
        }
    });

    display_ = wl_display_create();
    if (!display_) return std::unexpected(ServerError{"wl_display_create failed"});

    event_loop_ = wl_display_get_event_loop(display_);

    backend_ = wlr_backend_autocreate(event_loop_, nullptr);
    if (!backend_) return std::unexpected(ServerError{"wlr_backend_autocreate failed"});

    renderer_ = wlr_renderer_autocreate(backend_);
    if (!renderer_) return std::unexpected(ServerError{"wlr_renderer_autocreate failed"});

    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (!allocator_) return std::unexpected(ServerError{"wlr_allocator_autocreate failed"});

    compositor_    = wlr_compositor_create(display_, 5, renderer_);
    subcompositor_ = wlr_subcompositor_create(display_);
    data_device_   = wlr_data_device_manager_create(display_);
    output_layout_ = wlr_output_layout_create(display_);

    scene_ = wlr_scene_create();
    if (!scene_) return std::unexpected(ServerError{"wlr_scene_create failed"});

    wlr_scene_attach_output_layout(scene_, output_layout_);

    seat_ = wlr_seat_create(display_, "seat0");
    if (!seat_) return std::unexpected(ServerError{"wlr_seat_create failed"});

    cursor_     = wlr_cursor_create();
    cursor_mgr_ = wlr_xcursor_manager_create(nullptr, 24);
    wlr_cursor_attach_output_layout(cursor_, output_layout_);
    wlr_xcursor_manager_load(cursor_mgr_, 1);

    idle_notifier_  = wlr_idle_notifier_v1_create(display_);
    output_manager_ = wlr_output_manager_v1_create(display_);

    xdg_shell_ = wlr_xdg_shell_create(display_, 6);
    if (!xdg_shell_) return std::unexpected(ServerError{"wlr_xdg_shell_create failed"});

    layer_shell_wlr_ = wlr_layer_shell_v1_create(display_, 4);
    if (!layer_shell_wlr_) return std::unexpected(ServerError{"wlr_layer_shell_v1_create failed"});

    // Sub-systems
    workspace_    = std::make_unique<Workspace>(*this);
    input_        = std::make_unique<InputManager>(*this, cursor_, cursor_mgr_, seat_);
    layer_shell_  = std::make_unique<LayerShellManager>(*this, layer_shell_wlr_);
    session_lock_ = std::make_unique<SessionLockManager>(*this);
    ipc_          = std::make_unique<CompositorIpc>(*this);

    // Register listeners
    on_new_output_.notify = handle_new_output;
    wl_signal_add(&backend_->events.new_output, &on_new_output_);

    on_new_xdg_toplevel_.notify = handle_new_xdg_toplevel;
    wl_signal_add(&xdg_shell_->events.new_toplevel, &on_new_xdg_toplevel_);

    on_new_layer_surface_.notify = handle_new_layer_surface;
    wl_signal_add(&layer_shell_wlr_->events.new_surface, &on_new_layer_surface_);

    // Start Wayland socket
    const char* socket = wl_display_add_socket_auto(display_);
    if (!socket) return std::unexpected(ServerError{"wl_display_add_socket_auto failed"});
    socket_name_ = socket;
    setenv("WAYLAND_DISPLAY", socket, true);
    SL_INFO("Compositor socket: {}", socket_name_);

    if (!wlr_backend_start(backend_))
        return std::unexpected(ServerError{"wlr_backend_start failed"});

    return {};
}

std::expected<void, ServerError> Server::run() {
    SL_INFO("straylight-compositor running");

    // Notify systemd readiness if running under a systemd user service
    if (std::getenv("NOTIFY_SOCKET")) {
        // sd_notify("READY=1") equivalent — deferred to systemd integration
        SL_DEBUG("NOTIFY_SOCKET set; systemd readiness notification deferred");
    }

    wl_display_run(display_);
    return {};
}

// --- Static listener trampolines ---

void Server::handle_new_output(wl_listener* l, void* data) {
    auto* self   = server_from_listener(l, on_new_output_);
    auto* output = static_cast<wlr_output*>(data);
    Output::setup(*self, output);
}

void Server::handle_new_xdg_toplevel(wl_listener* l, void* data) {
    auto* self     = server_from_listener(l, on_new_xdg_toplevel_);
    auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);
    View::create(*self, toplevel);
}

void Server::handle_new_layer_surface(wl_listener* l, void* data) {
    auto* self    = server_from_listener(l, on_new_layer_surface_);
    auto* surface = static_cast<wlr_layer_surface_v1*>(data);
    self->layer_shell().on_new_surface(surface);
}

} // namespace straylight::compositor
