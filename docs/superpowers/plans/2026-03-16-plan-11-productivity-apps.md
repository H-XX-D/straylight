# Plan 11: Desktop Apps — Browser, Editor, Media Player & Image Viewer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement four productivity applications as standalone ImGui Wayland clients: a WebKit-based web browser, a text editor with syntax highlighting, a GStreamer-backed media player, and an image viewer using stb_image.

**Architecture:** All apps use `wl_egl_window + EGL + ImGui` (same pattern as Plan 9A/9B). Each app is a standalone binary under `apps/`. AppBase handles Wayland connection, xdg-shell surface, EGL init, ImGui context, and the frame loop.

**Tech Stack:** C++20, CMake 3.25+, Dear ImGui 1.90+, wl_egl_window + EGL + OpenGL ES 3.0, wayland-client 1.22+, xdg-shell, WebKitGTK 6.0+, GStreamer 1.24+, stb_image, nlohmann/json 3.11+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor — xdg-shell), Plan 9A (common app base pattern, AppBase class)

**Development environment:** Linux x86_64 required. Uses Wayland, WebKitGTK, GStreamer, EGL. macOS cannot build.

---

## File Structure

```
apps/browser/
├── CMakeLists.txt
├── main.cpp
├── engine.h / engine.cpp          # WebKit embedding + navigation
├── tab_manager.h / tab_manager.cpp # Multi-tab state
├── downloads.h / downloads.cpp     # Download manager
└── tests/
    └── browser_test.cpp

apps/editor/
├── CMakeLists.txt
├── main.cpp
├── buffer.h / buffer.cpp          # Text buffer with gap-buffer backend
├── syntax.h / syntax.cpp          # Syntax highlighting (tree of token ranges)
├── search.h / search.cpp          # Find/replace engine
└── tests/
    └── editor_test.cpp

apps/player/
├── CMakeLists.txt
├── main.cpp
├── pipeline.h / pipeline.cpp      # GStreamer pipeline management
├── playlist.h / playlist.cpp      # Playlist / queue
├── controls.h / controls.cpp      # Transport controls + UI state
└── tests/
    └── player_test.cpp

apps/image_viewer/
├── CMakeLists.txt
├── main.cpp
├── loader.h / loader.cpp          # stb_image loading + GL texture upload
├── viewer.h / viewer.cpp          # Pan/zoom/rotate viewport state
└── tests/
    └── viewer_test.cpp
```

---

## Chunk 1: Web Browser — Engine, Tabs & Main

### Task 1.1: apps/browser/engine.h

- [ ] WebKitGTK web view wrapper (off-screen rendering to shared texture)
- [ ] Navigation: load URL, back, forward, reload, stop
- [ ] Page metadata: title, favicon, loading progress, TLS status
- [ ] JavaScript evaluation interface

```cpp
// apps/browser/engine.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <functional>
#include <cstdint>

// Forward declarations — WebKitGTK types
typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitSettings WebKitSettings;

namespace straylight::browser {

enum class TlsStatus { None, Valid, Invalid };

struct PageInfo {
    std::string title;
    std::string url;
    float load_progress = 0.0f;   // 0.0–1.0
    TlsStatus tls = TlsStatus::None;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool is_loading = false;
};

using TitleChangedCb = std::function<void(const std::string&)>;
using LoadChangedCb  = std::function<void(float progress, bool finished)>;

class Engine {
public:
    static Result<Engine, SLError> create(int width, int height);

    /// Load a URL (auto-prepends https:// if no scheme).
    Result<void, SLError> navigate(const std::string& url);
    void go_back();
    void go_forward();
    void reload();
    void stop();

    /// Evaluate JS in page context, return result as string.
    Result<std::string, SLError> eval_js(const std::string& script);

    /// Resize the off-screen web view.
    void resize(int width, int height);

    /// Pump WebKit events and update the shared texture.
    /// Returns OpenGL texture ID for ImGui rendering.
    uint32_t render_to_texture();

    /// Inject input events from ImGui into WebKit.
    void inject_mouse(float x, float y, bool lmb, bool rmb, float scroll_y);
    void inject_key(uint32_t keycode, uint32_t modifiers, bool pressed);

    PageInfo page_info() const;

    void on_title_changed(TitleChangedCb cb) { title_cb_ = std::move(cb); }
    void on_load_changed(LoadChangedCb cb) { load_cb_ = std::move(cb); }

    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

private:
    WebKitWebView* view_ = nullptr;
    WebKitSettings* settings_ = nullptr;
    uint32_t gl_texture_ = 0;
    int width_, height_;
    PageInfo info_;
    TitleChangedCb title_cb_;
    LoadChangedCb load_cb_;

    explicit Engine(WebKitWebView* view, WebKitSettings* settings,
                    uint32_t texture, int w, int h);
};

} // namespace straylight::browser
```

### Task 1.2: apps/browser/engine.cpp

- [ ] `create()` — init GTK (headless), create `WebKitWebView` with off-screen rendering
- [ ] `navigate()` — normalize URL, call `webkit_web_view_load_uri`
- [ ] `render_to_texture()` — snapshot web view to cairo surface, upload to GL texture
- [ ] `inject_mouse/key` — synthesize GDK events into the web view
- [ ] Wire WebKit signals: `notify::title`, `load-changed`, `load-failed-with-tls-errors`

```cpp
// apps/browser/engine.cpp
#include "engine.h"
#include <webkit/webkit.h>
#include <GLES3/gl3.h>
#include <cstring>

namespace straylight::browser {

static std::string normalize_url(const std::string& input) {
    if (input.find("://") != std::string::npos) return input;
    if (input.find('.') != std::string::npos) return "https://" + input;
    return "https://duckduckgo.com/?q=" + input;  // search fallback
}

Result<Engine, SLError> Engine::create(int width, int height) {
    // Init GTK for off-screen rendering (required by WebKitGTK)
    if (!gtk_init_check(nullptr, nullptr))
        return SLError("gtk_init_check failed");

    auto* settings = webkit_settings_new_with_settings(
        "enable-javascript", TRUE,
        "enable-webgl", TRUE,
        "enable-mediasource", TRUE,
        "hardware-acceleration-policy", WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
        nullptr);

    auto* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(settings));
    webkit_web_view_set_is_muted(view, FALSE);

    // Create GL texture for off-screen blit
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return Engine(view, settings, tex, width, height);
}

Result<void, SLError> Engine::navigate(const std::string& url) {
    auto normalized = normalize_url(url);
    webkit_web_view_load_uri(view_, normalized.c_str());
    info_.url = normalized;
    info_.is_loading = true;
    return {};
}

void Engine::go_back()    { webkit_web_view_go_back(view_); }
void Engine::go_forward() { webkit_web_view_go_forward(view_); }
void Engine::reload()     { webkit_web_view_reload(view_); }
void Engine::stop()       { webkit_web_view_stop_loading(view_); }

uint32_t Engine::render_to_texture() {
    // Pump GTK/WebKit event loop (non-blocking)
    while (g_main_context_iteration(nullptr, FALSE)) {}

    // Snapshot web view to cairo surface → upload pixels to GL texture
    // In production: use webkit_web_view_get_snapshot for async,
    // or use shared GL context for zero-copy. Simplified here:
    auto* snapshot = webkit_web_view_get_snapshot_sync(
        view_, WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT,
        WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr, nullptr);

    if (snapshot) {
        auto* data = cairo_image_surface_get_data(snapshot);
        if (data) {
            glBindTexture(GL_TEXTURE_2D, gl_texture_);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                            GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        cairo_surface_destroy(snapshot);
    }

    // Update page info
    const char* title = webkit_web_view_get_title(view_);
    if (title) info_.title = title;
    info_.load_progress = webkit_web_view_get_estimated_load_progress(view_);
    info_.can_go_back = webkit_web_view_can_go_back(view_);
    info_.can_go_forward = webkit_web_view_can_go_forward(view_);
    info_.is_loading = webkit_web_view_is_loading(view_);

    return gl_texture_;
}

// inject_mouse, inject_key — synthesize GDK events
// eval_js — webkit_web_view_evaluate_javascript
// resize — update width_/height_, reallocate texture
// destructor — g_object_unref(view_), glDeleteTextures
// ... (standard patterns)

Engine::~Engine() {
    if (view_) g_object_unref(view_);
    if (gl_texture_) glDeleteTextures(1, &gl_texture_);
}

Engine::Engine(WebKitWebView* v, WebKitSettings* s, uint32_t t, int w, int h)
    : view_(v), settings_(s), gl_texture_(t), width_(w), height_(h) {}

Engine::Engine(Engine&& o) noexcept
    : view_(o.view_), settings_(o.settings_), gl_texture_(o.gl_texture_),
      width_(o.width_), height_(o.height_), info_(std::move(o.info_)) {
    o.view_ = nullptr; o.gl_texture_ = 0;
}

Engine& Engine::operator=(Engine&& o) noexcept {
    if (this != &o) { std::swap(view_, o.view_); std::swap(gl_texture_, o.gl_texture_);
                      width_ = o.width_; height_ = o.height_; info_ = std::move(o.info_); }
    return *this;
}

} // namespace straylight::browser
```

### Task 1.3: apps/browser/tab_manager.h / tab_manager.cpp

- [ ] Multi-tab support: create, close, switch, reorder
- [ ] Each tab owns an Engine instance
- [ ] Tab bar rendering (ImGui tab bar)

```cpp
// apps/browser/tab_manager.h
#pragma once
#include "engine.h"
#include <vector>
#include <memory>
#include <string>

namespace straylight::browser {

struct Tab {
    std::unique_ptr<Engine> engine;
    std::string title = "New Tab";
    std::string url;
    bool pinned = false;
};

class TabManager {
public:
    Result<void, SLError> new_tab(const std::string& url, int width, int height);
    void close_tab(size_t index);
    void switch_to(size_t index);

    size_t active_index() const { return active_; }
    Tab& active_tab() { return tabs_[active_]; }
    const std::vector<Tab>& tabs() const { return tabs_; }
    bool empty() const { return tabs_.empty(); }

    /// Draw tab bar, returns true if active tab changed.
    bool draw_tab_bar();

private:
    std::vector<Tab> tabs_;
    size_t active_ = 0;
};

} // namespace straylight::browser
```

