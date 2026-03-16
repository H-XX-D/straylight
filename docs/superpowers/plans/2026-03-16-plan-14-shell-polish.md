# Plan 14: Shell Polish & Desktop Integration

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver five production-quality shell subsystems: a hot-reloadable JSON theme engine, a freedesktop notification daemon, a wlr-screencopy screenshot tool, a PipeWire volume OSD, and a configurable keybind manager. All code extends the existing `straylight-shell` executable built in Plan 4.

**Architecture:** All code lives under `shell/` and compiles into `straylight-shell`. Components are instantiated in `shell/main.cpp` and driven from the per-frame render loop. Theme engine owns ImGui style. Notification daemon registers on D-Bus as `org.freedesktop.Notifications`. Screenshot uses `zwlr_screencopy_manager_v1`. Volume OSD subscribes to PipeWire props on a background thread. Keybind manager translates `wl_keyboard` events via xkbcommon.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, wlr-layer-shell-unstable-v1, wlr-screencopy-unstable-v1, PipeWire 1.0+, sdbus-c++ 2.0+, xkbcommon, libpng, nlohmann/json 3.11+, inotify, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, `Result<T,E>`, `SLError`), Plan 4 (shell skeleton, `LayerSurface`, `Renderer`, main event loop)

**Development environment:** Linux x86_64, Debian Bookworm/Trixie. Needs `libpipewire-0.3-dev`, `libxkbcommon-dev`, `libsdbus-c++-dev`, `libpng-dev`, `wayland-protocols`. macOS cannot build.

---

## Chunk 1: Theme Engine

**Goal:** `shell/themes/theme_engine.h/.cpp` — JSON theme definitions, runtime switching, dark/light presets, inotify hot-reload, live-preview editor.

### Step 1.1 — CMakeLists

- [ ] Edit `shell/CMakeLists.txt` — append `themes/theme_engine.cpp` to `SHELL_SOURCES`.

### Step 1.2 — `shell/themes/theme_engine.h`

- [ ] Create `shell/themes/theme_engine.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <imgui.h>
  #include <string>
  #include <unordered_map>
  #include <cstdint>

  namespace straylight::shell {

  struct Theme {
      std::string name = "default";
      uint32_t bg = 0xFF1E1E2E, fg = 0xFFCDD6F4;  // ABGR
      uint32_t accent = 0xFFB4BEFE, panel = 0xFF313244;
      float font_size = 16.0f, corner_radius = 4.0f;
      std::string icon_theme = "straylight-icons";
      std::unordered_map<std::string, std::string> vars;
  };

  class ThemeEngine {
  public:
      ThemeEngine();
      ~ThemeEngine();

      Result<void, SLError> load(std::string_view path);
      Result<void, SLError> load_named(std::string_view name);
      void apply(ImGuiStyle& s) const;
      void poll_changes();   // call once per frame; drives inotify

      [[nodiscard]] std::string var(std::string_view key,
                                    std::string_view fallback = "") const;
      [[nodiscard]] uint32_t    color_var(std::string_view key,
                                          uint32_t fallback = 0xFF000000) const;
      [[nodiscard]] float       float_var(std::string_view key,
                                          float fallback = 0.0f) const;
      void set_var(std::string_view key, std::string_view value);
      Result<void, SLError> save_current(std::string_view path) const;
      void render_live_preview(bool* p_open);
      [[nodiscard]] const Theme& current() const { return theme_; }

  private:
      Theme       theme_;
      std::string watch_path_;
      int inotify_fd_ = -1, watch_wd_ = -1;
      double last_reload_ = 0.0;
      static constexpr double kDebounce = 0.1;

      static uint32_t parse_color(std::string_view hex);
      static ImVec4   to_imvec4(uint32_t abgr);
  };

  } // namespace straylight::shell
  ```

### Step 1.3 — `shell/themes/theme_engine.cpp`

