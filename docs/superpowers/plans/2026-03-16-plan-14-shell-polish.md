# Plan 14: Shell Polish — Themes, Notifications, Screenshot, Volume OSD, Shortcuts, Widget Framework

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Upgrade the six stub/minimal subsystems in `shell/` to production-quality implementations: hot-reloadable CSS-variable theme engine, full notification system with urgency and actions, screenshot tool with region/window capture, PipeWire volume OSD, configurable keyboard shortcuts with JSON persistence, and an embeddable desktop widget framework for dock panels.

**Architecture:** All code lives under `shell/` within the existing `straylight-shell` executable. Components communicate through the shell's per-frame render loop in `main.cpp`. PipeWire integration uses `libpipewire-0.3`. Screenshot capture uses `wlr-screencopy-unstable-v1`. Shortcuts persist to `~/.config/straylight/keybinds.json`. The widget framework reuses Plan 13's `WidgetBase` interface to allow ML/HPC widgets to embed in dock panels.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, Wayland layer-shell, wlr-screencopy-unstable-v1, PipeWire 0.3, nlohmann/json 3.11+, inotify, xkbcommon, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common), Plan 4 (shell skeleton, layer_surface, renderer), Plan 13 (WidgetBase interface)

**Development environment:** Linux x86_64 required. Needs `libpipewire-0.3-dev`, `libxkbcommon-dev`, `wayland-protocols`. macOS cannot build.

---

## Chunk 1: Theme Engine Refinement + Notification System

### Step 1.1 — Theme engine: CSS-like variables and extended palette

Enhance `shell/themes/theme_engine.h/.cpp` to support a full variable system beyond the four hardcoded colors.

- [ ] Extend `Theme` struct in `shell/themes/theme_engine.h`
  ```cpp
  struct ThemeVar {
      std::string key;   // e.g. "surface.hover", "border.radius.lg"
      std::string value; // color hex, float, or px string
  };

  struct Theme {
      std::string name = "default";
      // Core palette (backward-compat)
      uint32_t bg = 0xFF1E1E2E, fg = 0xFFCDD6F4;
      uint32_t accent = 0xFFB4BEFE, panel = 0xFF313244;
      float font_size = 16.0f, corner_radius = 4.0f;
      std::string icon_theme = "straylight-icons";
      // Extended variable map: "namespace.property" -> value
      std::unordered_map<std::string, std::string> vars;
  };
  ```

- [ ] Add variable resolution to `ThemeEngine`
  ```cpp
  class ThemeEngine {
  public:
      // ... existing methods ...

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
  };
  ```

- [ ] Implement variable parsing in `load()` — JSON schema:
  ```json
  {
    "name": "catppuccin-mocha",
    "colors": { "bg": "#1E1E2E", "fg": "#CDD6F4", "accent": "#B4BEFE", "panel": "#313244" },
    "vars": {
      "surface.hover": "#45475A",
      "surface.active": "#585B70",
      "border.color": "#6C7086",
      "border.radius.sm": "2.0",
      "border.radius.lg": "8.0",
      "text.dim": "#A6ADC8",
      "success": "#A6E3A1",
      "warning": "#F9E2AF",
      "error": "#F38BA8",
      "spacing.sm": "4.0",
      "spacing.md": "8.0",
      "spacing.lg": "16.0",
      "toast.bg": "#313244",
      "toast.border": "#6C7086",
      "osd.bg": "#1E1E2ECC"
    },
    "font_size": 16.0,
    "corner_radius": 4.0
  }
  ```

- [ ] Implement `var()`, `color_var()`, `float_var()`, `set_var()` in `theme_engine.cpp`
  - `var()` does direct lookup in `current_theme_.vars`, returns fallback if missing
  - `color_var()` calls `parse_color()` on the var value
  - `float_var()` calls `std::stof()` with try/catch
  - `set_var()` writes to `current_theme_.vars` in-place (runtime override for live preview)

### Step 1.2 — Theme engine: hot-reload and live preview panel