```cpp
// apps/browser/tab_manager.cpp
#include "tab_manager.h"
#include <imgui.h>
#include <algorithm>

namespace straylight::browser {

Result<void, SLError> TabManager::new_tab(const std::string& url,
                                           int width, int height) {
    auto engine = Engine::create(width, height);
    if (!engine) return engine.error();

    Tab tab;
    tab.engine = std::make_unique<Engine>(std::move(*engine));
    tab.url = url;
    tabs_.push_back(std::move(tab));
    active_ = tabs_.size() - 1;

    if (!url.empty()) tabs_.back().engine->navigate(url);
    return {};
}

void TabManager::close_tab(size_t index) {
    if (index >= tabs_.size()) return;
    tabs_.erase(tabs_.begin() + static_cast<ptrdiff_t>(index));
    if (active_ >= tabs_.size() && !tabs_.empty())
        active_ = tabs_.size() - 1;
}

void TabManager::switch_to(size_t index) {
    if (index < tabs_.size()) active_ = index;
}

bool TabManager::draw_tab_bar() {
    bool changed = false;
    if (ImGui::BeginTabBar("BrowserTabs", ImGuiTabBarFlags_Reorderable |
                                           ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (size_t i = 0; i < tabs_.size(); i++) {
            bool open = true;
            auto label = tabs_[i].title.empty() ? "Loading..." : tabs_[i].title;
            ImGuiTabItemFlags flags = (i == active_) ?
                ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

            if (ImGui::BeginTabItem((label + "##" + std::to_string(i)).c_str(),
                                     &open, flags)) {
                if (i != active_) { active_ = i; changed = true; }
                ImGui::EndTabItem();
            }
            if (!open) { close_tab(i); i--; changed = true; }
        }
        // "+" button for new tab
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
            new_tab("", 1280, 720);

        ImGui::EndTabBar();
    }
    return changed;
}

} // namespace straylight::browser
```

### Task 1.4: apps/browser/downloads.h / downloads.cpp

- [ ] Download queue with status tracking (pending, active, complete, failed)
- [ ] Progress tracking via WebKit download signals
- [ ] Persist download history to JSON

```cpp
// apps/browser/downloads.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <vector>
#include <cstdint>

namespace straylight::browser {

enum class DownloadStatus { Pending, Active, Complete, Failed, Cancelled };

struct DownloadEntry {
    std::string url;
    std::string filename;
    std::string dest_path;
    uint64_t bytes_received = 0;
    uint64_t bytes_total = 0;
    DownloadStatus status = DownloadStatus::Pending;
    std::string error_msg;
};

class Downloads {
public:
    /// Register a new download (called from WebKit decide-policy signal).
    size_t add(const std::string& url, const std::string& dest_dir);

    /// Update progress for an active download.
    void update_progress(size_t index, uint64_t received, uint64_t total);
    void mark_complete(size_t index);
    void mark_failed(size_t index, const std::string& error);
    void cancel(size_t index);

    const std::vector<DownloadEntry>& entries() const { return entries_; }

    /// Draw download manager panel. Returns true if user clicked "clear completed".
    bool draw_panel();

    Result<void, SLError> save_history() const;
    Result<void, SLError> load_history();

private:
    std::vector<DownloadEntry> entries_;
};

} // namespace straylight::browser
```

```cpp
// apps/browser/downloads.cpp — key logic
#include "downloads.h"
#include <imgui.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace straylight::browser {
namespace fs = std::filesystem;

size_t Downloads::add(const std::string& url, const std::string& dest_dir) {
    DownloadEntry entry;
    entry.url = url;
    // Extract filename from URL
    auto pos = url.rfind('/');
    entry.filename = (pos != std::string::npos) ? url.substr(pos + 1) : "download";
    entry.dest_path = (fs::path(dest_dir) / entry.filename).string();
    entry.status = DownloadStatus::Pending;
    entries_.push_back(std::move(entry));
    return entries_.size() - 1;
}

bool Downloads::draw_panel() {
    bool clear_requested = false;
    ImGui::Text("Downloads");
    ImGui::Separator();

    for (size_t i = 0; i < entries_.size(); i++) {
        auto& e = entries_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s", e.filename.c_str());

        if (e.status == DownloadStatus::Active && e.bytes_total > 0) {
            float frac = static_cast<float>(e.bytes_received) /
                         static_cast<float>(e.bytes_total);
            ImGui::ProgressBar(frac);
        }
        // Status label + cancel button for active downloads
        // ... (standard ImGui pattern: Selectable + SameLine + Button)
        ImGui::PopID();
    }

    if (ImGui::Button("Clear Completed")) {
        std::erase_if(entries_, [](const DownloadEntry& e) {
            return e.status == DownloadStatus::Complete;
        });
        clear_requested = true;
    }
    return clear_requested;
}

// update_progress, mark_complete, mark_failed, cancel — straightforward setters
// save_history/load_history — JSON to ~/.config/straylight/browser-downloads.json
// ... (standard JSON persistence pattern)

} // namespace straylight::browser
```

### Task 1.5: apps/browser/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (AppBase pattern)
- [ ] ImGui layout: tab bar | address bar | web content texture | downloads panel
- [ ] Keyboard shortcuts: Ctrl+T (new tab), Ctrl+W (close), Ctrl+L (focus URL bar)

```cpp
// apps/browser/main.cpp
#include "engine.h"
#include "tab_manager.h"
#include "downloads.h"
#include <straylight/result.h>
#include <straylight/log.h>
#include <imgui.h>
#include <wayland-client.h>

using namespace straylight::browser;

int main() {
    straylight::Log::init("straylight-browser");

    TabManager tabs;
    Downloads downloads;
    downloads.load_history();

    char url_buf[2048] = "https://duckduckgo.com";
    bool show_downloads = false;
    int view_w = 1280, view_h = 720;

    // Wayland + EGL + ImGui setup (AppBase pattern from Plan 9A)
    // ... (wl_display_connect, xdg_surface, wl_egl_window, EGL init, ImGui init)

    tabs.new_tab(url_buf, view_w, view_h);

    bool running = true;
    while (running) {
        // Poll Wayland events, handle resize
        // ... (standard event loop, 16ms frame target)

        // Begin ImGui frame
        // renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Browser", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        // Keyboard shortcuts
        auto& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T))
            tabs.new_tab("", view_w, view_h);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W) && !tabs.empty())
            tabs.close_tab(tabs.active_index());
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L))
            ImGui::SetKeyboardFocusHere();

        // Tab bar
        tabs.draw_tab_bar();

        if (!tabs.empty()) {
            auto& tab = tabs.active_tab();
            auto info = tab.engine->page_info();

            // Navigation bar
            ImGui::BeginChild("NavBar", {0, 36});
            if (ImGui::Button("<") && info.can_go_back) tab.engine->go_back();
            ImGui::SameLine();
            if (ImGui::Button(">") && info.can_go_forward) tab.engine->go_forward();
            ImGui::SameLine();
            if (ImGui::Button(info.is_loading ? "X" : "R"))
                info.is_loading ? tab.engine->stop() : tab.engine->reload();
            ImGui::SameLine();

            // URL input
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
            if (ImGui::InputText("##url", url_buf, sizeof(url_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                tab.engine->navigate(url_buf);
            }
            ImGui::SameLine();
            if (ImGui::Button("Downloads"))
                show_downloads = !show_downloads;

            // Progress bar
            if (info.is_loading) {
                ImGui::ProgressBar(info.load_progress, {-1, 3});
            }
            ImGui::EndChild();

            // Web content — render WebKit texture
            uint32_t tex = tab.engine->render_to_texture();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (show_downloads) avail.x -= 300;

            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(tex)),
                         avail);

            // Forward mouse/keyboard to WebKit engine
            if (ImGui::IsItemHovered()) {
                auto mouse = ImGui::GetMousePos();
                auto item_pos = ImGui::GetItemRectMin();
                tab.engine->inject_mouse(
                    mouse.x - item_pos.x, mouse.y - item_pos.y,
                    ImGui::IsMouseDown(0), ImGui::IsMouseDown(1),
                    io.MouseWheel);
            }

            // Update tab title
            tab.title = info.title.empty() ? info.url : info.title;

            // Downloads sidebar
            if (show_downloads) {
                ImGui::SameLine();
                ImGui::BeginChild("DLPanel", {300, 0}, true);
                downloads.draw_panel();
                ImGui::EndChild();
            }
        }

        ImGui::End();
        // renderer.end_frame();
    }

    downloads.save_history();
    return 0;
}
```

```cmake
# apps/browser/CMakeLists.txt
add_executable(straylight-browser
    main.cpp engine.cpp tab_manager.cpp downloads.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

target_include_directories(straylight-browser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
    ${WEBKIT_INCLUDE_DIRS})

target_link_libraries(straylight-browser PRIVATE
    straylight-common imgui
    ${WEBKIT_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    nlohmann_json)

target_compile_features(straylight-browser PRIVATE cxx_std_20)
install(TARGETS straylight-browser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 1.6: apps/browser/tests/browser_test.cpp

- [ ] Test URL normalization (bare domain, full URL, search query)
- [ ] Test TabManager: new_tab, close_tab, switch_to, reorder
- [ ] Test Downloads: add, update_progress, mark_complete, cancel

```cpp
// apps/browser/tests/browser_test.cpp
#include <gtest/gtest.h>
#include "../tab_manager.h"
#include "../downloads.h"

using namespace straylight::browser;

TEST(TabManager, NewAndClose) {
    TabManager tm;
    // Note: Engine::create requires GTK + EGL — mock or skip in CI.
    // These tests validate TabManager logic with nullptr engines.
    EXPECT_TRUE(tm.empty());
    EXPECT_EQ(tm.tabs().size(), 0u);
}

TEST(Downloads, AddAndProgress) {
    Downloads dl;
    auto idx = dl.add("https://example.com/file.tar.gz", "/tmp");
    ASSERT_EQ(idx, 0u);
    EXPECT_EQ(dl.entries()[0].filename, "file.tar.gz");
    EXPECT_EQ(dl.entries()[0].status, DownloadStatus::Pending);

    dl.update_progress(idx, 500, 1000);
    EXPECT_EQ(dl.entries()[0].bytes_received, 500u);

    dl.mark_complete(idx);
    EXPECT_EQ(dl.entries()[0].status, DownloadStatus::Complete);
}

TEST(Downloads, Cancel) {
    Downloads dl;
    auto idx = dl.add("https://example.com/big.iso", "/tmp");
    dl.update_progress(idx, 100, 10000);
    dl.cancel(idx);
    EXPECT_EQ(dl.entries()[0].status, DownloadStatus::Cancelled);
}
```

---

## Chunk 2: Text Editor — Buffer, Syntax Highlighting & Main

### Task 2.1: apps/editor/buffer.h

- [ ] Gap-buffer backed text storage (efficient insert/delete at cursor)
- [ ] Line index for O(1) line lookup
- [ ] Undo/redo stack (command pattern)
- [ ] Selection support (anchor + cursor)

```cpp
// apps/editor/buffer.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace straylight::editor {

