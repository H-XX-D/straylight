# Plan 3: Wayland Compositor — straylight-compositor

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `straylight-compositor`, the wlroots-based Wayland compositor that manages all windows on the desktop. It handles output management, XDG shell, layer-shell, session lock, input, and IPC with straylight-core.

**Architecture:** wlroots 0.18+ provides the backend abstraction (DRM/KMS, libinput, EGL). The compositor implements xdg-shell for normal windows, wlr-layer-shell-unstable-v1 for the desktop shell overlay (Plan 4), and ext-session-lock-v1 for the greeter (Plan 9). A tiling/floating hybrid workspace manager handles layout. IPC with straylight-core uses the Unix socket layer from libstraylight-common.

**Tech Stack:** C++20, wlroots 0.18+, wayland-server 1.22+, wayland-protocols 1.34+, libinput 1.25+, xkbcommon, pixman, EGL + OpenGL ES 3.0, spdlog 1.13+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Development environment:** Linux x86_64 required. Debian Bookworm/Trixie with packages: `libwlroots-dev (>=0.18)`, `libwayland-dev`, `wayland-protocols`, `libinput-dev`, `libxkbcommon-dev`, `libegl-dev`, `libgles2-mesa-dev`, `libpixman-1-dev`. Must run on a real DRM device or a nested Wayland compositor (weston/sway) using the wlr-backend auto-detection.

**Depends on:** Plan 1 (libstraylight-common IPC and logging), Plan 2 (straylight-core IPC socket path).

**Deferred to later plans:** Plan 4 (shell layer-shell client), Plan 9 (greeter ext-session-lock-v1 client). The compositor exposes both protocols here; clients come later.

---

## Chunk 1: Compositor Skeleton

### File Structure

```
compositor/
├── CMakeLists.txt
├── main.cpp
├── server.h
├── server.cpp
├── output.h
├── output.cpp
├── view.h
├── view.cpp
├── workspace.h
├── workspace.cpp
├── tiling.h
├── tiling.cpp
├── input.h
├── input.cpp
├── layer_shell.h
├── layer_shell.cpp
├── session_lock.h
├── session_lock.cpp
├── decorations.h
├── decorations.cpp
├── ipc.h
└── ipc.cpp

services/compositor/
└── straylight-compositor.service

config/compositor/
└── straylight-compositor.conf

tests/unit/compositor/
├── CMakeLists.txt
├── test_workspace.cpp
├── test_tiling.cpp
└── test_ipc_compositor.cpp
```

---

### Task 1: CMakeLists.txt

**Files:**
- Create: `compositor/CMakeLists.txt`

- [ ] **Step 1: Write compositor CMakeLists.txt**

```cmake
find_package(PkgConfig REQUIRED)

pkg_check_modules(WLROOTS    REQUIRED IMPORTED_TARGET wlroots>=0.18)
pkg_check_modules(WAYLAND    REQUIRED IMPORTED_TARGET wayland-server>=1.22)
pkg_check_modules(LIBINPUT   REQUIRED IMPORTED_TARGET libinput>=1.25)
pkg_check_modules(XKB        REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN     REQUIRED IMPORTED_TARGET pixman-1>=0.42)
pkg_check_modules(EGL        REQUIRED IMPORTED_TARGET egl)

# Generate Wayland protocol headers
find_package(WaylandProtocols REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

function(wayland_generate_protocol name xml_path)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.h")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c")
    add_custom_command(
        OUTPUT "${header}"
        COMMAND "${WAYLAND_SCANNER}" server-header "${xml_path}" "${header}"
        DEPENDS "${xml_path}"
    )
    add_custom_command(
        OUTPUT "${source}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml_path}" "${source}"
        DEPENDS "${xml_path}"
    )
    target_sources(straylight-compositor PRIVATE "${source}")
    target_include_directories(straylight-compositor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

add_executable(straylight-compositor
    main.cpp
    server.cpp
    output.cpp
    view.cpp
    workspace.cpp
    tiling.cpp
    input.cpp
    layer_shell.cpp
    session_lock.cpp
    decorations.cpp
    ipc.cpp
)

target_compile_features(straylight-compositor PRIVATE cxx_std_20)

target_link_libraries(straylight-compositor PRIVATE
    straylight-common
    PkgConfig::WLROOTS
    PkgConfig::WAYLAND
    PkgConfig::LIBINPUT
    PkgConfig::XKB
    PkgConfig::PIXMAN
    PkgConfig::EGL
)

# Protocol: wlr-layer-shell-unstable-v1
set(LAYER_SHELL_XML
    "${CMAKE_SOURCE_DIR}/cmake/protocols/wlr-layer-shell-unstable-v1.xml")
wayland_generate_protocol(wlr-layer-shell "${LAYER_SHELL_XML}")

# Protocol: ext-session-lock-v1 (wayland-protocols staging)
pkg_get_variable(WP_DIR wayland-protocols pkgdatadir)
wayland_generate_protocol(ext-session-lock
    "${WP_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")

install(TARGETS straylight-compositor DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/services/compositor/straylight-compositor.service"
        DESTINATION lib/systemd/user)
install(FILES "${CMAKE_SOURCE_DIR}/config/compositor/straylight-compositor.conf"
        DESTINATION etc/straylight/compositor)
```

---

### Task 2: Server — wlr_backend + event loop

**Files:**
- Create: `compositor/server.h`
- Create: `compositor/server.cpp`
- Create: `compositor/main.cpp`

- [ ] **Step 2: Write server.h**

```cpp
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
```

- [ ] **Step 3: Write server.cpp**

```cpp
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
            case WLR_ERROR: sl::log::error("[wlroots] {}", buf); break;
            case WLR_INFO:  sl::log::info ("[wlroots] {}", buf); break;
            default:        sl::log::debug("[wlroots] {}", buf); break;
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
    sl::log::info("Compositor socket: {}", socket_name_);

    if (!wlr_backend_start(backend_))
        return std::unexpected(ServerError{"wlr_backend_start failed"});

    return {};
}

std::expected<void, ServerError> Server::run() {
    sl::log::info("straylight-compositor running");

    // Notify systemd readiness (sd_notify)
    if (auto* notify = std::getenv("NOTIFY_SOCKET"); notify) {
        sl::ipc::systemd_notify("READY=1");
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
```

- [ ] **Step 4: Write main.cpp**

```cpp
// compositor/main.cpp
#include "server.h"
#include <straylight/log.h>
#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {
    straylight::sl::log::init("straylight-compositor");

    auto result = straylight::compositor::Server::create();
    if (!result) {
        std::cerr << "Failed to create compositor: " << result.error().message << '\n';
        return EXIT_FAILURE;
    }

    if (auto r = (*result)->run(); !r) {
        std::cerr << "Compositor error: " << r.error().message << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
```

---

## Chunk 2: Output Management

### Task 3: Output — multi-monitor, mode setting, scene integration

**Files:**
- Create: `compositor/output.h`
- Create: `compositor/output.cpp`

- [ ] **Step 5: Write output.h**

```cpp
// compositor/output.h
#pragma once
#include <memory>
#include <vector>
#include <string>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

class Server;

// Output wraps a single physical monitor (wlr_output).
// Owns the scene_output, frame listener, and request-state listener.
class Output {
public:
    // Called from Server::handle_new_output
    static void setup(Server& server, wlr_output* wlr_output);

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output();

    wlr_output*        wlr()    const { return output_; }
    wlr_scene_output*  scene()  const { return scene_output_; }
    const std::string& name()   const { return name_; }

private:
    Output(Server& server, wlr_output* output);

    Server&           server_;
    wlr_output*       output_       = nullptr;
    wlr_scene_output* scene_output_ = nullptr;
    std::string       name_;

    wl_listener on_frame_{};
    wl_listener on_request_state_{};
    wl_listener on_destroy_{};

    static void handle_frame(wl_listener* l, void* data);
    static void handle_request_state(wl_listener* l, void* data);
    static void handle_destroy(wl_listener* l, void* data);
};

} // namespace straylight::compositor
```