- [ ] Refactor `watch_for_changes()` / `poll_changes()` to support watching a directory
  - Watch `/etc/straylight/themes/` and `~/.config/straylight/themes/` via inotify
  - On `IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE`, reload the matching theme file
  - Debounce: ignore events within 100ms of a previous reload (editors write temp files)
  ```cpp
  // Add to ThemeEngine private:
  std::chrono::steady_clock::time_point last_reload_time_{};
  static constexpr auto kReloadDebounce = std::chrono::milliseconds(100);
  ```

- [ ] Add `render_live_preview()` method to `ThemeEngine`
  ```cpp
  /// Render an ImGui panel showing all theme variables with live editing.
  /// Changes apply immediately via set_var() but are NOT persisted until
  /// save_current() is called.
  void render_live_preview(bool* p_open);

  /// Persist current theme (including runtime overrides) to a JSON file.
  Result<void, SLError> save_current(std::string_view path);
  ```

- [ ] Implement `render_live_preview()` in `theme_engine.cpp`
  - ImGui window titled "Theme Editor"
  - Section 1: Core colors — four `ImGui::ColorEdit4` widgets for bg/fg/accent/panel
  - Section 2: Extended vars — iterate `var_keys()`, render `ColorEdit4` for hex vars, `DragFloat` for numeric vars
  - Section 3: Buttons — "Apply" (calls `apply(ImGui::GetStyle())`), "Save" (calls `save_current()`), "Revert" (re-`load()` from disk)
  - All edits call `set_var()` and immediately re-apply style

- [ ] Implement `save_current()` — serialize `current_theme_` back to JSON, write atomically (write to `.tmp`, rename)

- [ ] Write `tests/unit/shell/test_theme_engine_vars.cpp`
  - TEST: `var()` returns fallback for missing key
  - TEST: `color_var()` parses "#FF0000" correctly to ABGR
  - TEST: `float_var()` parses "8.0" to 8.0f
  - TEST: `set_var()` overrides existing variable
  - TEST: `load()` populates vars map from JSON
  - TEST: `save_current()` round-trips correctly

### Step 1.3 — Notification system: urgency levels and actions

Upgrade `shell/widgets/notification.h/.cpp` from basic toasts to a full notification system.

- [ ] Extend `Notification` struct
  ```cpp
  enum class Urgency : uint8_t { Low = 0, Normal = 1, Critical = 2 };

  struct NotifAction {
      std::string key;    // e.g. "reply", "dismiss"
      std::string label;  // e.g. "Reply", "Dismiss"
  };

  struct Notification {
      uint32_t    id = 0;
      std::string app_name, summary, body, icon;
      Urgency     urgency = Urgency::Normal;
      int         expire_ms = 5000;    // -1 = persistent (Critical default)
      double      created_at = 0.0;
      std::vector<NotifAction> actions;
      bool        resident = false;    // true = stays in history after dismiss
  };
  ```

- [ ] Extend `NotificationManager`
  ```cpp
  class NotificationManager {
  public:
      static constexpr int kMaxToasts = 5;
      static constexpr int kMaxHistory = 100;

      using ActionCallback = std::function<void(uint32_t id, std::string_view action_key)>;

      NotificationManager();
      ~NotificationManager();

      /// Send notification with full parameters.
      uint32_t notify(Notification notif);

      /// Convenience: simple text notification.
      uint32_t notify(const std::string& app, const std::string& summary,
                      const std::string& body, Urgency urgency = Urgency::Normal,
                      int expire_ms = 5000);

      void close(uint32_t id);
      void close_all();

      /// Register callback for action button clicks.
      void set_action_callback(ActionCallback cb);

      /// Render active toasts. Call once per frame.
      void render();

      /// Render notification history panel (for settings/notification center).
      void render_history(bool* p_open);

      /// Toggle Do Not Disturb mode (suppresses visual toasts, still queues).
      void set_dnd(bool enabled);
      [[nodiscard]] bool dnd() const;

      [[nodiscard]] int count() const;
      [[nodiscard]] const std::deque<Notification>& queue() const;
      [[nodiscard]] const std::vector<Notification>& history() const;

  private:
      std::deque<Notification> queue_;
      std::vector<Notification> history_;
      uint32_t next_id_ = 1;
      bool dnd_ = false;
      ActionCallback action_cb_;

      void expire_old();
      void push_to_history(const Notification& n);
      ImVec4 urgency_accent(Urgency u) const;
  };
  ```