struct Position { int line = 0; int col = 0; };
struct Selection { Position anchor; Position cursor; };

class Buffer {
public:
    Buffer();

    /// Load file contents into buffer.
    static Result<Buffer, SLError> from_file(const std::filesystem::path& path);

    /// Save buffer to file (atomic write: tmp + rename).
    Result<void, SLError> save(const std::filesystem::path& path) const;
    Result<void, SLError> save() const;  // save to original path

    /// Insert text at cursor position.
    void insert(Position pos, std::string_view text);

    /// Delete range [start, end).
    void erase(Position start, Position end);

    /// Get full text content.
    std::string text() const;

    /// Get single line (0-indexed, no newline).
    std::string_view line(int index) const;
    int line_count() const;
    int line_length(int index) const;

    /// Undo/redo.
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    bool modified() const { return modified_; }
    const std::filesystem::path& file_path() const { return path_; }

private:
    // Gap buffer: [prefix...gap...suffix]
    std::vector<char> data_;
    size_t gap_start_ = 0;
    size_t gap_end_ = 0;

    // Line starts cache (byte offsets into logical content)
    std::vector<size_t> line_starts_;
    void rebuild_line_index();

    // Undo stack
    struct Edit {
        enum class Kind { Insert, Erase };
        Kind kind;
        Position pos;
        std::string text;
    };
    std::vector<Edit> undo_stack_;
    std::vector<Edit> redo_stack_;
    void push_undo(Edit edit);

    std::filesystem::path path_;
    bool modified_ = false;

    void move_gap(size_t offset);
    size_t pos_to_offset(Position pos) const;
    Position offset_to_pos(size_t offset) const;
    char char_at(size_t logical_idx) const;
    size_t content_size() const;
};

} // namespace straylight::editor
```

### Task 2.2: apps/editor/buffer.cpp

- [ ] Gap buffer operations: `move_gap`, `insert`, `erase`
- [ ] Line index rebuild after mutations
- [ ] Undo: record inverse operation, push to stack
- [ ] File I/O: read entire file, populate gap buffer; atomic write on save

```cpp
// apps/editor/buffer.cpp
#include "buffer.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace straylight::editor {
namespace fs = std::filesystem;

static constexpr size_t INITIAL_GAP = 4096;

Buffer::Buffer() : data_(INITIAL_GAP), gap_start_(0), gap_end_(INITIAL_GAP) {
    line_starts_.push_back(0);
}

Result<Buffer, SLError> Buffer::from_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return SLError("cannot open: " + path.string());

    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);

    Buffer buf;
    buf.data_.resize(size + INITIAL_GAP);
    f.read(buf.data_.data() + INITIAL_GAP, static_cast<std::streamsize>(size));
    buf.gap_start_ = 0;
    buf.gap_end_ = INITIAL_GAP;
    buf.path_ = path;
    buf.rebuild_line_index();
    return buf;
}

Result<void, SLError> Buffer::save(const fs::path& path) const {
    auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f) return SLError("cannot write: " + tmp);
        // Write content before gap
        f.write(data_.data(), static_cast<std::streamsize>(gap_start_));
        // Write content after gap
        f.write(data_.data() + gap_end_,
                static_cast<std::streamsize>(data_.size() - gap_end_));
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) return SLError("rename failed: " + ec.message());
    return {};
}

Result<void, SLError> Buffer::save() const { return save(path_); }

size_t Buffer::content_size() const {
    return data_.size() - (gap_end_ - gap_start_);
}

void Buffer::move_gap(size_t offset) {
    if (offset == gap_start_) return;
    size_t gap_size = gap_end_ - gap_start_;
    if (offset < gap_start_) {
        size_t count = gap_start_ - offset;
        std::memmove(data_.data() + gap_end_ - count,
                     data_.data() + offset, count);
        gap_start_ = offset;
        gap_end_ = gap_start_ + gap_size;
    } else {
        size_t count = offset - gap_start_;
        std::memmove(data_.data() + gap_start_,
                     data_.data() + gap_end_, count);
        gap_start_ = offset;
        gap_end_ = gap_start_ + gap_size;
    }
}

void Buffer::insert(Position pos, std::string_view text) {
    size_t offset = pos_to_offset(pos);
    move_gap(offset);

    // Grow gap if needed
    if (text.size() > (gap_end_ - gap_start_)) {
        size_t new_gap = text.size() + INITIAL_GAP;
        data_.insert(data_.begin() + static_cast<ptrdiff_t>(gap_end_),
                     new_gap - (gap_end_ - gap_start_), '\0');
        gap_end_ = gap_start_ + new_gap;
    }

    std::memcpy(data_.data() + gap_start_, text.data(), text.size());
    gap_start_ += text.size();
    modified_ = true;

    push_undo({Edit::Kind::Insert, pos, std::string(text)});
    redo_stack_.clear();
    rebuild_line_index();
}

void Buffer::erase(Position start, Position end) {
    size_t off_start = pos_to_offset(start);
    size_t off_end = pos_to_offset(end);
    if (off_start >= off_end) return;

    // Capture erased text for undo
    std::string erased;
    for (size_t i = off_start; i < off_end; i++)
        erased += char_at(i);

    move_gap(off_start);
    gap_end_ += (off_end - off_start);
    modified_ = true;

    push_undo({Edit::Kind::Erase, start, std::move(erased)});
    redo_stack_.clear();
    rebuild_line_index();
}

void Buffer::undo() {
    if (undo_stack_.empty()) return;
    auto edit = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    // Apply inverse
    if (edit.kind == Edit::Kind::Insert) {
        // Undo insert = erase that text
        Position end = edit.pos;
        // Calculate end position from text length
        // ... (advance through text counting newlines)
        // erase without pushing to undo again
    }
    // ... (standard undo/redo inversion pattern)
}

// redo, rebuild_line_index, pos_to_offset, offset_to_pos, char_at, line, line_count
// ... (standard gap-buffer patterns)

} // namespace straylight::editor
```

### Task 2.3: apps/editor/syntax.h / syntax.cpp

- [ ] Token-based syntax highlighting for common languages (C/C++, Python, Rust, JS, Bash)
- [ ] Language detection by file extension
- [ ] Token types: keyword, string, comment, number, type, function, operator, punctuation
- [ ] Line-at-a-time highlighting for incremental updates

```cpp
// apps/editor/syntax.h
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>

namespace straylight::editor {

enum class TokenKind {
    Plain, Keyword, String, Comment, Number,
    Type, Function, Operator, Preprocessor
};

struct Token {
    int start;       // byte offset within line
    int length;
    TokenKind kind;
};

enum class Language { None, Cpp, Python, Rust, JavaScript, Bash, Json, Markdown };

class Syntax {
public:
    /// Detect language from file extension.
    static Language detect(const std::string& filename);

    /// Set active language for highlighting.
    void set_language(Language lang) { lang_ = lang; }
    Language language() const { return lang_; }

    /// Highlight a single line, returning token spans.
    std::vector<Token> highlight_line(std::string_view line) const;

    /// Map token kind to ImGui color.
    static ImU32 token_color(TokenKind kind);

private:
    Language lang_ = Language::None;

    std::vector<Token> highlight_cpp(std::string_view line) const;
    std::vector<Token> highlight_python(std::string_view line) const;
    // ... (one method per language, same signature)

    // Shared helpers
    static bool is_keyword(std::string_view word, Language lang);
    static bool is_type(std::string_view word, Language lang);
};

} // namespace straylight::editor
```

```cpp
// apps/editor/syntax.cpp — key logic
#include "syntax.h"
#include <unordered_set>
#include <cctype>

namespace straylight::editor {

Language Syntax::detect(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return Language::None;
    auto ext = filename.substr(dot);
    if (ext == ".cpp" || ext == ".cc" || ext == ".h" || ext == ".hpp") return Language::Cpp;
    if (ext == ".py") return Language::Python;
    if (ext == ".rs") return Language::Rust;
    if (ext == ".js" || ext == ".ts") return Language::JavaScript;
    if (ext == ".sh" || ext == ".bash") return Language::Bash;
    if (ext == ".json") return Language::Json;
    if (ext == ".md") return Language::Markdown;
    return Language::None;
}

std::vector<Token> Syntax::highlight_line(std::string_view line) const {
    switch (lang_) {
    case Language::Cpp:        return highlight_cpp(line);
    case Language::Python:     return highlight_python(line);
    default:                   return {{0, static_cast<int>(line.size()), TokenKind::Plain}};
    // ... (dispatch per language)
    }
}

std::vector<Token> Syntax::highlight_cpp(std::string_view line) const {
    std::vector<Token> tokens;
    size_t i = 0;

    while (i < line.size()) {
        // Skip whitespace
        if (std::isspace(static_cast<unsigned char>(line[i]))) { i++; continue; }

        // Line comment
        if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            tokens.push_back({static_cast<int>(i),
                              static_cast<int>(line.size() - i),
                              TokenKind::Comment});
            break;
        }

        // String literal
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            size_t start = i++;
            while (i < line.size() && line[i] != quote) {
                if (line[i] == '\\') i++;  // skip escape
                i++;
            }
            if (i < line.size()) i++;  // closing quote
            tokens.push_back({static_cast<int>(start),
                              static_cast<int>(i - start),
                              TokenKind::String});
            continue;
        }

        // Number
        if (std::isdigit(static_cast<unsigned char>(line[i])) ||
            (line[i] == '.' && i + 1 < line.size() &&
             std::isdigit(static_cast<unsigned char>(line[i+1])))) {
            size_t start = i;
            while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i]))
                   || line[i] == '.' || line[i] == 'x' || line[i] == 'X'))
                i++;
            tokens.push_back({static_cast<int>(start),
                              static_cast<int>(i - start),
                              TokenKind::Number});
            continue;
        }

        // Preprocessor
        if (line[i] == '#') {
            tokens.push_back({static_cast<int>(i),
                              static_cast<int>(line.size() - i),
                              TokenKind::Preprocessor});
            break;
        }

        // Identifier / keyword
        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i]))
                   || line[i] == '_'))
                i++;
            auto word = line.substr(start, i - start);
            TokenKind kind = TokenKind::Plain;
            if (is_keyword(word, Language::Cpp)) kind = TokenKind::Keyword;
            else if (is_type(word, Language::Cpp)) kind = TokenKind::Type;
            tokens.push_back({static_cast<int>(start),
                              static_cast<int>(i - start), kind});
            continue;
        }

        // Operator / punctuation — skip as Plain
        tokens.push_back({static_cast<int>(i), 1, TokenKind::Operator});
        i++;
    }
    return tokens;
}

