// shell/layer_surface.h
// Wayland layer-shell surface wrapper
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <memory>

struct wl_display;
struct wl_surface;
struct wl_compositor;

namespace straylight::shell {

/// Layer for the layer-shell surface (maps to zwlr_layer_shell_v1 layers).
enum class Layer : uint32_t {
    kBackground = 0,
    kBottom     = 1,
    kTop        = 2,
    kOverlay    = 3,
};

/// Anchor edges for layer-shell surface positioning.
enum class Anchor : uint32_t {
    kNone   = 0,
    kTop    = 1,
    kBottom = 2,
    kLeft   = 4,
    kRight  = 8,
};

inline Anchor operator|(Anchor a, Anchor b) {
    return static_cast<Anchor>(static_cast<uint32_t>(a) |
                               static_cast<uint32_t>(b));
}

/// Wraps zwlr_layer_shell_v1 and zwlr_layer_surface_v1 Wayland protocol
/// objects. Creates a layer-shell surface suitable for panels, overlays,
/// and background rendering.
class LayerSurface {
public:
    /// Create a new layer-shell surface on the given display.
    /// The compositor and layer_shell globals must already be bound.
    static Result<LayerSurface, SLError> create(
        wl_display* display,
        wl_compositor* compositor,
        void* layer_shell,   // zwlr_layer_shell_v1*
        Layer layer,
        Anchor anchor,
        int width,
        int height,
        int exclusive_zone = 0);

    /// Get the underlying wl_surface for EGL rendering.
    [[nodiscard]] wl_surface* surface() const;

    /// Get the configured width (may change after configure events).
    [[nodiscard]] int width() const;

    /// Get the configured height.
    [[nodiscard]] int height() const;

    /// Returns true if the surface has been configured by the compositor.
    [[nodiscard]] bool configured() const;

    /// Destroy the layer surface and associated Wayland objects.
    void destroy();

    ~LayerSurface();
    LayerSurface(LayerSurface&& other) noexcept;
    LayerSurface& operator=(LayerSurface&& other) noexcept;

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit LayerSurface(std::unique_ptr<Impl> impl);
};

} // namespace straylight::shell
