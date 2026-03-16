// shell/keybinds/keybind_manager.cpp
// Global keybind manager implementation.
//
// Config load order:
//   1. ~/.config/straylight/keybinds.json
//   2. /etc/straylight/keybinds.json
//
// JSON schema:
//   [{"keys": "Super+Return", "action": "launch", "args": "straylight-terminal"}, ...]
//
// Modifier tokens: Super, Ctrl, Alt, Shift  (case-insensitive)
// Key names: any XKB key name string (Return, F1-F12, space, a-z, 0-9,
//            XF86AudioRaiseVolume, XF86AudioLowerVolume, XF86AudioMute, ...)
//
// Hot-reload: inotify IN_CLOSE_WRITE / IN_MODIFY on the config file,
//             debounced 150 ms, same pattern as ThemeEngine.

#include "keybind_manager.h"

#include <straylight/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef __linux__
#  include <poll.h>
#  include <sys/inotify.h>
#  include <unistd.h>
// wlroots / wlr-keyboard-shortcuts-inhibit protocol support.
// We include the generated client-protocol header only when building on
// Linux with wlroots available.  On macOS (CI check builds) these symbols
// are absent, so the inhibitor code is entirely #ifdef-guarded.
#  ifdef HAVE_WLR_KEYBOARD_SHORTCUTS_INHIBIT
#    include <wlr-keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#  endif
#endif