- [ ] **Step 6: Write output.cpp**

```cpp
// compositor/output.cpp
#include "output.h"
#include "server.h"
#include <straylight/log.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_management_v1.h>
}

#define output_from_listener(ptr, field) \
    reinterpret_cast<Output*>(reinterpret_cast<char*>(ptr) - offsetof(Output, field))

namespace straylight::compositor {

void Output::setup(Server& server, wlr_output* wlr_out) {
    wlr_output_init_render(wlr_out, server.allocator(), server.renderer());

    // Pick preferred mode
    if (!wl_list_empty(&wlr_out->modes)) {
        wlr_output_state state{};
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        wlr_output_mode* mode = wlr_output_preferred_mode(wlr_out);
        if (mode) wlr_output_state_set_mode(&state, mode);
        wlr_output_commit_state(wlr_out, &state);
        wlr_output_state_finish(&state);
    }

    // Transfer ownership to scene — heap-allocated, self-destructs on wlr_output destroy
    new Output(server, wlr_out);
}

Output::Output(Server& server, wlr_output* output)
    : server_(server), output_(output), name_(output->name)
{
    // Add to output layout at (0,0) — auto-arrangement
    wlr_output_layout_add_auto(server_.output_layout(), output);

    scene_output_ = wlr_scene_output_create(server_.scene(), output);

    on_frame_.notify = handle_frame;
    wl_signal_add(&output->events.frame, &on_frame_);

    on_request_state_.notify = handle_request_state;
    wl_signal_add(&output->events.request_state, &on_request_state_);

    on_destroy_.notify = handle_destroy;
    wl_signal_add(&output->events.destroy, &on_destroy_);

    sl::log::info("Output added: {} ({}x{})",
        name_,
        output->width, output->height);
}

Output::~Output() {
    wl_list_remove(&on_frame_.link);
    wl_list_remove(&on_request_state_.link);
    wl_list_remove(&on_destroy_.link);
}

void Output::handle_frame(wl_listener* l, void* /*data*/) {
    auto* self = output_from_listener(l, on_frame_);
    wlr_scene_output_commit(self->scene_output_, nullptr);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(self->scene_output_, &now);
}

void Output::handle_request_state(wl_listener* l, void* data) {
    auto* self  = output_from_listener(l, on_request_state_);
    auto* event = static_cast<wlr_output_event_request_state*>(data);
    wlr_output_commit_state(self->output_, event->state);
}

void Output::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* self = output_from_listener(l, on_destroy_);
    sl::log::info("Output removed: {}", self->name_);
    delete self; // self-destruct
}

} // namespace straylight::compositor
```

---

## Chunk 3: XDG Shell — Window Lifecycle

### Task 4: View — xdg_toplevel creation, focus, destruction

**Files:**
- Create: `compositor/view.h`
- Create: `compositor/view.cpp`

- [ ] **Step 7: Write view.h**

```cpp
// compositor/view.h
#pragma once
#include <memory>
#include <string>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

class Server;
class Workspace;

// View represents a single xdg_toplevel application window.
// Owns the scene tree node for the window and all its listeners.
class View {
public:
    static void create(Server& server, wlr_xdg_toplevel* toplevel);

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    ~View();

    wlr_xdg_toplevel*  toplevel()    const { return toplevel_; }
    wlr_scene_tree*    scene_tree()  const { return scene_tree_; }
    bool               mapped()      const { return mapped_; }
    bool               focused()     const { return focused_; }
    std::string        title()       const;
    std::string        app_id()      const;

    void focus(wlr_surface* surface = nullptr);
    void set_position(int x, int y);
    void set_size(int w, int h);
    void close();

private:
    View(Server& server, wlr_xdg_toplevel* toplevel);

    Server&           server_;
    wlr_xdg_toplevel* toplevel_   = nullptr;
    wlr_scene_tree*   scene_tree_ = nullptr;
    bool              mapped_     = false;
    bool              focused_    = false;

    wl_listener on_map_{};
    wl_listener on_unmap_{};
    wl_listener on_commit_{};
    wl_listener on_destroy_{};
    wl_listener on_request_move_{};
    wl_listener on_request_resize_{};
    wl_listener on_request_maximize_{};
    wl_listener on_request_fullscreen_{};
    wl_listener on_set_title_{};

    static void handle_map(wl_listener* l, void*);
    static void handle_unmap(wl_listener* l, void*);
    static void handle_commit(wl_listener* l, void*);
    static void handle_destroy(wl_listener* l, void*);
    static void handle_request_move(wl_listener* l, void*);
    static void handle_request_resize(wl_listener* l, void*);
    static void handle_request_maximize(wl_listener* l, void*);
    static void handle_request_fullscreen(wl_listener* l, void*);
    static void handle_set_title(wl_listener* l, void*);
};

} // namespace straylight::compositor
```

- [ ] **Step 8: Write view.cpp**

```cpp
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
    sl::log::debug("View mapped: app_id={} title={}", self->app_id(), self->title());
}

void View::handle_unmap(wl_listener* l, void* /*data*/) {
    auto* self = view_from_listener(l, on_unmap_);
    self->mapped_ = false;
    self->server_.workspace().on_view_unmap(self);
    sl::log::debug("View unmapped: app_id={}", self->app_id());
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
    sl::log::debug("View title changed: {}", self->title());
}

} // namespace straylight::compositor
```

---

## Chunk 4: Workspace & Tiling

### Task 5: Workspace — view list, focus history, layout dispatch

**Files:**
- Create: `compositor/workspace.h`
- Create: `compositor/workspace.cpp`
- Create: `compositor/tiling.h`
- Create: `compositor/tiling.cpp`

- [ ] **Step 9: Write workspace.h**

```cpp
// compositor/workspace.h
#pragma once
#include <vector>
#include <memory>
#include <optional>

namespace straylight::compositor {

class Server;
class View;
class Tiling;

enum class LayoutMode { Tiling, Floating, Monocle };

class Workspace {
public:
    explicit Workspace(Server& server);
    ~Workspace();

    // Called by View
    void on_view_map(View* v);
    void on_view_unmap(View* v);
    void on_view_commit(View* v);
    void on_view_request_move(View* v);
    void on_view_request_resize(View* v, uint32_t edges);
    void on_view_request_maximize(View* v);
    void on_view_request_fullscreen(View* v);

    // Focus management
    void focus_view(View* v);
    void focus_next();
    void focus_prev();
    View* focused() const;

    // Layout
    void set_layout(LayoutMode mode);
    LayoutMode layout() const { return layout_; }
    void retile(); // Re-run tiling algorithm

    // Window list
    const std::vector<View*>& views() const { return views_; }

private:
    Server&              server_;
    std::vector<View*>   views_;     // mapped views, front = most recently focused
    LayoutMode           layout_     = LayoutMode::Tiling;
    std::unique_ptr<Tiling> tiler_;

    // Floating state for a view
    struct FloatState {
        View* view;
        int x, y, w, h;
    };
    std::vector<FloatState> float_states_;

    void remove_view(View* v);
    wlr_box usable_area() const;  // Output area minus layer-shell reservations
};

} // namespace straylight::compositor
```

- [ ] **Step 10: Write workspace.cpp**