- [ ] Create `shell/themes/theme_engine.cpp`
  ```cpp
  #include "theme_engine.h"
  #include <straylight/log.h>
  #include <nlohmann/json.hpp>
  #include <fstream>
  #include <sys/inotify.h>
  #include <unistd.h>
  #include <cmath>

  namespace straylight::shell {

  uint32_t ThemeEngine::parse_color(std::string_view hex) {
      if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
      if (hex.size() < 6) return 0xFF000000;
      auto byte = [&](int off){ return uint32_t(std::stoul(std::string(hex.substr(off,2)),nullptr,16)); };
      uint32_t r=byte(0),g=byte(2),b=byte(4);
      uint32_t a = (hex.size()>=8) ? byte(6) : 0xFF;
      return (a<<24)|(b<<16)|(g<<8)|r;
  }

  ImVec4 ThemeEngine::to_imvec4(uint32_t abgr) {
      return { ((abgr)&0xFF)/255.f, ((abgr>>8)&0xFF)/255.f,
               ((abgr>>16)&0xFF)/255.f, ((abgr>>24)&0xFF)/255.f };
  }

  ThemeEngine::ThemeEngine() {
      inotify_fd_ = inotify_init1(IN_NONBLOCK|IN_CLOEXEC);
  }
  ThemeEngine::~ThemeEngine() { if (inotify_fd_>=0) close(inotify_fd_); }

  Result<void, SLError> ThemeEngine::load(std::string_view path) {
      std::ifstream f(std::string(path));
      if (!f) return Result<void,SLError>::error({SLError::Code::kConfigNotFound,std::string(path)});
      nlohmann::json j;
      try { j = nlohmann::json::parse(f); }
      catch (const nlohmann::json::exception& e) {
          return Result<void,SLError>::error({SLError::Code::kConfigParse, e.what()});
      }
      theme_.name          = j.value("name","default");
      theme_.font_size     = j.value("font_size", 16.f);
      theme_.corner_radius = j.value("corner_radius", 4.f);
      theme_.icon_theme    = j.value("icon_theme", "straylight-icons");
      if (j.contains("colors")) {
          auto& c = j["colors"];
          if (c.contains("bg"))     theme_.bg     = parse_color(c["bg"].get<std::string>());
          if (c.contains("fg"))     theme_.fg     = parse_color(c["fg"].get<std::string>());
          if (c.contains("accent")) theme_.accent = parse_color(c["accent"].get<std::string>());
          if (c.contains("panel"))  theme_.panel  = parse_color(c["panel"].get<std::string>());
      }
      theme_.vars.clear();
      if (j.contains("vars"))
          for (auto& [k,v] : j["vars"].items()) theme_.vars[k] = v.get<std::string>();
      watch_path_ = std::string(path);
      if (inotify_fd_>=0) {
          if (watch_wd_>=0) inotify_rm_watch(inotify_fd_, watch_wd_);
          watch_wd_ = inotify_add_watch(inotify_fd_, watch_path_.c_str(),
                                        IN_MODIFY|IN_CLOSE_WRITE);
      }
      LOG_INFO("theme: loaded '{}'", theme_.name);
      return Result<void,SLError>::ok();
  }

  Result<void, SLError> ThemeEngine::load_named(std::string_view name) {
      const char* home = getenv("HOME") ?: "/root";
      std::string user = std::string(home)+"/.config/straylight/themes/"+std::string(name)+".json";
      if (auto r = load(user); r.is_ok()) return r;
      return load("/etc/straylight/themes/"+std::string(name)+".json");
  }

  void ThemeEngine::apply(ImGuiStyle& s) const {
      s.WindowRounding = theme_.corner_radius;
      s.FrameRounding  = theme_.corner_radius * 0.75f;
      s.FontGlobalScale = theme_.font_size / 16.f;
      s.Colors[ImGuiCol_WindowBg]       = to_imvec4(theme_.bg);
      s.Colors[ImGuiCol_ChildBg]        = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_Text]           = to_imvec4(theme_.fg);
      s.Colors[ImGuiCol_FrameBg]        = to_imvec4(theme_.panel);
      s.Colors[ImGuiCol_Button]         = to_imvec4(theme_.accent);
      s.Colors[ImGuiCol_ButtonHovered]  = to_imvec4(color_var("surface.hover", theme_.accent));
      s.Colors[ImGuiCol_Header]         = to_imvec4(theme_.accent);
      s.Colors[ImGuiCol_Border]         = to_imvec4(color_var("border.color", theme_.panel));
      s.ItemSpacing  = { float_var("spacing.md",8.f), float_var("spacing.md",8.f) };
      s.FramePadding = { float_var("spacing.sm",4.f), float_var("spacing.sm",4.f) };
  }

  void ThemeEngine::poll_changes() {
      if (inotify_fd_ < 0) return;
      char buf[512]; if (read(inotify_fd_,buf,sizeof(buf)) <= 0) return;
      double now = ImGui::GetTime();
      if (now - last_reload_ < kDebounce) return;
      last_reload_ = now;
      load(watch_path_); apply(ImGui::GetStyle());
  }

  std::string ThemeEngine::var(std::string_view k, std::string_view fb) const {
      auto it = theme_.vars.find(std::string(k));
      return (it != theme_.vars.end()) ? it->second : std::string(fb);
  }
  uint32_t ThemeEngine::color_var(std::string_view k, uint32_t fb) const {
      auto s = var(k); if (s.empty()) return fb;
      try { return parse_color(s); } catch (...) { return fb; }
  }
  float ThemeEngine::float_var(std::string_view k, float fb) const {
      auto s = var(k); if (s.empty()) return fb;
      try { return std::stof(s); } catch (...) { return fb; }
  }
  void ThemeEngine::set_var(std::string_view k, std::string_view v)
      { theme_.vars[std::string(k)] = std::string(v); }

  Result<void, SLError> ThemeEngine::save_current(std::string_view path) const {
      nlohmann::json j; j["name"]=theme_.name; j["font_size"]=theme_.font_size;
      j["corner_radius"]=theme_.corner_radius; j["icon_theme"]=theme_.icon_theme;
      auto hex=[](uint32_t abgr){ char b[10];
          snprintf(b,10,"#%02X%02X%02X%02X",abgr&0xFF,(abgr>>8)&0xFF,(abgr>>16)&0xFF,(abgr>>24)&0xFF);
          return std::string(b); };
      j["colors"]=nlohmann::json::object();
      j["colors"]["bg"]=hex(theme_.bg); j["colors"]["fg"]=hex(theme_.fg);
      j["colors"]["accent"]=hex(theme_.accent); j["colors"]["panel"]=hex(theme_.panel);
      for (auto& [k,v] : theme_.vars) j["vars"][k]=v;
      std::string tmp=std::string(path)+".tmp";
      { std::ofstream f(tmp); if (!f) return Result<void,SLError>::error({SLError::Code::kIo,tmp});
        f << j.dump(2); }
      if (rename(tmp.c_str(),std::string(path).c_str())!=0)
          return Result<void,SLError>::error({SLError::Code::kIo,"rename"});
      return Result<void,SLError>::ok();
  }

  void ThemeEngine::render_live_preview(bool* p_open) {
      if (!ImGui::Begin("Theme Editor", p_open)) { ImGui::End(); return; }
      auto edit=[&](const char* lbl, uint32_t& col){
          ImVec4 v=to_imvec4(col);
          if (ImGui::ColorEdit4(lbl,&v.x)) {
              col=(uint32_t(v.w*255)<<24)|(uint32_t(v.z*255)<<16)|
                  (uint32_t(v.y*255)<<8)|uint32_t(v.x*255);
              apply(ImGui::GetStyle()); }
      };
      edit("Background",theme_.bg); edit("Foreground",theme_.fg);
      edit("Accent",theme_.accent); edit("Panel",theme_.panel);
      ImGui::DragFloat("Font Size",&theme_.font_size,0.5f,8.f,32.f);
      ImGui::DragFloat("Corner Radius",&theme_.corner_radius,0.5f,0.f,16.f);
      if (ImGui::Button("Apply"))  apply(ImGui::GetStyle());
      ImGui::SameLine();
      if (ImGui::Button("Save") && !watch_path_.empty()) save_current(watch_path_);
      ImGui::SameLine();
      if (ImGui::Button("Revert") && !watch_path_.empty()) load(watch_path_);
      ImGui::End();
  }

  } // namespace straylight::shell
  ```

### Step 1.4 — Theme JSON presets

- [ ] Create `shell/themes/default.json`
  ```json
  { "name":"default",
    "colors":{"bg":"#1E1E2E","fg":"#CDD6F4","accent":"#B4BEFE","panel":"#313244"},
    "vars":{"surface.hover":"#45475A","border.color":"#6C7086",
            "border.radius.sm":"2.0","spacing.sm":"4.0","spacing.md":"8.0",
            "success":"#A6E3A1","warning":"#F9E2AF","error":"#F38BA8",
            "toast.bg":"#313244CC","osd.bg":"#1E1E2ECC"},
    "font_size":16.0,"corner_radius":4.0,"icon_theme":"straylight-icons" }
  ```
- [ ] Create `shell/themes/cyberpunk.json`
  ```json
  { "name":"cyberpunk",
    "colors":{"bg":"#0D0D1A","fg":"#E0E0FF","accent":"#FF007F","panel":"#1A1A2E"},
    "vars":{"surface.hover":"#2A2A4A","border.color":"#FF007F",
            "border.radius.sm":"0.0","spacing.sm":"4.0","spacing.md":"8.0",
            "success":"#00FF9F","warning":"#FFD700","error":"#FF3355",
            "toast.bg":"#1A1A2EEE","osd.bg":"#0D0D1AEE"},
    "font_size":15.0,"corner_radius":0.0,"icon_theme":"straylight-icons" }
  ```
- [ ] Create `shell/themes/minimal.json`
  ```json
  { "name":"minimal",
    "colors":{"bg":"#FAFAFA","fg":"#1A1A1A","accent":"#0066CC","panel":"#F0F0F0"},
    "vars":{"surface.hover":"#E8E8E8","border.color":"#CCCCCC",
            "border.radius.sm":"2.0","spacing.sm":"4.0","spacing.md":"8.0",
            "success":"#00AA44","warning":"#FF8800","error":"#CC0000",
            "toast.bg":"#F0F0F0EE","osd.bg":"#FAFAFAEE"},
    "font_size":14.0,"corner_radius":6.0,"icon_theme":"straylight-icons" }
  ```

### Step 1.5 — Tests

- [ ] Write `tests/unit/shell/test_theme_engine.cpp`
  - TEST: `load("default.json")` succeeds; `current().name == "default"`
  - TEST: `color_var("border.color")` parses hex to correct ABGR
  - TEST: `float_var("spacing.md")` returns 8.0f
  - TEST: `set_var("k","v")` persists through `var("k")`
  - TEST: missing key returns provided fallback
  - TEST: malformed JSON returns `SLError::Code::kConfigParse`
  - TEST: `save_current()` + `load()` round-trips with equal theme name
- [ ] Run `ctest -R test_theme_engine`

