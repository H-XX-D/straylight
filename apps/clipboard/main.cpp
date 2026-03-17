// apps/clipboard/main.cpp
// StrayLight Clipboard Manager — Wayland + EGL + ImGui
// List view with search, pin items, clear, click-to-copy-back.
#include "history.h"
#include "watcher.h"

#include <straylight/log.h>

#include <wayland-client.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <wayland-egl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <xdg-shell-client-protocol.h>

// wlr-data-control protocol for writing back to clipboard
#ifdef HAVE_DATA_CONTROL_PROTOCOL
#include <wlr-data-control-unstable-v1-client-protocol.h>
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace straylight::clipboard;

std::atomic<bool> g_running{true};
void signal_handler(int) { g_running.store(false, std::memory_order_relaxed); }

// ---------------------------------------------------------------------------
// Wayland state
// ---------------------------------------------------------------------------

struct WaylandState {
    wl_display*    display          = nullptr;
    wl_registry*   registry         = nullptr;
    wl_compositor* compositor       = nullptr;
    wl_seat*       seat             = nullptr;
    xdg_wm_base*   xdg_wm_base_ptr = nullptr;
    wl_surface*    surface          = nullptr;
    xdg_surface*   xdg_surface_ptr  = nullptr;
    xdg_toplevel*  toplevel         = nullptr;
    wl_egl_window* egl_window       = nullptr;

#ifdef HAVE_DATA_CONTROL_PROTOCOL
    zwlr_data_control_manager_v1* dc_manager = nullptr;
#endif

    int  width = 520, height = 700;
    bool configured = false, needs_resize = false;
};

void reg_global(void* data, wl_registry* reg, uint32_t name,
                const char* iface, uint32_t ver);
void reg_global_remove(void*, wl_registry*, uint32_t) {}
const wl_registry_listener reg_listener = {
    .global        = reg_global,
    .global_remove = reg_global_remove,
};
void wm_ping(void*, xdg_wm_base* b, uint32_t s) { xdg_wm_base_pong(b, s); }
const xdg_wm_base_listener wm_listener = { .ping = wm_ping };
void surf_configure(void* d, xdg_surface* s, uint32_t serial) {
    xdg_surface_ack_configure(s, serial);
    static_cast<WaylandState*>(d)->configured = true;
}
const xdg_surface_listener surf_listener = { .configure = surf_configure };
void tl_configure(void* d, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
    auto* ws = static_cast<WaylandState*>(d);
    if (w > 0 && h > 0 && (w != ws->width || h != ws->height)) {
        ws->width = w; ws->height = h; ws->needs_resize = true;
    }
}
void tl_close(void*, xdg_toplevel*) { g_running.store(false, std::memory_order_relaxed); }
void tl_bounds(void*, xdg_toplevel*, int32_t, int32_t) {}
void tl_caps(void*, xdg_toplevel*, wl_array*) {}
const xdg_toplevel_listener tl_listener = {
    .configure        = tl_configure,
    .close            = tl_close,
    .configure_bounds = tl_bounds,
    .wm_capabilities  = tl_caps,
};
void seat_caps(void*, wl_seat*, uint32_t) {}
void seat_name(void*, wl_seat*, const char*) {}
const wl_seat_listener seat_listener = { .capabilities = seat_caps, .name = seat_name };

void reg_global(void* data, wl_registry* reg, uint32_t name,
                const char* iface, uint32_t ver) {
    auto* ws = static_cast<WaylandState*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0)
        ws->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, std::min(ver, 4u)));
    else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        ws->xdg_wm_base_ptr = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, std::min(ver, 2u)));
        xdg_wm_base_add_listener(ws->xdg_wm_base_ptr, &wm_listener, ws);
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
        ws->seat = static_cast<wl_seat*>(
            wl_registry_bind(reg, name, &wl_seat_interface, std::min(ver, 5u)));
        wl_seat_add_listener(ws->seat, &seat_listener, ws);
    }
#ifdef HAVE_DATA_CONTROL_PROTOCOL
    else if (std::strcmp(iface, zwlr_data_control_manager_v1_interface.name) == 0) {
        ws->dc_manager = static_cast<zwlr_data_control_manager_v1*>(
            wl_registry_bind(reg, name,
                             &zwlr_data_control_manager_v1_interface,
                             std::min(ver, 2u)));
    }
#endif
}