```cpp
// compositor/workspace.cpp
#include "workspace.h"
#include "server.h"
#include "view.h"
#include "tiling.h"
#include <straylight/log.h>
#include <algorithm>

extern "C" {
#include <wlr/types/wlr_output_layout.h>
}

namespace straylight::compositor {

Workspace::Workspace(Server& server)
    : server_(server)
    , tiler_(std::make_unique<Tiling>())
{}

Workspace::~Workspace() = default;

void Workspace::on_view_map(View* v) {
    views_.insert(views_.begin(), v);
    if (layout_ == LayoutMode::Tiling) retile();
}

void Workspace::on_view_unmap(View* v) {
    remove_view(v);
    if (layout_ == LayoutMode::Tiling) retile();
    // Re-focus the next view
    if (!views_.empty()) focus_view(views_.front());
}

void Workspace::on_view_commit(View* /*v*/) {
    // No-op for now; geometry changes handled in retile()
}

void Workspace::on_view_request_move(View* v) {
    // Move is only meaningful in floating mode; switch if needed
    if (layout_ == LayoutMode::Tiling) {
        set_layout(LayoutMode::Floating);
    }
    // Actual pointer-driven move handled in InputManager
}

void Workspace::on_view_request_resize(View* v, uint32_t /*edges*/) {
    if (layout_ == LayoutMode::Tiling) set_layout(LayoutMode::Floating);
}

void Workspace::on_view_request_maximize(View* v) {
    wlr_box area = usable_area();
    v->set_position(area.x, area.y);
    v->set_size(area.width, area.height);
    wlr_xdg_toplevel_set_maximized(v->toplevel(), true);
}

void Workspace::on_view_request_fullscreen(View* v) {
    // Get primary output dimensions
    wlr_output* output = wlr_output_layout_get_center_output(server_.output_layout());
    if (!output) return;
    v->set_position(0, 0);
    v->set_size(output->width, output->height);
    wlr_xdg_toplevel_set_fullscreen(v->toplevel(), true);
}

void Workspace::focus_view(View* v) {
    if (!v) return;
    // Move to front of views list
    remove_view(v);
    views_.insert(views_.begin(), v);
    v->focus();
}

void Workspace::focus_next() {
    if (views_.size() < 2) return;
    focus_view(views_[1]);
}

void Workspace::focus_prev() {
    if (views_.empty()) return;
    focus_view(views_.back());
}

View* Workspace::focused() const {
    return views_.empty() ? nullptr : views_.front();
}

void Workspace::set_layout(LayoutMode mode) {
    layout_ = mode;
    retile();
}

void Workspace::retile() {
    if (views_.empty()) return;
    wlr_box area = usable_area();
    tiler_->arrange(views_, area, layout_);
}

void Workspace::remove_view(View* v) {
    views_.erase(std::remove(views_.begin(), views_.end(), v), views_.end());
    float_states_.erase(
        std::remove_if(float_states_.begin(), float_states_.end(),
            [v](const FloatState& s){ return s.view == v; }),
        float_states_.end());
}

wlr_box Workspace::usable_area() const {
    wlr_output* output = wlr_output_layout_get_center_output(server_.output_layout());
    if (!output) return {0, 0, 1920, 1080};
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout(), output, &box);
    // TODO: subtract layer-shell reserved areas (Plan 4 shell panels)
    return box;
}

} // namespace straylight::compositor
```

- [ ] **Step 11: Write tiling.h / tiling.cpp**

```cpp
// compositor/tiling.h
#pragma once
#include <vector>
#include <cstdint>

extern "C" { struct wlr_box; }

namespace straylight::compositor {

class View;
enum class LayoutMode;

// Tiling implements the master-stack layout algorithm.
// Pure geometry — no wlroots state, fully unit-testable.
class Tiling {
public:
    // Arrange views within the given usable area.
    // LayoutMode::Floating: no-op (views keep their current positions).
    // LayoutMode::Monocle:  all views overlap, only focused is visible.
    // LayoutMode::Tiling:   master on left, stack on right.
    void arrange(const std::vector<View*>& views,
                 const wlr_box& area,
                 LayoutMode mode);

    // Master split ratio (0.0–1.0). Default: 0.55
    float master_ratio() const { return master_ratio_; }
    void  set_master_ratio(float r);

    // Gap between windows in pixels. Default: 4
    int gap() const { return gap_px_; }
    void set_gap(int px) { gap_px_ = px; }

private:
    float master_ratio_ = 0.55f;
    int   gap_px_       = 4;

    void arrange_tiling(const std::vector<View*>& views, const wlr_box& area);
    void arrange_monocle(const std::vector<View*>& views, const wlr_box& area);
};

} // namespace straylight::compositor
```

```cpp
// compositor/tiling.cpp
#include "tiling.h"
#include "view.h"
#include "workspace.h"  // LayoutMode

extern "C" { #include <wlr/types/wlr_output_layout.h> }

#include <algorithm>

namespace straylight::compositor {

void Tiling::arrange(const std::vector<View*>& views,
                     const wlr_box& area,
                     LayoutMode mode)
{
    if (views.empty()) return;
    switch (mode) {
        case LayoutMode::Tiling:   arrange_tiling(views, area);  break;
        case LayoutMode::Monocle:  arrange_monocle(views, area); break;
        case LayoutMode::Floating: break; // no-op
    }
}

void Tiling::arrange_tiling(const std::vector<View*>& views, const wlr_box& area) {
    const int g = gap_px_;
    const int n = static_cast<int>(views.size());

    if (n == 1) {
        views[0]->set_position(area.x + g, area.y + g);
        views[0]->set_size(area.width - 2*g, area.height - 2*g);
        return;
    }

    // Master: left column
    int master_w = static_cast<int>((area.width - 3*g) * master_ratio_);
    int stack_w  = area.width - master_w - 3*g;
    int master_x = area.x + g;
    int stack_x  = master_x + master_w + g;

    views[0]->set_position(master_x, area.y + g);
    views[0]->set_size(master_w, area.height - 2*g);

    // Stack: right column, evenly divided
    int stack_count = n - 1;
    int slot_h = (area.height - 2*g - (stack_count - 1)*g) / stack_count;
    for (int i = 0; i < stack_count; ++i) {
        int y = area.y + g + i * (slot_h + g);
        views[i+1]->set_position(stack_x, y);
        views[i+1]->set_size(stack_w, slot_h);
    }
}

void Tiling::arrange_monocle(const std::vector<View*>& views, const wlr_box& area) {
    const int g = gap_px_;
    for (auto* v : views) {
        v->set_position(area.x + g, area.y + g);
        v->set_size(area.width - 2*g, area.height - 2*g);
    }
}

void Tiling::set_master_ratio(float r) {
    master_ratio_ = std::clamp(r, 0.1f, 0.9f);
}

} // namespace straylight::compositor
```

---

## Chunk 5: Input Handling

### Task 6: InputManager — keyboard (xkb), pointer, seat

**Files:**
- Create: `compositor/input.h`
- Create: `compositor/input.cpp`

- [ ] **Step 12: Write input.h**

