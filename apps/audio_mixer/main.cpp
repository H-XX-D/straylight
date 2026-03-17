// apps/audio_mixer/main.cpp
// StrayLight Audio Mixer — Wayland + EGL + ImGui
// Per-app volume sliders, peak meters, output device selector.
#include "pipewire_client.h"
#include "mixer.h"

#include <straylight/log.h>

#include <wayland-client.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <wayland-egl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <xdg-shell-client-protocol.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

using namespace straylight::mixer;

std::atomic<bool> g_running{true};
void signal_handler(int) { g_running.store(false, std::memory_order_relaxed); }

// ---------------------------------------------------------------------------
// Wayland boilerplate
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
    int  width = 800, height = 500;
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
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

void apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f; s.FrameRounding = 3.0f;
    s.ItemSpacing = ImVec2(8.0f, 6.0f); s.FramePadding = ImVec2(6.0f, 4.0f);
    s.WindowPadding = ImVec2(12.0f, 12.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = ImVec4(0.08f, 0.08f, 0.13f, 1.0f);
    c[ImGuiCol_ChildBg]           = ImVec4(0.10f, 0.10f, 0.16f, 1.0f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.14f, 0.14f, 0.22f, 1.0f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.20f, 0.20f, 0.30f, 1.0f);
    c[ImGuiCol_Button]            = ImVec4(0.0f,  0.55f, 0.38f, 0.8f);
    c[ImGuiCol_ButtonHovered]     = ImVec4(0.0f,  0.80f, 0.55f, 1.0f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.0f,  1.00f, 0.67f, 1.0f);
    c[ImGuiCol_SliderGrab]        = ImVec4(0.0f,  0.80f, 0.55f, 1.0f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.0f,  1.00f, 0.67f, 1.0f);
    c[ImGuiCol_Header]            = ImVec4(0.0f,  0.55f, 0.38f, 0.6f);
    c[ImGuiCol_HeaderHovered]     = ImVec4(0.0f,  0.80f, 0.55f, 0.8f);
    c[ImGuiCol_HeaderActive]      = ImVec4(0.0f,  1.00f, 0.67f, 1.0f);
    c[ImGuiCol_Separator]         = ImVec4(0.20f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_Border]            = ImVec4(0.20f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_Text]              = ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.0f,  0.80f, 0.55f, 1.0f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.0f, 1.0f, 0.67f, 1.0f);
}

// ---------------------------------------------------------------------------
// Draw a vertical peak meter for one channel
// ---------------------------------------------------------------------------

void draw_peak_meter(const PeakSample& m, float width, float height) {
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      IM_COL32(20, 20, 35, 255));

    // RMS bar (green -> yellow -> red gradient based on level)
    float rms_h = m.rms * height;
    ImU32 rms_col = IM_COL32(0, 200, 130, 220);
    if (m.rms > 0.85f) rms_col = IM_COL32(255, 60, 60, 220);
    else if (m.rms > 0.7f) rms_col = IM_COL32(255, 200, 0, 220);

    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + height - rms_h),
        ImVec2(pos.x + width, pos.y + height),
        rms_col);

    // Peak hold line
    float peak_y = pos.y + height - m.peak * height;
    dl->AddLine(ImVec2(pos.x, peak_y),
                ImVec2(pos.x + width, peak_y),
                IM_COL32(255, 255, 255, 180), 1.5f);

    // Border
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                IM_COL32(50, 50, 80, 200));

    ImGui::Dummy(ImVec2(width, height));
}

// ---------------------------------------------------------------------------
// Draw a single mixer channel strip
// ---------------------------------------------------------------------------