- [ ] Implement enhanced `render()` in `notification.cpp`
  - Toast styling varies by urgency:
    - `Low`: dim border, smaller font, auto-dismiss 3s
    - `Normal`: standard theme accent border, 5s
    - `Critical`: red/error-color border, pulsing glow, persistent until dismissed
  - Each toast renders action buttons in a row below the body text
  - Click an action button -> calls `action_cb_(id, key)` then closes the toast
  - Slide-in animation: toasts lerp from x=screen_right to final position over 200ms
  - DND mode: skip rendering but still queue and history-track

- [ ] Implement `render_history()` — scrollable ImGui panel listing past notifications
  - Show timestamp, app name, summary, urgency badge
  - "Clear History" button at top

- [ ] Implement `push_to_history()` — moves dismissed/expired notifications into `history_`, capped at `kMaxHistory`

- [ ] Write `tests/unit/shell/test_notification_full.cpp`
  - TEST: Critical notification does not auto-expire
  - TEST: DND suppresses render count but queues persist
  - TEST: Action callback fires with correct id and key
  - TEST: History is capped at kMaxHistory
  - TEST: close_all() empties queue

### Step 1.4 — CMake and main.cpp integration

- [ ] Update `shell/CMakeLists.txt` — no new source files in this chunk, just verify existing sources compile with new headers

- [ ] Update `shell/main.cpp` — wire theme live-preview toggle (e.g. Super+T hotkey placeholder) and pass `ThemeEngine` reference to notification manager for urgency colors
  ```cpp
  // In the main loop, after existing render calls:
  if (show_theme_editor) {
      theme_engine.render_live_preview(&show_theme_editor);
  }
  ```

---

## Chunk 2: Screenshot Tool + Volume OSD + Keyboard Shortcuts

### Step 2.1 — Screenshot tool: wlr-screencopy integration

Replace the stub in `shell/widgets/screenshot.h/.cpp` with a real implementation.

- [ ] Update `shell/CMakeLists.txt` — generate protocol bindings for `wlr-screencopy-unstable-v1.xml`
  ```cmake
  set(SCREENCOPY_PROTOCOL
      "${CMAKE_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")
  # Same pattern as layer-shell: wayland-scanner client-header + private-code
  ```
  - Add `pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)` for Chunk 2.2
  - Link `straylight-shell` against `${PIPEWIRE_LIBRARIES}` and `png` (libpng for screenshot saving)

- [ ] Rewrite `shell/widgets/screenshot.h`
  ```cpp
  #pragma once
  #include <straylight/result.h>
  #include <straylight/error.h>
  #include <functional>
  #include <string>

  struct wl_display;
  struct wl_output;

  namespace straylight::shell {

  enum class CaptureMode { FullScreen, Region, Window };

  struct CaptureRequest {
      CaptureMode mode = CaptureMode::FullScreen;
      int x = 0, y = 0, w = 0, h = 0;  // Region mode only
      std::string output_path;            // Empty = auto-generate
      bool copy_to_clipboard = true;
  };

  struct CaptureResult {
      std::string saved_path;
      int width = 0, height = 0;
      size_t file_size = 0;
  };

  /// Screenshot tool using wlr-screencopy-unstable-v1.
  /// Supports full-screen, rectangular region, and window capture.
  class Screenshot {
  public:
      using CompletionCb = std::function<void(Result<CaptureResult, SLError>)>;

      Screenshot();
      ~Screenshot();

      /// Initialize with Wayland globals. Must call before capture.
      void init(wl_display* display, void* screencopy_manager,
                wl_output* output);

      /// Start an async capture. Result delivered via callback.
      void capture(CaptureRequest req, CompletionCb on_complete);

      /// Render the region-selection overlay (interactive rubber-band).
      /// Returns true while selection is active.
      bool render_region_selector();

      /// Cancel an in-progress region selection.
      void cancel_selection();

      /// Generate default output path: ~/Pictures/screenshot-YYYYMMDD-HHMMSS.png
      static std::string default_path();

  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };

  } // namespace straylight::shell
  ```