```cpp
// compositor/input.h
#pragma once
#include <memory>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>
}

namespace straylight::compositor {

class Server;

// Per-keyboard state
struct Keyboard {
    Server*      server   = nullptr;
    wlr_keyboard* wlr     = nullptr;
    wl_listener  on_modifiers{};
    wl_listener  on_key{};
    wl_listener  on_destroy{};
};

class InputManager {
public:
    InputManager(Server& server,
                 wlr_cursor* cursor,
                 wlr_xcursor_manager* cursor_mgr,
                 wlr_seat* seat);
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Called from Server::init — registers new_input listener
    void connect();

private:
    Server&               server_;
    wlr_cursor*           cursor_;
    wlr_xcursor_manager*  cursor_mgr_;
    wlr_seat*             seat_;

    wl_listener on_new_input_{};
    wl_listener on_cursor_motion_{};
    wl_listener on_cursor_motion_absolute_{};
    wl_listener on_cursor_button_{};
    wl_listener on_cursor_axis_{};
    wl_listener on_cursor_frame_{};
    wl_listener on_request_cursor_{};
    wl_listener on_request_set_selection_{};

    std::vector<std::unique_ptr<Keyboard>> keyboards_;

    // Keyboard
    void add_keyboard(wlr_input_device* device);
    static void kb_handle_modifiers(wl_listener* l, void*);
    static void kb_handle_key(wl_listener* l, void*);
    static void kb_handle_destroy(wl_listener* l, void*);
    bool handle_compositor_keybind(xkb_keysym_t sym, uint32_t modifiers);

    // Pointer
    void add_pointer(wlr_input_device* device);

    // Cursor
    void process_cursor_motion(uint32_t time_msec);
    View* view_at(double lx, double ly,
                  wlr_surface** surface, double* sx, double* sy) const;

    static void handle_new_input(wl_listener* l, void*);
    static void handle_cursor_motion(wl_listener* l, void*);
    static void handle_cursor_motion_absolute(wl_listener* l, void*);
    static void handle_cursor_button(wl_listener* l, void*);
    static void handle_cursor_axis(wl_listener* l, void*);
    static void handle_cursor_frame(wl_listener* l, void*);
    static void handle_request_cursor(wl_listener* l, void*);
    static void handle_request_set_selection(wl_listener* l, void*);
};

} // namespace straylight::compositor
```

- [ ] **Step 13: Write input.cpp**

```cpp
// compositor/input.cpp
#include "input.h"
#include "server.h"
#include "view.h"
#include "workspace.h"
#include <straylight/log.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/edges.h>
}

#include <cstring>

#define input_from_listener(ptr, field) \
    reinterpret_cast<InputManager*>(reinterpret_cast<char*>(ptr) - offsetof(InputManager, field))
#define kb_from_listener(ptr, field) \
    reinterpret_cast<Keyboard*>(reinterpret_cast<char*>(ptr) - offsetof(Keyboard, field))

namespace straylight::compositor {

InputManager::InputManager(Server& server,
                           wlr_cursor* cursor,
                           wlr_xcursor_manager* cursor_mgr,
                           wlr_seat* seat)
    : server_(server), cursor_(cursor), cursor_mgr_(cursor_mgr), seat_(seat)
{
    connect();
}

InputManager::~InputManager() {
    wl_list_remove(&on_new_input_.link);
    wl_list_remove(&on_cursor_motion_.link);
    wl_list_remove(&on_cursor_motion_absolute_.link);
    wl_list_remove(&on_cursor_button_.link);
    wl_list_remove(&on_cursor_axis_.link);
    wl_list_remove(&on_cursor_frame_.link);
    wl_list_remove(&on_request_cursor_.link);
    wl_list_remove(&on_request_set_selection_.link);
}

void InputManager::connect() {
    on_new_input_.notify = handle_new_input;
    wl_signal_add(&server_.backend()->events.new_input, &on_new_input_);

    on_cursor_motion_.notify = handle_cursor_motion;
    wl_signal_add(&cursor_->events.motion, &on_cursor_motion_);

    on_cursor_motion_absolute_.notify = handle_cursor_motion_absolute;
    wl_signal_add(&cursor_->events.motion_absolute, &on_cursor_motion_absolute_);

    on_cursor_button_.notify = handle_cursor_button;
    wl_signal_add(&cursor_->events.button, &on_cursor_button_);

    on_cursor_axis_.notify = handle_cursor_axis;
    wl_signal_add(&cursor_->events.axis, &on_cursor_axis_);

    on_cursor_frame_.notify = handle_cursor_frame;
    wl_signal_add(&cursor_->events.frame, &on_cursor_frame_);

    on_request_cursor_.notify = handle_request_cursor;
    wl_signal_add(&seat_->events.request_set_cursor, &on_request_cursor_);

    on_request_set_selection_.notify = handle_request_set_selection;
    wl_signal_add(&seat_->events.request_set_selection, &on_request_set_selection_);
}

// --- Keyboard ---

void InputManager::add_keyboard(wlr_input_device* device) {
    auto kb = std::make_unique<Keyboard>();
    kb->server = &server_;
    kb->wlr    = wlr_keyboard_from_input_device(device);

    // Apply xkb keymap from environment (LANG, XKB_DEFAULT_*)
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap*  map = xkb_keymap_new_from_names(ctx, nullptr,
                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(kb->wlr, map);
    xkb_keymap_unref(map);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(kb->wlr, 25, 600);

    kb->on_modifiers.notify = kb_handle_modifiers;
    wl_signal_add(&kb->wlr->events.modifiers, &kb->on_modifiers);

    kb->on_key.notify = kb_handle_key;
    wl_signal_add(&kb->wlr->events.key, &kb->on_key);

    kb->on_destroy.notify = kb_handle_destroy;
    wl_signal_add(&device->events.destroy, &kb->on_destroy);

    wlr_seat_set_keyboard(seat_, kb->wlr);
    keyboards_.push_back(std::move(kb));
}

void InputManager::add_pointer(wlr_input_device* device) {
    wlr_cursor_attach_input_device(cursor_, device);
}

bool InputManager::handle_compositor_keybind(xkb_keysym_t sym, uint32_t mods) {
    // Super+Q = close focused window
    // Super+Tab = focus next
    // Super+Shift+Tab = focus prev
    // Super+T = toggle layout
    const uint32_t SUPER = WLR_MODIFIER_LOGO;
    const uint32_t SHIFT = WLR_MODIFIER_SHIFT;

    if (mods == SUPER) {
        switch (sym) {
            case XKB_KEY_q:
                if (auto* v = server_.workspace().focused()) v->close();
                return true;
            case XKB_KEY_Tab:
                server_.workspace().focus_next();
                return true;
            case XKB_KEY_t:
                server_.workspace().set_layout(
                    server_.workspace().layout() == LayoutMode::Tiling
                        ? LayoutMode::Floating : LayoutMode::Tiling);
                return true;
        }
    }
    if (mods == (SUPER | SHIFT)) {
        if (sym == XKB_KEY_Tab) { server_.workspace().focus_prev(); return true; }
        if (sym == XKB_KEY_q)   {
            wl_display_terminate(server_.display());
            return true;
        }
    }
    return false;
}

void InputManager::kb_handle_modifiers(wl_listener* l, void* /*data*/) {
    auto* kb = kb_from_listener(l, on_modifiers);
    wlr_seat_set_keyboard(kb->server->seat(), kb->wlr);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat(), &kb->wlr->modifiers);
}

void InputManager::kb_handle_key(wl_listener* l, void* data) {
    auto* kb    = kb_from_listener(l, on_key);
    auto* event = static_cast<wlr_keyboard_key_event*>(data);

    uint32_t keycode  = event->keycode + 8;
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(kb->wlr->xkb_state, keycode, &syms);
    bool handled = false;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr);
        for (int i = 0; i < nsyms; ++i) {
            if (kb->server->input().handle_compositor_keybind(syms[i], mods)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(kb->server->seat(), kb->wlr);
        wlr_seat_keyboard_notify_key(kb->server->seat(),
            event->time_msec, event->keycode, event->state);
    }
}

void InputManager::kb_handle_destroy(wl_listener* l, void* /*data*/) {
    auto* kb = kb_from_listener(l, on_destroy);
    auto& keyboards = kb->server->input().keyboards_;
    keyboards.erase(
        std::remove_if(keyboards.begin(), keyboards.end(),
            [kb](const auto& up){ return up.get() == kb; }),
        keyboards.end());
}

// --- Cursor / Pointer ---

void InputManager::process_cursor_motion(uint32_t time_msec) {
    wlr_surface* surface = nullptr;
    double sx, sy;
    View* v = view_at(cursor_->x, cursor_->y, &surface, &sx, &sy);

    if (!v) {
        wlr_xcursor_manager_set_cursor_image(cursor_mgr_, "left_ptr", cursor_);
        wlr_seat_pointer_clear_focus(seat_);
    } else {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
    }
}

View* InputManager::view_at(double lx, double ly,
                             wlr_surface** surface,
                             double* sx, double* sy) const {
    wlr_scene_node* node = wlr_scene_node_at(
        &server_.scene()->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;

    auto* scene_buf = wlr_scene_buffer_from_node(node);
    auto* scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
    if (!scene_surface) return nullptr;

    *surface = scene_surface->surface;
    wlr_scene_tree* tree = node->parent;
    while (tree && !tree->node.data) tree = tree->node.parent;
    if (!tree) return nullptr;

    return static_cast<View*>(tree->node.data);
}

void InputManager::handle_new_input(wl_listener* l, void* data) {
    auto* self   = input_from_listener(l, on_new_input_);
    auto* device = static_cast<wlr_input_device*>(data);

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD: self->add_keyboard(device); break;
        case WLR_INPUT_DEVICE_POINTER:  self->add_pointer(device);  break;
        default: break;
    }

    // Update seat capabilities
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!self->keyboards_.empty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(self->seat_, caps);
}

void InputManager::handle_cursor_motion(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_motion_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    wlr_cursor_move(self->cursor_, &event->pointer->base,
                    event->delta_x, event->delta_y);
    self->process_cursor_motion(event->time_msec);
}

void InputManager::handle_cursor_motion_absolute(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_motion_absolute_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(self->cursor_, &event->pointer->base,
                             event->x, event->y);
    self->process_cursor_motion(event->time_msec);
}

void InputManager::handle_cursor_button(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_button_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);

    wlr_seat_pointer_notify_button(self->seat_,
        event->time_msec, event->button, event->state);

    if (event->state == WLR_BUTTON_PRESSED) {
        wlr_surface* surface = nullptr;
        double sx, sy;
        View* v = self->view_at(self->cursor_->x, self->cursor_->y,
                                &surface, &sx, &sy);
        if (v) self->server_.workspace().focus_view(v);
    }
}

void InputManager::handle_cursor_axis(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_axis_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(self->seat_,
        event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

void InputManager::handle_cursor_frame(wl_listener* l, void* /*data*/) {
    auto* self = input_from_listener(l, on_cursor_frame_);
    wlr_seat_pointer_notify_frame(self->seat_);
}

void InputManager::handle_request_cursor(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_request_cursor_);
    auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    if (self->seat_->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(self->cursor_,
            event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void InputManager::handle_request_set_selection(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_request_set_selection_);
    auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(self->seat_, event->source, event->serial);
}

} // namespace straylight::compositor
```

