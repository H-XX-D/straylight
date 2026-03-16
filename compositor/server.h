// compositor/server.h
#pragma once
#include <memory>
#include <string>
#include <expected>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/log.h>
}

#include <straylight/common.h>

namespace straylight::compositor {

struct ServerError {
    std::string message;
};

class Output;
class View;
class Workspace;
class InputManager;
class LayerShellManager;
class SessionLockManager;
class CompositorIpc;

class Server {
public:
    static std::expected<std::unique_ptr<Server>, ServerError> create();
    ~Server();

    // No copy, no move — owns wayland display lifetime
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Run the event loop. Blocks until compositor exits.
    std::expected<void, ServerError> run();

    // Access for sub-systems
    wl_display*           display()       const { return display_; }
    wlr_backend*          backend()       const { return backend_; }
    wlr_renderer*         renderer()      const { return renderer_; }
    wlr_allocator*        allocator()     const { return allocator_; }
    wlr_scene*            scene()         const { return scene_; }
    wlr_output_layout*    output_layout() const { return output_layout_; }
    wlr_seat*             seat()          const { return seat_; }
    wlr_xdg_shell*        xdg_shell()     const { return xdg_shell_; }

    Workspace&            workspace()     { return *workspace_; }
    InputManager&         input()         { return *input_; }
    LayerShellManager&    layer_shell()   { return *layer_shell_; }
    SessionLockManager&   session_lock()  { return *session_lock_; }
    CompositorIpc&        ipc()           { return *ipc_; }

private:
    Server() = default;
    std::expected<void, ServerError> init();

    // Wayland + wlroots objects (raw, owned)
    wl_display*                     display_       = nullptr;
    wl_event_loop*                  event_loop_    = nullptr;
    wlr_backend*                    backend_       = nullptr;
    wlr_renderer*                   renderer_      = nullptr;
    wlr_allocator*                  allocator_     = nullptr;
    wlr_compositor*                 compositor_    = nullptr;
    wlr_subcompositor*              subcompositor_ = nullptr;
    wlr_data_device_manager*        data_device_   = nullptr;
    wlr_output_layout*              output_layout_ = nullptr;
    wlr_scene*                      scene_         = nullptr;
    wlr_xdg_shell*                  xdg_shell_     = nullptr;
    wlr_layer_shell_v1*             layer_shell_wlr_ = nullptr;
    wlr_seat*                       seat_          = nullptr;
    wlr_cursor*                     cursor_        = nullptr;
    wlr_xcursor_manager*            cursor_mgr_    = nullptr;
    wlr_idle_notifier_v1*           idle_notifier_ = nullptr;
    wlr_output_manager_v1*          output_manager_ = nullptr;

    // Wayland listeners (must outlive the objects they listen on)
    wl_listener on_new_output_{};
    wl_listener on_new_xdg_toplevel_{};
    wl_listener on_new_layer_surface_{};

    // Owned sub-systems
    std::unique_ptr<Workspace>          workspace_;
    std::unique_ptr<InputManager>       input_;
    std::unique_ptr<LayerShellManager>  layer_shell_;
    std::unique_ptr<SessionLockManager> session_lock_;
    std::unique_ptr<CompositorIpc>      ipc_;

    // Listener callbacks (static, trampoline to member)
    static void handle_new_output(wl_listener* l, void* data);
    static void handle_new_xdg_toplevel(wl_listener* l, void* data);
    static void handle_new_layer_surface(wl_listener* l, void* data);

    std::string socket_name_;
};

} // namespace straylight::compositor