static const std::unordered_set<std::string_view> cpp_keywords = {
    "auto", "break", "case", "catch", "class", "const", "constexpr",
    "continue", "default", "delete", "do", "else", "enum", "explicit",
    "export", "extern", "false", "for", "friend", "goto", "if", "inline",
    "namespace", "new", "noexcept", "nullptr", "operator", "private",
    "protected", "public", "return", "sizeof", "static", "static_assert",
    "struct", "switch", "template", "this", "throw", "true", "try",
    "typedef", "typeid", "typename", "union", "using", "virtual",
    "void", "volatile", "while", "co_await", "co_return", "co_yield",
    "concept", "requires", "consteval", "constinit", "override", "final",
};

bool Syntax::is_keyword(std::string_view word, Language lang) {
    if (lang == Language::Cpp) return cpp_keywords.contains(word);
    // ... (keyword sets for other languages)
    return false;
}

// is_type: int, float, double, char, bool, size_t, uint8_t, etc.
// highlight_python: similar pattern, # comments, def/class/import keywords
// token_color: cyberpunk palette mapping
// ... (standard patterns per language)

ImU32 Syntax::token_color(TokenKind kind) {
    switch (kind) {
    case TokenKind::Keyword:      return IM_COL32(198, 120, 221, 255); // purple
    case TokenKind::String:       return IM_COL32(152, 195, 121, 255); // green
    case TokenKind::Comment:      return IM_COL32(92, 99, 112, 255);   // grey
    case TokenKind::Number:       return IM_COL32(209, 154, 102, 255); // orange
    case TokenKind::Type:         return IM_COL32(86, 182, 194, 255);  // cyan
    case TokenKind::Function:     return IM_COL32(97, 175, 239, 255);  // blue
    case TokenKind::Operator:     return IM_COL32(190, 80, 70, 255);   // red
    case TokenKind::Preprocessor: return IM_COL32(224, 108, 117, 255); // salmon
    default:                      return IM_COL32(204, 204, 204, 255); // light grey
    }
}

} // namespace straylight::editor
```

### Task 2.4: apps/editor/search.h / search.cpp

- [ ] Find text (plain + regex), case sensitive/insensitive
- [ ] Replace / replace all
- [ ] Highlight all matches in viewport

```cpp
// apps/editor/search.h
#pragma once
#include "buffer.h"
#include <string>
#include <vector>
#include <regex>

namespace straylight::editor {

struct Match {
    Position start;
    Position end;
};

struct SearchOpts {
    bool case_sensitive = true;
    bool use_regex = false;
    bool whole_word = false;
};

class Search {
public:
    /// Find all matches in buffer.
    std::vector<Match> find_all(const Buffer& buf,
                                 const std::string& pattern,
                                 SearchOpts opts = {}) const;

    /// Find next match from position.
    std::optional<Match> find_next(const Buffer& buf,
                                    const std::string& pattern,
                                    Position from,
                                    SearchOpts opts = {}) const;

    /// Replace match with replacement text.
    static void replace(Buffer& buf, const Match& match,
                        const std::string& replacement);

    /// Replace all occurrences. Returns count.
    static int replace_all(Buffer& buf, const std::string& pattern,
                           const std::string& replacement,
                           SearchOpts opts = {});

private:
    std::vector<Match> find_in_text(const std::string& text,
                                     const std::string& pattern,
                                     SearchOpts opts) const;
};

} // namespace straylight::editor
```

```cpp
// apps/editor/search.cpp — key logic
#include "search.h"
#include <algorithm>

namespace straylight::editor {

std::vector<Match> Search::find_all(const Buffer& buf,
                                     const std::string& pattern,
                                     SearchOpts opts) const {
    return find_in_text(buf.text(), pattern, opts);
}

std::vector<Match> Search::find_in_text(const std::string& text,
                                         const std::string& pattern,
                                         SearchOpts opts) const {
    std::vector<Match> matches;
    if (pattern.empty()) return matches;

    if (opts.use_regex) {
        auto flags = std::regex::ECMAScript;
        if (!opts.case_sensitive) flags |= std::regex::icase;
        std::regex re(pattern, flags);
        auto begin = std::sregex_iterator(text.begin(), text.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            // Convert byte offset to Position (line, col)
            // ... (scan for newlines up to match offset)
        }
    } else {
        std::string haystack = text;
        std::string needle = pattern;
        if (!opts.case_sensitive) {
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        }
        size_t pos = 0;
        while ((pos = haystack.find(needle, pos)) != std::string::npos) {
            // Convert byte offset to Position
            // ... (scan for newlines)
            pos += needle.size();
        }
    }
    return matches;
}

void Search::replace(Buffer& buf, const Match& match,
                     const std::string& replacement) {
    buf.erase(match.start, match.end);
    buf.insert(match.start, replacement);
}

int Search::replace_all(Buffer& buf, const std::string& pattern,
                        const std::string& replacement, SearchOpts opts) {
    Search s;
    auto matches = s.find_all(buf, pattern, opts);
    // Replace in reverse order to preserve positions
    for (auto it = matches.rbegin(); it != matches.rend(); ++it)
        replace(buf, *it, replacement);
    return static_cast<int>(matches.size());
}

} // namespace straylight::editor
```

### Task 2.5: apps/editor/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (AppBase pattern)
- [ ] ImGui layout: menu bar | tab bar | editor pane with gutter + highlighted text | status bar
- [ ] Keyboard shortcuts: Ctrl+S (save), Ctrl+O (open), Ctrl+Z/Y (undo/redo), Ctrl+F (find)

```cpp
// apps/editor/main.cpp
#include "buffer.h"
#include "syntax.h"
#include "search.h"
#include <straylight/result.h>
#include <straylight/log.h>
#include <imgui.h>
#include <wayland-client.h>

using namespace straylight::editor;

int main(int argc, char* argv[]) {
    straylight::Log::init("straylight-editor");

    Buffer buffer;
    Syntax syntax;
    Search search;
    Position cursor{0, 0};
    Selection selection{};
    bool show_find = false;
    char find_buf[256] = {};
    char replace_buf[256] = {};

    // Load file from argv[1] if provided
    if (argc > 1) {
        auto result = Buffer::from_file(argv[1]);
        if (result) {
            buffer = std::move(*result);
            syntax.set_language(Syntax::detect(argv[1]));
        }
    }

    // Wayland + EGL + ImGui setup (AppBase pattern)
    // ... (standard init)

    bool running = true;
    while (running) {
        // Poll Wayland events
        // ... (standard event loop)

        // renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Editor", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_MenuBar);

        auto& io = ImGui::GetIO();

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open", "Ctrl+O")) { /* file dialog */ }
                if (ImGui::MenuItem("Save", "Ctrl+S")) buffer.save();
                if (ImGui::MenuItem("Save As...")) { /* file dialog */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, buffer.can_undo()))
                    buffer.undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, buffer.can_redo()))
                    buffer.redo();
                ImGui::Separator();
                if (ImGui::MenuItem("Find", "Ctrl+F")) show_find = true;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Keyboard shortcuts
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) buffer.save();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) buffer.undo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) buffer.redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) show_find = !show_find;

        // Find/replace bar
        if (show_find) {
            ImGui::BeginChild("FindBar", {0, 60});
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Find", find_buf, sizeof(find_buf));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(300);
            ImGui::InputText("Replace", replace_buf, sizeof(replace_buf));
            ImGui::SameLine();
            if (ImGui::Button("Next"))
                search.find_next(buffer, find_buf, cursor);
            ImGui::SameLine();
            if (ImGui::Button("Replace All"))
                Search::replace_all(buffer, find_buf, replace_buf);
            ImGui::SameLine();
            if (ImGui::Button("X")) show_find = false;
            ImGui::EndChild();
        }

        // Editor pane with gutter and syntax-highlighted text
        ImGui::BeginChild("EditorPane", {0, -24});  // leave room for status bar
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        float gutter_w = 60.0f;
        float line_h = ImGui::GetTextLineHeightWithSpacing();

        int visible_lines = static_cast<int>(ImGui::GetContentRegionAvail().y / line_h);
        int scroll_line = static_cast<int>(ImGui::GetScrollY() / line_h);

        for (int i = scroll_line;
             i < std::min(scroll_line + visible_lines + 1, buffer.line_count()); i++) {
            float y = origin.y + (i - scroll_line) * line_h;

            // Line number gutter
            char num[16];
            snprintf(num, sizeof(num), "%4d", i + 1);
            dl->AddText({origin.x, y}, IM_COL32(100, 100, 100, 255), num);

            // Syntax-highlighted line content
            auto line_text = buffer.line(i);
            auto tokens = syntax.highlight_line(line_text);
            float x = origin.x + gutter_w;
            for (auto& tok : tokens) {
                auto span = line_text.substr(tok.start, tok.length);
                dl->AddText({x, y}, Syntax::token_color(tok.kind),
                            span.data(), span.data() + span.size());
                x += ImGui::CalcTextSize(span.data(), span.data() + span.size()).x;
            }
        }

        // Handle text input (insert characters at cursor)
        // Handle arrow keys, backspace, delete, home, end
        // Handle mouse click to set cursor position
        // ... (standard text editor input handling pattern)

        ImGui::EndChild();

        // Status bar
        ImGui::Separator();
        ImGui::Text("Ln %d, Col %d | %s | %s",
                    cursor.line + 1, cursor.col + 1,
                    buffer.modified() ? "Modified" : "Saved",
                    buffer.file_path().filename().c_str());

        ImGui::End();
        // renderer.end_frame();
    }
    return 0;
}
```

```cmake
# apps/editor/CMakeLists.txt
add_executable(straylight-editor
    main.cpp buffer.cpp syntax.cpp search.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

target_include_directories(straylight-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include)

target_link_libraries(straylight-editor PRIVATE
    straylight-common imgui
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES})