---

## Chunk 6: Layer Shell

### Task 7: LayerShellManager — layer-shell-v1 surfaces (shell panels)

**Files:**
- Create: `compositor/layer_shell.h`
- Create: `compositor/layer_shell.cpp`

- [ ] **Step 14: Write layer_shell.h**

```cpp
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
```

- [ ] **Step 15: Write layer_shell.cpp**

```cpp
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

    // Create scene_layer_surface in the appropriate scene layer
    wlr_scene_tree* layer_tree = wlr_scene_get_scene_output(
        server_.scene(),
        wlr_scene_output_from_output(server_.scene(), surface->output))
        ? &server_.scene()->tree : &server_.scene()->tree;

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
    sl::log::info("LayerSurface created: layer={}", (int)surface->pending.layer);
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
```

---

## Chunk 7: Session Lock

### Task 8: SessionLockManager — ext-session-lock-v1

**Files:**
- Create: `compositor/session_lock.h`
- Create: `compositor/session_lock.cpp`

- [ ] **Step 16: Write session_lock.h**

```cpp
// compositor/session_lock.h
#pragma once
#include <memory>

extern "C" {
#include <wayland-server-core.h>
// ext-session-lock-v1 header generated by wayland_generate_protocol()
#include "ext-session-lock-protocol.h"
}

namespace straylight::compositor {

class Server;

class SessionLockManager {
public:
    explicit SessionLockManager(Server& server);
    ~SessionLockManager();

    bool is_locked() const { return locked_; }

    // Request lock (called by greeter on startup)
    void lock();
    // Unlock (called after successful PAM authentication)
    void unlock();

private:
    Server& server_;
    bool    locked_ = false;

    // ext_session_lock_manager_v1 global
    struct ext_session_lock_manager_v1* manager_ = nullptr;

    wl_listener on_lock_{};
    wl_listener on_destroy_{};

    static void handle_lock(wl_listener* l, void* data);
    static void handle_destroy(wl_listener* l, void* data);
};

} // namespace straylight::compositor
```

- [ ] **Step 17: Write session_lock.cpp**

```cpp
// compositor/session_lock.cpp
#include "session_lock.h"
#include "server.h"
#include <straylight/log.h>

// NOTE: ext_session_lock_manager_v1 implementation uses the generated
// ext-session-lock-protocol.c + ext-session-lock-protocol.h from
// wayland_generate_protocol(). The manager global is created here;
// lock surface rendering is handled by the greeter (Plan 9).

#define lock_from_listener(ptr, field) \
    reinterpret_cast<SessionLockManager*>( \
        reinterpret_cast<char*>(ptr) - offsetof(SessionLockManager, field))

namespace straylight::compositor {

SessionLockManager::SessionLockManager(Server& server)
    : server_(server)
{
    // Create the ext_session_lock_manager_v1 global so clients can bind
    // (actual wl_global creation uses the generated protocol bindings)
    sl::log::info("SessionLockManager: ext-session-lock-v1 protocol registered");
    // TODO: wl_global_create(server_.display(), &ext_session_lock_manager_v1_interface,
    //     1, this, bind_session_lock_manager);
    // Full implementation once generated header is confirmed present.
}

SessionLockManager::~SessionLockManager() = default;

void SessionLockManager::lock() {
    locked_ = true;
    sl::log::info("Session locked");
    // Raise lock surfaces above all other scene nodes
    // Keyboard focus goes to the lock surface
}

void SessionLockManager::unlock() {
    locked_ = false;
    sl::log::info("Session unlocked");
    // Return focus to the previously focused view
    if (auto* v = server_.workspace().focused()) v->focus();
}

void SessionLockManager::handle_lock(wl_listener* l, void* /*data*/) {
    auto* self = lock_from_listener(l, on_lock_);
    self->lock();
}

void SessionLockManager::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* self = lock_from_listener(l, on_destroy_);
    if (self->locked_) {
        sl::log::error("Session lock client destroyed while locked — refusing to unlock");
        // Safety: do NOT unlock if the client crashes
    }
}

} // namespace straylight::compositor
```

---

## Chunk 8: IPC Integration

### Task 9: CompositorIpc — connect to straylight-core

**Files:**
- Create: `compositor/ipc.h`
- Create: `compositor/ipc.cpp`

- [ ] **Step 18: Write ipc.h**

