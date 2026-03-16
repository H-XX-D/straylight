# Plan 14: Shell Polish & Desktop Integration

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver five production-quality shell subsystems that complete the StrayLight desktop experience: a hot-reloadable JSON theme engine, a freedesktop-compliant notification daemon, a wlr-screencopy screenshot tool, a PipeWire volume OSD overlay, and a configurable keybind manager. All code extends the existing `straylight-shell` executable built in Plan 4.

**Architecture:** All code lives under `shell/` and is compiled into `straylight-shell`. The five subsystems are instantiated in `shell/main.cpp` and driven from the existing per-frame render loop. The theme engine owns ImGui style state. The notification daemon registers on D-Bus as `org.freedesktop.Notifications`. Screenshot uses `zwlr_screencopy_manager_v1` from wlr-screencopy-unstable-v1. Volume OSD subscribes to PipeWire property changes on a background thread. The keybind manager translates `wl_keyboard` key events via xkbcommon.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, wl_egl_window + EGL, wlr-layer-shell-unstable-v1, wlr-screencopy-unstable-v1, PipeWire 1.0+, sdbus-c++ 2.0+, xkbcommon, libpng, nlohmann/json 3.11+, inotify, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, Result<T,E>, SLError), Plan 4 (shell skeleton, LayerSurface, Renderer, main event loop)

**Development environment:** Linux x86_64, Debian Bookworm/Trixie. Needs `libpipewire-0.3-dev`, `libxkbcommon-dev`, `libsdbus-c++-dev`, `libpng-dev`, `wayland-protocols`. macOS cannot build.

---

## Chunk 1: Theme Engine

**Goal:** Implement `shell/themes/theme_engine.h/.cpp` — JSON theme definitions with a full color/spacing variable map, runtime theme switching, dark/light presets, inotify hot-reload, and an ImGui live-preview editor.

### Step 1.1 — CMakeLists additions for Chunk 1

- [ ] Edit `shell/CMakeLists.txt` — add `themes/theme_engine.cpp` to `SHELL_SOURCES` if not already present; no new packages required for this chunk

### Step 1.2 — `shell/themes/theme_engine.h`

- [ ] Create `shell/themes/theme_engine.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <nlohmann/json.hpp>
  #include <imgui.h>
  #include <string>
  #include <unordered_map>
  #include <vector>
  #include <cstdint>

  namespace straylight::shell {

  struct Theme {
      std::string name        = "default";
      uint32_t    bg          = 0xFF1E1E2E;  // ABGR
      uint32_t    fg          = 0xFFCDD6F4;
      uint32_t    accent      = 0xFFB4BEFE;
      uint32_t    panel       = 0xFF313244;
      float       font_size   = 16.0f;
      float       corner_radius = 4.0f;
      std::string icon_theme  = "straylight-icons";
      // Extended variables: "namespace.property" -> string value
      std::unordered_map<std::string, std::string> vars;
  };

  class ThemeEngine {
  public:
      ThemeEngine();
      ~ThemeEngine();

      /// Load theme from JSON file. Returns error on parse failure.
      Result<void, SLError> load(std::string_view path);

      /// Load theme by name from the system/user theme directories.
      Result<void, SLError> load_named(std::string_view name);

      /// Apply current theme to the provided ImGuiStyle.
      void apply(ImGuiStyle& style) const;

      /// Poll inotify fd for file changes; call once per frame.
      void poll_changes();

      /// Resolve a string variable. Returns fallback if key absent.
      [[nodiscard]] std::string var(std::string_view key,
                                    std::string_view fallback = "") const;

      /// Resolve a color variable to ABGR uint32.
      [[nodiscard]] uint32_t color_var(std::string_view key,
                                       uint32_t fallback = 0xFF000000) const;

      /// Resolve a float variable.
      [[nodiscard]] float float_var(std::string_view key,
                                    float fallback = 0.0f) const;

      /// Override a variable at runtime (not persisted).
      void set_var(std::string_view key, std::string_view value);

      /// Persist current theme (including runtime overrides) atomically.
      Result<void, SLError> save_current(std::string_view path) const;

      /// Render an ImGui live-preview editor panel.
      void render_live_preview(bool* p_open);

      [[nodiscard]] const Theme& current() const { return theme_; }

  private:
      Theme       theme_;
      std::string watch_path_;
      int         inotify_fd_  = -1;
      int         watch_wd_    = -1;
      double      last_reload_ = 0.0;
      static constexpr double kReloadDebounce = 0.1;

      static uint32_t parse_color(std::string_view hex);
      static ImVec4   to_imvec4(uint32_t abgr);
      void            apply_inotify_watch(std::string_view path);
  };

  } // namespace straylight::shell
  ```

### Step 1.3 — `shell/themes/theme_engine.cpp`