- [ ] Implement `shell/widgets/screenshot.cpp`
  - `Impl` struct holds: wl_display*, screencopy_manager*, wl_output*, active SHM buffer, region selection state
  - `capture()` for FullScreen mode:
    1. `zwlr_screencopy_manager_v1_capture_output(manager, 0, output)`
    2. Listen for `buffer` event -> allocate wl_shm buffer (WL_SHM_FORMAT_ARGB8888)
    3. `zwlr_screencopy_frame_v1_copy(frame, buffer)`
    4. Listen for `ready` event -> encode SHM data to PNG via libpng -> write to `output_path`
    5. Call `on_complete` with CaptureResult
  - `capture()` for Region mode:
    1. Capture full screen first (same as above)
    2. Crop the SHM buffer to (x, y, w, h) before PNG encoding
  - `render_region_selector()`:
    1. Full-screen semi-transparent overlay (ImGui fullscreen window, 0.3 alpha)
    2. Track mouse down -> drag -> mouse up to define rectangle
    3. Draw rubber-band rectangle with dashed border
    4. On release: call `capture()` with Region mode and the selected rect
    5. ESC cancels
  - `default_path()`: `~/Pictures/screenshot-` + strftime `%Y%m%d-%H%M%S` + `.png`
  - PNG encoding helper: `write_png(const uint8_t* rgba, int w, int h, const std::string& path)`

- [ ] Write `tests/unit/shell/test_screenshot.cpp`
  - TEST: `default_path()` contains "screenshot-" and ".png"
  - TEST: PNG write helper produces valid file from synthetic RGBA buffer
  - TEST: Region crop produces correct dimensions

### Step 2.2 — Volume OSD: PipeWire integration

Replace the stub in `shell/widgets/volume_osd.h/.cpp`.

- [ ] Rewrite `shell/widgets/volume_osd.h`
  ```cpp
  #pragma once
  #include <atomic>
  #include <memory>
  #include <thread>

  namespace straylight::shell {

  /// Volume OSD overlay. Monitors the default PipeWire audio sink and
  /// displays a transient volume bar when the level changes.
  class VolumeOsd {
  public:
      VolumeOsd();
      ~VolumeOsd();

      /// Start PipeWire monitoring thread.
      void start();

      /// Stop monitoring.
      void stop();

      /// Show the OSD at a given level (0.0-1.0). Also called internally
      /// when PipeWire volume changes.
      void show(float level, bool muted = false);

      /// Render the OSD if visible. Call once per frame.
      void render();

      [[nodiscard]] bool is_visible() const;
      [[nodiscard]] float level() const;
      [[nodiscard]] bool muted() const;

  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };

  } // namespace straylight::shell
  ```

- [ ] Implement `shell/widgets/volume_osd.cpp`
  - `Impl` struct:
    ```cpp
    struct Impl {
        std::atomic<float> level{0.0f};
        std::atomic<bool> muted{false};
        std::atomic<bool> visible{false};
        double show_time = 0.0;
        static constexpr double kDisplayDuration = 2.0; // seconds

        // PipeWire state
        pw_main_loop* loop = nullptr;
        pw_context* context = nullptr;
        pw_core* core = nullptr;
        pw_registry* registry = nullptr;
        spa_hook registry_listener{};
        std::thread pw_thread;
        std::atomic<bool> running{false};
    };
    ```
  - `start()`:
    1. `pw_init(nullptr, nullptr)`
    2. Create `pw_main_loop`, `pw_context`, `pw_core` connected to default PipeWire server
    3. Get `pw_registry`, add listener for `PW_TYPE_INTERFACE_Node` events
    4. On node added with `media.class` = `Audio/Sink`: track as default sink
    5. Monitor the sink's `PW_KEY_NODE_DESCRIPTION` and volume params via `pw_node_subscribe_params(SPA_PARAM_Props)`
    6. On param change: extract `SPA_PROP_channelVolumes` and `SPA_PROP_mute`, convert cubic volume to linear, call `show()`
    7. Run `pw_main_loop_run()` on background thread
  - `stop()`: signal loop quit, join thread, destroy PipeWire objects
  - `render()`:
    1. If not visible, return
    2. Compute elapsed = `ImGui::GetTime() - show_time`; if > `kDisplayDuration`, set `visible = false`, return
    3. Fade alpha: full opacity for first 1.5s, then linear fade to 0 over 0.5s
    4. Render centered overlay:
       - Background: rounded rect, theme `osd.bg` color, 200x60px
       - Icon: speaker icon (muted = crossed out)
       - Bar: horizontal fill bar, accent color, width proportional to level
       - Text: percentage label right-aligned
    5. Position: bottom-center of screen, 80px from bottom edge