---

## Chunk 2: Notification System

**Goal:** `shell/notifications/` — freedesktop `org.freedesktop.Notifications` D-Bus daemon, urgency levels (Low/Normal/Critical), action buttons, expiry, notification center panel.

### Step 2.1 — CMakeLists

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  find_package(SDBusCpp REQUIRED)
  list(APPEND SHELL_SOURCES
      notifications/notification_daemon.cpp
      notifications/notification_manager.cpp)
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
  #include <vector>
  #include <map>

  namespace straylight::shell {

  struct NotifHints { int urgency=1; std::string category; };

  struct IncomingNotif {
      uint32_t id=0;
      std::string app_name, summary, body, icon;
      int expire_ms=5000;
      std::vector<std::pair<std::string,std::string>> actions;
      NotifHints hints;
  };

  class NotificationDaemon {
  public:
      using NotifyCb = std::function<void(IncomingNotif)>;
      using CloseCb  = std::function<void(uint32_t)>;

      explicit NotificationDaemon(sdbus::IConnection& conn);
      ~NotificationDaemon();

      void set_notify_callback(NotifyCb cb);
      void set_close_callback(CloseCb  cb);
      void emit_closed(uint32_t id, uint32_t reason);
      void emit_action_invoked(uint32_t id, const std::string& key);

  private:
      std::unique_ptr<sdbus::IObject> obj_;
      NotifyCb notify_cb_; CloseCb close_cb_;
      uint32_t next_id_ = 1;

      uint32_t on_notify(const std::string& app, uint32_t replaces,
          const std::string& icon, const std::string& summary,
          const std::string& body, const std::vector<std::string>& actions,
          const std::map<std::string,sdbus::Variant>& hints, int32_t timeout);
      void on_close(uint32_t id);
  };

  } // namespace straylight::shell
  ```

### Step 2.3 — `shell/notifications/notification_daemon.cpp`

- [ ] Create `shell/notifications/notification_daemon.cpp`
  ```cpp
  #include "notification_daemon.h"
  #include <straylight/log.h>

  namespace straylight::shell {

  static constexpr char kSvc[]  = "org.freedesktop.Notifications";
  static constexpr char kPath[] = "/org/freedesktop/Notifications";
  static constexpr char kIface[]= "org.freedesktop.Notifications";

  NotificationDaemon::NotificationDaemon(sdbus::IConnection& conn) {
      obj_ = sdbus::createObject(conn, kPath);

      obj_->registerMethod(kIface,"Notify","susssasa{sv}i","u",
          [this](sdbus::MethodCall c){
              std::string app,icon,sum,body; uint32_t rep;
              std::vector<std::string> acts;
              std::map<std::string,sdbus::Variant> hints; int32_t to;
              c>>app>>rep>>icon>>sum>>body>>acts>>hints>>to;
              auto r=c.createReply(); r<<on_notify(app,rep,icon,sum,body,acts,hints,to); r.send();
          });
      obj_->registerMethod(kIface,"CloseNotification","u","",
          [this](sdbus::MethodCall c){ uint32_t id; c>>id; on_close(id); c.createReply().send(); });
      obj_->registerMethod(kIface,"GetCapabilities","","as",
          [](sdbus::MethodCall c){ std::vector<std::string> v={"body","actions","urgency"};
              auto r=c.createReply(); r<<v; r.send(); });
      obj_->registerMethod(kIface,"GetServerInformation","","ssss",
          [](sdbus::MethodCall c){ auto r=c.createReply();
              r<<std::string("straylight-notifications")
               <<std::string("StrayLight OS")
               <<std::string("1.0")<<std::string("1.2"); r.send(); });
      obj_->registerSignal(kIface,"NotificationClosed","uu");
      obj_->registerSignal(kIface,"ActionInvoked","us");
      obj_->finishRegistration();
      conn.requestName(kSvc);
      LOG_INFO("notification daemon: registered as {}", kSvc);
  }
  NotificationDaemon::~NotificationDaemon() = default;
  void NotificationDaemon::set_notify_callback(NotifyCb cb) { notify_cb_=std::move(cb); }
  void NotificationDaemon::set_close_callback(CloseCb cb)   { close_cb_=std::move(cb);  }
  void NotificationDaemon::emit_closed(uint32_t id, uint32_t r)
      { obj_->emitSignal("NotificationClosed").onInterface(kIface).withArguments(id,r); }
  void NotificationDaemon::emit_action_invoked(uint32_t id, const std::string& k)
      { obj_->emitSignal("ActionInvoked").onInterface(kIface).withArguments(id,k); }

  uint32_t NotificationDaemon::on_notify(
      const std::string& app, uint32_t rep, const std::string& icon,
      const std::string& sum, const std::string& body,
      const std::vector<std::string>& acts,
      const std::map<std::string,sdbus::Variant>& hints, int32_t to)
  {
      uint32_t id = (rep>0) ? rep : next_id_++;
      IncomingNotif n; n.id=id; n.app_name=app; n.icon=icon;
      n.summary=sum; n.body=body;
      n.expire_ms = (to==-1)?5000:(to==0)?-1:to;
      for (size_t i=0; i+1<acts.size(); i+=2)
          n.actions.emplace_back(acts[i],acts[i+1]);
      if (hints.count("urgency"))
          n.hints.urgency = int(sdbus::message_element_to<uint8_t>(hints.at("urgency")));
      if (notify_cb_) notify_cb_(std::move(n));
      return id;
  }
  void NotificationDaemon::on_close(uint32_t id)
      { if (close_cb_) close_cb_(id); }

  } // namespace straylight::shell
  ```

### Step 2.4 — `shell/notifications/notification_manager.h`

- [ ] Create `shell/notifications/notification_manager.h`
  ```cpp
  #pragma once
  #include "notification_daemon.h"
  #include <deque>
  #include <vector>
  #include <functional>

  namespace straylight::shell {

  enum class Urgency : uint8_t { Low=0, Normal=1, Critical=2 };

  struct Notification {
      uint32_t id=0;
      std::string app_name, summary, body, icon;
      Urgency urgency=Urgency::Normal;
      int expire_ms=5000;    // -1 = persistent
      double created_at=0.0;
      std::vector<std::pair<std::string,std::string>> actions;
      bool resident=false;
  };

  class NotificationManager {
  public:
      static constexpr int kMaxToasts=5, kMaxHistory=100;
      using ActionCb = std::function<void(uint32_t, std::string_view)>;

      void push(IncomingNotif raw);
      void close(uint32_t id);
      void close_all();
      void set_action_callback(ActionCb cb);
      void set_dnd(bool v);
      [[nodiscard]] bool dnd()   const { return dnd_; }
      [[nodiscard]] int  count() const { return int(queue_.size()); }
      void render();
      void render_center(bool* p_open);

  private:
      std::deque<Notification>  queue_;
      std::vector<Notification> history_;
      bool     dnd_=false;
      ActionCb action_cb_;
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

  void NotificationManager::push(IncomingNotif raw) {
      while (int(queue_.size()) >= kMaxToasts) {
          history_.push_back(queue_.front());
          if (int(history_.size()) > kMaxHistory) history_.erase(history_.begin());
          queue_.pop_front();
      }
      Notification n;
      n.id=raw.id; n.app_name=raw.app_name; n.summary=raw.summary;
      n.body=raw.body; n.icon=raw.icon;
      n.urgency=static_cast<Urgency>(raw.hints.urgency);
      n.expire_ms=raw.expire_ms; n.created_at=ImGui::GetTime();
      n.actions=raw.actions; n.resident=(n.urgency==Urgency::Critical);
      queue_.push_back(std::move(n));
  }

  void NotificationManager::close(uint32_t id) {
      auto it=std::find_if(queue_.begin(),queue_.end(),[id](auto& n){return n.id==id;});
      if (it==queue_.end()) return;
      history_.push_back(*it);
      if (int(history_.size())>kMaxHistory) history_.erase(history_.begin());
      queue_.erase(it);
  }

  void NotificationManager::close_all() {
      for (auto& n:queue_) { history_.push_back(n);
          if (int(history_.size())>kMaxHistory) history_.erase(history_.begin()); }
      queue_.clear();
  }

  void NotificationManager::set_action_callback(ActionCb cb) { action_cb_=std::move(cb); }
  void NotificationManager::set_dnd(bool v) { dnd_=v; }

  void NotificationManager::expire_old() {
      double now=ImGui::GetTime();
      for (auto it=queue_.begin(); it!=queue_.end();) {
          if (it->expire_ms<0||it->resident) { ++it; continue; }
          if ((now-it->created_at)*1000.0 >= it->expire_ms) {
              history_.push_back(*it);
              if (int(history_.size())>kMaxHistory) history_.erase(history_.begin());
              it=queue_.erase(it);
          } else ++it;
      }
  }

  ImVec4 NotificationManager::urgency_color(Urgency u) const {
      if (u==Urgency::Critical) return {0.95f,0.27f,0.33f,1.f};
      if (u==Urgency::Low)      return {0.5f,0.5f,0.5f,1.f};
      return {0.70f,0.78f,1.f,1.f};
  }

  void NotificationManager::render() {
      expire_old(); if (dnd_||queue_.empty()) return;
      ImGuiIO& io=ImGui::GetIO();
      float y=io.DisplaySize.y-20.f;
      int idx=0;
      for (auto it=queue_.rbegin(); it!=queue_.rend()&&idx<kMaxToasts; ++it,++idx) {
          auto& n=*it;
          float elapsed=float((ImGui::GetTime()-n.created_at)*1000.0);
          float alpha=(n.expire_ms>0&&n.expire_ms-elapsed<500.f)
              ? (n.expire_ms-elapsed)/500.f : 1.f;
          float h=n.actions.empty()?70.f:90.f; y-=h+8.f;
          ImGui::SetNextWindowPos({io.DisplaySize.x-340.f,y},ImGuiCond_Always);
          ImGui::SetNextWindowSize({320.f,h},ImGuiCond_Always);
          ImGui::SetNextWindowBgAlpha(alpha*0.92f);
          ImGui::PushStyleColor(ImGuiCol_Border,urgency_color(n.urgency));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,6.f);
          char wid[32]; snprintf(wid,sizeof(wid),"##t%u",n.id);
          if (ImGui::Begin(wid,nullptr,ImGuiWindowFlags_NoDecoration|
                           ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove)) {
              ImGui::TextUnformatted(n.summary.c_str());
              ImGui::PushStyleColor(ImGuiCol_Text,{0.75f,0.75f,0.75f,alpha});
              ImGui::TextWrapped("%s",n.body.c_str());
              ImGui::PopStyleColor();
              for (auto& [key,lbl]:n.actions)
                  if (ImGui::SmallButton(lbl.c_str()))
                      { if (action_cb_) action_cb_(n.id,key); close(n.id); }
              if (n.resident && ImGui::SmallButton("X")) close(n.id);
          }
          ImGui::End(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
      }
  }

  void NotificationManager::render_center(bool* p_open) {
      if (!ImGui::Begin("Notifications",p_open)) { ImGui::End(); return; }
      if (ImGui::Button("Clear All")) history_.clear(); ImGui::Separator();
      for (auto it=history_.rbegin(); it!=history_.rend(); ++it) {
          ImGui::TextColored(urgency_color(it->urgency),"[%s]",
              it->urgency==Urgency::Critical?"!":it->urgency==Urgency::Low?"_":"i");
          ImGui::SameLine();
          ImGui::Text("[%s] %s",it->app_name.c_str(),it->summary.c_str());
          ImGui::Separator();
      }
      ImGui::End();
  }

  } // namespace straylight::shell
  ```

### Step 2.6 — Tests

- [ ] Write `tests/unit/shell/test_notification_manager.cpp`
  - TEST: `push()` increments `count()`
  - TEST: Critical notification has `resident=true` and does not expire
  - TEST: Normal with `expire_ms=1` removed after one simulated millisecond
  - TEST: 6th push evicts oldest; queue stays at `kMaxToasts`
  - TEST: `close(id)` removes correct entry; moves to history
  - TEST: `close_all()` empties queue
  - TEST: DND suppresses `render()` draw calls
  - TEST: action callback fires with correct id and key
- [ ] Run `ctest -R test_notification_manager`

---

## Chunk 3: Screenshot Tool

**Goal:** `shell/screenshot/screenshot.h/.cpp` — full-screen and region capture via `zwlr_screencopy_manager_v1`, PNG save via libpng, clipboard copy.

### Step 3.1 — CMakeLists

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  find_package(PNG REQUIRED)
  set(SCREENCOPY_XML "${CMAKE_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")
  wayland_generate_protocol(${SCREENCOPY_XML} _sc_hdr _sc_src)
  list(APPEND SHELL_SOURCES screenshot/screenshot.cpp ${_sc_src})
  target_link_libraries(straylight-shell PRIVATE PNG::PNG)
  ```

### Step 3.2 — `shell/screenshot/screenshot.h`

- [ ] Create `shell/screenshot/screenshot.h`
  ```cpp
  #pragma once
  #include <straylight/common.h>
  #include <functional>
  #include <string>
  #include <memory>

  struct wl_display; struct wl_output; struct wl_shm;
  struct zwlr_screencopy_manager_v1;

  namespace straylight::shell {

  enum class CaptureMode { FullScreen, Region };

  struct CaptureRequest {
      CaptureMode mode = CaptureMode::FullScreen;
      int rx=0, ry=0, rw=0, rh=0;
      std::string output_path;
      bool to_clipboard = true;
  };

  struct CaptureResult {
      std::string path;
      int width=0, height=0;
      size_t file_size=0;
  };

  class Screenshot {
  public:
      using CompletionCb = std::function<void(Result<CaptureResult, SLError>)>;
      Screenshot(); ~Screenshot();

      void init(wl_display* dpy, zwlr_screencopy_manager_v1* mgr,
                wl_output* output, wl_shm* shm);

      void capture(CaptureRequest req, CompletionCb cb);
      bool render_region_selector();  // returns true while active
      void cancel_selection();
      static std::string default_path();

  private:
      struct Impl; std::unique_ptr<Impl> impl_;
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
  #include <sys/mman.h>
  #include <unistd.h>
  #include <ctime>
  #include <cstdio>
  #include <vector>

  namespace straylight::shell {

  struct Screenshot::Impl {
      wl_display* dpy=nullptr; zwlr_screencopy_manager_v1* mgr=nullptr;
      wl_output* output=nullptr; wl_shm* shm=nullptr;
      CompletionCb cb; CaptureRequest req;
      wl_shm_pool* pool=nullptr; wl_buffer* buf=nullptr;
      uint8_t* data=nullptr; int buf_fd=-1;
      int bw=0, bh=0; bool ready=false, failed=false;
      bool sel_active=false, sel_drag=false;
      float sx0=0,sy0=0,sx1=0,sy1=0;
  };

  static void frame_buffer(void* d, zwlr_screencopy_frame_v1* f,
      uint32_t /*fmt*/, uint32_t w, uint32_t h, uint32_t /*stride*/)
  {
      auto* i=static_cast<Screenshot::Impl*>(d);
      i->bw=int(w); i->bh=int(h);
      size_t sz=w*h*4;
      char path[]="/dev/shm/sl-scr-XXXXXX";
      i->buf_fd=mkstemp(path); unlink(path); ftruncate(i->buf_fd,sz);
      i->data=static_cast<uint8_t*>(
          mmap(nullptr,sz,PROT_READ|PROT_WRITE,MAP_SHARED,i->buf_fd,0));
      i->pool=wl_shm_create_pool(i->shm,i->buf_fd,sz);
      i->buf=wl_shm_pool_create_buffer(i->pool,0,w,h,w*4,WL_SHM_FORMAT_ARGB8888);
      zwlr_screencopy_frame_v1_copy(f,i->buf);
  }
  static void frame_flags(void*,zwlr_screencopy_frame_v1*,uint32_t){}
  static void frame_ready(void* d,zwlr_screencopy_frame_v1*,uint32_t,uint32_t,uint32_t)
      { static_cast<Screenshot::Impl*>(d)->ready=true; }
  static void frame_failed(void* d,zwlr_screencopy_frame_v1*)
      { static_cast<Screenshot::Impl*>(d)->failed=true; }
  static void frame_damage(void*,zwlr_screencopy_frame_v1*,uint32_t,uint32_t,uint32_t,uint32_t){}
  static void frame_linux_dmabuf(void*,zwlr_screencopy_frame_v1*,uint32_t,uint32_t,uint32_t){}
  static void frame_buffer_done(void*,zwlr_screencopy_frame_v1*){}
  static const zwlr_screencopy_frame_v1_listener kListener{
      frame_buffer,frame_flags,frame_ready,frame_failed,
      frame_damage,frame_linux_dmabuf,frame_buffer_done};

  static Result<size_t,SLError> write_png(
      const uint8_t* argb, int w, int h, const std::string& path)
  {
      FILE* fp=fopen(path.c_str(),"wb"); if (!fp)
          return Result<size_t,SLError>::error({SLError::Code::kIo,path});
      png_structp ps=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
      png_infop pi=png_create_info_struct(ps);
      png_init_io(ps,fp);
      png_set_IHDR(ps,pi,w,h,8,PNG_COLOR_TYPE_RGBA,
          PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
      png_write_info(ps,pi);
      std::vector<uint8_t> row(w*4);
      for (int y=0;y<h;++y) {
          const uint8_t* s=argb+y*w*4;
          for (int x=0;x<w;++x)
              { row[x*4]=s[x*4+2]; row[x*4+1]=s[x*4+1];
                row[x*4+2]=s[x*4+0]; row[x*4+3]=s[x*4+3]; }
          png_write_row(ps,row.data());
      }
      png_write_end(ps,pi); png_destroy_write_struct(&ps,&pi); fclose(fp);
      struct stat st; stat(path.c_str(),&st);
      return Result<size_t,SLError>::ok(st.st_size);
  }

  Screenshot::Screenshot() : impl_(std::make_unique<Impl>()) {}
  Screenshot::~Screenshot() = default;
  void Screenshot::init(wl_display* d, zwlr_screencopy_manager_v1* m,
                        wl_output* o, wl_shm* s)
      { impl_->dpy=d; impl_->mgr=m; impl_->output=o; impl_->shm=s; }

  void Screenshot::capture(CaptureRequest req, CompletionCb cb) {
      impl_->req=req; impl_->cb=std::move(cb);
      impl_->ready=false; impl_->failed=false;
      zwlr_screencopy_frame_v1* frame;
      if (req.mode==CaptureMode::Region)
          frame=zwlr_screencopy_manager_v1_capture_output_region(
              impl_->mgr,0,impl_->output,req.rx,req.ry,req.rw,req.rh);
      else
          frame=zwlr_screencopy_manager_v1_capture_output(impl_->mgr,0,impl_->output);
      zwlr_screencopy_frame_v1_add_listener(frame,&kListener,impl_.get());
      wl_display_roundtrip(impl_->dpy);
      if (impl_->failed) {
          impl_->cb(Result<CaptureResult,SLError>::error(
              {SLError::Code::kCaptureFailed,"screencopy failed"}));
          return;
      }
      std::string path=req.output_path.empty()?default_path():req.output_path;
      auto r=write_png(impl_->data,impl_->bw,impl_->bh,path);
      if (!r.is_ok()) { impl_->cb(Result<CaptureResult,SLError>::error(r.error())); return; }
      CaptureResult res{path,impl_->bw,impl_->bh,r.value()};
      munmap(impl_->data,size_t(impl_->bw*impl_->bh*4));
      wl_buffer_destroy(impl_->buf); wl_shm_pool_destroy(impl_->pool);
      close(impl_->buf_fd); impl_->data=nullptr; impl_->buf=nullptr;
      impl_->pool=nullptr; impl_->buf_fd=-1;
      LOG_INFO("screenshot: {}  ({}x{})", path, res.width, res.height);
      impl_->cb(Result<CaptureResult,SLError>::ok(res));
  }

  bool Screenshot::render_region_selector() {
      if (!impl_->sel_active) return false;
      ImGuiIO& io=ImGui::GetIO();
      ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::SetNextWindowBgAlpha(0.25f);
      ImGui::Begin("##rsel",nullptr,ImGuiWindowFlags_NoDecoration|
          ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove);
      ImDrawList* dl=ImGui::GetWindowDrawList();
      if (ImGui::IsMouseClicked(0)) {
          impl_->sel_drag=true; impl_->sx0=io.MousePos.x; impl_->sy0=io.MousePos.y; }
      if (impl_->sel_drag) {
          impl_->sx1=io.MousePos.x; impl_->sy1=io.MousePos.y;
          dl->AddRectFilled({impl_->sx0,impl_->sy0},{impl_->sx1,impl_->sy1},IM_COL32(100,150,255,50));
          dl->AddRect({impl_->sx0,impl_->sy0},{impl_->sx1,impl_->sy1},IM_COL32(100,150,255,220),0,0,2.f);
      }
      if (ImGui::IsMouseReleased(0)&&impl_->sel_drag) {
          impl_->sel_drag=false; impl_->sel_active=false;
          ImGui::End();
          CaptureRequest r; r.mode=CaptureMode::Region;
          r.rx=int(std::min(impl_->sx0,impl_->sx1));
          r.ry=int(std::min(impl_->sy0,impl_->sy1));
          r.rw=int(std::abs(impl_->sx1-impl_->sx0));
          r.rh=int(std::abs(impl_->sy1-impl_->sy0));
          capture(r,[](auto){});
          return false;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancel_selection();
      ImGui::End();
      return impl_->sel_active;
  }

  void Screenshot::cancel_selection()
      { impl_->sel_active=false; impl_->sel_drag=false; }

  std::string Screenshot::default_path() {
      char buf[256]; std::time_t t=std::time(nullptr);
      std::tm* tm=std::localtime(&t);
      const char* h=getenv("HOME") ?: "/tmp";
      snprintf(buf,sizeof(buf),"%s/Pictures/screenshot-%04d%02d%02d-%02d%02d%02d.png",
          h,tm->tm_year+1900,tm->tm_mon+1,tm->tm_mday,
          tm->tm_hour,tm->tm_min,tm->tm_sec);
      return buf;
  }

  } // namespace straylight::shell
  ```

### Step 3.4 — Tests

- [ ] Write `tests/unit/shell/test_screenshot.cpp`
  - TEST: `default_path()` contains "screenshot-" and ends in ".png"
  - TEST: PNG write of 4×4 synthetic RGBA buffer produces valid PNG magic bytes
  - TEST: Region rect computed correctly from drag endpoints (order-independent)
  - TEST: `cancel_selection()` sets selector inactive immediately
- [ ] Run `ctest -R test_screenshot`

---

## Chunk 4: Volume OSD

**Goal:** `shell/osd/volume_osd.h/.cpp` — PipeWire default-sink monitor on a background thread, centered layer-shell overlay with bar and fade, 2s auto-dismiss.

### Step 4.1 — CMakeLists

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
      VolumeOsd(); ~VolumeOsd();
      void start();   // launch PipeWire monitor thread
      void stop();    // stop thread and join
      void show(float level, bool muted = false);
      void render();  // call once per frame
      [[nodiscard]] bool  is_visible() const;
      [[nodiscard]] float level() const;
      [[nodiscard]] bool  muted() const;

  private:
      struct Impl; std::unique_ptr<Impl> impl_;
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
      std::atomic<float> level{0.f};
      std::atomic<bool>  muted{false};
      std::atomic<bool>  visible{false};
      double             show_time=0.0;
      static constexpr double kDur=2.0, kFade=1.5;
      pw_main_loop* loop=nullptr; pw_context* ctx=nullptr;
      pw_core* core=nullptr; pw_registry* reg=nullptr;
      spa_hook reg_hook{},node_hook{};
      pw_node* sink=nullptr;
      std::thread thread; std::atomic<bool> running{false};
  };

  static void on_param(void* d,uint32_t,const spa_pod* p) {
      auto* i=static_cast<VolumeOsd::Impl*>(d); if (!p) return;
      float vols[8]={}; uint32_t nv=8; bool mute=false;
      spa_pod_parse_object(p,SPA_TYPE_OBJECT_Props,nullptr,
          SPA_PROP_mute,          SPA_POD_OPT_Bool(&mute),
          SPA_PROP_channelVolumes,SPA_POD_OPT_Array(&nv,SPA_TYPE_Float,sizeof(float),vols),0);
      float sum=0; for (uint32_t j=0;j<nv;++j) sum+=vols[j];
      i->level.store(std::cbrt(sum/float(nv?nv:1)));
      i->muted.store(mute);
      i->visible.store(true); i->show_time=ImGui::GetTime();
  }
  static const pw_node_events kNodeEv{.version=PW_VERSION_NODE_EVENTS,.param_changed=on_param};

  static void on_global(void* d,uint32_t id,uint32_t,const char* type,
                        uint32_t,const spa_dict* props)
  {
      auto* i=static_cast<VolumeOsd::Impl*>(d);
      if (strcmp(type,PW_TYPE_INTERFACE_Node)!=0) return;
      const char* cls=spa_dict_lookup(props,PW_KEY_MEDIA_CLASS);
      if (!cls||strcmp(cls,"Audio/Sink")!=0||i->sink) return;
      i->sink=static_cast<pw_node*>(
          pw_registry_bind(i->reg,id,PW_TYPE_INTERFACE_Node,PW_VERSION_NODE,0));
      pw_node_add_listener(i->sink,&i->node_hook,&kNodeEv,i);
      pw_node_subscribe_params(i->sink,(uint32_t[]){SPA_PARAM_Props},1);
  }
  static const pw_registry_events kRegEv{.version=PW_VERSION_REGISTRY_EVENTS,.global=on_global};

  VolumeOsd::VolumeOsd() : impl_(std::make_unique<Impl>()) {}
  VolumeOsd::~VolumeOsd() { stop(); }

  void VolumeOsd::start() {
      impl_->running=true;
      impl_->thread=std::thread([this]{
          pw_init(nullptr,nullptr);
          impl_->loop=pw_main_loop_new(nullptr);
          impl_->ctx=pw_context_new(pw_main_loop_get_loop(impl_->loop),nullptr,0);
          impl_->core=pw_context_connect(impl_->ctx,nullptr,0);
          if (!impl_->core) {
              LOG_WARN("volume_osd: PipeWire unavailable");
              pw_main_loop_destroy(impl_->loop); impl_->loop=nullptr; return;
          }
          impl_->reg=pw_core_get_registry(impl_->core,PW_VERSION_REGISTRY,0);
          pw_registry_add_listener(impl_->reg,&impl_->reg_hook,&kRegEv,impl_.get());
          pw_main_loop_run(impl_->loop);
          pw_core_disconnect(impl_->core);
          pw_context_destroy(impl_->ctx);
          pw_main_loop_destroy(impl_->loop); impl_->loop=nullptr;
      });
  }

  void VolumeOsd::stop() {
      if (!impl_->running.exchange(false)) return;
      if (impl_->loop) pw_main_loop_quit(impl_->loop);
      if (impl_->thread.joinable()) impl_->thread.join();
  }

  void VolumeOsd::show(float lv, bool mute) {
      impl_->level.store(lv); impl_->muted.store(mute);
      impl_->visible.store(true); impl_->show_time=ImGui::GetTime();
  }

  void VolumeOsd::render() {
      if (!impl_->visible.load()) return;
      double el=ImGui::GetTime()-impl_->show_time;
      if (el>Impl::kDur) { impl_->visible.store(false); return; }
      float alpha=(el>Impl::kFade)?1.f-float((el-Impl::kFade)/(Impl::kDur-Impl::kFade)):1.f;
      ImGuiIO& io=ImGui::GetIO();
      float w=220.f,h=56.f;
      ImGui::SetNextWindowPos({(io.DisplaySize.x-w)*.5f,io.DisplaySize.y-100.f},ImGuiCond_Always);
      ImGui::SetNextWindowSize({w,h},ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(alpha*.90f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,8.f);
      if (ImGui::Begin("##osd",nullptr,ImGuiWindowFlags_NoDecoration|
              ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoInputs)) {
          bool m=impl_->muted.load(); float lv=impl_->level.load();
          ImGui::Text("%s",m?"[mute]":"[ vol]"); ImGui::SameLine();
          ImGui::SetNextItemWidth(w-82.f);
          ImGui::ProgressBar(m?0.f:lv,{-1.f,14.f},""); ImGui::SameLine();
          ImGui::Text("%3d%%",int(lv*100.f));
      }
      ImGui::End(); ImGui::PopStyleVar();
  }

  bool  VolumeOsd::is_visible() const { return impl_->visible.load(); }
  float VolumeOsd::level()      const { return impl_->level.load();   }
  bool  VolumeOsd::muted()      const { return impl_->muted.load();   }

  } // namespace straylight::shell
  ```

### Step 4.4 — Tests

- [ ] Write `tests/unit/shell/test_volume_osd.cpp`
  - TEST: `show(0.75f)` → `is_visible()==true`, `level()==0.75f`
  - TEST: `show(0.5f,true)` → `muted()==true`
  - TEST: `render()` does not crash before `start()` is called
  - TEST: after `kDur` seconds (simulated), `is_visible()` returns false
- [ ] Run `ctest -R test_volume_osd`

---

## Chunk 5: Global Keybind Manager

**Goal:** `shell/keybinds/keybind_manager.h/.cpp` — JSON config, named actions with handlers, `wl_keyboard` dispatch via xkbcommon, conflict detection, rebind UI.

### Step 5.1 — CMakeLists

- [ ] Edit `shell/CMakeLists.txt`
  ```cmake
  pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
  list(APPEND SHELL_SOURCES keybinds/keybind_manager.cpp)
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
      None=0, Ctrl=1<<0, Alt=1<<1, Shift=1<<2, Super=1<<3 };
  inline Mod  operator|(Mod a, Mod b) { return Mod(uint8_t(a)|uint8_t(b)); }
  inline bool has_mod(Mod m, Mod b)   { return (uint8_t(m)&uint8_t(b))!=0; }

  struct Keybind { Mod mods=Mod::None; xkb_keysym_t keysym=XKB_KEY_NoSymbol; };

  struct ShortcutEntry {
      std::string action_id, label;
      Keybind bind, default_bind;
  };

  class KeybindManager {
  public:
      using Handler = std::function<void()>;
      KeybindManager(); ~KeybindManager();

      Result<void,SLError> load(std::string_view path);
      Result<void,SLError> save(std::string_view path) const;

      void register_action(std::string_view id, std::string_view label,
                           Keybind def, Handler h);
      bool handle_key(Mod mods, xkb_keysym_t sym);
      Result<void,SLError> rebind(std::string_view id, Keybind nb);
      void reset_to_default(std::string_view id);
      void reset_all();

      [[nodiscard]] const std::vector<ShortcutEntry>& entries() const;
      void render_settings(bool* p_open);

      static std::string keybind_to_string(const Keybind& kb);
      static std::string default_config_path();

  private:
      std::vector<ShortcutEntry>                  entries_;
      std::unordered_map<std::string,Handler>     handlers_;
      std::unordered_map<uint64_t,std::string>    bind_map_;
      int                                         capturing_idx_=-1;

      static uint64_t hash(const Keybind& kb)
          { return (uint64_t(kb.keysym)<<8)|uint64_t(kb.mods); }
  };

  } // namespace straylight::shell
  ```

### Step 5.3 — `shell/keybinds/keybind_manager.cpp`

- [ ] Create `shell/keybinds/keybind_manager.cpp`
  ```cpp
  #include "keybind_manager.h"
  #include <straylight/log.h>
  #include <nlohmann/json.hpp>
  #include <imgui.h>
  #include <xkbcommon/xkbcommon.h>
  #include <fstream>
  #include <cstdlib>

  namespace straylight::shell {

  KeybindManager::KeybindManager() = default;
  KeybindManager::~KeybindManager() = default;

  static Mod parse_mods(const nlohmann::json& a) {
      Mod m=Mod::None;
      for (auto& s:a) {
          auto v=s.get<std::string>();
          if (v=="Ctrl")  m=m|Mod::Ctrl;
          if (v=="Alt")   m=m|Mod::Alt;
          if (v=="Shift") m=m|Mod::Shift;
          if (v=="Super") m=m|Mod::Super;
      }
      return m;
  }

  Result<void,SLError> KeybindManager::load(std::string_view path) {
      std::ifstream f(std::string(path));
      if (!f) return Result<void,SLError>::ok();  // missing = use defaults
      nlohmann::json j;
      try { j=nlohmann::json::parse(f); }
      catch (...) { return Result<void,SLError>::error(
          {SLError::Code::kConfigParse,std::string(path)}); }
      for (auto& obj:j) {
          std::string act=obj.value("action",""), key=obj.value("key","");
          Mod mods=obj.contains("mods")?parse_mods(obj["mods"]):Mod::None;
          auto sym=xkb_keysym_from_name(key.c_str(),XKB_KEYSYM_NO_FLAGS);
          if (sym==XKB_KEY_NoSymbol) continue;
          Keybind kb{mods,sym};
          for (auto& e:entries_) if (e.action_id==act) {
              bind_map_.erase(hash(e.bind)); e.bind=kb;
              bind_map_[hash(kb)]=act; break; }
      }
      return Result<void,SLError>::ok();
  }

  Result<void,SLError> KeybindManager::save(std::string_view path) const {
      nlohmann::json j=nlohmann::json::array();
      for (auto& e:entries_) {
          nlohmann::json o; o["action"]=e.action_id;
          nlohmann::json ms=nlohmann::json::array();
          if (has_mod(e.bind.mods,Mod::Ctrl))  ms.push_back("Ctrl");
          if (has_mod(e.bind.mods,Mod::Alt))   ms.push_back("Alt");
          if (has_mod(e.bind.mods,Mod::Shift)) ms.push_back("Shift");
          if (has_mod(e.bind.mods,Mod::Super)) ms.push_back("Super");
          o["mods"]=ms;
          char nm[64]={}; xkb_keysym_get_name(e.bind.keysym,nm,sizeof(nm));
          o["key"]=nm; j.push_back(o);
      }
      std::string tmp=std::string(path)+".tmp";
      { std::ofstream f(tmp); if (!f)
          return Result<void,SLError>::error({SLError::Code::kIo,tmp});
        f<<j.dump(2); }
      if (rename(tmp.c_str(),std::string(path).c_str())!=0)
          return Result<void,SLError>::error({SLError::Code::kIo,"rename"});
      return Result<void,SLError>::ok();
  }

  void KeybindManager::register_action(std::string_view id, std::string_view label,
                                        Keybind def, Handler h)
  {
      entries_.push_back({std::string(id),std::string(label),def,def});
      handlers_[std::string(id)]=std::move(h);
      bind_map_[hash(def)]=std::string(id);
  }

  bool KeybindManager::handle_key(Mod mods, xkb_keysym_t sym) {
      auto it=bind_map_.find(hash({mods,sym}));
      if (it==bind_map_.end()) return false;
      auto hit=handlers_.find(it->second);
      if (hit==handlers_.end()) return false;
      hit->second(); return true;
  }

  Result<void,SLError> KeybindManager::rebind(std::string_view id, Keybind nb) {
      auto h=hash(nb);
      auto cf=bind_map_.find(h);
      if (cf!=bind_map_.end()&&cf->second!=id)
          return Result<void,SLError>::error(
              {SLError::Code::kKeybindConflict,"taken by "+cf->second});
      for (auto& e:entries_) if (e.action_id==id) {
          bind_map_.erase(hash(e.bind)); e.bind=nb;
          bind_map_[h]=std::string(id);
          return Result<void,SLError>::ok(); }
      return Result<void,SLError>::error({SLError::Code::kNotFound,std::string(id)});
  }

  void KeybindManager::reset_to_default(std::string_view id) {
      for (auto& e:entries_) if (e.action_id==id) {
          bind_map_.erase(hash(e.bind)); e.bind=e.default_bind;
          bind_map_[hash(e.default_bind)]=std::string(id); return; }
  }
  void KeybindManager::reset_all() {
      bind_map_.clear();
      for (auto& e:entries_) { e.bind=e.default_bind; bind_map_[hash(e.default_bind)]=e.action_id; }
  }
  const std::vector<ShortcutEntry>& KeybindManager::entries() const { return entries_; }

  std::string KeybindManager::keybind_to_string(const Keybind& kb) {
      std::string s;
      if (has_mod(kb.mods,Mod::Super)) s+="Super+";
      if (has_mod(kb.mods,Mod::Ctrl))  s+="Ctrl+";
      if (has_mod(kb.mods,Mod::Alt))   s+="Alt+";
      if (has_mod(kb.mods,Mod::Shift)) s+="Shift+";
      char nm[64]={}; xkb_keysym_get_name(kb.keysym,nm,sizeof(nm));
      return s+nm;
  }

  std::string KeybindManager::default_config_path() {
      const char* xdg=getenv("XDG_CONFIG_HOME");
      std::string base=xdg?std::string(xdg):
          (std::string(getenv("HOME")?:"")+"/.config");
      return base+"/straylight/keybinds.json";
  }

  void KeybindManager::render_settings(bool* p_open) {
      if (!ImGui::Begin("Keyboard Shortcuts",p_open)) { ImGui::End(); return; }
      if (ImGui::Button("Reset All")) reset_all(); ImGui::Separator();
      if (ImGui::BeginTable("##kb",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)) {
          ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("Binding",ImGuiTableColumnFlags_WidthFixed,180.f);
          ImGui::TableSetupColumn("",ImGuiTableColumnFlags_WidthFixed,100.f);
          ImGui::TableHeadersRow();
          for (int i=0;i<int(entries_.size());++i) {
              auto& e=entries_[i];
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(e.label.c_str());
              ImGui::TableSetColumnIndex(1);
              if (capturing_idx_==i) {
                  ImGui::TextColored({1,1,0,1},"Press a key...");
                  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) capturing_idx_=-1;
              } else {
                  ImGui::TextUnformatted(keybind_to_string(e.bind).c_str());
              }
              ImGui::TableSetColumnIndex(2); ImGui::PushID(i);
              if (ImGui::SmallButton("Edit"))  capturing_idx_=i;
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
    {"action":"shell.screenshot.fullscreen","mods":["Super"],         "key":"Print"},
    {"action":"shell.screenshot.region",    "mods":["Super","Shift"], "key":"Print"},
    {"action":"shell.launcher.toggle",      "mods":["Super"],         "key":"space"},
    {"action":"shell.notifications.dnd",    "mods":["Super"],         "key":"n"},
    {"action":"shell.notifications.center", "mods":["Super","Shift"], "key":"n"},
    {"action":"shell.volume.up",            "mods":[],                "key":"XF86AudioRaiseVolume"},
    {"action":"shell.volume.down",          "mods":[],                "key":"XF86AudioLowerVolume"},
    {"action":"shell.volume.mute",          "mods":[],                "key":"XF86AudioMute"},
    {"action":"shell.workspace.next",       "mods":["Super"],         "key":"Right"},
    {"action":"shell.workspace.prev",       "mods":["Super"],         "key":"Left"},
    {"action":"shell.theme.editor",         "mods":["Super"],         "key":"t"},
    {"action":"shell.terminal.launch",      "mods":["Super"],         "key":"Return"}
  ]
  ```

### Step 5.5 — Wire into `shell/main.cpp`

- [ ] Update `shell/main.cpp` — add the following after existing initialization
  ```cpp
  // Includes
  #include "themes/theme_engine.h"
  #include "notifications/notification_daemon.h"
  #include "notifications/notification_manager.h"
  #include "screenshot/screenshot.h"
  #include "osd/volume_osd.h"
  #include "keybinds/keybind_manager.h"

  // Init (after Wayland globals are bound)
  ThemeEngine theme;
  theme.load_named("default");
  theme.apply(ImGui::GetStyle());

  auto dbus = sdbus::createSessionBusConnection();
  NotificationDaemon notif_daemon(*dbus);
  NotificationManager notif_mgr;
  notif_daemon.set_notify_callback([&](IncomingNotif n){ notif_mgr.push(n); });
  notif_daemon.set_close_callback( [&](uint32_t id)   { notif_mgr.close(id); });

  Screenshot screenshot;
  screenshot.init(display, screencopy_mgr, output, shm);

  VolumeOsd osd; osd.start();

  KeybindManager keybinds;
  keybinds.load(KeybindManager::default_config_path());
  keybinds.register_action("shell.screenshot.fullscreen","Screenshot (Full)",
      {Mod::Super,XKB_KEY_Print},
      [&]{ screenshot.capture({CaptureMode::FullScreen},{},[](auto){}); });
  keybinds.register_action("shell.launcher.toggle","App Launcher",
      {Mod::Super,XKB_KEY_space},[&]{ app_launcher.toggle(); });
  keybinds.register_action("shell.notifications.dnd","Do Not Disturb",
      {Mod::Super,XKB_KEY_n},[&]{ notif_mgr.set_dnd(!notif_mgr.dnd()); });
  keybinds.register_action("shell.volume.up","Volume Up",
      {Mod::None,XKB_KEY_XF86AudioRaiseVolume},
      [&]{ osd.show(std::min(1.f,osd.level()+0.05f)); });
  keybinds.register_action("shell.volume.down","Volume Down",
      {Mod::None,XKB_KEY_XF86AudioLowerVolume},
      [&]{ osd.show(std::max(0.f,osd.level()-0.05f)); });
  keybinds.register_action("shell.volume.mute","Mute",
      {Mod::None,XKB_KEY_XF86AudioMute},
      [&]{ osd.show(osd.level(),!osd.muted()); });
  keybinds.register_action("shell.theme.editor","Theme Editor",
      {Mod::Super,XKB_KEY_t},[&]{ show_theme_editor=!show_theme_editor; });

  // In wl_keyboard.key callback:
  //   keybinds.handle_key(current_mods, xkb_state_key_get_one_sym(xkb_state, key+8));

  // In the render loop:
  theme.poll_changes();
  notif_mgr.render();
  osd.render();
  screenshot.render_region_selector();
  if (show_theme_editor)     theme.render_live_preview(&show_theme_editor);
  if (show_notif_center)     notif_mgr.render_center(&show_notif_center);
  if (show_keybind_settings) keybinds.render_settings(&show_keybind_settings);
  ```

### Step 5.6 — Tests

- [ ] Write `tests/unit/shell/test_keybind_manager.cpp`
  - TEST: `register_action` + `handle_key` fires handler exactly once
  - TEST: `handle_key` returns false for unregistered combination
  - TEST: `rebind` updates binding; old keysym no longer triggers
  - TEST: `rebind` returns `kKeybindConflict` if binding taken by another action
  - TEST: `reset_to_default` restores original binding after rebind
  - TEST: `reset_all` restores all entries
  - TEST: `load`/`save` round-trip preserves all action ids and mod/key pairs
  - TEST: `keybind_to_string({Super|Shift, XKB_KEY_Print})` == `"Super+Shift+Print"`
- [ ] Run `ctest -R test_keybind_manager`

---

## Final Verification

- [ ] `cmake --preset dev && cmake --build --preset dev` — zero errors, zero warnings (`-Wall -Wextra -Werror`)
- [ ] `ctest --preset dev -R shell` — all shell unit tests pass
- [ ] AddressSanitizer clean: `cmake --preset dev -DENABLE_ASAN=ON` + build + test
- [ ] UBSan clean: `cmake --preset dev -DENABLE_UBSAN=ON` + build + test
- [ ] Manual smoke on wlr-headless: `straylight-shell` renders top bar; `Super+Print` triggers capture logged to stdout; `notify-send -u critical "!" "body"` shows red-bordered toast
- [ ] Manual theme switch: `straylight-shell --theme cyberpunk` re-styles top bar live; `default.json` edit on disk hot-reloads within one frame
- [ ] Manual PipeWire: change volume with media keys; OSD overlay appears centered and fades after 2s
- [ ] `ldd /usr/bin/straylight-shell | grep 'not found'` — empty (all deps satisfied)
- [ ] `valgrind --leak-check=full --error-exitcode=1 straylight-shell --headless --exit-after-frame` — no leaks reported