// ---------------------------------------------------------------------------
// Copy text back to clipboard via zwlr_data_control_manager_v1
// ---------------------------------------------------------------------------

struct SendCtx {
    std::string text;
    std::mutex  mtx;
};

#ifdef HAVE_DATA_CONTROL_PROTOCOL
void dc_source_send(void* data,
                    zwlr_data_control_source_v1* src,
                    const char* /*mime*/,
                    int32_t fd) {
    auto* ctx = static_cast<SendCtx*>(data);
    std::lock_guard lock(ctx->mtx);
    const char* p   = ctx->text.data();
    ssize_t     rem = static_cast<ssize_t>(ctx->text.size());
    while (rem > 0) {
        ssize_t n = ::write(fd, p, static_cast<size_t>(rem));
        if (n <= 0) break;
        p += n; rem -= n;
    }
    ::close(fd);
}

void dc_source_cancelled(void* data, zwlr_data_control_source_v1* src) {
    auto* ctx = static_cast<SendCtx*>(data);
    delete ctx;
    zwlr_data_control_source_v1_destroy(src);
}

const zwlr_data_control_source_v1_listener dc_src_listener = {
    .send      = dc_source_send,
    .cancelled = dc_source_cancelled,
};

void copy_to_clipboard(WaylandState& ws, const std::string& text) {
    if (!ws.dc_manager || !ws.seat) return;

    auto* src = zwlr_data_control_manager_v1_create_data_source(ws.dc_manager);
    zwlr_data_control_source_v1_offer(src, "text/plain;charset=utf-8");
    zwlr_data_control_source_v1_offer(src, "text/plain");
    zwlr_data_control_source_v1_offer(src, "UTF8_STRING");

    auto* ctx  = new SendCtx;
    ctx->text  = text;
    zwlr_data_control_source_v1_add_listener(src, &dc_src_listener, ctx);

    // Get (or create) a device for this seat
    auto* device = zwlr_data_control_manager_v1_get_data_device(ws.dc_manager, ws.seat);
    zwlr_data_control_device_v1_set_selection(device, src);
    zwlr_data_control_device_v1_destroy(device);

    wl_display_flush(ws.display);
}
#else
void copy_to_clipboard(WaylandState& /*ws*/, const std::string& /*text*/) {
    // Without the protocol, we can't set the clipboard from a background app.
}
#endif

// ---------------------------------------------------------------------------
// Apply theme
// ---------------------------------------------------------------------------

void apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.FrameRounding = 4.0f;
    s.ItemSpacing = ImVec2(8.0f, 6.0f); s.FramePadding = ImVec2(6.0f, 4.0f);
    s.WindowPadding = ImVec2(10.0f, 10.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]      = ImVec4(0.08f, 0.08f, 0.13f, 1.0f);
    c[ImGuiCol_ChildBg]       = ImVec4(0.10f, 0.10f, 0.16f, 1.0f);
    c[ImGuiCol_FrameBg]       = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);
    c[ImGuiCol_Button]        = ImVec4(0.0f,  0.55f, 0.38f, 0.8f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.0f,  0.80f, 0.55f, 1.0f);
    c[ImGuiCol_ButtonActive]  = ImVec4(0.0f,  1.00f, 0.67f, 1.0f);
    c[ImGuiCol_Header]        = ImVec4(0.0f,  0.55f, 0.38f, 0.6f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.0f,  0.80f, 0.55f, 0.8f);
    c[ImGuiCol_HeaderActive]  = ImVec4(0.0f,  1.00f, 0.67f, 1.0f);
    c[ImGuiCol_Separator]     = ImVec4(0.20f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_Border]        = ImVec4(0.20f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_Text]          = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
    c[ImGuiCol_TextDisabled]  = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    c[ImGuiCol_ScrollbarBg]   = ImVec4(0.08f, 0.08f, 0.13f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.20f, 0.32f, 1.0f);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main(int /*argc*/, char* /*argv*/[]) {
    using namespace straylight;
    using namespace straylight::clipboard;

    Log::init("straylight-clipboard");
    SL_INFO("StrayLight Clipboard Manager starting");

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // --- Wayland ----------------------------------------------------------
    WaylandState ws;
    ws.display = wl_display_connect(nullptr);
    if (!ws.display) { SL_CRITICAL("No Wayland display"); return EXIT_FAILURE; }
    ws.registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(ws.registry, &reg_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.xdg_wm_base_ptr) {
        SL_CRITICAL("Missing required Wayland globals");
        wl_display_disconnect(ws.display);
        return EXIT_FAILURE;
    }

    ws.surface         = wl_compositor_create_surface(ws.compositor);
    ws.xdg_surface_ptr = xdg_wm_base_get_xdg_surface(ws.xdg_wm_base_ptr, ws.surface);
    xdg_surface_add_listener(ws.xdg_surface_ptr, &surf_listener, &ws);
    ws.toplevel = xdg_surface_get_toplevel(ws.xdg_surface_ptr);
    xdg_toplevel_add_listener(ws.toplevel, &tl_listener, &ws);
    xdg_toplevel_set_title(ws.toplevel, "StrayLight Clipboard");
    xdg_toplevel_set_app_id(ws.toplevel, "straylight-clipboard");
    xdg_toplevel_set_min_size(ws.toplevel, 360, 500);
    wl_surface_commit(ws.surface);
    wl_display_roundtrip(ws.display);

    // --- EGL --------------------------------------------------------------
    EGLDisplay egl_disp = eglGetDisplay(
        reinterpret_cast<EGLNativeDisplayType>(ws.display));
    EGLint major = 0, minor = 0;
    eglInitialize(egl_disp, &major, &minor);
    constexpr EGLint cfg_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE
    };
    EGLConfig egl_cfg = nullptr; EGLint num_cfgs = 0;
    eglChooseConfig(egl_disp, cfg_attribs, &egl_cfg, 1, &num_cfgs);
    ws.egl_window = wl_egl_window_create(ws.surface, ws.width, ws.height);
    EGLSurface egl_surf = eglCreateWindowSurface(egl_disp, egl_cfg,
        reinterpret_cast<EGLNativeWindowType>(ws.egl_window), nullptr);
    constexpr EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE
    };
    EGLContext egl_ctx = eglCreateContext(egl_disp, egl_cfg,
                                          EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);

    // --- ImGui ------------------------------------------------------------
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize  = ImVec2(float(ws.width), float(ws.height));
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    apply_theme();

    // --- Clipboard engine -------------------------------------------------
    ClipHistory history;
    (void)history.load();

    ClipWatcher watcher;
    (void)watcher.start(ws.display, history, [](const ClipEntry& /*e*/) {
        // Notification hook — could update a tray icon badge in the future
    });

    // --- App state --------------------------------------------------------
    char search_buf[256] = {};
    std::string status_msg;
    std::chrono::steady_clock::time_point status_until{};

    auto set_status = [&](const std::string& msg) {
        status_msg   = msg;
        status_until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    };

    SL_INFO("Clipboard Manager UI ready; {} entries loaded", history.size());

    // --- Main loop --------------------------------------------------------
    while (g_running.load(std::memory_order_relaxed)) {
        wl_display_dispatch_pending(ws.display);
        wl_display_flush(ws.display);

        if (ws.needs_resize) {
            ws.needs_resize = false;
            wl_egl_window_resize(ws.egl_window, ws.width, ws.height, 0, 0);
            io.DisplaySize = ImVec2(float(ws.width), float(ws.height));
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        constexpr ImGuiWindowFlags kWin =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToDisplayFront;

        if (ImGui::Begin("##Clipboard", nullptr, kWin)) {
            // Title bar
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
            ImGui::Text("STRAYLIGHT CLIPBOARD");
            ImGui::PopStyleColor();
            ImGui::SameLine(io.DisplaySize.x - 60.0f);
            if (ImGui::SmallButton("Close"))
                g_running.store(false, std::memory_order_relaxed);
            ImGui::Separator();

            // Search + toolbar row
            ImGui::SetNextItemWidth(io.DisplaySize.x * 0.5f);
            ImGui::InputText("##search", search_buf, sizeof(search_buf));
            ImGui::SameLine(0.0f, 12.0f);
            if (ImGui::Button("Clear All")) {
                history.clear_all();
                (void)history.save();
                set_status("Cleared all entries.");
            }
            ImGui::SameLine(0.0f, 8.0f);
            if (ImGui::Button("Clear Unpinned")) {
                history.clear_unpinned();
                (void)history.save();
                set_status("Cleared unpinned entries.");
            }
            ImGui::Separator();
            ImGui::Spacing();

            const auto entries = history.entries();
            std::string filter(search_buf);

            // Compute filtered list indices
            std::vector<size_t> filtered;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (filter.empty()) {
                    filtered.push_back(i);
                } else {
                    const std::string& text =
                        entries[i].kind == EntryKind::Text ? entries[i].text : entries[i].mime;
                    if (text.find(filter) != std::string::npos)
                        filtered.push_back(i);
                }
            }

            ImGui::Text("%zu / %zu entries", filtered.size(), entries.size());
            ImGui::Spacing();

            float list_h = io.DisplaySize.y - 110.0f;
            if (!status_msg.empty() &&
                std::chrono::steady_clock::now() < status_until) {
                list_h -= 22.0f;
            }

            if (ImGui::BeginChild("##list", ImVec2(0.0f, list_h), false,
                                  ImGuiWindowFlags_HorizontalScrollbar)) {
                for (size_t fi = 0; fi < filtered.size(); ++fi) {
                    size_t idx = filtered[fi];
                    const auto& e = entries[idx];

                    ImGui::PushID(static_cast<int>(idx));

                    // Row background highlight for pinned
                    if (e.pinned) {
                        ImVec2 row_min = ImGui::GetCursorScreenPos();
                        float row_w    = ImGui::GetContentRegionAvail().x;
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            row_min,
                            ImVec2(row_min.x + row_w, row_min.y + ImGui::GetTextLineHeightWithSpacing()),
                            IM_COL32(0, 80, 50, 60));
                    }

                    // Index + pin indicator
                    ImGui::TextDisabled("%03zu", fi + 1);
                    ImGui::SameLine(0.0f, 6.0f);
                    if (e.pinned) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
                        ImGui::TextUnformatted("[P]");
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextDisabled("   ");
                    }
                    ImGui::SameLine(0.0f, 6.0f);

                    // Preview text — click copies back
                    std::string preview = e.preview(100);
                    bool clicked = ImGui::Selectable(preview.c_str(), false,
                                                      0, ImVec2(0.0f, 0.0f));
                    if (clicked && e.kind == EntryKind::Text) {
                        copy_to_clipboard(ws, e.text);
                        set_status("Copied: " + e.preview(40));
                    }
                    if (ImGui::IsItemHovered() && e.kind == EntryKind::Text) {
                        ImGui::SetTooltip("Click to copy back to clipboard");
                    }

                    // Action buttons on the right
                    float btn_x = ImGui::GetContentRegionAvail().x;
                    ImGui::SameLine(ImGui::GetWindowWidth() - 115.0f);

                    if (ImGui::SmallButton(e.pinned ? "Unpin" : "Pin")) {
                        history.toggle_pin(idx);
                        (void)history.save();
                    }
                    ImGui::SameLine(0.0f, 4.0f);
                    if (ImGui::SmallButton("Del")) {
                        history.remove(idx);
                        (void)history.save();
                        ImGui::PopID();
                        break; // entries changed, restart iteration
                    }

                    ImGui::PopID();
                    ImGui::Separator();
                }
            }
            ImGui::EndChild();

            // Status bar
            if (!status_msg.empty() &&
                std::chrono::steady_clock::now() < status_until) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
                ImGui::TextUnformatted(status_msg.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, ws.width, ws.height);
        glClearColor(0.08f, 0.08f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        eglSwapBuffers(egl_disp, egl_surf);

        usleep(16000);
    }

    // --- Cleanup ----------------------------------------------------------
    SL_INFO("Clipboard Manager shutting down");
    watcher.stop();
    (void)history.save();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(egl_disp, egl_surf);
    eglDestroyContext(egl_disp, egl_ctx);
    eglTerminate(egl_disp);

    wl_egl_window_destroy(ws.egl_window);
    xdg_toplevel_destroy(ws.toplevel);
    xdg_surface_destroy(ws.xdg_surface_ptr);
    wl_surface_destroy(ws.surface);
    if (ws.seat) wl_seat_destroy(ws.seat);
#ifdef HAVE_DATA_CONTROL_PROTOCOL
    if (ws.dc_manager) zwlr_data_control_manager_v1_destroy(ws.dc_manager);
#endif
    xdg_wm_base_destroy(ws.xdg_wm_base_ptr);
    wl_compositor_destroy(ws.compositor);
    wl_registry_destroy(ws.registry);
    wl_display_disconnect(ws.display);

    SL_INFO("Clipboard Manager exited cleanly");
    return EXIT_SUCCESS;
}