- [ ] Create `shell/themes/theme_engine.cpp`
  ```cpp
  #include "theme_engine.h"
  #include <straylight/log.h>
  #include <fstream>
  #include <sstream>
  #include <cmath>
  #include <sys/inotify.h>
  #include <unistd.h>
  #include <imgui.h>

  namespace straylight::shell {

  static uint32_t parse_color(std::string_view hex) {
      if (hex.size() >= 1 && hex[0] == '#') hex = hex.substr(1);
      if (hex.size() < 6) return 0xFF000000;
      uint32_t r = std::stoul(std::string(hex.substr(0,2)), nullptr, 16);
      uint32_t g = std::stoul(std::string(hex.substr(2,2)), nullptr, 16);
      uint32_t b = std::stoul(std::string(hex.substr(4,2)), nullptr, 16);
      uint32_t a = (hex.size() >= 8)
          ? std::stoul(std::string(hex.substr(6,2)), nullptr, 16)
          : 0xFF;
      return (a << 24) | (b << 16) | (g << 8) | r;  // ABGR
  }

  static ImVec4 to_imvec4(uint32_t abgr) {
      float r = ((abgr)       & 0xFF) / 255.0f;
      float g = ((abgr >> 8)  & 0xFF) / 255.0f;
      float b = ((abgr >> 16) & 0xFF) / 255.0f;
      float a = ((abgr >> 24) & 0xFF) / 255.0f;
      return {r, g, b, a};
  }

  ThemeEngine::ThemeEngine() {
      inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  }

  ThemeEngine::~ThemeEngine() {
      if (inotify_fd_ >= 0) close(inotify_fd_);
  }

  Result<void, SLError> ThemeEngine::load(std::string_view path) {
      std::ifstream f(std::string(path));
      if (!f) return Result<void, SLError>::error(
          SLError{SLError::Code::kConfigNotFound, std::string(path)});
      nlohmann::json j;
      try { j = nlohmann::json::parse(f); }
      catch (const nlohmann::json::exception& e) {
          return Result<void, SLError>::error(
              SLError{SLError::Code::kConfigParse, e.what()});
      }

      theme_.name         = j.value("name", "default");
      theme_.font_size    = j.value("font_size", 16.0f);
      theme_.corner_radius = j.value("corner_radius", 4.0f);
      theme_.icon_theme   = j.value("icon_theme", "straylight-icons");

      if (j.contains("colors")) {
          auto& c = j["colors"];
          if (c.contains("bg"))     theme_.bg     = parse_color(c["bg"].get<std::string>());
          if (c.contains("fg"))     theme_.fg     = parse_color(c["fg"].get<std::string>());
          if (c.contains("accent")) theme_.accent = parse_color(c["accent"].get<std::string>());
          if (c.contains("panel"))  theme_.panel  = parse_color(c["panel"].get<std::string>());
      }
      theme_.vars.clear();
      if (j.contains("vars")) {
          for (auto& [k, v] : j["vars"].items())
              theme_.vars[k] = v.get<std::string>();
      }

      watch_path_ = std::string(path);
      apply_inotify_watch(path);
      LOG_INFO("theme: loaded '{}'", theme_.name);
      return Result<void, SLError>::ok();
  }

  Result<void, SLError> ThemeEngine::load_named(std::string_view name) {
      std::string user_path =
          std::string(getenv("HOME") ? getenv("HOME") : "/root") +
          "/.config/straylight/themes/" + std::string(name) + ".json";
      if (auto r = load(user_path); r.is_ok()) return r;
      return load("/etc/straylight/themes/" + std::string(name) + ".json");
  }

  void ThemeEngine::apply(ImGuiStyle& s) const {
      s.WindowRounding    = theme_.corner_radius;
      s.FrameRounding     = theme_.corner_radius * 0.75f;
      s.ScrollbarRounding = theme_.corner_radius;
      s.GrabRounding      = theme_.corner_radius * 0.5f;
      s.FontGlobalScale   = theme_.font_size / 16.0f;

      s.Colors[ImGuiCol_WindowBg]        = to_imvec4(theme_.bg);
      s.Colors[ImGuiCol_ChildBg]         = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_PopupBg]         = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_Text]            = to_imvec4(theme_.fg);
      s.Colors[ImGuiCol_Border]          =
          to_imvec4(color_var("border.color", theme_.panel));
      s.Colors[ImGuiCol_FrameBg]         = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_FrameBgHovered]  =
          to_imvec4(color_var("surface.hover", theme_.panel));
      s.Colors[ImGuiCol_FrameBgActive]   =
          to_imvec4(color_var("surface.active", theme_.accent));
      s.Colors[ImGuiCol_TitleBg]         = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_TitleBgActive]   = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_Button]          = to_imvec4(theme_.accent);
      s.Colors[ImGuiCol_ButtonHovered]   =
          to_imvec4(color_var("surface.hover", theme_.accent));
      s.Colors[ImGuiCol_ButtonActive]    =
          to_imvec4(color_var("surface.active", theme_.accent));
      s.Colors[ImGuiCol_Header]          = to_imvec4(theme_.accent);
      s.Colors[ImGuiCol_HeaderHovered]   =
          to_imvec4(color_var("surface.hover", theme_.accent));
      s.Colors[ImGuiCol_ScrollbarBg]     = to_imvec4(theme_.bg);
      s.Colors[ImGuiCol_ScrollbarGrab]   = to_imvec4(theme_.panel);
      s.ItemSpacing   = {float_var("spacing.md", 8.0f), float_var("spacing.md", 8.0f)};
      s.FramePadding  = {float_var("spacing.sm", 4.0f), float_var("spacing.sm", 4.0f)};
  }

  void ThemeEngine::poll_changes() {
      if (inotify_fd_ < 0) return;
      char buf[4096];
      ssize_t n = read(inotify_fd_, buf, sizeof(buf));
      if (n <= 0) return;
      double now = ImGui::GetTime();
      if (now - last_reload_ < kReloadDebounce) return;
      last_reload_ = now;
      load(watch_path_);
      apply(ImGui::GetStyle());
  }

  std::string ThemeEngine::var(std::string_view key,
                                std::string_view fallback) const {
      auto it = theme_.vars.find(std::string(key));
      return (it != theme_.vars.end()) ? it->second : std::string(fallback);
  }

  uint32_t ThemeEngine::color_var(std::string_view key, uint32_t fallback) const {
      auto s = var(key);
      if (s.empty()) return fallback;
      try { return parse_color(s); } catch (...) { return fallback; }
  }

  float ThemeEngine::float_var(std::string_view key, float fallback) const {
      auto s = var(key);
      if (s.empty()) return fallback;
      try { return std::stof(s); } catch (...) { return fallback; }
  }

  void ThemeEngine::set_var(std::string_view key, std::string_view value) {
      theme_.vars[std::string(key)] = std::string(value);
  }

  Result<void, SLError> ThemeEngine::save_current(std::string_view path) const {
      nlohmann::json j;
      j["name"]          = theme_.name;
      j["font_size"]     = theme_.font_size;
      j["corner_radius"] = theme_.corner_radius;
      j["icon_theme"]    = theme_.icon_theme;

      auto to_hex = [](uint32_t abgr) -> std::string {
          char buf[10];
          uint8_t r = abgr & 0xFF, g = (abgr>>8)&0xFF,
                  b = (abgr>>16)&0xFF, a = (abgr>>24)&0xFF;
          snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
          return buf;
      };
      j["colors"]["bg"]     = to_hex(theme_.bg);
      j["colors"]["fg"]     = to_hex(theme_.fg);
      j["colors"]["accent"] = to_hex(theme_.accent);
      j["colors"]["panel"]  = to_hex(theme_.panel);
      for (auto& [k, v] : theme_.vars) j["vars"][k] = v;

      std::string tmp = std::string(path) + ".tmp";
      { std::ofstream f(tmp); if (!f)
          return Result<void, SLError>::error(
              SLError{SLError::Code::kIo, tmp});
        f << j.dump(2); }
      if (rename(tmp.c_str(), std::string(path).c_str()) != 0)
          return Result<void, SLError>::error(
              SLError{SLError::Code::kIo, "rename failed"});
      return Result<void, SLError>::ok();
  }

  void ThemeEngine::render_live_preview(bool* p_open) {
      if (!ImGui::Begin("Theme Editor", p_open)) { ImGui::End(); return; }
      ImGui::SeparatorText("Core Colors");
      auto edit_color = [&](const char* label, uint32_t& col) {
          ImVec4 v = to_imvec4(col);
          if (ImGui::ColorEdit4(label, &v.x)) {
              uint8_t r = uint8_t(v.x*255), g = uint8_t(v.y*255),
                      b = uint8_t(v.z*255), a = uint8_t(v.w*255);
              col = (a<<24)|(b<<16)|(g<<8)|r;
              apply(ImGui::GetStyle());
          }
      };
      edit_color("Background", theme_.bg);
      edit_color("Foreground", theme_.fg);
      edit_color("Accent",     theme_.accent);
      edit_color("Panel",      theme_.panel);
      ImGui::DragFloat("Font Size",     &theme_.font_size,     0.5f, 8.0f, 32.0f);
      ImGui::DragFloat("Corner Radius", &theme_.corner_radius, 0.5f, 0.0f, 16.0f);

      ImGui::SeparatorText("Extended Variables");
      for (auto& [k, v] : theme_.vars) {
          char buf[256]; snprintf(buf, sizeof(buf), "%s", v.c_str());
          ImGui::PushID(k.c_str());
          if (ImGui::InputText(k.c_str(), buf, sizeof(buf)))
              set_var(k, buf);
          ImGui::PopID();
      }

      ImGui::Separator();
      if (ImGui::Button("Apply"))  apply(ImGui::GetStyle());
      ImGui::SameLine();
      if (ImGui::Button("Save") && !watch_path_.empty())
          save_current(watch_path_);
      ImGui::SameLine();
      if (ImGui::Button("Revert") && !watch_path_.empty())
          load(watch_path_);
      ImGui::End();
  }

  void ThemeEngine::apply_inotify_watch(std::string_view path) {
      if (inotify_fd_ < 0) return;
      if (watch_wd_ >= 0) inotify_rm_watch(inotify_fd_, watch_wd_);
      watch_wd_ = inotify_add_watch(inotify_fd_, std::string(path).c_str(),
                                    IN_MODIFY | IN_CLOSE_WRITE);
  }

  } // namespace straylight::shell
  ```

### Step 1.4 — Theme JSON presets

- [ ] Create `shell/themes/default.json`
  ```json
  {
    "name": "default",
    "colors": { "bg": "#1E1E2E", "fg": "#CDD6F4",
                "accent": "#B4BEFE", "panel": "#313244" },
    "vars": {
      "surface.hover": "#45475A", "surface.active": "#585B70",
      "border.color": "#6C7086", "border.radius.sm": "2.0",
      "border.radius.lg": "8.0", "text.dim": "#A6ADC8",
      "success": "#A6E3A1", "warning": "#F9E2AF", "error": "#F38BA8",
      "spacing.sm": "4.0", "spacing.md": "8.0", "spacing.lg": "16.0",
      "toast.bg": "#313244CC", "osd.bg": "#1E1E2ECC"
    },
    "font_size": 16.0, "corner_radius": 4.0,
    "icon_theme": "straylight-icons"
  }
  ```

- [ ] Create `shell/themes/cyberpunk.json`
  ```json
  {
    "name": "cyberpunk",
    "colors": { "bg": "#0D0D1A", "fg": "#E0E0FF",
                "accent": "#FF007F", "panel": "#1A1A2E" },
    "vars": {
      "surface.hover": "#2A2A4A", "surface.active": "#FF007F44",
      "border.color": "#FF007F", "border.radius.sm": "0.0",
      "border.radius.lg": "2.0", "text.dim": "#9090AA",
      "success": "#00FF9F", "warning": "#FFD700", "error": "#FF3355",
      "spacing.sm": "4.0", "spacing.md": "8.0", "spacing.lg": "16.0",
      "toast.bg": "#1A1A2EEE", "osd.bg": "#0D0D1AEE"
    },
    "font_size": 15.0, "corner_radius": 0.0,
    "icon_theme": "straylight-icons"
  }
  ```

- [ ] Create `shell/themes/minimal.json`
  ```json
  {
    "name": "minimal",
    "colors": { "bg": "#FAFAFA", "fg": "#1A1A1A",
                "accent": "#0066CC", "panel": "#F0F0F0" },
    "vars": {
      "surface.hover": "#E8E8E8", "surface.active": "#0066CC22",
      "border.color": "#CCCCCC", "border.radius.sm": "2.0",
      "border.radius.lg": "6.0", "text.dim": "#666666",
      "success": "#00AA44", "warning": "#FF8800", "error": "#CC0000",
      "spacing.sm": "4.0", "spacing.md": "8.0", "spacing.lg": "16.0",
      "toast.bg": "#F0F0F0EE", "osd.bg": "#FAFAFAEE"
    },
    "font_size": 14.0, "corner_radius": 6.0,
    "icon_theme": "straylight-icons"
  }
  ```

### Step 1.5 — Tests

