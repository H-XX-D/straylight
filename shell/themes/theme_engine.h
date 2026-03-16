// shell/themes/theme_engine.h
// JSON-based theme loading, CSS-variable system, ImGui style application, hot-reload
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ImGuiStyle;

namespace straylight::shell {

/// Extended theme variable — "namespace.property" -> value string.
struct ThemeVar {
    std::string key;    // e.g. "surface.hover", "border.radius.lg"
    std::string value;  // color hex, float string, or px string
};

/// Theme definition loaded from JSON.
/// Schema: { "name", "colors": { "bg", "fg", "accent", "panel" },
///           "vars": { ... }, "font_size", "corner_radius", "icon_theme" }
struct Theme {
    std::string name          = "default";
    // Core palette (ABGR uint32, backward-compatible)
    uint32_t    bg            = 0xFF1E1E2E;
    uint32_t    fg            = 0xFFCDD6F4;
    uint32_t    accent        = 0xFFB4BEFE;
    uint32_t    panel         = 0xFF313244;
    float       font_size     = 16.0f;
    float       corner_radius = 4.0f;
    std::string icon_theme    = "straylight-icons";
    // Extended variable map: "namespace.property" -> value
    std::unordered_map<std::string, std::string> vars;
};

/// Loads themes from JSON config files and applies color/style to ImGui.
/// Supports live reload via inotify directory watch and a live-preview editor panel.
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

    // -----------------------------------------------------------------------
    // Variable resolution
    // -----------------------------------------------------------------------

    /// Resolve a theme variable by key. Returns fallback if unset.
    [[nodiscard]] std::string var(std::string_view key,
                                   std::string_view fallback = "") const;

    /// Resolve a color variable. Returns ABGR uint32.
    [[nodiscard]] uint32_t color_var(std::string_view key,
                                     uint32_t fallback = 0xFF000000) const;

    /// Resolve a float variable (font sizes, radii, spacing).
    [[nodiscard]] float float_var(std::string_view key,
                                   float fallback = 0.0f) const;

    /// Get all variable keys (for live-preview enumeration).
    [[nodiscard]] std::vector<std::string> var_keys() const;

    /// Override a variable at runtime (live preview, not persisted).
    void set_var(std::string_view key, std::string_view value);

    // -----------------------------------------------------------------------
    // Hot-reload
    // -----------------------------------------------------------------------

    /// Start watching both /etc/straylight/themes/ and
    /// ~/.config/straylight/themes/ for changes via inotify.
    void watch_for_changes();

    /// Poll for file changes and reload if needed. Call once per frame.
    void poll_changes();

    // -----------------------------------------------------------------------
    // Live-preview and persistence
    // -----------------------------------------------------------------------

    /// Render an ImGui panel showing all theme variables with live editing.
    /// Changes apply immediately via set_var() but are NOT persisted until
    /// save_current() is called.
    void render_live_preview(bool* p_open);

    /// Persist current theme (including runtime overrides) to a JSON file.
    /// Writes atomically via a .tmp file + rename.
    Result<void, SLError> save_current(std::string_view path);

    // -----------------------------------------------------------------------
    // Static helpers
    // -----------------------------------------------------------------------

    /// Parse a "#RRGGBB" or "#RRGGBBAA" string to ABGR uint32.
    static uint32_t parse_color(const std::string& hex);

    /// Convert ABGR uint32 to "#RRGGBBAA" hex string.
    static std::string color_to_hex(uint32_t abgr);

private:
    Theme       current_theme_;
    std::string watched_path_;  // last loaded file path (for single-file reload)

    // inotify handles
    int inotify_fd_   = -1;
    int watch_fd_sys_ = -1;  // /etc/straylight/themes/
    int watch_fd_usr_ = -1;  // ~/.config/straylight/themes/

    // Debounce: ignore events within 100 ms of the previous reload
    std::chrono::steady_clock::time_point last_reload_time_{};
    static constexpr auto kReloadDebounce = std::chrono::milliseconds(100);
};

} // namespace straylight::shell