namespace straylight::shell {

namespace fs   = std::filesystem;
using json     = nlohmann::json;

// ---------------------------------------------------------------------------
// Internal constants
// ---------------------------------------------------------------------------

static constexpr const char* kSysConfig = "/etc/straylight/keybinds.json";

// ---------------------------------------------------------------------------
// XKB keysym look-up table
// ---------------------------------------------------------------------------
// We do not link xkbcommon at the shell layer (it lives in the compositor);
// instead we maintain a compact static table of the names the config is
// likely to use.  Covers: letters, digits, function keys, modifiers, common
// special keys, and XTEST multimedia symbols.

namespace {

struct KeyEntry { const char* name; uint32_t keysym; };

// XKB keysym values sourced from <xkbcommon/xkbcommon-keysyms.h>
static constexpr std::array<KeyEntry, 120> kKeyTable{{
    // --- Letters (lowercase keysym = lowercase letter) ---
    {"a", 0x0061}, {"b", 0x0062}, {"c", 0x0063}, {"d", 0x0064},
    {"e", 0x0065}, {"f", 0x0066}, {"g", 0x0067}, {"h", 0x0068},
    {"i", 0x0069}, {"j", 0x006a}, {"k", 0x006b}, {"l", 0x006c},
    {"m", 0x006d}, {"n", 0x006e}, {"o", 0x006f}, {"p", 0x0070},
    {"q", 0x0071}, {"r", 0x0072}, {"s", 0x0073}, {"t", 0x0074},
    {"u", 0x0075}, {"v", 0x0076}, {"w", 0x0077}, {"x", 0x0078},
    {"y", 0x0079}, {"z", 0x007a},
    // --- Digits ---
    {"0", 0x0030}, {"1", 0x0031}, {"2", 0x0032}, {"3", 0x0033},
    {"4", 0x0034}, {"5", 0x0035}, {"6", 0x0036}, {"7", 0x0037},
    {"8", 0x0038}, {"9", 0x0039},
    // --- Function keys ---
    {"F1",  0xFFBE}, {"F2",  0xFFBF}, {"F3",  0xFFC0}, {"F4",  0xFFC1},
    {"F5",  0xFFC2}, {"F6",  0xFFC3}, {"F7",  0xFFC4}, {"F8",  0xFFC5},
    {"F9",  0xFFC6}, {"F10", 0xFFC7}, {"F11", 0xFFC8}, {"F12", 0xFFC9},
    // --- Editing / navigation ---
    {"Return",    0xFF0D}, {"KP_Enter",  0xFF8D},
    {"space",     0x0020}, {"Tab",       0xFF09}, {"BackSpace", 0xFF08},
    {"Escape",    0xFF1B}, {"Delete",    0xFFFF}, {"Insert",    0xFF63},
    {"Home",      0xFF50}, {"End",       0xFF57},
    {"Page_Up",   0xFF55}, {"Page_Down", 0xFF56},
    {"Left",      0xFF51}, {"Up",        0xFF52},
    {"Right",     0xFF53}, {"Down",      0xFF54},
    {"Print",     0xFF61}, {"Pause",     0xFF13},
    // --- Punctuation / symbols ---
    {"minus",      0x002D}, {"equal",      0x003D},
    {"bracketleft",0x005B}, {"bracketright",0x005D},
    {"backslash",  0x005C}, {"semicolon",  0x003B},
    {"apostrophe", 0x0027}, {"grave",      0x0060},
    {"comma",      0x002C}, {"period",     0x002E},
    {"slash",      0x002F},
    // --- Multimedia / XF86 ---
    {"XF86AudioRaiseVolume", 0x1008FF13},
    {"XF86AudioLowerVolume", 0x1008FF11},
    {"XF86AudioMute",        0x1008FF12},
    {"XF86AudioPlay",        0x1008FF14},
    {"XF86AudioStop",        0x1008FF15},
    {"XF86AudioNext",        0x1008FF17},
    {"XF86AudioPrev",        0x1008FF16},
    {"XF86MonBrightnessUp",  0x1008FF02},
    {"XF86MonBrightnessDown",0x1008FF03},
    {"XF86ScreenSaver",      0x1008FF2D},
    {"XF86Sleep",            0x1008FF2F},
    {"XF86WebCam",           0x1008FF8F},
}};

static std::string to_lower(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Static helpers — keysym_for_name, parse_keys, parse_action
// ---------------------------------------------------------------------------

uint32_t KeybindManager::keysym_for_name(std::string_view name) {
    // Exact match first (case-sensitive, covers XF86* and capitalised names)
    for (const auto& e : kKeyTable) {
        if (name == e.name) return e.keysym;
    }
    // Case-insensitive fallback for letters/digits
    std::string lo = to_lower(name);
    for (const auto& e : kKeyTable) {
        if (lo == to_lower(e.name)) return e.keysym;
    }
    return 0;
}

// Parse "Super+Ctrl+Return" → {mods, keysym}
std::pair<Modifier, uint32_t> KeybindManager::parse_keys(std::string_view keys_str) {
    Modifier  mods   = Modifier::None;
    uint32_t  keysym = 0;

    // Split on '+'
    std::vector<std::string> tokens;
    {
        std::string s(keys_str);
        std::istringstream ss(s);
        std::string tok;
        while (std::getline(ss, tok, '+')) {
            if (!tok.empty()) tokens.push_back(tok);
        }
    }

    if (tokens.empty()) return {Modifier::None, 0};

    // All but the last token are modifiers; last token is the key name.
    // Exception: a bare "XF86*" token is both mod-free and the key itself.
    for (std::size_t i = 0; i < tokens.size() - 1; ++i) {
        const std::string lo = to_lower(tokens[i]);
        if      (lo == "super" || lo == "win" || lo == "meta") mods = mods | Modifier::Super;
        else if (lo == "ctrl"  || lo == "control")             mods = mods | Modifier::Ctrl;
        else if (lo == "alt"   || lo == "mod1")                mods = mods | Modifier::Alt;
        else if (lo == "shift")                                 mods = mods | Modifier::Shift;
        else {
            SL_WARN("KeybindManager: unknown modifier token '{}' in '{}'",
                    tokens[i], keys_str);
        }
    }

    const std::string& key_name = tokens.back();
    keysym = keysym_for_name(key_name);
    if (keysym == 0) {
        SL_WARN("KeybindManager: unrecognised key name '{}' in '{}'",
                key_name, keys_str);
    }
    return {mods, keysym};
}

KeybindAction KeybindManager::parse_action(std::string_view action_str) {
    // Normalise to lower-case for comparison
    std::string lo = to_lower(action_str);

    if (lo == "launch")           return KeybindAction::Launch;
    if (lo == "screenshot")       return KeybindAction::Screenshot;
    if (lo == "volume_up")        return KeybindAction::VolumeUp;
    if (lo == "volume_down")      return KeybindAction::VolumeDown;
    if (lo == "volume_mute" ||
        lo == "mute")             return KeybindAction::VolumeMute;
    if (lo == "close_window" ||
        lo == "close")            return KeybindAction::CloseWindow;
    if (lo == "toggle_launcher" ||
        lo == "launcher")         return KeybindAction::ToggleLauncher;

    // workspace_N — "workspace_1" … "workspace_9", or "workspace" + N
    if (lo.rfind("workspace_", 0) == 0 || lo.rfind("workspace", 0) == 0) {
        return KeybindAction::Workspace;
    }

    SL_WARN("KeybindManager: unknown action '{}'", action_str);
    return KeybindAction::Unknown;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

KeybindManager::KeybindManager() = default;

KeybindManager::~KeybindManager() {
#ifdef __linux__
    if (inotify_fd_ >= 0) {
        if (watch_fd_ >= 0) inotify_rm_watch(inotify_fd_, watch_fd_);
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        watch_fd_   = -1;
    }

#  ifdef HAVE_WLR_KEYBOARD_SHORTCUTS_INHIBIT
    if (inhibitor_) {
        zwlr_keyboard_shortcuts_inhibitor_v1_destroy(
            static_cast<zwlr_keyboard_shortcuts_inhibitor_v1*>(inhibitor_));
        inhibitor_ = nullptr;
    }
    if (inhibit_manager_) {
        zwlr_keyboard_shortcuts_inhibit_manager_v1_destroy(
            static_cast<zwlr_keyboard_shortcuts_inhibit_manager_v1*>(inhibit_manager_));
        inhibit_manager_ = nullptr;
    }
#  endif
#endif
}

// ---------------------------------------------------------------------------
// Config path resolution
// ---------------------------------------------------------------------------

namespace {

static std::string user_config_path() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char* home = std::getenv("HOME");
        base = std::string(home ? home : "/root") + "/.config";
    }
    return base + "/straylight/keybinds.json";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// load() / load_from()
// ---------------------------------------------------------------------------

Result<void, SLError> KeybindManager::load() {
    // Search user path first, then system default
    for (const std::string& path : {user_config_path(), std::string(kSysConfig)}) {
        if (fs::exists(path)) {
            return load_from(path);
        }
    }
    // No config found — not necessarily fatal; log and return empty table
    SL_WARN("KeybindManager: no keybinds config found; using empty table");
    keybinds_.clear();
    return Result<void, SLError>::ok();
}

Result<void, SLError> KeybindManager::load_from(std::string_view path) {
    std::string path_str(path);

    if (!fs::exists(path_str)) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound,
                    "Keybinds config not found: " + path_str});
    }