- [ ] Write `tests/unit/shell/test_theme_engine.cpp`
  - TEST: `load()` of `default.json` succeeds and sets `name = "default"`
  - TEST: `color_var("border.color")` parses hex to correct ABGR value
  - TEST: `float_var("spacing.md")` returns 8.0f
  - TEST: `set_var()` overrides an existing variable
  - TEST: missing key in `var()` returns provided fallback
  - TEST: malformed JSON returns `SLError::Code::kConfigParse`
  - TEST: `save_current()` round-trips through `load()` with identical values

- [ ] Run `ctest -R test_theme_engine` — must pass before Chunk 2

---

## Chunk 2: Notification System

**Goal:** Implement `shell/notifications/` — a freedesktop `org.freedesktop.Notifications` D-Bus daemon, notification center panel, urgency levels, action buttons, and expiry.

### Step 2.1 — CMakeLists additions for Chunk 2

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  find_package(SDBusCpp REQUIRED)   # sdbus-c++ for D-Bus
  list(APPEND SHELL_SOURCES
      notifications/notification_daemon.cpp
      notifications/notification_manager.cpp
  )
  target_link_libraries(straylight-shell PRIVATE SDBusCpp::sdbus-c++)
  ```

### Step 2.2 — `shell/notifications/notification_daemon.h`

- [ ] Create `shell/notifications/notification_daemon.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <sdbus-c++/sdbus-c++.h>
  #include <functional>
  #include <string>
  #include <map>

  namespace straylight::shell {

  struct NotifHints {
      int urgency = 1;  // 0=Low, 1=Normal, 2=Critical
      std::string category;
      std::string desktop_entry;
  };

  struct IncomingNotif {
      uint32_t    id = 0;
      std::string app_name, summary, body, icon;
      int         expire_ms = 5000;
      std::vector<std::pair<std::string,std::string>> actions; // key, label
      NotifHints  hints;
  };

  /// Implements org.freedesktop.Notifications on the session D-Bus.
  class NotificationDaemon {
  public:
      using NotifyCallback = std::function<void(IncomingNotif)>;
      using CloseCallback  = std::function<void(uint32_t id)>;

      explicit NotificationDaemon(sdbus::IConnection& conn);
      ~NotificationDaemon();

      void set_notify_callback(NotifyCallback cb);
      void set_close_callback(CloseCallback  cb);

      /// Emit NotificationClosed signal (reason: 1=expired,2=dismissed,3=close_notif,4=undefined)
      void emit_closed(uint32_t id, uint32_t reason);

      /// Emit ActionInvoked signal.
      void emit_action_invoked(uint32_t id, const std::string& action_key);

  private:
      std::unique_ptr<sdbus::IObject> obj_;
      NotifyCallback notify_cb_;
      CloseCallback  close_cb_;
      uint32_t       next_id_ = 1;

      uint32_t on_notify(const std::string& app_name, uint32_t replaces_id,
                         const std::string& icon, const std::string& summary,
                         const std::string& body,
                         const std::vector<std::string>& actions,
                         const std::map<std::string,sdbus::Variant>& hints,
                         int32_t expire_timeout);
      void on_close_notification(uint32_t id);
      void on_get_capabilities(std::vector<std::string>& caps);
      void on_get_server_info(std::string& name, std::string& vendor,
                              std::string& version, std::string& spec_version);
  };

  } // namespace straylight::shell
  ```

### Step 2.3 — `shell/notifications/notification_daemon.cpp`

- [ ] Create `shell/notifications/notification_daemon.cpp`
  ```cpp
  #include "notification_daemon.h"
  #include <straylight/log.h>

  namespace straylight::shell {

  static constexpr char kServiceName[]   = "org.freedesktop.Notifications";
  static constexpr char kObjectPath[]    = "/org/freedesktop/Notifications";
  static constexpr char kInterfaceName[] = "org.freedesktop.Notifications";

  NotificationDaemon::NotificationDaemon(sdbus::IConnection& conn) {
      obj_ = sdbus::createObject(conn, kObjectPath);

      obj_->registerMethod(kInterfaceName, "Notify",
          "susssasa{sv}i", "u",
          [this](sdbus::MethodCall call) {
              std::string app; uint32_t replaces; std::string icon, summary, body;
              std::vector<std::string> actions;
              std::map<std::string,sdbus::Variant> hints; int32_t timeout;
              call >> app >> replaces >> icon >> summary >> body
                   >> actions >> hints >> timeout;
              uint32_t id = on_notify(app, replaces, icon, summary, body,
                                      actions, hints, timeout);
              auto reply = call.createReply(); reply << id; reply.send();
          });

      obj_->registerMethod(kInterfaceName, "CloseNotification", "u", "",
          [this](sdbus::MethodCall call) {
              uint32_t id; call >> id; on_close_notification(id);
              call.createReply().send();
          });

      obj_->registerMethod(kInterfaceName, "GetCapabilities", "", "as",
          [this](sdbus::MethodCall call) {
              std::vector<std::string> caps;
              on_get_capabilities(caps);
              auto r = call.createReply(); r << caps; r.send();
          });

      obj_->registerMethod(kInterfaceName, "GetServerInformation", "", "ssss",
          [this](sdbus::MethodCall call) {
              std::string n, ve, ver, spec;
              on_get_server_info(n, ve, ver, spec);
              auto r = call.createReply(); r << n << ve << ver << spec; r.send();
          });

      obj_->registerSignal(kInterfaceName, "NotificationClosed", "uu");
      obj_->registerSignal(kInterfaceName, "ActionInvoked",       "us");
      obj_->finishRegistration();

      conn.requestName(kServiceName);
      LOG_INFO("notification daemon: registered on D-Bus as {}", kServiceName);
  }

  NotificationDaemon::~NotificationDaemon() = default;

  void NotificationDaemon::set_notify_callback(NotifyCallback cb)
      { notify_cb_ = std::move(cb); }
  void NotificationDaemon::set_close_callback(CloseCallback cb)
      { close_cb_ = std::move(cb); }

  void NotificationDaemon::emit_closed(uint32_t id, uint32_t reason) {
      obj_->emitSignal("NotificationClosed")
           .onInterface(kInterfaceName)
           .withArguments(id, reason);
  }

  void NotificationDaemon::emit_action_invoked(uint32_t id,
                                                const std::string& key) {
      obj_->emitSignal("ActionInvoked")
           .onInterface(kInterfaceName)
           .withArguments(id, key);
  }

  uint32_t NotificationDaemon::on_notify(
      const std::string& app_name, uint32_t replaces_id,
      const std::string& icon,     const std::string& summary,
      const std::string& body,
      const std::vector<std::string>& actions,
      const std::map<std::string,sdbus::Variant>& hints,
      int32_t expire_timeout)
  {
      uint32_t id = (replaces_id > 0) ? replaces_id : next_id_++;
      IncomingNotif n;
      n.id = id; n.app_name = app_name; n.icon = icon;
      n.summary = summary; n.body = body;
      n.expire_ms = (expire_timeout == -1) ? 5000 :
                    (expire_timeout ==  0) ? -1   : expire_timeout;

      for (size_t i = 0; i + 1 < actions.size(); i += 2)
          n.actions.emplace_back(actions[i], actions[i+1]);

      if (hints.count("urgency"))
          n.hints.urgency = sdbus::message_element_to<uint8_t>(hints.at("urgency"));
      if (hints.count("category"))
          n.hints.category = sdbus::message_element_to<std::string>(hints.at("category"));

      if (notify_cb_) notify_cb_(std::move(n));
      return id;
  }

  void NotificationDaemon::on_close_notification(uint32_t id) {
      if (close_cb_) close_cb_(id);
  }

  void NotificationDaemon::on_get_capabilities(
      std::vector<std::string>& caps)
  {
      caps = {"body", "actions", "urgency", "persistence"};
  }

  void NotificationDaemon::on_get_server_info(
      std::string& name, std::string& vendor,
      std::string& version, std::string& spec_version)
  {
      name = "straylight-notifications"; vendor  = "StrayLight OS";
      version = "1.0";                   spec_version = "1.2";
  }

  } // namespace straylight::shell
  ```

### Step 2.4 — `shell/notifications/notification_manager.h`

- [ ] Create `shell/notifications/notification_manager.h`
  ```cpp
  #pragma once
  #include "notification_daemon.h"
  #include <deque>
  #include <vector>

  namespace straylight::shell {

  enum class Urgency : uint8_t { Low = 0, Normal = 1, Critical = 2 };

  struct Notification {
      uint32_t    id         = 0;
      std::string app_name, summary, body, icon;
      Urgency     urgency    = Urgency::Normal;
      int         expire_ms  = 5000;   // -1 = persistent
      double      created_at = 0.0;
      std::vector<std::pair<std::string,std::string>> actions;
      bool        resident   = false;
  };

  class NotificationManager {
  public:
      static constexpr int kMaxToasts  = 5;
      static constexpr int kMaxHistory = 100;

      using ActionCb = std::function<void(uint32_t id, std::string_view key)>;

      NotificationManager();
      ~NotificationManager();

      void push(IncomingNotif raw);
      void close(uint32_t id);
      void close_all();
      void set_action_callback(ActionCb cb);
      void set_dnd(bool v);
      [[nodiscard]] bool dnd() const { return dnd_; }
      [[nodiscard]] int  count() const { return int(queue_.size()); }

      /// Render active toasts. Call once per frame.
      void render();

      /// Render the notification center panel.
      void render_center(bool* p_open);

  private:
      std::deque<Notification>   queue_;
      std::vector<Notification>  history_;
      bool                       dnd_       = false;
      ActionCb                   action_cb_;

      void expire_old();
      ImVec4 urgency_color(Urgency u) const;
  };

  } // namespace straylight::shell
  ```

### Step 2.5 — `shell/notifications/notification_manager.cpp`

- [ ] Create `shell/notifications/notification_manager.cpp`
  ```cpp
  #include "notification_manager.h"
  #include <imgui.h>
  #include <algorithm>

  namespace straylight::shell {

  NotificationManager::NotificationManager() = default;
  NotificationManager::~NotificationManager() = default;

  void NotificationManager::push(IncomingNotif raw) {
      // Evict oldest if full
      while (int(queue_.size()) >= kMaxToasts) {
          history_.push_back(queue_.front());
          if (int(history_.size()) > kMaxHistory) history_.erase(history_.begin());
          queue_.pop_front();
      }
      Notification n;
      n.id         = raw.id;
      n.app_name   = raw.app_name;
      n.summary    = raw.summary;
      n.body       = raw.body;
      n.icon       = raw.icon;
      n.urgency    = static_cast<Urgency>(raw.hints.urgency);
      n.expire_ms  = raw.expire_ms;
      n.created_at = ImGui::GetTime();
      n.actions    = raw.actions;
      n.resident   = (n.urgency == Urgency::Critical);
      queue_.push_back(std::move(n));
  }

  void NotificationManager::close(uint32_t id) {
      auto it = std::find_if(queue_.begin(), queue_.end(),
                             [id](auto& n){ return n.id == id; });
      if (it == queue_.end()) return;
      history_.push_back(*it);
      if (int(history_.size()) > kMaxHistory) history_.erase(history_.begin());
      queue_.erase(it);
  }

  void NotificationManager::close_all() {
      for (auto& n : queue_) {
          history_.push_back(n);
          if (int(history_.size()) > kMaxHistory) history_.erase(history_.begin());
      }
      queue_.clear();
  }

  void NotificationManager::set_action_callback(ActionCb cb)
      { action_cb_ = std::move(cb); }

  void NotificationManager::set_dnd(bool v) { dnd_ = v; }

  void NotificationManager::expire_old() {
      double now = ImGui::GetTime();
      auto it = queue_.begin();
      while (it != queue_.end()) {
          if (it->expire_ms < 0 || it->resident) { ++it; continue; }
          double age = (now - it->created_at) * 1000.0;
          if (age >= it->expire_ms) {
              history_.push_back(*it);
              if (int(history_.size()) > kMaxHistory)
                  history_.erase(history_.begin());
              it = queue_.erase(it);
          } else { ++it; }
      }
  }

  ImVec4 NotificationManager::urgency_color(Urgency u) const {
      switch (u) {
          case Urgency::Low:      return {0.5f,0.5f,0.5f,1.0f};
          case Urgency::Normal:   return {0.70f,0.78f,1.0f,1.0f};
          case Urgency::Critical: return {0.95f,0.27f,0.33f,1.0f};
      }
      return {1,1,1,1};
  }

  void NotificationManager::render() {
      expire_old();
      if (dnd_ || queue_.empty()) return;

      ImGuiIO& io = ImGui::GetIO();
      float x = io.DisplaySize.x - 340.0f;
      float y = io.DisplaySize.y - 20.0f;
      int idx = 0;
      for (auto it = queue_.rbegin(); it != queue_.rend() && idx < kMaxToasts;
           ++it, ++idx) {
          auto& n = *it;
          float elapsed = float((ImGui::GetTime() - n.created_at) * 1000.0);
          float alpha = 1.0f;
          if (n.expire_ms > 0) {
              float remaining = float(n.expire_ms) - elapsed;
              if (remaining < 500.0f) alpha = remaining / 500.0f;
          }
          float toast_h = n.actions.empty() ? 70.0f : 90.0f;
          y -= toast_h + 8.0f;

          ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
          ImGui::SetNextWindowSize({320.0f, toast_h}, ImGuiCond_Always);
          ImGui::SetNextWindowBgAlpha(alpha * 0.92f);
          ImGui::PushStyleColor(ImGuiCol_Border, urgency_color(n.urgency));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

          char wid[32]; snprintf(wid, sizeof(wid), "##toast%u", n.id);
          ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove;
          if (ImGui::Begin(wid, nullptr, flags)) {
              ImGui::TextUnformatted(n.summary.c_str());
              ImGui::PushStyleColor(ImGuiCol_Text, {0.75f,0.75f,0.75f,alpha});
              ImGui::TextWrapped("%s", n.body.c_str());
              ImGui::PopStyleColor();

              for (auto& [key, label] : n.actions) {
                  if (ImGui::SmallButton(label.c_str())) {
                      if (action_cb_) action_cb_(n.id, key);
                      close(n.id);
                  }
                  ImGui::SameLine();
              }
              if (!n.actions.empty() || n.resident)
                  if (ImGui::SmallButton("X")) close(n.id);
          }
          ImGui::End();
          ImGui::PopStyleVar();
          ImGui::PopStyleColor();
      }
  }

  void NotificationManager::render_center(bool* p_open) {
      if (!ImGui::Begin("Notifications", p_open)) { ImGui::End(); return; }
      if (ImGui::Button("Clear All")) history_.clear();
      ImGui::Separator();
      for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
          const char* badge = (it->urgency == Urgency::Critical) ? "[!]" :
                              (it->urgency == Urgency::Low)      ? "[ ]" : "[i]";
          ImGui::TextColored(urgency_color(it->urgency), "%s", badge);
          ImGui::SameLine();
          ImGui::Text("[%s] %s", it->app_name.c_str(), it->summary.c_str());
          if (!it->body.empty())
              ImGui::TextDisabled("  %s", it->body.c_str());
          ImGui::Separator();
      }
      ImGui::End();
  }

  } // namespace straylight::shell
  ```

### Step 2.6 — Tests

- [ ] Write `tests/unit/shell/test_notification_manager.cpp`
  - TEST: `push()` adds to queue; `count()` increments
  - TEST: Critical notification has `resident = true`, does not auto-expire
  - TEST: Normal notification with `expire_ms = 1` is removed after expiry
  - TEST: 6th push evicts oldest (queue capped at `kMaxToasts`)
  - TEST: `close(id)` removes correct entry and adds to history
  - TEST: `close_all()` empties queue
  - TEST: DND set to true: `render()` draws nothing (check ImGui draw list)
  - TEST: Action callback fires with correct id and key

- [ ] Run `ctest -R test_notification` — must pass before Chunk 3

---

## Chunk 3: Screenshot Tool

**Goal:** Implement `shell/screenshot/screenshot.h/.cpp` — full-screen, region, and window capture via `wlr-screencopy-unstable-v1`, save to PNG via libpng, and copy to clipboard.

### Step 3.1 — CMakeLists additions for Chunk 3

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  find_package(PNG REQUIRED)
  set(SCREENCOPY_XML
      "${CMAKE_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")
  wayland_generate_protocol(${SCREENCOPY_XML} screencopy_hdr screencopy_src)
  list(APPEND SHELL_SOURCES screenshot/screenshot.cpp ${screencopy_src})
  target_include_directories(straylight-shell PRIVATE ${CMAKE_BINARY_DIR}/protocols)
  target_link_libraries(straylight-shell PRIVATE PNG::PNG)
  ```