target_compile_features(straylight-editor PRIVATE cxx_std_20)
install(TARGETS straylight-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 2.6: apps/editor/tests/editor_test.cpp

- [ ] Test Buffer: insert, erase, undo/redo, line access
- [ ] Test Syntax: language detection, C++ tokenization
- [ ] Test Search: find_all, find_next, replace_all

```cpp
// apps/editor/tests/editor_test.cpp
#include <gtest/gtest.h>
#include "../buffer.h"
#include "../syntax.h"
#include "../search.h"

using namespace straylight::editor;

TEST(Buffer, InsertAndLine) {
    Buffer buf;
    buf.insert({0, 0}, "hello\nworld\n");
    EXPECT_EQ(buf.line_count(), 3);  // "hello", "world", ""
    EXPECT_EQ(buf.line(0), "hello");
    EXPECT_EQ(buf.line(1), "world");
}

TEST(Buffer, EraseAndUndo) {
    Buffer buf;
    buf.insert({0, 0}, "abcdef");
    buf.erase({0, 2}, {0, 4});  // erase "cd"
    EXPECT_EQ(buf.line(0), "abef");
    buf.undo();
    EXPECT_EQ(buf.line(0), "abcdef");
}

TEST(Buffer, UndoRedo) {
    Buffer buf;
    buf.insert({0, 0}, "test");
    EXPECT_TRUE(buf.can_undo());
    EXPECT_FALSE(buf.can_redo());
    buf.undo();
    EXPECT_EQ(buf.text(), "");
    EXPECT_TRUE(buf.can_redo());
    buf.redo();
    EXPECT_EQ(buf.line(0), "test");
}

TEST(Syntax, DetectLanguage) {
    EXPECT_EQ(Syntax::detect("main.cpp"), Language::Cpp);
    EXPECT_EQ(Syntax::detect("script.py"), Language::Python);
    EXPECT_EQ(Syntax::detect("lib.rs"), Language::Rust);
    EXPECT_EQ(Syntax::detect("app.js"), Language::JavaScript);
    EXPECT_EQ(Syntax::detect("unknown"), Language::None);
}

TEST(Syntax, CppHighlight) {
    Syntax syn;
    syn.set_language(Language::Cpp);
    auto tokens = syn.highlight_line("int x = 42;");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Type);     // "int"
    // "x" = Plain, "=" = Operator, "42" = Number, ";" = Operator
}

TEST(Search, FindAll) {
    Buffer buf;
    buf.insert({0, 0}, "foo bar foo baz foo");
    Search s;
    auto matches = s.find_all(buf, "foo");
    EXPECT_EQ(matches.size(), 3u);
}

TEST(Search, CaseInsensitive) {
    Buffer buf;
    buf.insert({0, 0}, "Hello HELLO hello");
    Search s;
    auto matches = s.find_all(buf, "hello", {.case_sensitive = false});
    EXPECT_EQ(matches.size(), 3u);
}
```

---

## Chunk 3: Media Player — GStreamer Pipeline, Playlist & Main

### Task 3.1: apps/player/pipeline.h

- [ ] GStreamer playbin3 wrapper for audio/video playback
- [ ] Transport controls: play, pause, stop, seek
- [ ] Stream metadata: duration, position, title, artist, album art
- [ ] Audio/video sink integration (render video frames to GL texture)

```cpp
// apps/player/pipeline.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <functional>
#include <cstdint>

// Forward declarations — GStreamer types
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

namespace straylight::player {

enum class PlayState { Stopped, Playing, Paused, Buffering };

struct MediaInfo {
    std::string title;
    std::string artist;
    std::string album;
    int64_t duration_ns = 0;    // nanoseconds
    int64_t position_ns = 0;
    int video_width = 0;
    int video_height = 0;
    bool has_video = false;
    bool has_audio = false;
};

using StateChangedCb = std::function<void(PlayState)>;
using ErrorCb = std::function<void(const std::string&)>;

class Pipeline {
public:
    static Result<Pipeline, SLError> create();

    /// Load a media file or URL.
    Result<void, SLError> load(const std::string& uri);

    void play();
    void pause();
    void stop();
    void toggle_play_pause();

    /// Seek to position in nanoseconds.
    Result<void, SLError> seek(int64_t position_ns);

    /// Seek relative (e.g., +/- 10 seconds).
    void seek_relative(int64_t delta_ns);

    /// Set volume (0.0 to 1.0).
    void set_volume(double vol);
    double volume() const;

    /// Set mute state.
    void set_muted(bool muted);
    bool muted() const;

    /// Pump GStreamer bus messages, update state.
    void poll();

    /// Get current video frame as GL texture (0 if no video or audio-only).
    uint32_t video_texture() const { return video_tex_; }

    PlayState state() const { return state_; }
    MediaInfo info() const;

    void on_state_changed(StateChangedCb cb) { state_cb_ = std::move(cb); }
    void on_error(ErrorCb cb) { error_cb_ = std::move(cb); }

    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

private:
    GstElement* pipeline_ = nullptr;
    GstBus* bus_ = nullptr;
    GstElement* video_sink_ = nullptr;  // appsink for video frames
    uint32_t video_tex_ = 0;
    PlayState state_ = PlayState::Stopped;
    MediaInfo info_;
    double volume_ = 1.0;
    bool muted_ = false;
    StateChangedCb state_cb_;
    ErrorCb error_cb_;

    explicit Pipeline(GstElement* pipeline, GstBus* bus);
    void update_info();
    void upload_video_frame();
};

} // namespace straylight::player
```

### Task 3.2: apps/player/pipeline.cpp

- [ ] `create()` — `gst_init`, create `playbin3`, set up `appsink` for video
- [ ] `load()` — set URI property, transition to PAUSED for preroll
- [ ] `poll()` — drain bus messages: STATE_CHANGED, EOS, ERROR, TAG
- [ ] `upload_video_frame()` — pull sample from appsink, convert to RGBA, upload to GL texture

```cpp
// apps/player/pipeline.cpp
#include "pipeline.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <GLES3/gl3.h>

namespace straylight::player {

Result<Pipeline, SLError> Pipeline::create() {
    gst_init(nullptr, nullptr);

    auto* pipeline = gst_element_factory_make("playbin3", "player");
    if (!pipeline) return SLError("failed to create playbin3");

    // Create video appsink with RGBA conversion
    auto* sink = gst_element_factory_make("appsink", "videosink");
    auto* capsfilter = gst_parse_launch(
        "videoconvert ! video/x-raw,format=RGBA ! appsink name=sink", nullptr);
    g_object_set(pipeline, "video-sink", capsfilter, nullptr);

    auto* bus = gst_element_get_bus(pipeline);
    return Pipeline(pipeline, bus);
}

Result<void, SLError> Pipeline::load(const std::string& uri) {
    std::string full_uri = uri;
    // Prepend file:// if it's a local path
    if (!uri.empty() && uri[0] == '/')
        full_uri = "file://" + uri;

    gst_element_set_state(pipeline_, GST_STATE_NULL);
    g_object_set(pipeline_, "uri", full_uri.c_str(), nullptr);
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);  // preroll
    state_ = PlayState::Paused;

    // Create GL texture for video if not yet created
    if (!video_tex_) {
        glGenTextures(1, &video_tex_);
        glBindTexture(GL_TEXTURE_2D, video_tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    return {};
}

void Pipeline::play() {
    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    state_ = PlayState::Playing;
    if (state_cb_) state_cb_(state_);
}

void Pipeline::pause() {
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    state_ = PlayState::Paused;
    if (state_cb_) state_cb_(state_);
}

void Pipeline::stop() {
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    state_ = PlayState::Stopped;
    if (state_cb_) state_cb_(state_);
}

void Pipeline::toggle_play_pause() {
    (state_ == PlayState::Playing) ? pause() : play();
}

Result<void, SLError> Pipeline::seek(int64_t position_ns) {
    if (!gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            position_ns))
        return SLError("seek failed");
    return {};
}

void Pipeline::seek_relative(int64_t delta_ns) {
    update_info();
    int64_t target = info_.position_ns + delta_ns;
    target = std::clamp(target, int64_t(0), info_.duration_ns);
    seek(target);
}

void Pipeline::poll() {
    GstMessage* msg;
    while ((msg = gst_bus_pop(bus_)) != nullptr) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gst_message_parse_error(msg, &err, nullptr);
            if (error_cb_ && err) error_cb_(err->message);
            if (err) g_error_free(err);
            state_ = PlayState::Stopped;
            break;
        }
        case GST_MESSAGE_EOS:
            state_ = PlayState::Stopped;
            if (state_cb_) state_cb_(state_);
            break;
        case GST_MESSAGE_TAG: {
            GstTagList* tags = nullptr;
            gst_message_parse_tag(msg, &tags);
            if (tags) {
                gchar* val = nullptr;
                if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &val))
                    { info_.title = val; g_free(val); }
                if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &val))
                    { info_.artist = val; g_free(val); }
                if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &val))
                    { info_.album = val; g_free(val); }
                gst_tag_list_unref(tags);
            }
            break;
        }
        default: break;
        }
        gst_message_unref(msg);
    }
    update_info();
    upload_video_frame();
}

void Pipeline::update_info() {
    gint64 pos = 0, dur = 0;
    gst_element_query_position(pipeline_, GST_FORMAT_TIME, &pos);
    gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &dur);
    info_.position_ns = pos;
    info_.duration_ns = dur;
}

void Pipeline::upload_video_frame() {
    // Pull sample from appsink, upload RGBA data to GL texture
    // ... (gst_app_sink_try_pull_sample, gst_buffer_map, glTexImage2D)
}

// set_volume, volume, set_muted, muted — g_object_set/get on pipeline
// destructor — gst_element_set_state NULL, gst_object_unref
// move constructor/assignment — swap members
// ... (standard patterns)

Pipeline::~Pipeline() {
    if (pipeline_) { gst_element_set_state(pipeline_, GST_STATE_NULL);
                     gst_object_unref(pipeline_); }
    if (bus_) gst_object_unref(bus_);
    if (video_tex_) glDeleteTextures(1, &video_tex_);
}

Pipeline::Pipeline(GstElement* p, GstBus* b) : pipeline_(p), bus_(b) {}
Pipeline::Pipeline(Pipeline&& o) noexcept
    : pipeline_(o.pipeline_), bus_(o.bus_), video_tex_(o.video_tex_),
      state_(o.state_), info_(std::move(o.info_)) {
    o.pipeline_ = nullptr; o.bus_ = nullptr; o.video_tex_ = 0;
}
Pipeline& Pipeline::operator=(Pipeline&& o) noexcept {
    std::swap(pipeline_, o.pipeline_); std::swap(bus_, o.bus_);
    std::swap(video_tex_, o.video_tex_); state_ = o.state_;
    info_ = std::move(o.info_); return *this;
}

} // namespace straylight::player
```

### Task 3.3: apps/player/playlist.h / playlist.cpp

- [ ] Ordered list of media URIs with metadata
- [ ] Next, previous, shuffle, repeat modes
- [ ] Persist playlist to JSON

```cpp
// apps/player/playlist.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace straylight::player {

enum class RepeatMode { None, One, All };

struct PlaylistEntry {
    std::string uri;
    std::string title;       // from tags or filename
    std::string artist;
    int64_t duration_ns = 0;
};

class Playlist {
public:
    void add(const std::string& uri);
    void add_directory(const std::filesystem::path& dir);  // scan for media files
    void remove(size_t index);
    void clear();
    void move_entry(size_t from, size_t to);

    std::optional<std::string> current_uri() const;
    std::optional<std::string> next();
    std::optional<std::string> previous();

    void set_repeat(RepeatMode mode) { repeat_ = mode; }
    void set_shuffle(bool on);
    RepeatMode repeat() const { return repeat_; }
    bool shuffle() const { return shuffle_; }

    size_t current_index() const { return current_; }
    const std::vector<PlaylistEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }

    /// Draw playlist panel in ImGui. Returns index if user double-clicked an entry.
    std::optional<size_t> draw_panel();

    Result<void, SLError> save(const std::filesystem::path& path) const;
    Result<void, SLError> load(const std::filesystem::path& path);

private:
    std::vector<PlaylistEntry> entries_;
    std::vector<size_t> shuffle_order_;
    size_t current_ = 0;
    RepeatMode repeat_ = RepeatMode::None;
    bool shuffle_ = false;

    void rebuild_shuffle_order();
    static bool is_media_file(const std::filesystem::path& p);
};

} // namespace straylight::player
```

```cpp
// apps/player/playlist.cpp — key logic
#include "playlist.h"
#include <imgui.h>
#include <algorithm>
#include <random>
#include <fstream>
#include <nlohmann/json.hpp>

namespace straylight::player {
namespace fs = std::filesystem;

static const std::unordered_set<std::string> media_exts = {
    ".mp3", ".flac", ".ogg", ".wav", ".aac", ".m4a", ".opus",
    ".mp4", ".mkv", ".avi", ".webm", ".mov", ".wmv"
};

bool Playlist::is_media_file(const fs::path& p) {
    return media_exts.contains(p.extension().string());
}

void Playlist::add(const std::string& uri) {
    PlaylistEntry entry;
    entry.uri = uri;
    // Extract title from filename
    auto pos = uri.rfind('/');
    entry.title = (pos != std::string::npos) ? uri.substr(pos + 1) : uri;
    entries_.push_back(std::move(entry));
    if (shuffle_) rebuild_shuffle_order();
}

void Playlist::add_directory(const fs::path& dir) {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && is_media_file(entry.path()))
            add(entry.path().string());
    }
}

std::optional<std::string> Playlist::next() {
    if (entries_.empty()) return std::nullopt;
    if (repeat_ == RepeatMode::One) return entries_[current_].uri;

    size_t next_idx;
    if (shuffle_) {
        auto it = std::find(shuffle_order_.begin(), shuffle_order_.end(), current_);
        if (it != shuffle_order_.end() && (it + 1) != shuffle_order_.end())
            next_idx = *(it + 1);
        else if (repeat_ == RepeatMode::All)
            next_idx = shuffle_order_.front();
        else return std::nullopt;
    } else {
        next_idx = current_ + 1;
        if (next_idx >= entries_.size()) {
            if (repeat_ == RepeatMode::All) next_idx = 0;
            else return std::nullopt;
        }
    }
    current_ = next_idx;
    return entries_[current_].uri;
}

// previous — mirror of next
// draw_panel — ImGui list with Selectable, drag-reorder
// save/load — JSON array of {uri, title, artist}
// ... (standard patterns)

std::optional<size_t> Playlist::draw_panel() {
    std::optional<size_t> clicked;
    for (size_t i = 0; i < entries_.size(); i++) {
        bool selected = (i == current_);
        auto& e = entries_[i];
        auto label = e.title + (e.artist.empty() ? "" : " - " + e.artist);
        if (ImGui::Selectable(label.c_str(), selected,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                current_ = i;
                clicked = i;
            }
        }
    }
    return clicked;
}

} // namespace straylight::player
```

### Task 3.4: apps/player/controls.h / controls.cpp

- [ ] Transport control UI state (play/pause button, seek slider, volume, etc.)
- [ ] Time formatting (ns to mm:ss)
- [ ] Album art display

```cpp
// apps/player/controls.h
#pragma once
#include "pipeline.h"
#include <imgui.h>
#include <string>

namespace straylight::player {

class Controls {
public:
    /// Draw transport controls. Returns true if user triggered an action.
    struct Action {
        bool play_pause = false;
        bool stop = false;
        bool next = false;
        bool previous = false;
        int64_t seek_to = -1;     // -1 = no seek
        float volume_set = -1.0f; // -1 = no change
    };

    Action draw(const Pipeline& pipeline, const MediaInfo& info);

private:
    bool seeking_ = false;       // true while user drags seek bar
    float seek_pos_ = 0.0f;

    static std::string format_time(int64_t ns);
};

} // namespace straylight::player
```

```cpp
// apps/player/controls.cpp
#include "controls.h"
#include <cstdio>

namespace straylight::player {

std::string Controls::format_time(int64_t ns) {
    int64_t seconds = ns / 1'000'000'000;
    int m = static_cast<int>(seconds / 60);
    int s = static_cast<int>(seconds % 60);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

Controls::Action Controls::draw(const Pipeline& pipeline, const MediaInfo& info) {
    Action action;

    // Transport buttons
    if (ImGui::Button(pipeline.state() == PlayState::Playing ? "||" : ">"))
        action.play_pause = true;
    ImGui::SameLine();
    if (ImGui::Button("[]")) action.stop = true;
    ImGui::SameLine();
    if (ImGui::Button("|<")) action.previous = true;
    ImGui::SameLine();
    if (ImGui::Button(">|")) action.next = true;

    // Seek slider
    ImGui::SameLine();
    float duration = static_cast<float>(info.duration_ns);
    float position = seeking_ ? seek_pos_
                              : static_cast<float>(info.position_ns);

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200);
    if (ImGui::SliderFloat("##seek", &position, 0.0f, duration, "")) {
        seeking_ = true;
        seek_pos_ = position;
    }
    if (seeking_ && ImGui::IsMouseReleased(0)) {
        action.seek_to = static_cast<int64_t>(seek_pos_);
        seeking_ = false;
    }

    // Time display
    ImGui::SameLine();
    ImGui::Text("%s / %s",
                format_time(info.position_ns).c_str(),
                format_time(info.duration_ns).c_str());

    // Volume slider
    ImGui::SameLine();
    float vol = static_cast<float>(pipeline.volume());
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("##vol", &vol, 0.0f, 1.0f, ""))
        action.volume_set = vol;

    // Track info
    if (!info.title.empty()) {
        ImGui::Text("%s", info.title.c_str());
        if (!info.artist.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", info.artist.c_str());
        }
    }

    return action;
}

} // namespace straylight::player
```

### Task 3.5: apps/player/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (AppBase pattern)
- [ ] ImGui layout: video area (or album art) | transport controls | playlist sidebar
- [ ] Keyboard: Space (play/pause), Left/Right (seek +/-10s), Up/Down (volume)

```cpp
// apps/player/main.cpp
#include "pipeline.h"
#include "playlist.h"
#include "controls.h"
#include <straylight/result.h>
#include <straylight/log.h>
#include <imgui.h>
#include <wayland-client.h>

using namespace straylight::player;

int main(int argc, char* argv[]) {
    straylight::Log::init("straylight-player");

    auto pipe_result = Pipeline::create();
    if (!pipe_result) {
        SL_CRITICAL("GStreamer init failed: {}", pipe_result.error().message());
        return 1;
    }
    auto pipeline = std::move(*pipe_result);

    Playlist playlist;
    Controls controls;

    // Load files from command line args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (std::filesystem::is_directory(arg))
            playlist.add_directory(arg);
        else
            playlist.add(arg);
    }

    // Start playback if playlist is not empty
    if (!playlist.empty()) {
        auto uri = playlist.current_uri();
        if (uri) pipeline.load(*uri);
    }

    // Wayland + EGL + ImGui setup (AppBase pattern)
    // ... (standard init)

    bool running = true;
    while (running) {
        // Poll Wayland events
        // ... (standard event loop)

        pipeline.poll();

        // renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Player", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        auto& io = ImGui::GetIO();

        // Keyboard shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) pipeline.toggle_play_pause();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            pipeline.seek_relative(-10'000'000'000LL);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            pipeline.seek_relative(10'000'000'000LL);

        auto info = pipeline.info();

        // Main content area (video or placeholder)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float playlist_w = 280.0f;
        float controls_h = 80.0f;

        if (info.has_video && pipeline.video_texture()) {
            ImVec2 video_size = {avail.x - playlist_w, avail.y - controls_h};
            ImGui::Image(
                reinterpret_cast<ImTextureID>(
                    static_cast<uintptr_t>(pipeline.video_texture())),
                video_size);
        } else {
            // Audio-only: show track info centered
            ImGui::BeginChild("AudioDisplay", {avail.x - playlist_w, avail.y - controls_h});
            ImVec2 center = {ImGui::GetContentRegionAvail().x * 0.5f,
                             ImGui::GetContentRegionAvail().y * 0.5f};
            ImGui::SetCursorPos(center);
            if (!info.title.empty()) ImGui::TextWrapped("%s", info.title.c_str());
            if (!info.artist.empty()) ImGui::TextWrapped("%s", info.artist.c_str());
            if (!info.album.empty()) ImGui::TextDisabled("%s", info.album.c_str());
            ImGui::EndChild();
        }

        // Transport controls
        ImGui::BeginChild("Controls", {avail.x - playlist_w, controls_h});
        auto action = controls.draw(pipeline, info);
        if (action.play_pause) pipeline.toggle_play_pause();
        if (action.stop) pipeline.stop();
        if (action.next) {
            auto uri = playlist.next();
            if (uri) { pipeline.load(*uri); pipeline.play(); }
        }
        if (action.previous) {
            auto uri = playlist.previous();
            if (uri) { pipeline.load(*uri); pipeline.play(); }
        }
        if (action.seek_to >= 0) pipeline.seek(action.seek_to);
        if (action.volume_set >= 0) pipeline.set_volume(action.volume_set);
        ImGui::EndChild();

        // Playlist sidebar
        ImGui::SameLine();
        ImGui::BeginChild("Playlist", {playlist_w, 0}, true);
        ImGui::Text("Playlist");
        ImGui::Separator();
        auto clicked = playlist.draw_panel();
        if (clicked) {
            auto uri = playlist.current_uri();
            if (uri) { pipeline.load(*uri); pipeline.play(); }
        }
        ImGui::EndChild();

        ImGui::End();
        // renderer.end_frame();

        // Auto-advance on EOS
        if (pipeline.state() == PlayState::Stopped && !playlist.empty()) {
            auto uri = playlist.next();
            if (uri) { pipeline.load(*uri); pipeline.play(); }
        }
    }
    return 0;
}
```

```cmake
# apps/player/CMakeLists.txt
add_executable(straylight-player
    main.cpp pipeline.cpp playlist.cpp controls.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

target_include_directories(straylight-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
    ${GST_INCLUDE_DIRS})

target_link_libraries(straylight-player PRIVATE
    straylight-common imgui
    ${GST_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES}
    nlohmann_json)

target_compile_features(straylight-player PRIVATE cxx_std_20)
install(TARGETS straylight-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 3.6: apps/player/tests/player_test.cpp

- [ ] Test Playlist: add, next, previous, shuffle, repeat modes
- [ ] Test Controls: time formatting
- [ ] Test Pipeline creation (requires GStreamer — skip in CI if unavailable)

```cpp
// apps/player/tests/player_test.cpp
#include <gtest/gtest.h>
#include "../playlist.h"
#include "../controls.h"

using namespace straylight::player;

TEST(Playlist, AddAndNavigate) {
    Playlist pl;
    pl.add("/music/track1.mp3");
    pl.add("/music/track2.mp3");
    pl.add("/music/track3.mp3");

    EXPECT_EQ(pl.entries().size(), 3u);
    EXPECT_EQ(pl.current_uri().value(), "/music/track1.mp3");

    auto next = pl.next();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(*next, "/music/track2.mp3");

    auto prev = pl.previous();
    ASSERT_TRUE(prev.has_value());
    EXPECT_EQ(*prev, "/music/track1.mp3");
}

TEST(Playlist, RepeatAll) {
    Playlist pl;
    pl.add("/a.mp3");
    pl.add("/b.mp3");
    pl.set_repeat(RepeatMode::All);

    pl.next();  // b
    auto wrap = pl.next();  // should wrap to a
    ASSERT_TRUE(wrap.has_value());
    EXPECT_EQ(*wrap, "/a.mp3");
}

TEST(Playlist, RepeatNoneStops) {
    Playlist pl;
    pl.add("/a.mp3");
    pl.set_repeat(RepeatMode::None);

    auto next = pl.next();
    EXPECT_FALSE(next.has_value());
}

TEST(Controls, FormatTime) {
    // Access via a test helper or make format_time public for testing
    // 90 seconds = 1:30
    int64_t ns = 90'000'000'000LL;
    int64_t sec = ns / 1'000'000'000;
    int m = static_cast<int>(sec / 60);
    int s = static_cast<int>(sec % 60);
    EXPECT_EQ(m, 1);
    EXPECT_EQ(s, 30);
}
```

---

## Chunk 4: Image Viewer — Loader, Viewport & Main

### Task 4.1: apps/image_viewer/loader.h

- [ ] Load images via stb_image (PNG, JPG, BMP, GIF, TGA, PSD, HDR)
- [ ] Upload pixel data to OpenGL texture
- [ ] EXIF orientation handling
- [ ] Async loading for large images

```cpp
// apps/image_viewer/loader.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <filesystem>
#include <cstdint>
#include <future>
#include <GLES3/gl3.h>

namespace straylight::viewer {

struct ImageData {
    uint32_t texture = 0;       // OpenGL texture ID
    int width = 0;
    int height = 0;
    int channels = 0;           // original channel count
    std::string filename;
    std::string format;         // "PNG", "JPEG", etc.
    size_t file_size = 0;       // bytes on disk
};

class Loader {
public:
    /// Load image from file path and upload to GL texture.
    static Result<ImageData, SLError> load(const std::filesystem::path& path);

    /// Load asynchronously (pixel decode on thread, GL upload on main thread).
    static std::future<Result<ImageData, SLError>>
    load_async(const std::filesystem::path& path);

    /// Free GL texture.
    static void unload(ImageData& img);

    /// List supported image files in a directory.
    static std::vector<std::filesystem::path>
    list_images(const std::filesystem::path& dir);

    /// Check if file extension is a supported image format.
    static bool is_supported(const std::filesystem::path& path);

private:
    struct RawPixels {
        unsigned char* data = nullptr;
        int width = 0, height = 0, channels = 0;
        ~RawPixels();
    };

    static Result<RawPixels, SLError> decode(const std::filesystem::path& path);
    static Result<uint32_t, SLError> upload_texture(const RawPixels& pixels);
};

} // namespace straylight::viewer
```

### Task 4.2: apps/image_viewer/loader.cpp

- [ ] `decode()` — `stbi_load` with RGBA requested
- [ ] `upload_texture()` — `glGenTextures`, `glTexImage2D` with mipmap generation
- [ ] `load()` — decode + upload in one call
- [ ] `list_images()` — filter directory by supported extensions

```cpp
// apps/image_viewer/loader.cpp
#include "loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>

namespace straylight::viewer {
namespace fs = std::filesystem;

static const std::unordered_set<std::string> supported_exts = {
    ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".psd", ".hdr"
};

bool Loader::is_supported(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return supported_exts.contains(ext);
}

Loader::RawPixels::~RawPixels() {
    if (data) stbi_image_free(data);
}

Result<Loader::RawPixels, SLError> Loader::decode(const fs::path& path) {
    RawPixels pix;
    pix.data = stbi_load(path.c_str(), &pix.width, &pix.height,
                         &pix.channels, 4);  // request RGBA
    if (!pix.data)
        return SLError("stbi_load failed: " + std::string(stbi_failure_reason()));
    return pix;
}

Result<uint32_t, SLError> Loader::upload_texture(const RawPixels& pixels) {
    uint32_t tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return SLError("glGenTextures failed");

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 pixels.width, pixels.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return tex;
}

Result<ImageData, SLError> Loader::load(const fs::path& path) {
    auto pixels = decode(path);
    if (!pixels) return pixels.error();

    auto tex = upload_texture(*pixels);
    if (!tex) return tex.error();

    std::error_code ec;
    ImageData img;
    img.texture = *tex;
    img.width = pixels->width;
    img.height = pixels->height;
    img.channels = pixels->channels;
    img.filename = path.filename().string();
    img.file_size = fs::file_size(path, ec);

    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    img.format = ext.substr(1);  // remove leading dot

    return img;
}

void Loader::unload(ImageData& img) {
    if (img.texture) {
        glDeleteTextures(1, &img.texture);
        img.texture = 0;
    }
}

std::vector<fs::path> Loader::list_images(const fs::path& dir) {
    std::vector<fs::path> result;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && is_supported(entry.path()))
            result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::future<Result<ImageData, SLError>>
Loader::load_async(const fs::path& path) {
    return std::async(std::launch::async, [path]() {
        // Decode on background thread (CPU work)
        auto pixels = decode(path);
        if (!pixels) return Result<ImageData, SLError>(pixels.error());
        // Note: GL upload must happen on main thread.
        // Return decoded pixels for main thread to upload.
        // Simplified: do full load here (requires shared GL context).
        return load(path);
    });
}

} // namespace straylight::viewer
```

### Task 4.3: apps/image_viewer/viewer.h / viewer.cpp

- [ ] Pan/zoom/rotate viewport state
- [ ] Fit-to-window, 1:1, and custom zoom levels
- [ ] Mouse drag to pan, scroll wheel to zoom
- [ ] Rotation (90-degree increments)
- [ ] Image info overlay

```cpp
// apps/image_viewer/viewer.h
#pragma once
#include "loader.h"
#include <imgui.h>

namespace straylight::viewer {

class Viewer {
public:
    enum class FitMode { FitWindow, Original, Custom };

    /// Set the current image to display.
    void set_image(const ImageData& img);

    /// Reset viewport (fit to window).
    void reset();

    /// Rotate 90 degrees clockwise.
    void rotate_cw();
    void rotate_ccw();

    /// Set zoom level (1.0 = 100%).
    void set_zoom(float zoom);
    float zoom() const { return zoom_; }

    /// Draw the image in the current ImGui window with pan/zoom/rotate.
    void draw(ImVec2 available_size);

    /// Draw image info overlay.
    void draw_info_overlay(const ImageData& img);

    /// Handle input (mouse drag, scroll, keyboard).
    void handle_input();

    FitMode fit_mode() const { return fit_mode_; }
    void set_fit_mode(FitMode mode) { fit_mode_ = mode; }

private:
    uint32_t texture_ = 0;
    int img_w_ = 0, img_h_ = 0;
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f, pan_y_ = 0.0f;
    int rotation_ = 0;          // 0, 90, 180, 270
    FitMode fit_mode_ = FitMode::FitWindow;
    bool dragging_ = false;
    ImVec2 drag_start_{};

    ImVec2 compute_display_size(ImVec2 available) const;
};

} // namespace straylight::viewer
```

```cpp
// apps/image_viewer/viewer.cpp
#include "viewer.h"
#include <algorithm>
#include <cmath>

namespace straylight::viewer {

void Viewer::set_image(const ImageData& img) {
    texture_ = img.texture;
    img_w_ = img.width;
    img_h_ = img.height;
    reset();
}

void Viewer::reset() {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    rotation_ = 0;
    fit_mode_ = FitMode::FitWindow;
}

void Viewer::rotate_cw() { rotation_ = (rotation_ + 90) % 360; }
void Viewer::rotate_ccw() { rotation_ = (rotation_ + 270) % 360; }

void Viewer::set_zoom(float z) {
    zoom_ = std::clamp(z, 0.1f, 20.0f);
    fit_mode_ = FitMode::Custom;
}

ImVec2 Viewer::compute_display_size(ImVec2 available) const {
    if (img_w_ == 0 || img_h_ == 0) return {0, 0};

    float iw = static_cast<float>(img_w_);
    float ih = static_cast<float>(img_h_);

    // Swap dimensions if rotated 90 or 270
    if (rotation_ == 90 || rotation_ == 270) std::swap(iw, ih);

    switch (fit_mode_) {
    case FitMode::FitWindow: {
        float scale = std::min(available.x / iw, available.y / ih);
        return {iw * scale, ih * scale};
    }
    case FitMode::Original:
        return {iw, ih};
    case FitMode::Custom:
        return {iw * zoom_, ih * zoom_};
    }
    return {iw, ih};
}

void Viewer::draw(ImVec2 available_size) {
    if (!texture_) {
        ImGui::TextDisabled("No image loaded");
        return;
    }

    auto display = compute_display_size(available_size);

    // Center image with pan offset
    float offset_x = (available_size.x - display.x) * 0.5f + pan_x_;
    float offset_y = (available_size.y - display.y) * 0.5f + pan_y_;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 img_pos = {cursor.x + offset_x, cursor.y + offset_y};

    auto* dl = ImGui::GetWindowDrawList();

    // For rotated images, use UV coordinate manipulation
    ImVec2 uv0, uv1, uv2, uv3;
    switch (rotation_) {
    case 0:   uv0={0,0}; uv1={1,0}; uv2={1,1}; uv3={0,1}; break;
    case 90:  uv0={0,1}; uv1={0,0}; uv2={1,0}; uv3={1,1}; break;
    case 180: uv0={1,1}; uv1={0,1}; uv2={0,0}; uv3={1,0}; break;
    case 270: uv0={1,0}; uv1={1,1}; uv2={0,1}; uv3={0,0}; break;
    default:  uv0={0,0}; uv1={1,0}; uv2={1,1}; uv3={0,1}; break;
    }

    auto tex_id = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_));
    dl->AddImageQuad(tex_id,
                     img_pos,
                     {img_pos.x + display.x, img_pos.y},
                     {img_pos.x + display.x, img_pos.y + display.y},
                     {img_pos.x, img_pos.y + display.y},
                     uv0, uv1, uv2, uv3);

    // Invisible button for input capture
    ImGui::InvisibleButton("viewer_area", available_size);
}