void draw_channel_strip(Mixer& mixer,
                         const MixerChannel& ch,
                         const std::vector<MixerChannel>& sinks) {
    constexpr float kStripWidth  = 90.0f;
    constexpr float kMeterWidth  = 10.0f;
    constexpr float kMeterHeight = 180.0f;
    constexpr float kSliderHeight = 180.0f;

    ImGui::PushID(static_cast<int>(ch.node.id));

    ImGui::BeginGroup();

    // App label
    const std::string& lbl = ch.label();
    std::string short_lbl = lbl.size() > 12 ? lbl.substr(0, 11) + "." : lbl;
    ImGui::TextUnformatted(short_lbl.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", lbl.c_str());

    // Media class badge
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.6f, 1.0f));
    if (ch.node.is_device()) ImGui::TextUnformatted("[OUT]");
    else if (ch.node.is_sink_stream()) ImGui::TextUnformatted("[APP]");
    else ImGui::TextUnformatted("[IN]");
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Peak meters (one per channel, horizontal row)
    size_t n_ch = std::max(ch.meters.size(), size_t(1));
    float meter_row_w = float(n_ch) * (kMeterWidth + 2.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                          (kStripWidth - meter_row_w) * 0.5f);
    for (size_t i = 0; i < n_ch; ++i) {
        if (i < ch.meters.size())
            draw_peak_meter(ch.meters[i], kMeterWidth, kMeterHeight);
        else
            draw_peak_meter(PeakSample{}, kMeterWidth, kMeterHeight);
        if (i + 1 < n_ch) ImGui::SameLine(0.0f, 2.0f);
    }

    ImGui::Spacing();

    // Volume slider (vertical)
    float vol = ch.master_volume();
    char vol_id[32];
    std::snprintf(vol_id, sizeof(vol_id), "##vol%u", ch.node.id);
    ImGui::SetNextItemWidth(kStripWidth - 10.0f);
    if (ImGui::SliderFloat(vol_id, &vol, 0.0f, 1.0f, "%.2f",
                            ImGuiSliderFlags_None)) {
        mixer.set_master_volume(ch.node.id, vol);
    }

    // dB label
    char db_buf[16];
    std::snprintf(db_buf, sizeof(db_buf), "%.1f dB", Mixer::to_db(vol));
    ImGui::TextDisabled("%s", db_buf);

    // Mute button
    bool muted = ch.node.muted;
    if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(muted ? "MUTED##m" : "Mute##m", ImVec2(kStripWidth - 10.0f, 0.0f))) {
        mixer.toggle_mute(ch.node.id);
    }
    if (muted) ImGui::PopStyleColor();

    // Sink routing dropdown (only for app streams)
    if (ch.node.is_sink_stream() && !sinks.empty()) {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(kStripWidth - 10.0f);
        char sink_id[32];
        std::snprintf(sink_id, sizeof(sink_id), "##sink%u", ch.node.id);
        if (ImGui::BeginCombo(sink_id, "Route...", ImGuiComboFlags_NoArrowButton)) {
            for (const auto& sink : sinks) {
                const std::string& slabel = sink.label();
                if (ImGui::Selectable(slabel.c_str(), false)) {
                    mixer.route_stream(ch.node.id, sink.node.id);
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::EndGroup();
    ImGui::PopID();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------

int main(int /*argc*/, char* /*argv*/[]) {
    using namespace straylight;
    using namespace straylight::mixer;

    Log::init("straylight-audio-mixer");
    SL_INFO("StrayLight Audio Mixer starting");

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // --- PipeWire ---------------------------------------------------------
    PipeWireClient pw;
    Mixer mixer(pw);

    (void)pw.start([&mixer]() {
        mixer.refresh();
    });

    // Wait briefly for initial enumeration
    usleep(200000); // 200 ms
    mixer.refresh();

    // --- Wayland ----------------------------------------------------------
    WaylandState ws;
    ws.display = wl_display_connect(nullptr);
    if (!ws.display) {
        SL_CRITICAL("No Wayland display");
        pw.stop();
        return EXIT_FAILURE;
    }
    ws.registry = wl_display_get_registry(ws.display);
    wl_registry_add_listener(ws.registry, &reg_listener, &ws);
    wl_display_roundtrip(ws.display);

    if (!ws.compositor || !ws.xdg_wm_base_ptr) {
        SL_CRITICAL("Missing Wayland globals");
        pw.stop();
        wl_display_disconnect(ws.display);
        return EXIT_FAILURE;
    }

    ws.surface         = wl_compositor_create_surface(ws.compositor);
    ws.xdg_surface_ptr = xdg_wm_base_get_xdg_surface(ws.xdg_wm_base_ptr, ws.surface);
    xdg_surface_add_listener(ws.xdg_surface_ptr, &surf_listener, &ws);
    ws.toplevel = xdg_surface_get_toplevel(ws.xdg_surface_ptr);
    xdg_toplevel_add_listener(ws.toplevel, &tl_listener, &ws);
    xdg_toplevel_set_title(ws.toplevel, "StrayLight Audio Mixer");
    xdg_toplevel_set_app_id(ws.toplevel, "straylight-audio-mixer");
    xdg_toplevel_set_min_size(ws.toplevel, 600, 420);
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
    EGLContext egl_ctx = eglCreateContext(egl_disp, egl_cfg, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);

    // --- ImGui ------------------------------------------------------------
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize  = ImVec2(float(ws.width), float(ws.height));
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplOpenGL3_Init("#version 300 es");
    apply_theme();

    SL_INFO("Audio Mixer UI ready");

    auto last_tick = std::chrono::steady_clock::now();

    // --- Main loop --------------------------------------------------------
    while (g_running.load(std::memory_order_relaxed)) {
        wl_display_dispatch_pending(ws.display);
        wl_display_flush(ws.display);

        if (ws.needs_resize) {
            ws.needs_resize = false;
            wl_egl_window_resize(ws.egl_window, ws.width, ws.height, 0, 0);
            io.DisplaySize = ImVec2(float(ws.width), float(ws.height));
        }

        // Tick mixer peak decay
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_tick).count();
        last_tick = now;
        mixer.tick(dt);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(io.DisplaySize);
        constexpr ImGuiWindowFlags kWin =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToDisplayFront;

        if (ImGui::Begin("##AudioMixer", nullptr, kWin)) {
            // Title
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
            ImGui::Text("STRAYLIGHT AUDIO MIXER");
            ImGui::PopStyleColor();
            ImGui::SameLine(io.DisplaySize.x - 60.0f);
            if (ImGui::SmallButton("Close"))
                g_running.store(false, std::memory_order_relaxed);
            ImGui::Separator();

            const auto channels = mixer.channels();
            const auto sinks    = mixer.sinks();

            if (channels.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("No audio streams detected. "
                                    "Start a media application to see channels.");
            } else {
                // Horizontal scrolling mixer board
                if (ImGui::BeginChild("##mixer_board",
                                      ImVec2(0.0f, io.DisplaySize.y - 80.0f),
                                      false,
                                      ImGuiWindowFlags_HorizontalScrollbar)) {
                    constexpr float kStripWidth = 100.0f;
                    constexpr float kStripPad   = 8.0f;

                    // Section: Output Devices
                    bool has_devices = false;
                    for (const auto& ch : channels) {
                        if (ch.node.is_device()) { has_devices = true; break; }
                    }
                    if (has_devices) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
                        ImGui::TextUnformatted("OUTPUT DEVICES");
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        ImGui::Spacing();

                        bool first_dev = true;
                        for (const auto& ch : channels) {
                            if (!ch.node.is_device()) continue;
                            if (!first_dev) ImGui::SameLine(0.0f, kStripPad);
                            first_dev = false;
                            draw_channel_strip(mixer, ch, sinks);
                        }
                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();
                    }

                    // Section: Application Streams
                    bool has_streams = false;
                    for (const auto& ch : channels) {
                        if (!ch.node.is_device()) { has_streams = true; break; }
                    }
                    if (has_streams) {
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
                        ImGui::TextUnformatted("APPLICATION STREAMS");
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        ImGui::Spacing();

                        bool first_stream = true;
                        for (const auto& ch : channels) {
                            if (ch.node.is_device()) continue;
                            if (!first_stream) ImGui::SameLine(0.0f, kStripPad);
                            first_stream = false;
                            draw_channel_strip(mixer, ch, sinks);
                        }
                    }
                }
                ImGui::EndChild();
            }

            // Status bar
            ImGui::Separator();
            size_t n_streams = 0;
            for (const auto& ch : channels) if (!ch.node.is_device()) ++n_streams;
            ImGui::TextDisabled("%zu total | %zu app streams | %zu sinks",
                                channels.size(), n_streams, sinks.size());
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
    SL_INFO("Audio Mixer shutting down");
    pw.stop();

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
    xdg_wm_base_destroy(ws.xdg_wm_base_ptr);
    wl_compositor_destroy(ws.compositor);
    wl_registry_destroy(ws.registry);
    wl_display_disconnect(ws.display);

    SL_INFO("Audio Mixer exited cleanly");
    return EXIT_SUCCESS;
}