### Step 3.2 — `shell/screenshot/screenshot.h`

- [ ] Create `shell/screenshot/screenshot.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <functional>
  #include <string>
  #include <cstdint>

  struct wl_display; struct wl_output; struct wl_shm;
  struct zwlr_screencopy_manager_v1;

  namespace straylight::shell {

  enum class CaptureMode { FullScreen, Region };

  struct CaptureRequest {
      CaptureMode mode           = CaptureMode::FullScreen;
      int         rx = 0, ry = 0, rw = 0, rh = 0;  // Region mode
      std::string output_path;     // empty = auto-generate
      bool        to_clipboard = true;
  };

  struct CaptureResult {
      std::string path;
      int width = 0, height = 0;
      size_t file_size = 0;
  };

  class Screenshot {
  public:
      using CompletionCb = std::function<void(Result<CaptureResult, SLError>)>;

      Screenshot();
      ~Screenshot();

      void init(wl_display* dpy, zwlr_screencopy_manager_v1* mgr,
                wl_output* output, wl_shm* shm);

      /// Begin async capture. Result delivered on next Wayland roundtrip.
      void capture(CaptureRequest req, CompletionCb cb);

      /// Render region-selection overlay. Returns true while active.
      bool render_region_selector();
      void cancel_selection();

      static std::string default_path();

  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };

  } // namespace straylight::shell
  ```

### Step 3.3 — `shell/screenshot/screenshot.cpp`