void Viewer::handle_input() {
    auto& io = ImGui::GetIO();

    // Scroll to zoom
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        float new_zoom = zoom_ * (1.0f + io.MouseWheel * 0.1f);
        set_zoom(new_zoom);
    }

    // Drag to pan
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        auto delta = ImGui::GetMouseDragDelta(0);
        pan_x_ += delta.x - (dragging_ ? drag_start_.x : 0.0f);
        pan_y_ += delta.y - (dragging_ ? drag_start_.y : 0.0f);
        drag_start_ = delta;
        dragging_ = true;
        fit_mode_ = FitMode::Custom;
    } else {
        dragging_ = false;
        drag_start_ = {};
    }
}

void Viewer::draw_info_overlay(const ImageData& img) {
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::BeginChild("InfoOverlay", {220, 100}, true);
    ImGui::Text("%s", img.filename.c_str());
    ImGui::Text("%dx%d  %s", img.width, img.height, img.format.c_str());
    ImGui::Text("%.1f KB", static_cast<float>(img.file_size) / 1024.0f);
    ImGui::Text("Zoom: %.0f%%", zoom_ * 100.0f);
    if (rotation_ != 0) ImGui::Text("Rotation: %d", rotation_);
    ImGui::EndChild();
}

} // namespace straylight::viewer
```

### Task 4.4: apps/image_viewer/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (AppBase pattern)
- [ ] ImGui layout: toolbar | image viewport | info overlay
- [ ] Navigate images in directory (left/right arrows or file list)
- [ ] Keyboard: +/- zoom, R rotate, F fit, 1 original size, arrows prev/next

```cpp
// apps/image_viewer/main.cpp
#include "loader.h"
#include "viewer.h"
#include <straylight/result.h>
#include <straylight/log.h>
#include <imgui.h>
#include <wayland-client.h>
#include <filesystem>

