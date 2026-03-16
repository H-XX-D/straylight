// shell/panels/top_bar.h
// Desktop shell top taskbar panel
#pragma once

#include <string>
#include <cstdint>

namespace straylight::shell {

/// Renders the time in HH:MM format, updated every second.
class ClockWidget {
public:
    /// Render the clock into the current ImGui window at the cursor position.
    void render();

    /// Get the current formatted time string (for testing).
    [[nodiscard]] std::string formatted_time() const;
};

/// Top taskbar panel rendered as a full-width 32px bar.
/// Contains: workspace switcher (left), focused window title (center),
/// clock + system tray + notification badge (right).
class TopBar {
public:
    TopBar();
    ~TopBar();

    /// Render the top bar. Called once per frame.
    /// @param screen_width  Width of the output in pixels.
    void render(int screen_width);

    /// Set the focused window title (updated via compositor IPC).
    void set_focused_title(const std::string& title);

    /// Set the current workspace index (0-based).
    void set_workspace(int index);

    /// Set the notification count for the badge.
    void set_notification_count(int count);

private:
    ClockWidget clock_;
    std::string focused_title_;
    int workspace_index_    = 0;
    int notification_count_ = 0;

    void render_workspace_switcher();
    void render_window_title(int screen_width);
    void render_system_tray(int screen_width);
};

} // namespace straylight::shell
