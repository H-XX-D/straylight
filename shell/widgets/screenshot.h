// shell/widgets/screenshot.h
// Screenshot capture using wlr-screencopy-unstable-v1 and libpng
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <functional>
#include <memory>
#include <string>

struct wl_display;
struct wl_output;

namespace straylight::shell {

/// How the screenshot region is determined.
enum class CaptureMode { FullScreen, Region, Window };

/// Input parameters for a capture operation.
struct CaptureRequest {
    CaptureMode mode        = CaptureMode::FullScreen;
    int         x = 0, y = 0, w = 0, h = 0;  // Region mode only
    std::string output_path;                    // Empty = auto-generate
    bool        copy_to_clipboard = true;
};

/// Output data from a completed capture.
struct CaptureResult {
    std::string saved_path;
    int         width     = 0;
    int         height    = 0;
    size_t      file_size = 0;
};

/// Screenshot tool using wlr-screencopy-unstable-v1.
/// Supports full-screen, rectangular region, and an interactive
/// rubber-band region selector rendered via ImGui.
class Screenshot {
public:
    using CompletionCb = std::function<void(Result<CaptureResult, SLError>)>;

    Screenshot();
    ~Screenshot();

    /// Initialize with Wayland globals. Must be called before capture().
    /// @param screencopy_manager  Pointer to zwlr_screencopy_manager_v1.
    void init(wl_display* display, void* screencopy_manager, wl_output* output);

    /// Start an async capture. Result is delivered via callback once the
    /// Wayland ready event fires and PNG is written.
    void capture(CaptureRequest req, CompletionCb on_complete);

    /// Render the interactive region-selection overlay.
    /// Returns true while selection is still active (caller should skip
    /// normal UI rendering and call this each frame).
    /// On mouse-up, fires capture() internally with the selected rect.
    bool render_region_selector();

    /// Cancel an in-progress region selection without capturing.
    void cancel_selection();

    /// Generate a default output path:
    ///   ~/Pictures/screenshot-YYYYMMDD-HHMMSS.png
    static std::string default_path();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace straylight::shell