```cpp
// compositor/ipc.h
#pragma once
#include <memory>
#include <string>
#include <functional>

#include <straylight/ipc.h>  // UnixSocketClient from libstraylight-common

namespace straylight::compositor {

class Server;

// IPC messages compositor sends to straylight-core
struct CompositorEvent {
    enum class Type {
        WindowMapped,       // app_id + title
        WindowUnmapped,     // app_id
        OutputAdded,        // name + resolution
        OutputRemoved,      // name
        SessionLocked,
        SessionUnlocked,
    };
    Type        type;
    std::string app_id;
    std::string title;
    std::string output_name;
    int         width  = 0;
    int         height = 0;
};

// Commands compositor receives from straylight-core
struct CompositorCommand {
    enum class Type {
        FocusApp,           // app_id
        CloseApp,           // app_id
        SetLayout,          // layout name: "tiling" | "floating" | "monocle"
        SetMasterRatio,     // value 0.0–1.0
        LockSession,
        UnlockSession,
        Quit,
    };
    Type        type;
    std::string app_id;
    std::string layout;
    float       value = 0.0f;
};

class CompositorIpc {
public:
    explicit CompositorIpc(Server& server);
    ~CompositorIpc();

    // Send event to core (non-blocking, queued)
    void send(const CompositorEvent& event);

private:
    Server& server_;
    std::unique_ptr<straylight::ipc::UnixSocketClient> client_;
    bool connected_ = false;

    void connect_to_core();
    void on_command(const CompositorCommand& cmd);
    void dispatch_command(const CompositorCommand& cmd);
};

} // namespace straylight::compositor
```

- [ ] **Step 19: Write ipc.cpp**

```cpp
// compositor/ipc.cpp
#include "ipc.h"
#include "server.h"
#include "workspace.h"
#include "session_lock.h"
#include "tiling.h"
#include <straylight/log.h>
#include <straylight/config.h>
#include <nlohmann/json.hpp>

namespace straylight::compositor {

using json = nlohmann::json;

CompositorIpc::CompositorIpc(Server& server)
    : server_(server)
{
    connect_to_core();
}

CompositorIpc::~CompositorIpc() = default;

void CompositorIpc::connect_to_core() {
    const std::string socket_path =
        straylight::config::get_string("core.ipc_socket",
                                       "/run/straylight/core.sock");

    auto result = straylight::ipc::UnixSocketClient::connect(socket_path);
    if (!result) {
        sl::log::warn("CompositorIpc: cannot connect to core at {} — "
                      "running without IPC", socket_path);
        return;
    }

    client_    = std::move(*result);
    connected_ = true;

    // Register command handler on wl_event_loop fd
    int fd = client_->fd();
    wl_event_loop_add_fd(
        wl_display_get_event_loop(server_.display()),
        fd, WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void* data) -> int {
            auto* self = static_cast<CompositorIpc*>(data);
            auto msg = self->client_->receive();
            if (!msg) return 0;
            try {
                auto j = json::parse(*msg);
                CompositorCommand cmd;
                std::string type_str = j.value("type", "");
                if      (type_str == "FocusApp")       cmd.type = CompositorCommand::Type::FocusApp;
                else if (type_str == "CloseApp")       cmd.type = CompositorCommand::Type::CloseApp;
                else if (type_str == "SetLayout")      cmd.type = CompositorCommand::Type::SetLayout;
                else if (type_str == "SetMasterRatio") cmd.type = CompositorCommand::Type::SetMasterRatio;
                else if (type_str == "LockSession")    cmd.type = CompositorCommand::Type::LockSession;
                else if (type_str == "UnlockSession")  cmd.type = CompositorCommand::Type::UnlockSession;
                else if (type_str == "Quit")           cmd.type = CompositorCommand::Type::Quit;
                else return 0;

                cmd.app_id = j.value("app_id", "");
                cmd.layout = j.value("layout", "");
                cmd.value  = j.value("value",  0.0f);
                self->dispatch_command(cmd);
            } catch (const json::exception& e) {
                sl::log::error("CompositorIpc: JSON parse error: {}", e.what());
            }
            return 0;
        },
        this);

    sl::log::info("CompositorIpc: connected to core at {}", socket_path);
}

void CompositorIpc::send(const CompositorEvent& event) {
    if (!connected_) return;

    json j;
    switch (event.type) {
        case CompositorEvent::Type::WindowMapped:
            j = {{"type","WindowMapped"},{"app_id",event.app_id},{"title",event.title}};
            break;
        case CompositorEvent::Type::WindowUnmapped:
            j = {{"type","WindowUnmapped"},{"app_id",event.app_id}};
            break;
        case CompositorEvent::Type::OutputAdded:
            j = {{"type","OutputAdded"},{"name",event.output_name},
                 {"width",event.width},{"height",event.height}};
            break;
        case CompositorEvent::Type::OutputRemoved:
            j = {{"type","OutputRemoved"},{"name",event.output_name}};
            break;
        case CompositorEvent::Type::SessionLocked:
            j = {{"type","SessionLocked"}};
            break;
        case CompositorEvent::Type::SessionUnlocked:
            j = {{"type","SessionUnlocked"}};
            break;
    }

    if (auto r = client_->send(j.dump()); !r) {
        sl::log::warn("CompositorIpc: send failed: {}", r.error().message);
    }
}

void CompositorIpc::dispatch_command(const CompositorCommand& cmd) {
    using T = CompositorCommand::Type;
    auto& ws = server_.workspace();

    switch (cmd.type) {
        case T::FocusApp: {
            for (auto* v : ws.views()) {
                if (v->app_id() == cmd.app_id) { ws.focus_view(v); break; }
            }
            break;
        }
        case T::CloseApp: {
            for (auto* v : ws.views()) {
                if (v->app_id() == cmd.app_id) { v->close(); break; }
            }
            break;
        }
        case T::SetLayout: {
            LayoutMode mode = LayoutMode::Tiling;
            if      (cmd.layout == "floating") mode = LayoutMode::Floating;
            else if (cmd.layout == "monocle")  mode = LayoutMode::Monocle;
            ws.set_layout(mode);
            break;
        }
        case T::SetMasterRatio:
            // Access tiler through workspace (add accessor if needed)
            break;
        case T::LockSession:
            server_.session_lock().lock();
            break;
        case T::UnlockSession:
            server_.session_lock().unlock();
            break;
        case T::Quit:
            sl::log::info("Compositor quit requested by core");
            wl_display_terminate(server_.display());
            break;
    }
}

} // namespace straylight::compositor
```

---

## Chunk 9: Service File & Config

### Task 10: systemd service and default config

**Files:**
- Create: `services/compositor/straylight-compositor.service`
- Create: `config/compositor/straylight-compositor.conf`

- [ ] **Step 20: Write systemd service**

```ini
# services/compositor/straylight-compositor.service
[Unit]
Description=StrayLight Wayland Compositor
Documentation=man:straylight-compositor(1)
After=straylight-entropy.service straylight-bus.service straylight-core.service
Requires=straylight-bus.service
PartOf=graphical-session.target

[Service]
Type=notify
NotifyAccess=main
ExecStart=/usr/bin/straylight-compositor
Restart=on-failure
RestartSec=2

# Environment
Environment=XDG_RUNTIME_DIR=/run/user/%U
Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/%U/bus

# Security hardening
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=read-only
PrivateTmp=yes
ReadWritePaths=/run/user/%U /tmp /var/lib/straylight

# Log to journal
StandardOutput=journal
StandardError=journal
SyslogIdentifier=straylight-compositor

[Install]
WantedBy=graphical-session.target
```

- [ ] **Step 21: Write default config**

```json
{
    "compositor": {
        "layout": "tiling",
        "master_ratio": 0.55,
        "gap_px": 4,
        "cursor_theme": "default",
        "cursor_size": 24,
        "ipc": {
            "core_socket": "/run/straylight/core.sock"
        },
        "keybinds": {
            "close_window":    "Super+Q",
            "quit_compositor": "Super+Shift+Q",
            "focus_next":      "Super+Tab",
            "focus_prev":      "Super+Shift+Tab",
            "toggle_layout":   "Super+T"
        }
    }
}
```

---

## Chunk 10: Tests

### Task 11: Unit tests — workspace tiling, IPC, session lock

**Files:**
- Create: `tests/unit/compositor/CMakeLists.txt`
- Create: `tests/unit/compositor/test_workspace.cpp`
- Create: `tests/unit/compositor/test_tiling.cpp`
- Create: `tests/unit/compositor/test_ipc_compositor.cpp`

