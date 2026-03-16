// shell/keybinds/keybind_manager.h
// Global keybind manager: loads JSON config, registers with compositor via
// wlr_keyboard_shortcuts_inhibit, dispatches actions to shell components.
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward declarations — Wayland types
struct wl_display;
struct wl_seat;

namespace straylight::shell {

// ---------------------------------------------------------------------------
// Modifier flags (bit-field)
// ---------------------------------------------------------------------------

enum class Modifier : uint8_t {
    None  = 0x00,
    Super = 0x01,   // Win / Logo key (XKB: Mod4)
    Ctrl  = 0x02,   // Control
    Alt   = 0x04,   // Alt (XKB: Mod1)
    Shift = 0x08,   // Shift
};

inline Modifier operator|(Modifier a, Modifier b) noexcept {
    return static_cast<Modifier>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline Modifier operator&(Modifier a, Modifier b) noexcept {
    return static_cast<Modifier>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool has_modifier(Modifier set, Modifier flag) noexcept {
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(flag)) != 0;
}

// ---------------------------------------------------------------------------
// Action types
// ---------------------------------------------------------------------------

/// All actions the keybind manager can dispatch.
enum class KeybindAction {
    Launch,           // fork/exec a program; arg = command string
    Screenshot,       // trigger screenshot tool; arg = "" | "region" | "window"
    VolumeUp,         // raise volume by one step
    VolumeDown,       // lower volume by one step
    VolumeMute,       // toggle mute
    Workspace,        // switch to workspace N; arg = "1"–"9"
    CloseWindow,      // ask compositor to close focused window
    ToggleLauncher,   // show/hide the app launcher
    Unknown,          // unrecognised action — silently ignored
};

// ---------------------------------------------------------------------------
// Keybind descriptor
// ---------------------------------------------------------------------------

/// A single keybind entry as parsed from JSON.
struct Keybind {
    std::string   keys_str;  // raw string: e.g. "Super+Return"
    Modifier      mods  = Modifier::None;
    uint32_t      keysym = 0;  // XKB keysym value (xkbcommon-keysyms.h)
    KeybindAction action = KeybindAction::Unknown;
    std::string   args;        // optional argument for the action
};

// ---------------------------------------------------------------------------
// Action callback table
// ---------------------------------------------------------------------------

/// Callbacks provided by shell components so KeybindManager can dispatch
/// actions without coupling to concrete panel types.
struct KeybindCallbacks {
    /// Called for "launch" actions. arg is the command to execute.
    std::function<void(const std::string& cmd)> on_launch;

    /// Called for "screenshot" actions. arg: "", "region", or "window".
    std::function<void(const std::string& mode)> on_screenshot;

    /// Called for volume actions.
    std::function<void()> on_volume_up;
    std::function<void()> on_volume_down;
    std::function<void()> on_volume_mute;

    /// Called for "workspace_N" action. n is 1-based workspace index.
    std::function<void(int n)> on_workspace;

    /// Called for "close_window" action.
    std::function<void()> on_close_window;

    /// Called for "toggle_launcher" action.
    std::function<void()> on_toggle_launcher;
};

// ---------------------------------------------------------------------------
// KeybindManager
// ---------------------------------------------------------------------------

/// Loads keybinds from JSON config, converts key combos to XKB keysym+mods,
/// registers the inhibitor on the compositor seat, and dispatches actions to
/// registered shell callbacks on each key event.
///
/// Config search order (first found wins):
///   1. ~/.config/straylight/keybinds.json
///   2. /etc/straylight/keybinds.json
///
/// JSON schema:
/// @code
/// [
///   { "keys": "Super+Return",   "action": "launch",   "args": "straylight-terminal" },
///   { "keys": "Super+F2",       "action": "screenshot", "args": "region" },
///   { "keys": "XF86AudioRaiseVolume", "action": "volume_up" },
///   { "keys": "Super+1",        "action": "workspace_1" },
///   { "keys": "Super+W",        "action": "close_window" },
///   { "keys": "Super+Space",    "action": "toggle_launcher" }
/// ]
/// @endcode
///
/// Hot-reload: the config file is watched via inotify; call poll_changes()
/// once per compositor frame to pick up edits without restart.
class KeybindManager {
public:
    KeybindManager();
    ~KeybindManager();

    // Non-copyable, movable
    KeybindManager(const KeybindManager&)            = delete;
    KeybindManager& operator=(const KeybindManager&) = delete;
    KeybindManager(KeybindManager&&)                 = default;
    KeybindManager& operator=(KeybindManager&&)      = default;

    // ------------------------------------------------------------------
    // Initialization
    // ------------------------------------------------------------------

    /// Load keybinds from the first config file found in the search path.
    /// Parses JSON, resolves XKB keysyms, and builds the dispatch table.
    Result<void, SLError> load();

    /// Load keybinds from an explicit file path (useful for testing).
    Result<void, SLError> load_from(std::string_view path);

    /// Register action callbacks provided by shell components.
    void set_callbacks(KeybindCallbacks callbacks);

    /// Register the keyboard shortcuts inhibitor with the compositor.
    /// Requires a valid wl_seat from the registry.  Call after Wayland
    /// globals are bound.  Safe to call before load() — registration
    /// is deferred until the compositor advertises the inhibit manager.
    ///
    /// @note wlr_keyboard_shortcuts_inhibit_manager_v1 is bound from
    ///       the registry and stored internally; the caller only needs
    ///       to pass the wl_seat.
    void register_with_compositor(wl_display* display, wl_seat* seat);

    // ------------------------------------------------------------------
    // Event dispatch
    // ------------------------------------------------------------------

    /// Call when a keyboard key event arrives from the compositor.
    /// @param keysym      XKB keysym of the pressed key.
    /// @param mods        Active modifier mask at event time.
    /// @param pressed     true = key down, false = key up.
    /// @returns           true if a keybind matched and was dispatched.
    bool handle_key(uint32_t keysym, Modifier mods, bool pressed);

    // ------------------------------------------------------------------
    // Hot-reload (inotify)
    // ------------------------------------------------------------------

    /// Start watching the loaded config file for changes via inotify.
    void watch_for_changes();

    /// Poll inotify without blocking.  Call once per compositor frame.
    /// Reloads config and rebuilds the dispatch table if the file changed.
    void poll_changes();

    // ------------------------------------------------------------------
    // Inspection
    // ------------------------------------------------------------------

    /// Return all currently loaded keybinds (for settings UI display).
    [[nodiscard]] const std::vector<Keybind>& keybinds() const;

    /// Return the path that was actually loaded.
    [[nodiscard]] const std::string& loaded_path() const;

    // ------------------------------------------------------------------
    // Static helpers
    // ------------------------------------------------------------------

    /// Parse a modifier+key string like "Super+Ctrl+Return" into its
    /// modifier mask and XKB keysym.  Returns {Modifier::None, 0} on
    /// failure.
    static std::pair<Modifier, uint32_t> parse_keys(std::string_view keys_str);

    /// Map an action name string to a KeybindAction enum value.
    static KeybindAction parse_action(std::string_view action_str);

    /// Return the XKB keysym for a key name string (e.g. "Return", "F2",
    /// "space", "XF86AudioRaiseVolume").  Returns 0 if not found.
    static uint32_t keysym_for_name(std::string_view name);

private:
    void dispatch(const Keybind& kb);
    Result<void, SLError> parse_json(const nlohmann::json& j);

    std::vector<Keybind> keybinds_;
    KeybindCallbacks     callbacks_;
    std::string          loaded_path_;

    // inotify
    int inotify_fd_  = -1;
    int watch_fd_    = -1;

    // Debounce reload
    std::chrono::steady_clock::time_point last_reload_time_{};
    static constexpr auto kReloadDebounce = std::chrono::milliseconds(150);

    // Wayland inhibitor handles (opaque pointers; avoids pulling wlroots
    // headers into every translation unit that includes this header)
    wl_display* wl_display_       = nullptr;
    wl_seat*    wl_seat_          = nullptr;
    void*       inhibit_manager_  = nullptr; // zwlr_keyboard_shortcuts_inhibit_manager_v1*
    void*       inhibitor_        = nullptr; // zwlr_keyboard_shortcuts_inhibitor_v1*
};

} // namespace straylight::shell