- [ ] Write `tests/unit/shell/test_volume_osd.cpp`
  - TEST: `show(0.5)` sets visible=true and level=0.5
  - TEST: `muted()` returns correct state after `show(0.5, true)`
  - TEST: Render does not crash when PipeWire is not connected (graceful fallback)

### Step 2.3 — Keyboard shortcuts manager

New files: `shell/settings/shortcuts.h` and `shell/settings/shortcuts.cpp`.

- [ ] Create `shell/settings/shortcuts.h`
  ```cpp
  #pragma once
  #include <straylight/result.h>
  #include <straylight/error.h>
  #include <functional>
  #include <string>
  #include <unordered_map>
  #include <vector>
  #include <cstdint>

  namespace straylight::shell {

  /// Modifier flags matching xkbcommon.
  enum class Mod : uint8_t {
      None  = 0,
      Ctrl  = 1 << 0,
      Alt   = 1 << 1,
      Shift = 1 << 2,
      Super = 1 << 3,
  };
  inline Mod operator|(Mod a, Mod b) {
      return static_cast<Mod>(uint8_t(a) | uint8_t(b));
  }
  inline bool operator&(Mod a, Mod b) {
      return (uint8_t(a) & uint8_t(b)) != 0;
  }

  /// A keybind: modifier combo + xkb keysym.
  struct Keybind {
      Mod mods = Mod::None;
      uint32_t keysym = 0;  // XKB_KEY_*
  };

  /// Named action with its current binding.
  struct ShortcutEntry {
      std::string action_id;   // e.g. "shell.screenshot.fullscreen"
      std::string label;       // e.g. "Screenshot (Full Screen)"
      Keybind bind;
      Keybind default_bind;    // for "Reset to Default"
  };

  /// Manages configurable keyboard shortcuts with JSON persistence.
  /// Keybinds are stored in ~/.config/straylight/keybinds.json.
  class ShortcutManager {
  public:
      using ActionHandler = std::function<void()>;

      ShortcutManager();
      ~ShortcutManager();

      /// Load keybinds from JSON file. Missing actions get defaults.
      Result<void, SLError> load(std::string_view path);

      /// Save current keybinds to JSON file.
      Result<void, SLError> save(std::string_view path) const;

      /// Register an action with a default keybind.
      void register_action(std::string_view action_id,
                           std::string_view label,
                           Keybind default_bind,
                           ActionHandler handler);

      /// Process a key event. Returns true if a shortcut was triggered.
      bool handle_key(Mod mods, uint32_t keysym);

      /// Rebind an action. Returns error if keysym conflicts.
      Result<void, SLError> rebind(std::string_view action_id,
                                    Keybind new_bind);

      /// Reset an action to its default binding.
      void reset_to_default(std::string_view action_id);

      /// Reset all bindings to defaults.
      void reset_all();

      /// Get all registered shortcuts (for settings UI).
      [[nodiscard]] const std::vector<ShortcutEntry>& entries() const;

      /// Render the keyboard shortcuts settings panel.
      void render_settings(bool* p_open);

      /// Convert keybind to human-readable string (e.g. "Super+Shift+S").
      static std::string keybind_to_string(const Keybind& kb);

      /// Default config path.
      static std::string default_config_path();

  private:
      std::vector<ShortcutEntry> entries_;
      std::unordered_map<std::string, ActionHandler> handlers_;

      // Conflict detection: serialized keybind -> action_id
      std::unordered_map<uint64_t, std::string> bind_map_;

      static uint64_t keybind_hash(const Keybind& kb);
  };

  } // namespace straylight::shell
  ```

