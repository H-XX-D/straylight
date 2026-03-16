// shell/main.cpp
// StrayLight desktop shell — Wayland layer-shell client with ImGui UI
#include "renderer.h"
#include "layer_surface.h"
#include "panels/top_bar.h"
#include "panels/app_launcher.h"
#include "themes/theme_engine.h"
#include "widgets/notification.h"

#include <straylight/log.h>
#include <straylight/config.h>

#include <wayland-client.h>

#include <imgui.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <string>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    SL_INFO("Received signal {}, shutting down", signum);
    g_running.store(false, std::memory_order_relaxed);
}

// Wayland globals
struct WaylandState {
    wl_display*    display       = nullptr;
    wl_registry*   registry      = nullptr;
    wl_compositor* compositor    = nullptr;
    wl_seat*       seat          = nullptr;
    wl_output*     output        = nullptr;
    void*          layer_shell   = nullptr;  // zwlr_layer_shell_v1*
    int            output_width  = 1920;
    int            output_height = 1080;
};

// Registry listener — bind compositor, seat, layer_shell, output globals
void registry_global(void* data, wl_registry* registry,
                     uint32_t name, const char* interface,
                     uint32_t version) {
    auto* state = static_cast<WaylandState*>(data);
    const std::string iface(interface);

    if (iface == "wl_compositor") {
        state->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface,
                             std::min(version, 4u)));
    } else if (iface == "wl_seat") {
        state->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min(version, 7u)));
    } else if (iface == "wl_output") {
        state->output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface,
                             std::min(version, 4u)));
    } else if (iface == "zwlr_layer_shell_v1") {
        // Bind layer-shell protocol global
        state->layer_shell = wl_registry_bind(registry, name,
            // Interface struct from generated protocol bindings
            static_cast<const wl_interface*>(nullptr), // placeholder
            std::min(version, 4u));
    }
}

void registry_global_remove(void* /*data*/, wl_registry* /*registry*/,
                            uint32_t /*name*/) {
    // Handle output removal if needed
}

const wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    using namespace straylight;
    using namespace straylight::shell;

    // Initialize logging
    Log::init("straylight-shell");
    SL_INFO("StrayLight Shell starting");

    // Load configuration
    const std::string config_path =
        "/etc/straylight/shell/straylight-shell.conf";
    auto config_result = Config::load(config_path);
    if (!config_result.has_value()) {
        SL_WARN("Failed to load config from {}, using defaults", config_path);
    }

    // Install signal handlers
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // Connect to Wayland display
    WaylandState wl_state;
    wl_state.display = wl_display_connect(nullptr);
    if (!wl_state.display) {
        SL_CRITICAL("Failed to connect to Wayland display");
        return EXIT_FAILURE;
    }

    // Bind globals
    wl_state.registry = wl_display_get_registry(wl_state.display);
    wl_registry_add_listener(wl_state.registry, &registry_listener,
                             &wl_state);
    wl_display_roundtrip(wl_state.display);

    if (!wl_state.compositor) {
        SL_CRITICAL("wl_compositor not available");
        wl_display_disconnect(wl_state.display);
        return EXIT_FAILURE;
    }

    if (!wl_state.layer_shell) {
        SL_CRITICAL("zwlr_layer_shell_v1 not available");
        wl_display_disconnect(wl_state.display);
        return EXIT_FAILURE;
    }

    // Create top bar layer surface (32px, anchored top, full width)
    constexpr int kTopBarHeight = 32;
    auto layer_result = LayerSurface::create(
        wl_state.display,
        wl_state.compositor,
        wl_state.layer_shell,
        Layer::kTop,
        Anchor::kTop | Anchor::kLeft | Anchor::kRight,
        0,  // 0 = use full output width
        kTopBarHeight,
        kTopBarHeight);  // exclusive zone

    if (!layer_result.has_value()) {
        SL_CRITICAL("Failed to create layer surface: {}",
                    layer_result.error().message());
        wl_display_disconnect(wl_state.display);
        return EXIT_FAILURE;
    }

    auto layer = std::move(layer_result).value();
    const int bar_width = layer.width() > 0
                              ? layer.width()
                              : wl_state.output_width;

    // Create EGL renderer
    auto renderer_result = Renderer::create(
        wl_state.display, layer.surface(),
        bar_width, kTopBarHeight);

    if (!renderer_result.has_value()) {
        SL_CRITICAL("Failed to create renderer: {}",
                    renderer_result.error().message());
        wl_display_disconnect(wl_state.display);
        return EXIT_FAILURE;
    }

    auto renderer = std::move(renderer_result).value();

    // Initialize theme engine
    ThemeEngine theme_engine;
    auto theme_err = theme_engine.load("/etc/straylight/themes/default.json");
    if (theme_err.has_value()) {
        ImGuiStyle& style = ImGui::GetStyle();
        theme_engine.apply(style);
    }

    // Initialize UI panels
    TopBar top_bar;
    AppLauncher app_launcher;
    NotificationManager notifications;

    SL_INFO("Shell initialized, entering event loop");

    // Main event loop
    while (g_running.load(std::memory_order_relaxed)) {
        // Dispatch pending Wayland events
        if (wl_display_dispatch_pending(wl_state.display) == -1) {
            SL_ERROR("Wayland display error, exiting");
            break;
        }
        wl_display_flush(wl_state.display);

        // Render frame
        renderer.begin_frame();

        top_bar.render(bar_width);

        if (app_launcher.is_visible()) {
            app_launcher.render();
        }

        notifications.render();

        renderer.end_frame();
    }

    // Cleanup
    SL_INFO("Shell shutting down");
    layer.destroy();

    if (wl_state.seat)       wl_seat_destroy(wl_state.seat);
    if (wl_state.output)     wl_output_destroy(wl_state.output);
    if (wl_state.compositor) wl_compositor_destroy(wl_state.compositor);
    if (wl_state.registry)   wl_registry_destroy(wl_state.registry);
    wl_display_disconnect(wl_state.display);

    SL_INFO("Shell exited cleanly");
    return EXIT_SUCCESS;
}