    std::ifstream file(path_str);
    if (!file.is_open()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied,
                    "Cannot open keybinds config: " + path_str});
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::ParseError,
                    std::string("Keybinds JSON parse error: ") + e.what()});
    }

    auto result = parse_json(j);
    if (!result.has_value()) return result;

    loaded_path_ = path_str;
    SL_INFO("KeybindManager: loaded {} keybind(s) from {}",
            keybinds_.size(), path_str);
    return Result<void, SLError>::ok();
}

Result<void, SLError> KeybindManager::parse_json(const json& j) {
    if (!j.is_array()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::ParseError,
                    "Keybinds JSON root must be an array"});
    }

    std::vector<Keybind> parsed;
    parsed.reserve(j.size());

    for (std::size_t idx = 0; idx < j.size(); ++idx) {
        const auto& entry = j[idx];
        if (!entry.is_object()) {
            SL_WARN("KeybindManager: entry [{}] is not an object, skipping", idx);
            continue;
        }
        if (!entry.contains("keys") || !entry.contains("action")) {
            SL_WARN("KeybindManager: entry [{}] missing 'keys' or 'action', skipping", idx);
            continue;
        }

        Keybind kb;
        kb.keys_str = entry["keys"].get<std::string>();
        kb.args     = entry.value("args", std::string{});

        std::string action_str = entry["action"].get<std::string>();

        // workspace_N action encodes workspace number in either the action
        // string ("workspace_3") or in args ("3").  Normalise: store the
        // number in args so dispatch() doesn't need to re-parse.
        kb.action = parse_action(action_str);
        if (kb.action == KeybindAction::Workspace && kb.args.empty()) {
            // Extract trailing digit from action string, e.g. "workspace_3" → "3"
            auto pos = action_str.find_last_not_of("0123456789");
            if (pos != std::string::npos && pos + 1 < action_str.size()) {
                kb.args = action_str.substr(pos + 1);
            }
        }

        auto [mods, keysym] = parse_keys(kb.keys_str);
        kb.mods   = mods;
        kb.keysym = keysym;

        if (kb.keysym == 0 && kb.action != KeybindAction::Unknown) {
            SL_WARN("KeybindManager: could not resolve keysym for '{}', entry skipped",
                    kb.keys_str);
            continue;
        }

        parsed.push_back(std::move(kb));
    }

    keybinds_ = std::move(parsed);
    return Result<void, SLError>::ok();
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void KeybindManager::set_callbacks(KeybindCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
}

