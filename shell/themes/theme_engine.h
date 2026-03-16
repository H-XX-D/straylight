// shell/themes/theme_engine.h
// JSON-based theme loading and ImGui style application
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

struct ImGuiStyle;

namespace straylight::shell {

/// Theme definition loaded from JSON.
/// Schema: { "name", "colors": { "bg", "fg", "accent", "panel" },
///           "font_size", "corner_radius", "icon_theme" }
struct Theme {
    std::string name        = "default";
    uint32_t    bg          = 0xFF1E1E2E;  // ABGR
    uint32_t    fg          = 0xFFCDD6F4;
    uint32_t    accent      = 0xFFB4BEFE;
    uint32_t    panel       = 0xFF313244;
    float       font_size   = 16.0f;
    float       corner_radius = 4.0f;
    std::string icon_theme  = "straylight-icons";
};

/// Loads themes from JSON config files and applies color/style to ImGui.
/// Supports live reload via inotify watch on the config file path.
class ThemeEngine {
public:
    ThemeEngine();
    ~ThemeEngine();

    /// Load theme from a JSON file path.
    Result<Theme, SLError> load(std::string_view path);

    /// Load theme by name from the default themes directory.
    Result<Theme, SLError> load_by_name(std::string_view theme_name);

    /// Apply the currently loaded theme to an ImGui style.
    void apply(ImGuiStyle& style) const;

    /// Get the currently loaded theme.
    [[nodiscard]] const Theme& current() const;

    /// Start watching the theme file for changes (inotify).
    void watch_for_changes();

    /// Poll for file changes and reload if needed. Call once per frame.
    void poll_changes();

private:
    Theme current_theme_;
    std::string watched_path_;
    int inotify_fd_ = -1;
    int watch_fd_   = -1;

    static uint32_t parse_color(const std::string& hex);
};

} // namespace straylight::shell