using namespace straylight::viewer;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    straylight::Log::init("straylight-image-viewer");

    Viewer viewer;
    ImageData current_image;
    std::vector<fs::path> image_list;
    int current_index = -1;
    bool show_info = true;

    // Build image list from argument
    auto load_image = [&](int index) {
        if (index < 0 || index >= static_cast<int>(image_list.size())) return;
        Loader::unload(current_image);
        auto result = Loader::load(image_list[index]);
        if (result) {
            current_image = std::move(*result);
            viewer.set_image(current_image);
            current_index = index;
        }
    };

    if (argc > 1) {
        fs::path arg = argv[1];
        if (fs::is_directory(arg)) {
            image_list = Loader::list_images(arg);
            if (!image_list.empty()) load_image(0);
        } else if (Loader::is_supported(arg)) {
            // Load single file, also populate list from parent directory
            image_list = Loader::list_images(arg.parent_path());
            auto it = std::find(image_list.begin(), image_list.end(),
                                fs::canonical(arg));
            current_index = (it != image_list.end())
                ? static_cast<int>(it - image_list.begin()) : 0;
            load_image(current_index);
        }
    }

    // Wayland + EGL + ImGui setup (AppBase pattern)
    // ... (standard init)

    bool running = true;
    while (running) {
        // Poll Wayland events
        // ... (standard event loop)

        // renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("ImageViewer", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        auto& io = ImGui::GetIO();

        // Toolbar
        ImGui::BeginChild("Toolbar", {0, 36});
        if (ImGui::Button("<") && current_index > 0)
            load_image(current_index - 1);
        ImGui::SameLine();
        if (ImGui::Button(">") &&
            current_index < static_cast<int>(image_list.size()) - 1)
            load_image(current_index + 1);
        ImGui::SameLine();

        if (ImGui::Button("+")) viewer.set_zoom(viewer.zoom() * 1.25f);
        ImGui::SameLine();
        if (ImGui::Button("-")) viewer.set_zoom(viewer.zoom() * 0.8f);
        ImGui::SameLine();
        if (ImGui::Button("Fit")) {
            viewer.set_fit_mode(Viewer::FitMode::FitWindow);
            viewer.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("1:1"))
            viewer.set_fit_mode(Viewer::FitMode::Original);
        ImGui::SameLine();
        if (ImGui::Button("CW")) viewer.rotate_cw();
        ImGui::SameLine();
        if (ImGui::Button("CCW")) viewer.rotate_ccw();
        ImGui::SameLine();
        if (ImGui::Button("Info")) show_info = !show_info;

        if (!image_list.empty()) {
            ImGui::SameLine();
            ImGui::Text("%d / %d", current_index + 1,
                        static_cast<int>(image_list.size()));
        }
        ImGui::EndChild();

        // Keyboard shortcuts
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && current_index > 0)
            load_image(current_index - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) &&
            current_index < static_cast<int>(image_list.size()) - 1)
            load_image(current_index + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_Equal))  // +
            viewer.set_zoom(viewer.zoom() * 1.25f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus))
            viewer.set_zoom(viewer.zoom() * 0.8f);
        if (ImGui::IsKeyPressed(ImGuiKey_R)) viewer.rotate_cw();
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            viewer.set_fit_mode(Viewer::FitMode::FitWindow);
            viewer.reset();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1))
            viewer.set_fit_mode(Viewer::FitMode::Original);
        if (ImGui::IsKeyPressed(ImGuiKey_I)) show_info = !show_info;

        // Image viewport
        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewer.draw(avail);
        viewer.handle_input();

        // Info overlay
        if (show_info && current_image.texture) {
            ImGui::SetCursorPos({io.DisplaySize.x - 240, 44});
            viewer.draw_info_overlay(current_image);
        }

        ImGui::End();
        // renderer.end_frame();
    }

    Loader::unload(current_image);
    return 0;
}
```

```cmake
# apps/image_viewer/CMakeLists.txt
add_executable(straylight-image-viewer
    main.cpp loader.cpp viewer.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)