// ---------------------------------------------------------------------------
// Compositor inhibitor registration
// ---------------------------------------------------------------------------

void KeybindManager::register_with_compositor(wl_display* display, wl_seat* seat) {
    wl_display_ = display;
    wl_seat_    = seat;

#ifdef __linux__
#  ifdef HAVE_WLR_KEYBOARD_SHORTCUTS_INHIBIT
    // The inhibit manager is obtained via the Wayland registry.  In the full
    // compositor integration path, the shell's registry listener binds
    // zwlr_keyboard_shortcuts_inhibit_manager_v1 and passes the pointer here
    // via a second overload (not shown in this header, but wired in main.cpp).
    // Here we perform the final step: create the per-seat inhibitor so that
    // compositor-level shortcuts are suppressed and our keybind table takes
    // precedence for the registered combos.
    if (!inhibit_manager_ || !wl_seat_) {
        SL_WARN("KeybindManager: inhibit manager or seat not available; "
                "global keybinds will not suppress compositor shortcuts");
        return;
    }

    inhibitor_ = zwlr_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
        static_cast<zwlr_keyboard_shortcuts_inhibit_manager_v1*>(inhibit_manager_),
        wl_seat_);

    if (!inhibitor_) {
        SL_WARN("KeybindManager: failed to create keyboard shortcuts inhibitor");
        return;
    }
    SL_INFO("KeybindManager: keyboard shortcuts inhibitor registered with compositor");
#  else
    SL_INFO("KeybindManager: wlr-keyboard-shortcuts-inhibit not available at "
            "compile time; compositor inhibitor not registered");
#  endif
#endif
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------

bool KeybindManager::handle_key(uint32_t keysym, Modifier mods, bool pressed) {
    if (!pressed) return false;  // Only act on key-down events

    for (const auto& kb : keybinds_) {
        if (kb.keysym == keysym && kb.mods == mods) {
            dispatch(kb);
            return true;
        }
    }
    return false;
}

