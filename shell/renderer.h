// shell/renderer.h
// EGL + ImGui renderer for Wayland surfaces
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <memory>

// Forward declarations for Wayland types
struct wl_display;
struct wl_surface;

namespace straylight::shell {

/// EGL + OpenGL ES 3.0 renderer that drives ImGui on a Wayland surface.
/// Creates an EGL context via wl_egl_window, initializes ImGui with
/// OpenGL ES 3.0 backend. Call begin_frame() before ImGui draw calls,
/// end_frame() after to present.
class Renderer {
public:
    /// Create a renderer bound to the given Wayland surface.
    /// The surface must already be committed and configured.
    static Result<Renderer, SLError> create(wl_display* display,
                                            wl_surface* surface,
                                            int width, int height);

    /// Start a new ImGui frame. Call before any ImGui draw commands.
    void begin_frame();

    /// Finalize the ImGui frame, render draw data, and swap EGL buffers.
    void end_frame();

    /// Resize the EGL window and viewport.
    void resize(int width, int height);

    ~Renderer();
    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;

    // Non-copyable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit Renderer(std::unique_ptr<Impl> impl);
};

} // namespace straylight::shell