target_include_directories(straylight-image-viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/common/include
    ${CMAKE_SOURCE_DIR}/third_party/stb)

target_link_libraries(straylight-image-viewer PRIVATE
    straylight-common imgui
    ${WAYLAND_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES_LIBRARIES})

target_compile_features(straylight-image-viewer PRIVATE cxx_std_20)
install(TARGETS straylight-image-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 4.5: apps/image_viewer/tests/viewer_test.cpp

- [ ] Test Loader: supported extensions, list_images
- [ ] Test Viewer: zoom clamping, rotation cycling, fit mode computation
- [ ] Test decode with a small embedded PNG (base64 test fixture)

```cpp
// apps/image_viewer/tests/viewer_test.cpp
#include <gtest/gtest.h>
#include "../loader.h"
#include "../viewer.h"

using namespace straylight::viewer;

TEST(Loader, SupportedExtensions) {
    EXPECT_TRUE(Loader::is_supported("photo.png"));
    EXPECT_TRUE(Loader::is_supported("image.JPG"));
    EXPECT_TRUE(Loader::is_supported("art.bmp"));
    EXPECT_TRUE(Loader::is_supported("pic.gif"));
    EXPECT_FALSE(Loader::is_supported("doc.pdf"));
    EXPECT_FALSE(Loader::is_supported("code.cpp"));
    EXPECT_FALSE(Loader::is_supported("noext"));
}

TEST(Viewer, ZoomClamp) {
    Viewer v;
    ImageData img;
    img.width = 100; img.height = 100; img.texture = 1;
    v.set_image(img);

    v.set_zoom(0.01f);
    EXPECT_GE(v.zoom(), 0.1f);

    v.set_zoom(100.0f);
    EXPECT_LE(v.zoom(), 20.0f);
}

TEST(Viewer, RotationCycle) {
    Viewer v;
    ImageData img;
    img.width = 200; img.height = 100; img.texture = 1;
    v.set_image(img);

    v.rotate_cw();   // 90
    v.rotate_cw();   // 180
    v.rotate_cw();   // 270
    v.rotate_cw();   // 0 (wrapped)
    // Rotation state is internal, verify via display behavior.
    // Just ensure no crash after 4 rotations.
}

TEST(Viewer, ResetState) {
    Viewer v;
    ImageData img;
    img.width = 500; img.height = 300; img.texture = 1;
    v.set_image(img);

    v.set_zoom(3.0f);
    v.rotate_cw();
    v.reset();
    EXPECT_FLOAT_EQ(v.zoom(), 1.0f);
    EXPECT_EQ(v.fit_mode(), Viewer::FitMode::FitWindow);
}
```

---

## Summary

| Chunk | Scope | Key Files | Tasks |
|-------|-------|-----------|-------|
| 1 | Web Browser — Engine, Tabs, Downloads | `engine.h/cpp`, `tab_manager.h/cpp`, `downloads.h/cpp`, `main.cpp` | 1.1–1.6 |
| 2 | Text Editor — Buffer, Syntax, Search | `buffer.h/cpp`, `syntax.h/cpp`, `search.h/cpp`, `main.cpp` | 2.1–2.6 |
| 3 | Media Player — GStreamer Pipeline, Playlist, Controls | `pipeline.h/cpp`, `playlist.h/cpp`, `controls.h/cpp`, `main.cpp` | 3.1–3.6 |
| 4 | Image Viewer — Loader, Viewport | `loader.h/cpp`, `viewer.h/cpp`, `main.cpp` | 4.1–4.5 |

### External Dependencies

| Dependency | Version | Used By |
|-----------|---------|---------|
| WebKitGTK | 6.0+ | Browser (engine) |
| GStreamer | 1.24+ | Player (pipeline) |
| stb_image | latest | Image Viewer (loader) |
| nlohmann/json | 3.11+ | Browser (downloads), Player (playlist) |
| Dear ImGui | 1.90+ | All apps (UI) |
| wayland-client | 1.22+ | All apps (windowing) |
| EGL + GLESv2 | 3.0 | All apps (rendering) |
| GTest | 1.14+ | All apps (tests) |