void KeybindManager::dispatch(const Keybind& kb) {
    SL_DEBUG("KeybindManager: dispatch action={} args='{}' keys='{}'",
             static_cast<int>(kb.action), kb.args, kb.keys_str);

    switch (kb.action) {
    case KeybindAction::Launch:
        if (callbacks_.on_launch) {
            callbacks_.on_launch(kb.args);
        } else {
            // Fallback: fork/exec directly
            if (!kb.args.empty()) {
                std::string cmd = kb.args + " &";
                std::system(cmd.c_str()); // NOLINT(cert-env33-c)
            }
        }
        break;

    case KeybindAction::Screenshot:
        if (callbacks_.on_screenshot) {
            callbacks_.on_screenshot(kb.args);
        }
        break;

    case KeybindAction::VolumeUp:
        if (callbacks_.on_volume_up) callbacks_.on_volume_up();
        break;

    case KeybindAction::VolumeDown:
        if (callbacks_.on_volume_down) callbacks_.on_volume_down();
        break;

    case KeybindAction::VolumeMute:
        if (callbacks_.on_volume_mute) callbacks_.on_volume_mute();
        break;

    case KeybindAction::Workspace: {
        int n = 1;
        if (!kb.args.empty()) {
            try { n = std::stoi(kb.args); } catch (...) {}
        }
        if (callbacks_.on_workspace) callbacks_.on_workspace(n);
        break;
    }

    case KeybindAction::CloseWindow:
        if (callbacks_.on_close_window) callbacks_.on_close_window();
        break;

    case KeybindAction::ToggleLauncher:
        if (callbacks_.on_toggle_launcher) callbacks_.on_toggle_launcher();
        break;

    case KeybindAction::Unknown:
        break;
    }
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

const std::vector<Keybind>& KeybindManager::keybinds() const {
    return keybinds_;
}

const std::string& KeybindManager::loaded_path() const {
    return loaded_path_;
}

// ---------------------------------------------------------------------------
// Hot-reload — inotify
// ---------------------------------------------------------------------------

void KeybindManager::watch_for_changes() {
#ifdef __linux__
    if (loaded_path_.empty()) {
        SL_WARN("KeybindManager: no file loaded; cannot watch for changes");
        return;
    }

    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        SL_WARN("KeybindManager: inotify_init1 failed: {}", std::strerror(errno));
        return;
    }

    // Watch the directory containing the config file so we catch atomic
    // writes (editor saves a .tmp then renames — IN_MOVED_TO fires on dir).
    std::string dir = fs::path(loaded_path_).parent_path().string();
    if (dir.empty()) dir = ".";

    constexpr uint32_t kMask =
        IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO;

    watch_fd_ = inotify_add_watch(inotify_fd_, dir.c_str(), kMask);
    if (watch_fd_ < 0) {
        SL_WARN("KeybindManager: inotify_add_watch failed for '{}': {}",
                dir, std::strerror(errno));
        ::close(inotify_fd_);
        inotify_fd_ = -1;
        return;
    }

    SL_INFO("KeybindManager: watching '{}' for config changes", dir);
#endif
}

void KeybindManager::poll_changes() {
#ifdef __linux__
    if (inotify_fd_ < 0) return;

    struct pollfd pfd{inotify_fd_, POLLIN, 0};
    if (::poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) return;

    // Debounce
    auto now = std::chrono::steady_clock::now();
    if (now - last_reload_time_ < kReloadDebounce) {
        // Drain the fd but don't act
        char drain[4096];
        while (::read(inotify_fd_, drain, sizeof(drain)) > 0) {}
        return;
    }

    // Read events and collect the basename of any modified file
    alignas(struct inotify_event) char buf[4096];
    std::string modified_name;
    ssize_t len;
    while ((len = ::read(inotify_fd_, buf, sizeof(buf))) > 0) {
        for (char* ptr = buf; ptr < buf + len;) {
            auto* ev = reinterpret_cast<struct inotify_event*>(ptr);
            if (ev->len > 0 && ev->name[0] != '\0') {
                modified_name = ev->name;
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }

    last_reload_time_ = now;

    // Only reload if the changed file matches our loaded config basename
    if (!modified_name.empty()) {
        std::string basename = fs::path(loaded_path_).filename().string();
        if (modified_name != basename) return;
    }

    SL_INFO("KeybindManager: config changed, reloading '{}'", loaded_path_);
    auto result = load_from(loaded_path_);
    if (!result.has_value()) {
        SL_ERROR("KeybindManager: reload failed: {}", result.error().message());
    }
#endif
}

} // namespace straylight::shell