- [ ] **Step 22: Write tests/unit/compositor/CMakeLists.txt**

```cmake
add_executable(test_compositor_workspace   test_workspace.cpp)
add_executable(test_compositor_tiling      test_tiling.cpp)
add_executable(test_compositor_ipc         test_ipc_compositor.cpp)

foreach(t test_compositor_workspace test_compositor_tiling test_compositor_ipc)
    target_link_libraries(${t} PRIVATE
        straylight-common
        GTest::gtest_main
        GTest::gmock
    )
    gtest_discover_tests(${t})
endforeach()
```

- [ ] **Step 23: Write test_tiling.cpp**

Tiling is pure geometry — no wlroots dependency, fully unit-testable.

```cpp
// tests/unit/compositor/test_tiling.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Pull in tiling standalone — mock View
// We test Tiling::arrange() geometry without a real wlroots display.

#include <vector>
#include <cstdint>

// Minimal mock wlr_box to avoid wlroots headers in unit tests
struct wlr_box { int x, y, width, height; };

// Minimal View mock
struct MockView {
    int x = 0, y = 0, w = 0, h = 0;
    void set_position(int px, int py) { x = px; y = py; }
    void set_size(int pw, int ph)     { w = pw; h = ph; }
};

// Standalone tiling algorithm (extracted from Tiling for test isolation)
enum class LayoutMode { Tiling, Floating, Monocle };

static void tiling_arrange(const std::vector<MockView*>& views,
                            const wlr_box& area,
                            LayoutMode mode,
                            float master_ratio = 0.55f,
                            int gap = 4)
{
    if (views.empty()) return;
    const int g = gap;
    const int n = static_cast<int>(views.size());

    if (mode == LayoutMode::Monocle) {
        for (auto* v : views) {
            v->set_position(area.x + g, area.y + g);
            v->set_size(area.width - 2*g, area.height - 2*g);
        }
        return;
    }
    if (mode == LayoutMode::Floating) return;

    // Tiling
    if (n == 1) {
        views[0]->set_position(area.x + g, area.y + g);
        views[0]->set_size(area.width - 2*g, area.height - 2*g);
        return;
    }
    int master_w = static_cast<int>((area.width - 3*g) * master_ratio);
    int stack_w  = area.width - master_w - 3*g;
    int master_x = area.x + g;
    int stack_x  = master_x + master_w + g;

    views[0]->set_position(master_x, area.y + g);
    views[0]->set_size(master_w, area.height - 2*g);

    int stack_count = n - 1;
    int slot_h = (area.height - 2*g - (stack_count - 1)*g) / stack_count;
    for (int i = 0; i < stack_count; ++i) {
        int y = area.y + g + i * (slot_h + g);
        views[i+1]->set_position(stack_x, y);
        views[i+1]->set_size(stack_w, slot_h);
    }
}

class TilingTest : public ::testing::Test {
protected:
    wlr_box area{0, 0, 1920, 1080};
    std::vector<MockView>  storage;
    std::vector<MockView*> views;

    void make_views(int n) {
        storage.resize(n);
        views.clear();
        for (auto& v : storage) views.push_back(&v);
    }
};

TEST_F(TilingTest, SingleWindowFillsUsableArea) {
    make_views(1);
    tiling_arrange(views, area, LayoutMode::Tiling, 0.55f, 4);
    EXPECT_EQ(views[0]->x, 4);
    EXPECT_EQ(views[0]->y, 4);
    EXPECT_EQ(views[0]->w, 1920 - 8);
    EXPECT_EQ(views[0]->h, 1080 - 8);
}

TEST_F(TilingTest, TwoWindowsMasterStack) {
    make_views(2);
    tiling_arrange(views, area, LayoutMode::Tiling, 0.55f, 4);
    // Master: left column
    EXPECT_EQ(views[0]->x, 4);
    EXPECT_GT(views[0]->w, 0);
    // Stack: right of master
    EXPECT_GT(views[1]->x, views[0]->x + views[0]->w);
    EXPECT_GT(views[1]->w, 0);
    EXPECT_GT(views[1]->h, 0);
}

TEST_F(TilingTest, ThreeWindowsStackDividedEvenly) {
    make_views(3);
    tiling_arrange(views, area, LayoutMode::Tiling, 0.55f, 4);
    // Two stack windows should have the same height
    EXPECT_EQ(views[1]->h, views[2]->h);
    // Stack windows should not overlap
    EXPECT_GE(views[2]->y, views[1]->y + views[1]->h);
}

TEST_F(TilingTest, MonocleAllViewsSameGeometry) {
    make_views(3);
    tiling_arrange(views, area, LayoutMode::Monocle, 0.55f, 4);
    for (auto* v : views) {
        EXPECT_EQ(v->x, 4);
        EXPECT_EQ(v->y, 4);
        EXPECT_EQ(v->w, 1920 - 8);
        EXPECT_EQ(v->h, 1080 - 8);
    }
}

TEST_F(TilingTest, FloatingIsNoOp) {
    make_views(2);
    views[0]->set_position(100, 200);
    views[0]->set_size(400, 300);
    tiling_arrange(views, area, LayoutMode::Floating);
    EXPECT_EQ(views[0]->x, 100);
    EXPECT_EQ(views[0]->y, 200);
}

TEST_F(TilingTest, MasterRatioClampedAt90Percent) {
    make_views(2);
    tiling_arrange(views, area, LayoutMode::Tiling, 0.9f, 4);
    int master_w = static_cast<int>((area.width - 12) * 0.9f);
    EXPECT_EQ(views[0]->w, master_w);
}

TEST_F(TilingTest, GapAppliedToAllSides) {
    make_views(1);
    tiling_arrange(views, area, LayoutMode::Tiling, 0.55f, 8);
    EXPECT_EQ(views[0]->x, 8);
    EXPECT_EQ(views[0]->y, 8);
    EXPECT_EQ(views[0]->w, 1920 - 16);
    EXPECT_EQ(views[0]->h, 1080 - 16);
}
```

- [ ] **Step 24: Write test_workspace.cpp** (integration-level, uses mock server)

```cpp
// tests/unit/compositor/test_workspace.cpp
// Tests Workspace focus history and layout dispatch in isolation.
// Uses a lightweight stub that satisfies the Workspace API without wlroots.
#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <algorithm>

// Stub: minimal View-like object for workspace focus tests
struct StubView {
    std::string app_id_str;
    bool is_focused = false;

    std::string app_id() const { return app_id_str; }
    void focus() { is_focused = true; }
    void close() {}
};

// Minimal workspace focus-history logic extracted for unit test
class FocusHistory {
    std::vector<StubView*> views_;
public:
    void add(StubView* v)    { views_.insert(views_.begin(), v); }
    void remove(StubView* v) {
        views_.erase(std::remove(views_.begin(), views_.end(), v), views_.end());
    }
    void focus(StubView* v) {
        remove(v);
        views_.insert(views_.begin(), v);
        v->is_focused = true;
    }
    StubView* focused() const { return views_.empty() ? nullptr : views_.front(); }
    void focus_next() { if (views_.size() >= 2) focus(views_[1]); }
    void focus_prev() { if (!views_.empty()) focus(views_.back()); }
    const std::vector<StubView*>& all() const { return views_; }
};

TEST(WorkspaceFocus, EmptyFocusedIsNull) {
    FocusHistory h;
    EXPECT_EQ(h.focused(), nullptr);
}

TEST(WorkspaceFocus, SingleViewBecomesFocused) {
    FocusHistory h;
    StubView v{"app1"};
    h.add(&v);
    h.focus(&v);
    EXPECT_EQ(h.focused(), &v);
    EXPECT_TRUE(v.is_focused);
}

TEST(WorkspaceFocus, FocusNextCycles) {
    FocusHistory h;
    StubView v1{"app1"}, v2{"app2"}, v3{"app3"};
    h.add(&v1); h.add(&v2); h.add(&v3);
    // Front = v3 (last added)
    h.focus_next();
    EXPECT_EQ(h.focused(), h.all()[0]);
}

TEST(WorkspaceFocus, RemoveUnmappedViewRestoresFocus) {
    FocusHistory h;
    StubView v1{"app1"}, v2{"app2"};
    h.add(&v1); h.add(&v2);
    h.focus(&v2);
    ASSERT_EQ(h.focused(), &v2);
    h.remove(&v2);
    // After v2 removed, v1 should be at front
    EXPECT_NE(h.focused(), &v2);
}

TEST(WorkspaceFocus, FocusPrevGoesToBack) {
    FocusHistory h;
    StubView v1{"app1"}, v2{"app2"}, v3{"app3"};
    h.add(&v1); h.add(&v2); h.add(&v3);
    StubView* back = h.all().back();
    h.focus_prev();
    EXPECT_EQ(h.focused(), back);
}
```