- [ ] Implement `shell/settings/shortcuts.cpp`
  - `default_config_path()`: `$XDG_CONFIG_HOME/straylight/keybinds.json` (fallback `~/.config/...`)
  - `load()`: parse JSON array of `{ "action": "...", "mods": ["Ctrl","Shift"], "key": "Print" }`
    - Map key names to xkb keysyms via `xkb_keysym_from_name()`
    - Actions not in JSON keep their registered defaults
  - `save()`: serialize `entries_` to JSON, atomic write (tmp + rename)
  - `handle_key()`: compute `keybind_hash(mods, keysym)`, look up in `bind_map_`, call handler
  - `rebind()`: check `bind_map_` for conflicts, update entry and map
  - `render_settings()`:
    - ImGui table: columns = Action Label | Current Binding | [Edit] | [Reset]
    - Edit mode: capture next keypress via `ImGui::IsKeyPressed` scan + mod detection
    - Conflict warning shown inline if binding collides
  - `keybind_to_string()`: build string from mods ("Super+Ctrl+") + `xkb_keysym_get_name()`
  - JSON schema:
    ```json
    [
      { "action": "shell.screenshot.fullscreen", "mods": ["Super"], "key": "Print" },
      { "action": "shell.screenshot.region", "mods": ["Super","Shift"], "key": "Print" },
      { "action": "shell.volume.up", "mods": [], "key": "XF86AudioRaiseVolume" },
      { "action": "shell.volume.down", "mods": [], "key": "XF86AudioLowerVolume" },
      { "action": "shell.volume.mute", "mods": [], "key": "XF86AudioMute" },
      { "action": "shell.launcher.toggle", "mods": ["Super"], "key": "space" },
      { "action": "shell.notifications.dnd", "mods": ["Super"], "key": "n" },
      { "action": "shell.theme.editor", "mods": ["Super"], "key": "t" }
    ]
    ```

- [ ] Update `shell/CMakeLists.txt` — add `settings/shortcuts.cpp` to `SHELL_SOURCES`, link `xkbcommon`
  ```cmake
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
  # Add to SHELL_SOURCES: settings/shortcuts.cpp
  # Add to target_link_libraries: ${XKBCOMMON_LIBRARIES} ${PIPEWIRE_LIBRARIES} png
  ```

- [ ] Register default shortcuts in `shell/main.cpp`
  ```cpp
  ShortcutManager shortcuts;
  shortcuts.load(ShortcutManager::default_config_path());

  // Register default bindings
  shortcuts.register_action("shell.screenshot.fullscreen", "Screenshot (Full)",
      {Mod::Super, XKB_KEY_Print}, [&]{ screenshot.capture({CaptureMode::FullScreen}, ...); });
  shortcuts.register_action("shell.screenshot.region", "Screenshot (Region)",
      {Mod::Super | Mod::Shift, XKB_KEY_Print}, [&]{ /* start region selector */ });
  shortcuts.register_action("shell.launcher.toggle", "App Launcher",
      {Mod::Super, XKB_KEY_space}, [&]{ app_launcher.toggle(); });
  shortcuts.register_action("shell.notifications.dnd", "Do Not Disturb",
      {Mod::Super, XKB_KEY_n}, [&]{ notifications.set_dnd(!notifications.dnd()); });
  shortcuts.register_action("shell.theme.editor", "Theme Editor",
      {Mod::Super, XKB_KEY_t}, [&]{ show_theme_editor = !show_theme_editor; });
  ```

- [ ] Write `tests/unit/shell/test_shortcuts.cpp`
  - TEST: `register_action` + `handle_key` fires handler
  - TEST: `rebind` updates binding and old binding no longer fires
  - TEST: `rebind` returns conflict error if binding taken
  - TEST: `reset_to_default` restores original binding
  - TEST: `load`/`save` round-trip preserves all bindings
  - TEST: `keybind_to_string` produces correct output for Super+Shift+Print

---

## Chunk 3: Desktop Widget Framework for Dock Panels

### Step 3.1 — Widget framework: embedding WidgetBase in dock panels

New files: `shell/widgets/desktop_widget_framework.h` and `shell/widgets/desktop_widget_framework.cpp`.