- [ ] Create `shell/screenshot/screenshot.cpp`
  ```cpp
  #include "screenshot.h"
  #include <straylight/log.h>
  #include <imgui.h>
  #include <png.h>
  #include <wayland-client.h>
  #include "wlr-screencopy-unstable-v1-client-protocol.h"
  #include <ctime>
  #include <cstdio>
  #include <cstring>
  #include <sys/mman.h>
  #include <unistd.h>
  #include <fcntl.h>

  namespace straylight::shell {

  struct Screenshot::Impl {
      wl_display*                   dpy     = nullptr;
      zwlr_screencopy_manager_v1*   mgr     = nullptr;
      wl_output*                    output  = nullptr;
      wl_shm*                       shm     = nullptr;
      CompletionCb                  cb;
      CaptureRequest                req;

      // SHM buffer state
      wl_shm_pool*                  pool    = nullptr;
      wl_buffer*                    buf     = nullptr;
      uint8_t*                      data    = nullptr;
      int                           buf_fd  = -1;
      int                           bw = 0, bh = 0;
      bool                          ready   = false;
      bool                          failed  = false;

      // Region selector
      bool    sel_active  = false;
      bool    sel_dragging = false;
      float   sel_x0 = 0, sel_y0 = 0, sel_x1 = 0, sel_y1 = 0;
  };

  // ---- wlr_screencopy_frame callbacks ----
  static void frame_buffer(void* data,
      zwlr_screencopy_frame_v1* frame, uint32_t fmt,
      uint32_t w, uint32_t h, uint32_t /*stride*/)
  {
      auto* d = static_cast<Screenshot::Impl*>(data);
      d->bw = int(w); d->bh = int(h);
      size_t sz = w * h * 4;
      char path[] = "/dev/shm/sl-screen-XXXXXX";
      d->buf_fd = mkstemp(path); unlink(path);
      ftruncate(d->buf_fd, sz);
      d->data = static_cast<uint8_t*>(
          mmap(nullptr, sz, PROT_READ|PROT_WRITE, MAP_SHARED, d->buf_fd, 0));

      d->pool = wl_shm_create_pool(d->shm, d->buf_fd, sz);
      d->buf  = wl_shm_pool_create_buffer(d->pool, 0, w, h, w*4,
                    WL_SHM_FORMAT_ARGB8888);
      zwlr_screencopy_frame_v1_copy(frame, d->buf);
  }

  static void frame_flags(void*, zwlr_screencopy_frame_v1*, uint32_t) {}
  static void frame_ready(void* data, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t)
      { static_cast<Screenshot::Impl*>(data)->ready = true; }
  static void frame_failed(void* data, zwlr_screencopy_frame_v1*)
      { static_cast<Screenshot::Impl*>(data)->failed = true; }
  static void frame_damage(void*, zwlr_screencopy_frame_v1*,
      uint32_t, uint32_t, uint32_t, uint32_t) {}
  static void frame_linux_dmabuf(void*, zwlr_screencopy_frame_v1*,
      uint32_t, uint32_t, uint32_t) {}
  static void frame_buffer_done(void*, zwlr_screencopy_frame_v1*) {}

  static const zwlr_screencopy_frame_v1_listener kFrameListener = {
      frame_buffer, frame_flags, frame_ready, frame_failed,
      frame_damage, frame_linux_dmabuf, frame_buffer_done,
  };

  // ---- PNG write ----
  static Result<size_t, SLError> write_png(
      const uint8_t* argb, int w, int h, const std::string& path)
  {
      FILE* fp = fopen(path.c_str(), "wb");
      if (!fp) return Result<size_t, SLError>::error(
          SLError{SLError::Code::kIo, path});
      png_structp ps = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                               nullptr, nullptr, nullptr);
      png_infop pi = png_create_info_struct(ps);
      png_init_io(ps, fp);
      png_set_IHDR(ps, pi, w, h, 8, PNG_COLOR_TYPE_RGBA,
                   PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                   PNG_FILTER_TYPE_DEFAULT);
      png_write_info(ps, pi);
      std::vector<uint8_t> row(w * 4);
      for (int y = 0; y < h; ++y) {
          const uint8_t* src = argb + y * w * 4;
          for (int x = 0; x < w; ++x) {   // ARGB → RGBA
              row[x*4+0] = src[x*4+2];    // R
              row[x*4+1] = src[x*4+1];    // G
              row[x*4+2] = src[x*4+0];    // B
              row[x*4+3] = src[x*4+3];    // A
          }
          png_write_row(ps, row.data());
      }
      png_write_end(ps, pi);
      png_destroy_write_struct(&ps, &pi);
      fclose(fp);
      struct stat st; stat(path.c_str(), &st);
      return Result<size_t, SLError>::ok(st.st_size);
  }

  Screenshot::Screenshot() : impl_(std::make_unique<Impl>()) {}
  Screenshot::~Screenshot() = default;

  void Screenshot::init(wl_display* dpy, zwlr_screencopy_manager_v1* mgr,
                        wl_output* output, wl_shm* shm) {
      impl_->dpy = dpy; impl_->mgr = mgr;
      impl_->output = output; impl_->shm = shm;
  }

  void Screenshot::capture(CaptureRequest req, CompletionCb cb) {
      impl_->req    = req;
      impl_->cb     = std::move(cb);
      impl_->ready  = false;
      impl_->failed = false;

      zwlr_screencopy_frame_v1* frame;
      if (req.mode == CaptureMode::Region)
          frame = zwlr_screencopy_manager_v1_capture_output_region(
              impl_->mgr, 0, impl_->output,
              req.rx, req.ry, req.rw, req.rh);
      else
          frame = zwlr_screencopy_manager_v1_capture_output(
              impl_->mgr, 0, impl_->output);

      zwlr_screencopy_frame_v1_add_listener(frame, &kFrameListener,
                                             impl_.get());
      wl_display_roundtrip(impl_->dpy);  // drives callbacks

      if (impl_->failed) {
          impl_->cb(Result<CaptureResult, SLError>::error(
              SLError{SLError::Code::kCaptureFailed, "screencopy failed"}));
          return;
      }

      std::string path = req.output_path.empty() ? default_path() : req.output_path;
      auto r = write_png(impl_->data, impl_->bw, impl_->bh, path);
      if (!r.is_ok()) { impl_->cb(r.map([](size_t){return CaptureResult{};})); return; }

      CaptureResult res;
      res.path = path; res.width = impl_->bw;
      res.height = impl_->bh; res.file_size = r.value();

      // Cleanup
      munmap(impl_->data, size_t(impl_->bw * impl_->bh * 4));
      wl_buffer_destroy(impl_->buf);
      wl_shm_pool_destroy(impl_->pool);
      close(impl_->buf_fd);
      impl_->data = nullptr; impl_->buf = nullptr;
      impl_->pool = nullptr; impl_->buf_fd = -1;

      LOG_INFO("screenshot: saved {} ({}x{})", path, res.width, res.height);
      impl_->cb(Result<CaptureResult, SLError>::ok(res));
  }

  bool Screenshot::render_region_selector() {
      if (!impl_->sel_active) return false;
      ImGuiIO& io = ImGui::GetIO();
      ImGui::SetNextWindowPos({0,0});
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::SetNextWindowBgAlpha(0.25f);
      ImGuiWindowFlags fl = ImGuiWindowFlags_NoDecoration |
                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
      ImGui::Begin("##region_sel", nullptr, fl);
      ImDrawList* dl = ImGui::GetWindowDrawList();

      if (ImGui::IsMouseClicked(0)) {
          impl_->sel_dragging = true;
          impl_->sel_x0 = io.MousePos.x;
          impl_->sel_y0 = io.MousePos.y;
      }
      if (impl_->sel_dragging) {
          impl_->sel_x1 = io.MousePos.x;
          impl_->sel_y1 = io.MousePos.y;
          dl->AddRectFilled({impl_->sel_x0, impl_->sel_y0},
                            {impl_->sel_x1, impl_->sel_y1},
                            IM_COL32(100,150,255,50));
          dl->AddRect({impl_->sel_x0, impl_->sel_y0},
                      {impl_->sel_x1, impl_->sel_y1},
                      IM_COL32(100,150,255,220), 0.0f, 0, 2.0f);
      }
      if (ImGui::IsMouseReleased(0) && impl_->sel_dragging) {
          impl_->sel_dragging = false;
          impl_->sel_active   = false;
          ImGui::End();
          // Fire region capture
          CaptureRequest req;
          req.mode = CaptureMode::Region;
          req.rx = int(std::min(impl_->sel_x0, impl_->sel_x1));
          req.ry = int(std::min(impl_->sel_y0, impl_->sel_y1));
          req.rw = int(std::abs(impl_->sel_x1 - impl_->sel_x0));
          req.rh = int(std::abs(impl_->sel_y1 - impl_->sel_y0));
          capture(req, [](auto){});
          return false;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel_selection();
      ImGui::End();
      return impl_->sel_active;
  }

  void Screenshot::cancel_selection() {
      impl_->sel_active   = false;
      impl_->sel_dragging = false;
  }

  std::string Screenshot::default_path() {
      char buf[256];
      std::time_t t = std::time(nullptr);
      std::tm* tm = std::localtime(&t);
      const char* home = getenv("HOME");
      snprintf(buf, sizeof(buf), "%s/Pictures/screenshot-%04d%02d%02d-%02d%02d%02d.png",
               home ? home : "/tmp",
               tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
               tm->tm_hour, tm->tm_min, tm->tm_sec);
      return buf;
  }

  } // namespace straylight::shell
  ```