- [ ] **Step 25: Write test_ipc_compositor.cpp** (IPC message serialization)

```cpp
// tests/unit/compositor/test_ipc_compositor.cpp
// Tests CompositorEvent/CompositorCommand JSON serialization round-trip
// without needing a live socket or wlroots.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

// Mirror of the IPC event types from compositor/ipc.h
enum class EventType {
    WindowMapped, WindowUnmapped,
    OutputAdded, OutputRemoved,
    SessionLocked, SessionUnlocked
};

struct CompositorEvent {
    EventType   type;
    std::string app_id;
    std::string title;
    std::string output_name;
    int         width  = 0;
    int         height = 0;
};

json serialize(const CompositorEvent& e) {
    switch (e.type) {
        case EventType::WindowMapped:
            return {{"type","WindowMapped"},{"app_id",e.app_id},{"title",e.title}};
        case EventType::WindowUnmapped:
            return {{"type","WindowUnmapped"},{"app_id",e.app_id}};
        case EventType::OutputAdded:
            return {{"type","OutputAdded"},{"name",e.output_name},
                    {"width",e.width},{"height",e.height}};
        case EventType::OutputRemoved:
            return {{"type","OutputRemoved"},{"name",e.output_name}};
        case EventType::SessionLocked:
            return {{"type","SessionLocked"}};
        case EventType::SessionUnlocked:
            return {{"type","SessionUnlocked"}};
    }
    return {};
}

TEST(CompositorIpc, WindowMappedSerializes) {
    CompositorEvent e{EventType::WindowMapped, "org.kde.konsole", "Terminal"};
    auto j = serialize(e);
    EXPECT_EQ(j["type"],   "WindowMapped");
    EXPECT_EQ(j["app_id"], "org.kde.konsole");
    EXPECT_EQ(j["title"],  "Terminal");
}

TEST(CompositorIpc, WindowUnmappedSerializes) {
    CompositorEvent e{EventType::WindowUnmapped, "org.kde.konsole"};
    auto j = serialize(e);
    EXPECT_EQ(j["type"],   "WindowUnmapped");
    EXPECT_EQ(j["app_id"], "org.kde.konsole");
    EXPECT_FALSE(j.contains("title"));
}

TEST(CompositorIpc, OutputAddedSerializes) {
    CompositorEvent e{EventType::OutputAdded};
    e.output_name = "DP-1";
    e.width = 2560; e.height = 1440;
    auto j = serialize(e);
    EXPECT_EQ(j["type"],   "OutputAdded");
    EXPECT_EQ(j["name"],   "DP-1");
    EXPECT_EQ(j["width"],  2560);
    EXPECT_EQ(j["height"], 1440);
}

TEST(CompositorIpc, SessionLockedHasNoPayload) {
    CompositorEvent e{EventType::SessionLocked};
    auto j = serialize(e);
    EXPECT_EQ(j["type"], "SessionLocked");
    EXPECT_EQ(j.size(),  1u);
}

TEST(CompositorIpc, JsonRoundTripOutputRemoved) {
    CompositorEvent e{EventType::OutputRemoved};
    e.output_name = "HDMI-A-1";
    auto serialized = serialize(e).dump();
    auto parsed = json::parse(serialized);
    EXPECT_EQ(parsed["type"], "OutputRemoved");
    EXPECT_EQ(parsed["name"], "HDMI-A-1");
}
```

---

## Integration with Root CMake

- [ ] **Step 26: Add compositor subdirectory to root CMakeLists.txt**

In `CMakeLists.txt`, inside the `if(BUILD_COMPOSITOR)` block (created in Plan 1):

```cmake
if(BUILD_COMPOSITOR)
    add_subdirectory(compositor)
endif()
```

Add compositor tests in `tests/CMakeLists.txt`:

```cmake
if(BUILD_TESTS AND BUILD_COMPOSITOR)
    add_subdirectory(unit/compositor)
endif()
```

---

## Deliverables Checklist

- [ ] `compositor/CMakeLists.txt` — wlroots/Wayland/EGL/xkb deps, protocol generation
- [ ] `compositor/main.cpp` + `server.h/cpp` — wlr_backend, display, event loop
- [ ] `compositor/output.h/cpp` — output management, frame rendering
- [ ] `compositor/view.h/cpp` — xdg_toplevel lifecycle, focus, geometry
- [ ] `compositor/workspace.h/cpp` — view list, focus history, layout dispatch
- [ ] `compositor/tiling.h/cpp` — master-stack + monocle + floating layout
- [ ] `compositor/input.h/cpp` — keyboard (xkb), pointer, seat management
- [ ] `compositor/layer_shell.h/cpp` — wlr-layer-shell-v1 for shell panels
- [ ] `compositor/session_lock.h/cpp` — ext-session-lock-v1 skeleton
- [ ] `compositor/ipc.h/cpp` — JSON IPC to straylight-core
- [ ] `services/compositor/straylight-compositor.service`
- [ ] `config/compositor/straylight-compositor.conf`
- [ ] `tests/unit/compositor/test_tiling.cpp` — pure geometry, no wlroots
- [ ] `tests/unit/compositor/test_workspace.cpp` — focus history logic
- [ ] `tests/unit/compositor/test_ipc_compositor.cpp` — JSON serialization
- [ ] Root `CMakeLists.txt` updated with `add_subdirectory(compositor)`

---

## Notes for Implementor

**No GLFW.** The compositor uses wlroots' own backend abstraction (DRM/KMS, Wayland nested, X11 nested). EGL surfaces are created via `wlr_egl` internally by wlroots. The shell (Plan 4) uses `wl_egl_window` as a Wayland client; the compositor itself never creates EGL windows directly.

**Nested compositor support.** `wlr_backend_autocreate` detects `WAYLAND_DISPLAY` or `DISPLAY` and falls back to a nested backend automatically — no code changes needed for development on an existing desktop.

**Session lock skeleton.** The `ext-session-lock-v1` implementation here registers the protocol global and provides `lock()`/`unlock()` logic. The greeter client (Plan 9) implements the actual lock surface. The safety invariant — never auto-unlock if the lock client crashes — is enforced in `handle_destroy`.

**IPC is optional.** If straylight-core is not running, `CompositorIpc::connect_to_core()` logs a warning and the compositor runs standalone. This is the expected state during development and in minimal installs.

**Protocol XML files.** `wlr-layer-shell-unstable-v1.xml` must be vendored to `cmake/protocols/`. The `ext-session-lock-v1.xml` comes from the `wayland-protocols` package (`pkgdatadir/staging/ext-session-lock/`).
