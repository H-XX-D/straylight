// apps/clipboard/watcher.h
// Wayland clipboard watcher using the zwlr_data_control_manager_v1 protocol.
// Monitors wl_data_device_manager clipboard change events and feeds new content
// into a ClipHistory instance.
#pragma once

#include "history.h"

#include <straylight/result.h>
#include <straylight/error.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// Forward declare Wayland types to avoid polluting headers
struct wl_display;
struct wl_seat;

namespace straylight::clipboard {

/// Callback invoked on the watcher thread whenever new clipboard content arrives.
using ClipChangedFn = std::function<void(const ClipEntry&)>;

/// Monitors the Wayland clipboard via zwlr_data_control_manager_v1 and
/// pushes new content into a ClipHistory.
///
/// If the compositor does not advertise zwlr_data_control_manager_v1,
/// falls back to polling via the standard wl_data_device_manager.
class ClipWatcher {
public:
    ClipWatcher() = default;
    ~ClipWatcher() { stop(); }

    ClipWatcher(const ClipWatcher&)            = delete;
    ClipWatcher& operator=(const ClipWatcher&) = delete;

    /// Connect to the given Wayland display, bind globals, and start the
    /// background monitoring thread.
    Result<void, SLError> start(wl_display* display, ClipHistory& history,
                                 ClipChangedFn on_change = {});

    /// Stop the monitoring thread and disconnect.
    void stop();

    /// Returns true while the watcher thread is running.
    [[nodiscard]] bool running() const { return running_.load(); }

private:
    std::atomic<bool> running_{false};
    std::thread       thread_;
    ClipHistory*      history_   = nullptr;
    ClipChangedFn     on_change_;

    /// Thread entry point: event loop that reads the Wayland socket.
    void run_loop(wl_display* display);
};

} // namespace straylight::clipboard