- [ ] Create `shell/widgets/desktop_widget_framework.h`
  ```cpp
  #pragma once
  #include <straylight/widget.h>
  #include <memory>
  #include <string>
  #include <vector>
  #include <unordered_map>

  namespace straylight::shell {

  /// Where a widget can be embedded in the shell.
  enum class WidgetSlot {
      LeftDock,       // Left dock panel
      BottomDock,     // Bottom dock panel
      TopBarRight,    // Top bar right section
      Floating,       // Free-floating window
  };

  /// Configuration for an embedded widget instance.
  struct WidgetPlacement {
      std::string widget_id;           // Unique instance ID
      std::string widget_type;         // WidgetBase::name() match
      WidgetSlot slot = WidgetSlot::Floating;
      int order = 0;                   // Sort order within slot
      float width = 0, height = 0;     // 0 = auto-size
      bool visible = true;
  };

  /// Registry and layout manager for embeddable widgets.
  /// Allows Plan 13 ML/HPC widgets (or any WidgetBase) to be placed
  /// into dock panels, the top bar, or free-floating windows.
  class DesktopWidgetFramework {
  public:
      DesktopWidgetFramework();
      ~DesktopWidgetFramework();

      /// Register a widget type. The framework takes ownership.
      void register_widget(std::unique_ptr<straylight::WidgetBase> widget);

      /// Get all registered widget type names.
      [[nodiscard]] std::vector<std::string> available_types() const;

      /// Place a widget instance into a slot.
      Result<void, SLError> place(WidgetPlacement placement);

      /// Remove a widget instance by ID.
      void remove(std::string_view widget_id);

      /// Move a widget to a different slot.
      void move_to_slot(std::string_view widget_id, WidgetSlot slot);

      /// Get all placements for a given slot (sorted by order).
      [[nodiscard]] std::vector<const WidgetPlacement*>
          placements_for(WidgetSlot slot) const;

      /// Update all visible widgets (call once per frame at poll interval).
      void update_all();

      /// Render all widgets in a given slot. Called by the panel that
      /// owns the slot (e.g. LeftDock calls render_slot(LeftDock)).
      void render_slot(WidgetSlot slot);

      /// Render floating widgets as independent ImGui windows.
      void render_floating();

      /// Load widget layout from JSON config.
      Result<void, SLError> load_layout(std::string_view path);

      /// Save current layout to JSON config.
      Result<void, SLError> save_layout(std::string_view path) const;

      /// Render a widget-picker panel (drag widgets into slots).
      void render_picker(bool* p_open);

  private:
      // Type registry: type_name -> prototype widget
      std::unordered_map<std::string, std::unique_ptr<straylight::WidgetBase>>
          type_registry_;

      // Active instances: widget_id -> (placement, instantiated widget)
      struct WidgetInstance {
          WidgetPlacement placement;
          straylight::WidgetBase* widget = nullptr; // borrowed from registry
          double last_update = 0.0;
      };
      std::vector<WidgetInstance> instances_;

      straylight::WidgetBase* find_type(std::string_view type_name);
  };

  } // namespace straylight::shell
  ```

- [ ] Implement `shell/widgets/desktop_widget_framework.cpp`
  - `register_widget()`: store in `type_registry_` keyed by `widget->name()`
  - `place()`: validate type exists, create `WidgetInstance`, add to `instances_`
  - `update_all()`: for each visible instance, check if `ImGui::GetTime() - last_update >= widget->poll_interval()`, if so call `widget->update()`
  - `render_slot()`:
    1. Filter `instances_` for matching slot, sort by order
    2. For each: `ImGui::BeginChild(widget_id, size, border)` -> `widget->render(&visible)` -> `ImGui::EndChild()`
    3. If `visible` becomes false, remove the instance
  - `render_floating()`: for each Floating instance, render as `ImGui::Begin(name, &visible)` window
  - `load_layout()`: parse JSON array of `WidgetPlacement` objects
    ```json
    [
      { "id": "gpu-monitor-1", "type": "GPU Monitor", "slot": "LeftDock", "order": 0 },
      { "id": "cpu-usage-1", "type": "CPU Usage", "slot": "BottomDock", "order": 1 },
      { "id": "net-traffic-1", "type": "Network Traffic", "slot": "Floating",
        "width": 400, "height": 300 }
    ]
    ```
  - `save_layout()`: serialize to JSON, atomic write
  - `render_picker()`:
    - Two-column layout: left = available widget types, right = current placements
    - Click a type -> choose slot from dropdown -> "Add" button
    - Existing placements show [Move] [Remove] buttons
    - Drag-and-drop reorder within a slot via `ImGui::BeginDragDropSource/Target`