### Step 3.4 — Tests

- [ ] Write `tests/unit/shell/test_screenshot.cpp`
  - TEST: `default_path()` contains "screenshot-" and ends with ".png"
  - TEST: PNG write helper produces a file with valid PNG header (magic bytes `\x89PNG`)
  - TEST: Region crop coordinates are computed correctly from mouse drag endpoints
  - TEST: `cancel_selection()` sets selector inactive immediately

- [ ] Run `ctest -R test_screenshot` — must pass before Chunk 4

---

## Chunk 4: Volume OSD

**Goal:** Implement `shell/osd/volume_osd.h/.cpp` — a layer-shell overlay that monitors the default PipeWire audio sink, shows a volume bar on level change, and auto-fades after 2 seconds.

### Step 4.1 — CMakeLists additions for Chunk 4

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
  list(APPEND SHELL_SOURCES osd/volume_osd.cpp)
  target_include_directories(straylight-shell PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
  target_link_libraries(straylight-shell PRIVATE ${PIPEWIRE_LIBRARIES})
  ```

### Step 4.2 — `shell/osd/volume_osd.h`

- [ ] Create `shell/osd/volume_osd.h`
  ```cpp
  #pragma once
  #include <atomic>
  #include <memory>
  #include <thread>

  namespace straylight::shell {

  class VolumeOsd {
  public:
      VolumeOsd();
      ~VolumeOsd();

      /// Start PipeWire monitoring on a background thread.
      void start();
      /// Stop monitoring and join thread.
      void stop();

      /// Externally trigger a show (e.g. from keybind handler).
      void show(float level, bool muted = false);

      /// Render the OSD if visible. Call once per frame.
      void render();

      [[nodiscard]] bool  is_visible() const;
      [[nodiscard]] float level() const;
      [[nodiscard]] bool  muted() const;

  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };

  } // namespace straylight::shell
  ```

### Step 4.3 — `shell/osd/volume_osd.cpp`

- [ ] Create `shell/osd/volume_osd.cpp`
  ```cpp
  #include "volume_osd.h"
  #include <straylight/log.h>
  #include <imgui.h>
  #include <pipewire/pipewire.h>
  #include <spa/param/props.h>
  #include <spa/pod/iter.h>
  #include <cmath>

  namespace straylight::shell {

  struct VolumeOsd::Impl {
      std::atomic<float> level{0.0f};
      std::atomic<bool>  muted{false};
      std::atomic<bool>  visible{false};
      double             show_time = 0.0;
      static constexpr double kDisplayDur = 2.0;
      static constexpr double kFadeStart  = 1.5;

      pw_main_loop* loop     = nullptr;
      pw_context*   context  = nullptr;
      pw_core*      core     = nullptr;
      pw_registry*  registry = nullptr;
      spa_hook      reg_listener{};
      pw_node*      sink_node  = nullptr;
      spa_hook      node_listener{};
      std::thread   pw_thread;
      std::atomic<bool> running{false};
  };

  static void on_param_changed(void* data, uint32_t /*id*/,
                                const struct spa_pod* param)
  {
      auto* impl = static_cast<VolumeOsd::Impl*>(data);
      if (!param) return;
      float vols[8] = {0}; uint32_t n_vols = 8;
      bool mute = false;
      spa_pod_parse_object(param,
          SPA_TYPE_OBJECT_Props, nullptr,
          SPA_PROP_mute,           SPA_POD_OPT_Bool(&mute),
          SPA_PROP_channelVolumes, SPA_POD_OPT_Array(&n_vols,
              SPA_TYPE_Float, sizeof(float), vols),
          0);
      if (n_vols > 0) {
          float sum = 0; for (uint32_t i = 0; i < n_vols; ++i) sum += vols[i];
          float avg = sum / float(n_vols);
          // Convert cubic volume to linear perceived: cbrt
          impl->level.store(std::cbrt(avg), std::memory_order_relaxed);
      }
      impl->muted.store(mute, std::memory_order_relaxed);
      impl->visible.store(true, std::memory_order_relaxed);
      impl->show_time = ImGui::GetTime();
  }

  static const pw_node_events kNodeEvents = {
      .version      = PW_VERSION_NODE_EVENTS,
      .param_changed = on_param_changed,
  };

  static void registry_global(void* data, uint32_t id,
                               uint32_t /*perms*/, const char* type,
                               uint32_t /*ver*/, const struct spa_dict* props)
  {
      auto* impl = static_cast<VolumeOsd::Impl*>(data);
      if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
      const char* cls = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
      if (!cls || strcmp(cls, "Audio/Sink") != 0) return;
      if (impl->sink_node) return;  // already tracking one

      impl->sink_node = static_cast<pw_node*>(
          pw_registry_bind(impl->registry, id,
                           PW_TYPE_INTERFACE_Node,
                           PW_VERSION_NODE, 0));
      pw_node_add_listener(impl->sink_node, &impl->node_listener,
                           &kNodeEvents, impl);
      pw_node_subscribe_params(impl->sink_node,
                               (uint32_t[]){SPA_PARAM_Props}, 1);
  }

  static const pw_registry_events kRegEvents = {
      .version = PW_VERSION_REGISTRY_EVENTS,
      .global  = registry_global,
  };

  VolumeOsd::VolumeOsd() : impl_(std::make_unique<Impl>()) {}
  VolumeOsd::~VolumeOsd() { stop(); }

  void VolumeOsd::start() {
      impl_->running = true;
      impl_->pw_thread = std::thread([this]{
          pw_init(nullptr, nullptr);
          impl_->loop    = pw_main_loop_new(nullptr);
          impl_->context = pw_context_new(
              pw_main_loop_get_loop(impl_->loop), nullptr, 0);
          impl_->core    = pw_context_connect(impl_->context, nullptr, 0);
          if (!impl_->core) {
              LOG_WARN("volume_osd: PipeWire connection failed");
              pw_main_loop_destroy(impl_->loop);
              impl_->loop = nullptr;
              return;
          }
          impl_->registry = pw_core_get_registry(impl_->core,
                                                  PW_VERSION_REGISTRY, 0);
          pw_registry_add_listener(impl_->registry, &impl_->reg_listener,
                                   &kRegEvents, impl_.get());
          pw_main_loop_run(impl_->loop);
          pw_core_disconnect(impl_->core);
          pw_context_destroy(impl_->context);
          pw_main_loop_destroy(impl_->loop);
          impl_->loop = nullptr;
      });
  }

  void VolumeOsd::stop() {
      if (!impl_->running.exchange(false)) return;
      if (impl_->loop) pw_main_loop_quit(impl_->loop);
      if (impl_->pw_thread.joinable()) impl_->pw_thread.join();
  }

  void VolumeOsd::show(float level, bool muted) {
      impl_->level.store(level);
      impl_->muted.store(muted);
      impl_->visible.store(true);
      impl_->show_time = ImGui::GetTime();
  }

  void VolumeOsd::render() {
      if (!impl_->visible.load()) return;
      double elapsed = ImGui::GetTime() - impl_->show_time;
      if (elapsed > Impl::kDisplayDur) { impl_->visible.store(false); return; }

      float alpha = 1.0f;
      if (elapsed > Impl::kFadeStart)
          alpha = 1.0f - float((elapsed - Impl::kFadeStart) /
                               (Impl::kDisplayDur - Impl::kFadeStart));

      ImGuiIO& io = ImGui::GetIO();
      float w = 220.0f, h = 56.0f;
      float x = (io.DisplaySize.x - w) * 0.5f;
      float y = io.DisplaySize.y - 100.0f;

      ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
      ImGui::SetNextWindowSize({w, h}, ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(alpha * 0.90f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
      ImGui::Begin("##osd", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs);

      bool m = impl_->muted.load();
      float lv = impl_->level.load();
      ImGui::Text("%s", m ? "[mute]" : "[vol ]");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w - 80.0f);
      float display = m ? 0.0f : lv;
      ImGui::ProgressBar(display, {-1.0f, 14.0f}, "");
      ImGui::SameLine();
      ImGui::Text("%3d%%", int(lv * 100.0f));

      ImGui::End();
      ImGui::PopStyleVar();
  }

  bool  VolumeOsd::is_visible() const { return impl_->visible.load(); }
  float VolumeOsd::level()      const { return impl_->level.load();   }
  bool  VolumeOsd::muted()      const { return impl_->muted.load();   }

  } // namespace straylight::shell
  ```

### Step 4.4 — Tests

- [ ] Write `tests/unit/shell/test_volume_osd.cpp`
  - TEST: `show(0.75f)` sets `is_visible() == true` and `level() == 0.75f`
  - TEST: `show(0.5f, true)` sets `muted() == true`
  - TEST: `render()` does not crash when PipeWire thread never started
  - TEST: after `kDisplayDur` seconds (mock time), `is_visible()` returns false

- [ ] Run `ctest -R test_volume_osd` — must pass before Chunk 5

---

## Chunk 5: Global Keybind Manager

**Goal:** Implement `shell/keybinds/keybind_manager.h/.cpp` — reads JSON config, registers named actions with handlers, dispatches from `wl_keyboard` events via xkbcommon, with a settings UI for rebinding.

### Step 5.1 — CMakeLists additions for Chunk 5

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
  list(APPEND SHELL_SOURCES keybinds/keybind_manager.cpp)
  target_include_directories(straylight-shell PRIVATE ${XKBCOMMON_INCLUDE_DIRS})
  target_link_libraries(straylight-shell PRIVATE ${XKBCOMMON_LIBRARIES})
  ```

### Step 5.2 — `shell/keybinds/keybind_manager.h`

- [ ] Create `shell/keybinds/keybind_manager.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <functional>
  #include <string>
  #include <vector>
  #include <unordered_map>
  #include <cstdint>
  #include <xkbcommon/xkbcommon.h>

  namespace straylight::shell {

  enum class Mod : uint8_t {
      None  = 0,
      Ctrl  = 1 << 0,
      Alt   = 1 << 1,
      Shift = 1 << 2,
      Super = 1 << 3,
  };
  inline Mod operator|(Mod a, Mod b)
      { return static_cast<Mod>(uint8_t(a) | uint8_t(b)); }
  inline bool has_mod(Mod mask, Mod bit)
      { return (uint8_t(mask) & uint8_t(bit)) != 0; }

  struct Keybind {
      Mod      mods   = Mod::None;
      xkb_keysym_t keysym = XKB_KEY_NoSymbol;
  };

  struct ShortcutEntry {
      std::string action_id;
      std::string label;
      Keybind     bind;
      Keybind     default_bind;
  };

  class KeybindManager {
  public:
      using Handler = std::function<void()>;

      KeybindManager();
      ~KeybindManager();

      Result<void, SLError> load(std::string_view path);
      Result<void, SLError> save(std::string_view path) const;

      void register_action(std::string_view action_id,
                           std::string_view label,
                           Keybind default_bind,
                           Handler handler);

      /// Process a key press. Returns true if a handler was triggered.
      bool handle_key(Mod mods, xkb_keysym_t keysym);

      Result<void, SLError> rebind(std::string_view action_id,
                                    Keybind new_bind);
      void reset_to_default(std::string_view action_id);
      void reset_all();

      [[nodiscard]] const std::vector<ShortcutEntry>& entries() const;

      /// Render the keybinds settings panel.
      void render_settings(bool* p_open);

      static std::string keybind_to_string(const Keybind& kb);
      static std::string default_config_path();

  private:
      std::vector<ShortcutEntry>             entries_;
      std::unordered_map<std::string, Handler> handlers_;
      std::unordered_map<uint64_t, std::string> bind_map_;
      int capturing_idx_ = -1;  // index into entries_ being rebound

      static uint64_t keybind_hash(const Keybind& kb);
  };

  } // namespace straylight::shell
  ```

### Step 5.3 — `shell/keybinds/keybind_manager.cpp`

- [ ] Create `shell/keybinds/keybind_manager.cpp`
  ```cpp
  #include "keybind_manager.h"
  #include <straylight/log.h>
  #include <nlohmann/json.hpp>
  #include <xkbcommon/xkbcommon.h>
  #include <imgui.h>
  #include <fstream>
  #include <cstdlib>
  #include <cstring>
  #include <climits>

  namespace straylight::shell {

  KeybindManager::KeybindManager() = default;
  KeybindManager::~KeybindManager() = default;

  uint64_t KeybindManager::keybind_hash(const Keybind& kb) {
      return (uint64_t(kb.keysym) << 8) | uint64_t(kb.mods);
  }

  static Mod parse_mods(const nlohmann::json& arr) {
      Mod m = Mod::None;
      for (auto& s : arr) {
          std::string ms = s.get<std::string>();
          if (ms == "Ctrl")  m = m | Mod::Ctrl;
          if (ms == "Alt")   m = m | Mod::Alt;
          if (ms == "Shift") m = m | Mod::Shift;
          if (ms == "Super") m = m | Mod::Super;
      }
      return m;
  }

  Result<void, SLError> KeybindManager::load(std::string_view path) {
      std::ifstream f(std::string(path));
      if (!f) return Result<void, SLError>::ok();  // missing = use defaults
      nlohmann::json j;
      try { j = nlohmann::json::parse(f); }
      catch (...) { return Result<void, SLError>::error(
          SLError{SLError::Code::kConfigParse, std::string(path)}); }

      for (auto& obj : j) {
          std::string action = obj.value("action", "");
          std::string key    = obj.value("key",    "");
          Mod mods = obj.contains("mods") ? parse_mods(obj["mods"]) : Mod::None;
          xkb_keysym_t sym = xkb_keysym_from_name(key.c_str(),
                                                    XKB_KEYSYM_NO_FLAGS);
          if (sym == XKB_KEY_NoSymbol) continue;
          Keybind kb{mods, sym};
          for (auto& e : entries_) {
              if (e.action_id == action) {
                  bind_map_.erase(keybind_hash(e.bind));
                  e.bind = kb;
                  bind_map_[keybind_hash(kb)] = action;
                  break;
              }
          }
      }
      return Result<void, SLError>::ok();
  }

  Result<void, SLError> KeybindManager::save(std::string_view path) const {
      nlohmann::json j = nlohmann::json::array();
      for (auto& e : entries_) {
          nlohmann::json obj;
          obj["action"] = e.action_id;
          nlohmann::json mods = nlohmann::json::array();
          if (has_mod(e.bind.mods, Mod::Ctrl))  mods.push_back("Ctrl");
          if (has_mod(e.bind.mods, Mod::Alt))   mods.push_back("Alt");
          if (has_mod(e.bind.mods, Mod::Shift)) mods.push_back("Shift");
          if (has_mod(e.bind.mods, Mod::Super)) mods.push_back("Super");
          obj["mods"] = mods;
          char name[64] = {};
          xkb_keysym_get_name(e.bind.keysym, name, sizeof(name));
          obj["key"] = name;
          j.push_back(obj);
      }
      std::string tmp = std::string(path) + ".tmp";
      { std::ofstream f(tmp); if (!f)
          return Result<void, SLError>::error(
              SLError{SLError::Code::kIo, tmp});
        f << j.dump(2); }
      if (rename(tmp.c_str(), std::string(path).c_str()) != 0)
          return Result<void, SLError>::error(
              SLError{SLError::Code::kIo, "rename failed"});
      return Result<void, SLError>::ok();
  }

  void KeybindManager::register_action(std::string_view action_id,
                                        std::string_view label,
                                        Keybind default_bind,
                                        Handler handler)
  {
      ShortcutEntry e;
      e.action_id    = std::string(action_id);
      e.label        = std::string(label);
      e.bind         = default_bind;
      e.default_bind = default_bind;
      entries_.push_back(e);
      handlers_[e.action_id] = std::move(handler);
      bind_map_[keybind_hash(default_bind)] = e.action_id;
  }

  bool KeybindManager::handle_key(Mod mods, xkb_keysym_t keysym) {
      Keybind kb{mods, keysym};
      auto it = bind_map_.find(keybind_hash(kb));
      if (it == bind_map_.end()) return false;
      auto hit = handlers_.find(it->second);
      if (hit == handlers_.end()) return false;
      hit->second();
      return true;
  }

  Result<void, SLError> KeybindManager::rebind(std::string_view action_id,
                                                 Keybind new_bind)
  {
      uint64_t h = keybind_hash(new_bind);
      auto conflict = bind_map_.find(h);
      if (conflict != bind_map_.end() && conflict->second != action_id)
          return Result<void, SLError>::error(
              SLError{SLError::Code::kKeybindConflict,
                      "binding taken by " + conflict->second});
      for (auto& e : entries_) {
          if (e.action_id == action_id) {
              bind_map_.erase(keybind_hash(e.bind));
              e.bind = new_bind;
              bind_map_[h] = e.action_id;
              return Result<void, SLError>::ok();
          }
      }
      return Result<void, SLError>::error(
          SLError{SLError::Code::kNotFound, std::string(action_id)});
  }

  void KeybindManager::reset_to_default(std::string_view action_id) {
      for (auto& e : entries_) {
          if (e.action_id == action_id) {
              bind_map_.erase(keybind_hash(e.bind));
              e.bind = e.default_bind;
              bind_map_[keybind_hash(e.default_bind)] = e.action_id;
              return;
          }
      }
  }

  void KeybindManager::reset_all() {
      bind_map_.clear();
      for (auto& e : entries_) {
          e.bind = e.default_bind;
          bind_map_[keybind_hash(e.default_bind)] = e.action_id;
      }
  }

  const std::vector<ShortcutEntry>& KeybindManager::entries() const
      { return entries_; }

  std::string KeybindManager::keybind_to_string(const Keybind& kb) {
      std::string s;
      if (has_mod(kb.mods, Mod::Super)) s += "Super+";
      if (has_mod(kb.mods, Mod::Ctrl))  s += "Ctrl+";
      if (has_mod(kb.mods, Mod::Alt))   s += "Alt+";
      if (has_mod(kb.mods, Mod::Shift)) s += "Shift+";
      char name[64] = {}; xkb_keysym_get_name(kb.keysym, name, sizeof(name));
      s += name;
      return s;
  }

  std::string KeybindManager::default_config_path() {
      const char* xdg = getenv("XDG_CONFIG_HOME");
      std::string base = xdg ? std::string(xdg)
                              : (std::string(getenv("HOME") ? getenv("HOME") : "/root")
                                 + "/.config");
      return base + "/straylight/keybinds.json";
  }

  void KeybindManager::render_settings(bool* p_open) {
      if (!ImGui::Begin("Keyboard Shortcuts", p_open)) { ImGui::End(); return; }
      if (ImGui::Button("Reset All")) reset_all();
      ImGui::Separator();

      if (ImGui::BeginTable("kb_table", 3,
              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
          ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthFixed, 180.0f);
          ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed, 120.0f);
          ImGui::TableHeadersRow();

          for (int i = 0; i < int(entries_.size()); ++i) {
              auto& e = entries_[i];
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::TextUnformatted(e.label.c_str());
              ImGui::TableSetColumnIndex(1);
              if (capturing_idx_ == i) {
                  ImGui::TextColored({1,1,0,1}, "Press a key...");
                  // Scan for any key pressed
                  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                      if (ImGui::IsKeyPressed(ImGuiKey(k))) {
                          // Map ImGuiKey back to xkb_keysym best-effort
                          // (production code would track via wl_keyboard)
                          capturing_idx_ = -1; break;
                      }
                  }
                  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) capturing_idx_ = -1;
              } else {
                  ImGui::TextUnformatted(keybind_to_string(e.bind).c_str());
              }
              ImGui::TableSetColumnIndex(2);
              ImGui::PushID(i);
              if (ImGui::SmallButton("Edit"))  capturing_idx_ = i;
              ImGui::SameLine();
              if (ImGui::SmallButton("Reset")) reset_to_default(e.action_id);
              ImGui::PopID();
          }
          ImGui::EndTable();
      }
      ImGui::End();
  }

  } // namespace straylight::shell
  ```

### Step 5.4 — Default keybinds config

- [ ] Create `shell/keybinds/default_keybinds.json`
  ```json
  [
    { "action": "shell.screenshot.fullscreen",  "mods": ["Super"],          "key": "Print" },
    { "action": "shell.screenshot.region",      "mods": ["Super","Shift"],  "key": "Print" },
    { "action": "shell.launcher.toggle",        "mods": ["Super"],          "key": "space" },
    { "action": "shell.notifications.dnd",      "mods": ["Super"],          "key": "n"     },
    { "action": "shell.notifications.center",   "mods": ["Super","Shift"],  "key": "n"     },
    { "action": "shell.volume.up",              "mods": [],                 "key": "XF86AudioRaiseVolume" },
    { "action": "shell.volume.down",            "mods": [],                 "key": "XF86AudioLowerVolume" },
    { "action": "shell.volume.mute",            "mods": [],                 "key": "XF86AudioMute"        },
    { "action": "shell.workspace.next",         "mods": ["Super"],          "key": "Right" },
    { "action": "shell.workspace.prev",         "mods": ["Super"],          "key": "Left"  },
    { "action": "shell.theme.editor",           "mods": ["Super"],          "key": "t"     },
    { "action": "shell.terminal.launch",        "mods": ["Super"],          "key": "Return"}
  ]
  ```

### Step 5.5 — Wire into `shell/main.cpp`

- [ ] Update `shell/main.cpp` — register default actions after constructing `KeybindManager`
  ```cpp
  #include "themes/theme_engine.h"
  #include "notifications/notification_daemon.h"
  #include "notifications/notification_manager.h"
  #include "screenshot/screenshot.h"
  #include "osd/volume_osd.h"
  #include "keybinds/keybind_manager.h"

  // --- Init ---
  ThemeEngine theme_engine;
  theme_engine.load_named("default");
  theme_engine.apply(ImGui::GetStyle());

  auto dbus_conn = sdbus::createSessionBusConnection();
  NotificationDaemon notif_daemon(*dbus_conn);
  NotificationManager notif_mgr;
  notif_daemon.set_notify_callback([&](IncomingNotif n){ notif_mgr.push(n); });
  notif_daemon.set_close_callback( [&](uint32_t id)   { notif_mgr.close(id); });

  Screenshot screenshot;
  screenshot.init(display, screencopy_manager, output, shm);

  VolumeOsd volume_osd;
  volume_osd.start();

  KeybindManager keybinds;
  keybinds.load(KeybindManager::default_config_path());

  using K = xkb_keysym_t;
  keybinds.register_action("shell.screenshot.fullscreen", "Screenshot (Full Screen)",
      {Mod::Super, XKB_KEY_Print}, [&]{
          screenshot.capture({CaptureMode::FullScreen}, [](auto){});
      });
  keybinds.register_action("shell.screenshot.region", "Screenshot (Region)",
      {Mod::Super | Mod::Shift, XKB_KEY_Print}, [&]{
          /* region selector activated via render loop */
      });
  keybinds.register_action("shell.launcher.toggle", "App Launcher",
      {Mod::Super, XKB_KEY_space}, [&]{ app_launcher.toggle(); });
  keybinds.register_action("shell.notifications.dnd", "Do Not Disturb",
      {Mod::Super, XKB_KEY_n}, [&]{
          notif_mgr.set_dnd(!notif_mgr.dnd());
      });
  keybinds.register_action("shell.volume.up", "Volume Up",
      {Mod::None, XKB_KEY_XF86AudioRaiseVolume}, [&]{
          volume_osd.show(std::min(1.0f, volume_osd.level() + 0.05f));
      });
  keybinds.register_action("shell.volume.down", "Volume Down",
      {Mod::None, XKB_KEY_XF86AudioLowerVolume}, [&]{
          volume_osd.show(std::max(0.0f, volume_osd.level() - 0.05f));
      });
  keybinds.register_action("shell.volume.mute", "Mute/Unmute",
      {Mod::None, XKB_KEY_XF86AudioMute}, [&]{
          volume_osd.show(volume_osd.level(), !volume_osd.muted());
      });
  keybinds.register_action("shell.theme.editor", "Theme Editor",
      {Mod::Super, XKB_KEY_t}, [&]{ show_theme_editor = !show_theme_editor; });

  // --- Render loop additions ---
  // In wl_keyboard.key callback:
  //   keybinds.handle_key(current_mods, xkb_state_key_get_one_sym(xkb_state, key+8));

  theme_engine.poll_changes();
  notif_mgr.render();
  volume_osd.render();
  screenshot.render_region_selector();
  if (show_theme_editor)    theme_engine.render_live_preview(&show_theme_editor);
  if (show_notif_center)    notif_mgr.render_center(&show_notif_center);
  if (show_keybind_settings) keybinds.render_settings(&show_keybind_settings);
  ```

### Step 5.6 — Tests

- [ ] Write `tests/unit/shell/test_keybind_manager.cpp`
  - TEST: `register_action` + `handle_key` fires handler exactly once
  - TEST: `handle_key` returns false for unregistered binding
  - TEST: `rebind` updates binding; old keysym no longer fires
  - TEST: `rebind` returns conflict error if binding already taken by another action
  - TEST: `reset_to_default` restores original binding after rebind
  - TEST: `reset_all` restores all bindings
  - TEST: `load`/`save` round-trip preserves all action ids and bindings
  - TEST: `keybind_to_string({Super|Shift, XKB_KEY_Print})` == `"Super+Shift+Print"`

- [ ] Run `ctest -R test_keybind_manager` — must pass

---

## Final Verification

- [ ] `cmake --preset dev && cmake --build --preset dev` — zero errors, zero warnings (`-Wall -Wextra -Werror`)
- [ ] `ctest --preset dev -R shell` — all shell unit tests pass
- [ ] AddressSanitizer: `cmake --preset dev -DENABLE_ASAN=ON && cmake --build --preset dev && ctest --preset dev` — zero leaks
- [ ] UBSan: `cmake --preset dev -DENABLE_UBSAN=ON && cmake --build --preset dev && ctest --preset dev` — zero UB
- [ ] Manual: launch `straylight-shell` on wlr-headless backend; verify top bar renders, `Super+Print` triggers screenshot, volume OSD appears on PipeWire volume change
- [ ] Manual: `notify-send -u critical "Test" "Body"` — critical toast appears with red border, persists until dismissed
- [ ] Manual: switch theme to `cyberpunk` via D-Bus — top bar re-styles live without restart
- [ ] `ldd /usr/bin/straylight-shell | grep 'not found'` — empty output (all deps satisfied)
- [ ] `valgrind --leak-check=full --error-exitcode=1 straylight-shell --headless --exit-after-frame` — no leaks