- [ ] Update `shell/CMakeLists.txt` — add `widgets/desktop_widget_framework.cpp` to `SHELL_SOURCES`

### Step 3.2 — Dock panel integration

- [ ] Update `shell/panels/left_dock.h/.cpp` to host embedded widgets
  ```cpp
  class LeftDock {
  public:
      // ... existing ...

      /// Set the widget framework reference for embedded widgets.
      void set_widget_framework(DesktopWidgetFramework* fw);

      void render();  // now also calls fw_->render_slot(WidgetSlot::LeftDock)

  private:
      bool visible_ = true;
      DesktopWidgetFramework* fw_ = nullptr;
  };
  ```

- [ ] Update `shell/panels/bottom_dock.h/.cpp` — same pattern for `WidgetSlot::BottomDock`

- [ ] Update `shell/panels/top_bar.h/.cpp` — add a right-side section that calls `fw_->render_slot(WidgetSlot::TopBarRight)`

### Step 3.3 — Main loop wiring and final integration

- [ ] Update `shell/main.cpp` with full integration
  ```cpp
  #include "settings/shortcuts.h"
  #include "widgets/desktop_widget_framework.h"

  // After existing initialization:
  Screenshot screenshot;
  VolumeOsd volume_osd;
  volume_osd.start();

  ShortcutManager shortcuts;
  shortcuts.load(ShortcutManager::default_config_path());
  // ... register all default shortcuts ...

  DesktopWidgetFramework widget_fw;
  widget_fw.load_layout("~/.config/straylight/widget-layout.json");

  left_dock.set_widget_framework(&widget_fw);
  bottom_dock.set_widget_framework(&widget_fw);
  top_bar.set_widget_framework(&widget_fw);

  // In the render loop:
  widget_fw.update_all();

  top_bar.render(bar_width);
  left_dock.render();
  bottom_dock.render();
  widget_fw.render_floating();

  volume_osd.render();
  notifications.render();

  if (screenshot.render_region_selector()) { /* selection active */ }

  if (show_theme_editor) theme_engine.render_live_preview(&show_theme_editor);
  if (show_shortcuts)    shortcuts.render_settings(&show_shortcuts);
  if (show_widget_picker) widget_fw.render_picker(&show_widget_picker);
  ```

- [ ] Add Wayland keyboard event routing to ShortcutManager
  - In the existing `wl_keyboard` listener (or new one if not yet wired), on `key` event:
    1. Translate keycode to keysym via xkbcommon state
    2. Read modifier state from `wl_keyboard.modifiers` event
    3. Call `shortcuts.handle_key(mods, keysym)`

- [ ] Write `tests/unit/shell/test_widget_framework.cpp`
  - TEST: `register_widget` + `place` makes widget renderable in slot
  - TEST: `remove` removes instance
  - TEST: `move_to_slot` changes placement
  - TEST: `load_layout`/`save_layout` round-trip
  - TEST: `placements_for` returns sorted by order
  - TEST: `update_all` respects poll interval

- [ ] Write integration test `tests/integration/shell/test_shell_polish.cpp`
  - Smoke test: instantiate ThemeEngine, NotificationManager, ShortcutManager, DesktopWidgetFramework
  - Verify they can be wired together without crashes
  - Verify shortcut fires screenshot capture (mocked)
  - Verify notification with action callback triggers correctly

### Final CMakeLists diff summary

```cmake
# shell/CMakeLists.txt additions
set(SHELL_SOURCES
    # ... existing ...
    settings/shortcuts.cpp
    widgets/desktop_widget_framework.cpp
)

pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
find_package(PNG REQUIRED)

# Add screencopy protocol generation (same pattern as layer-shell)
set(SCREENCOPY_PROTOCOL
    "${CMAKE_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")
if(WAYLAND_SCANNER AND EXISTS "${SCREENCOPY_PROTOCOL}")
    # ... generate header and code, add to target_sources ...
endif()

target_link_libraries(straylight-shell PRIVATE
    # ... existing ...
    ${PIPEWIRE_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    PNG::PNG
)
```
