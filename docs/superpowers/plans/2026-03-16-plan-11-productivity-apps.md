# Plan 11: Desktop Apps — Browser, Editor, Media Player & Image Viewer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement four productivity applications as standalone ImGui Wayland clients: a WebKit-based web browser, a text editor with syntax highlighting, a GStreamer-backed media player, and an image viewer using stb_image.

**Architecture:** All apps use `wl_egl_window + EGL + ImGui` (same pattern as Plan 9A). Each app is a standalone binary under `apps/`. `AppBase::run()` handles Wayland connection, xdg-shell surface, EGL init, ImGui context, and the frame loop.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, EGL/OpenGL ES 3.0, wayland-client 1.22+, xdg-shell, WebKitGTK 6.0+, GStreamer 1.24+, stb_image, nlohmann/json 3.11+, GTest 1.14+

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor), Plan 9A (AppBase pattern)

**Development environment:** Linux x86_64 required.

---

## File Structure

```
apps/browser/
├── CMakeLists.txt
├── main.cpp
├── engine.h / engine.cpp
├── tab_manager.h / tab_manager.cpp
├── downloads.h / downloads.cpp
└── tests/browser_test.cpp

apps/editor/
├── CMakeLists.txt
├── main.cpp
├── buffer.h / buffer.cpp
├── syntax.h / syntax.cpp
├── search.h / search.cpp
└── tests/editor_test.cpp

apps/player/
├── CMakeLists.txt
├── main.cpp
├── pipeline.h / pipeline.cpp
├── playlist.h / playlist.cpp
├── controls.h / controls.cpp
└── tests/player_test.cpp

apps/image_viewer/
├── CMakeLists.txt
├── main.cpp
├── loader.h / loader.cpp
├── viewer.h / viewer.cpp
└── tests/viewer_test.cpp
```

---

## Chunk 1: Web Browser

### Task 1.1: apps/browser/engine.h

- [ ] WebKitGTK web view wrapper (off-screen rendering to GL texture)
- [ ] Navigation: load URL, back, forward, reload, stop
- [ ] Page metadata: title, progress, TLS status

```cpp
// apps/browser/engine.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <functional>
#include <cstdint>

typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitSettings WebKitSettings;

namespace straylight::browser {

enum class TlsStatus { None, Valid, Invalid };

struct PageInfo {
    std::string title, url;
    float load_progress = 0.0f;
    TlsStatus tls = TlsStatus::None;
    bool can_go_back = false, can_go_forward = false, is_loading = false;
};

class Engine {
public:
    static Result<Engine, SLError> create(int width, int height);
    Result<void, SLError> navigate(const std::string& url);
    void go_back();
    void go_forward();
    void reload();
    void stop();
    void resize(int width, int height);
    uint32_t render_to_texture();
    void inject_mouse(float x, float y, bool lmb, bool rmb, float scroll_y);
    void inject_key(uint32_t keycode, uint32_t modifiers, bool pressed);
    PageInfo page_info() const;
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;

private:
    WebKitWebView* view_ = nullptr;
    WebKitSettings* settings_ = nullptr;
    uint32_t gl_texture_ = 0;
    int width_, height_;
    PageInfo info_;
    explicit Engine(WebKitWebView*, WebKitSettings*, uint32_t, int, int);
};

} // namespace straylight::browser
```

### Task 1.2: apps/browser/engine.cpp

- [ ] `create()` — init GTK headless, `WebKitWebView` with off-screen rendering, GL texture
- [ ] `navigate()` — normalize URL (auto https://, search fallback), `webkit_web_view_load_uri`
- [ ] `render_to_texture()` — pump `g_main_context_iteration`, snapshot to cairo, upload GL
- [ ] Wire signals: `notify::title`, `load-changed`, TLS errors

```cpp
// apps/browser/engine.cpp
#include "engine.h"
#include <webkit/webkit.h>
#include <GLES3/gl3.h>

namespace straylight::browser {

static std::string normalize_url(const std::string& input) {
    if (input.find("://") != std::string::npos) return input;
    if (input.find('.') != std::string::npos) return "https://" + input;
    return "https://duckduckgo.com/?q=" + input;
}

Result<Engine, SLError> Engine::create(int width, int height) {
    if (!gtk_init_check(nullptr, nullptr))
        return SLError("gtk_init_check failed");
    auto* settings = webkit_settings_new_with_settings(
        "enable-javascript", TRUE, "enable-webgl", TRUE, nullptr);
    auto* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(settings));

    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    return Engine(view, settings, tex, width, height);
}

Result<void, SLError> Engine::navigate(const std::string& url) {
    auto normalized = normalize_url(url);
    webkit_web_view_load_uri(view_, normalized.c_str());
    info_.url = normalized; info_.is_loading = true;
    return {};
}

void Engine::go_back()    { webkit_web_view_go_back(view_); }
void Engine::go_forward() { webkit_web_view_go_forward(view_); }
void Engine::reload()     { webkit_web_view_reload(view_); }
void Engine::stop()       { webkit_web_view_stop_loading(view_); }

uint32_t Engine::render_to_texture() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    // Snapshot view → cairo surface → glTexSubImage2D → gl_texture_
    // Update info_ from webkit_web_view_get_title/progress/can_go_back etc.
    // ... (standard WebKit snapshot + GL upload pattern)
    return gl_texture_;
}

// inject_mouse/key: synthesize GDK events into view_
// resize: update dims, reallocate texture
// destructor: g_object_unref(view_), glDeleteTextures
// move ctor/assign: swap all members
// ... (standard resource management patterns)

} // namespace straylight::browser
```

### Task 1.3: apps/browser/tab_manager.h / tab_manager.cpp

- [ ] Multi-tab: create, close, switch; each Tab owns an Engine
- [ ] ImGui tab bar with reorder, close buttons, "+" for new tab

```cpp
// apps/browser/tab_manager.h
#pragma once
#include "engine.h"
#include <vector>
#include <memory>

namespace straylight::browser {

struct Tab {
    std::unique_ptr<Engine> engine;
    std::string title = "New Tab";
    std::string url;
};

class TabManager {
public:
    Result<void, SLError> new_tab(const std::string& url, int w, int h);
    void close_tab(size_t index);
    void switch_to(size_t index);
    size_t active_index() const { return active_; }
    Tab& active_tab() { return tabs_[active_]; }
    const std::vector<Tab>& tabs() const { return tabs_; }
    bool empty() const { return tabs_.empty(); }
    bool draw_tab_bar();  // returns true if active changed

private:
    std::vector<Tab> tabs_;
    size_t active_ = 0;
};

} // namespace straylight::browser
```

```cpp
// apps/browser/tab_manager.cpp — key logic
Result<void, SLError> TabManager::new_tab(const std::string& url, int w, int h) {
    auto engine = Engine::create(w, h);
    if (!engine) return engine.error();
    Tab tab; tab.engine = std::make_unique<Engine>(std::move(*engine));
    tabs_.push_back(std::move(tab)); active_ = tabs_.size() - 1;
    if (!url.empty()) tabs_.back().engine->navigate(url);
    return {};
}

bool TabManager::draw_tab_bar() {
    bool changed = false;
    if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_Reorderable)) {
        for (size_t i = 0; i < tabs_.size(); i++) {
            bool open = true;
            if (ImGui::BeginTabItem((tabs_[i].title + "##" + std::to_string(i)).c_str(), &open)) {
                if (i != active_) { active_ = i; changed = true; }
                ImGui::EndTabItem();
            }
            if (!open) { close_tab(i); i--; changed = true; }
        }
        if (ImGui::TabItemButton("+")) new_tab("", 1280, 720);
        ImGui::EndTabBar();
    }
    return changed;
}
// close_tab: erase + clamp active_; switch_to: bounds check
// ... (standard vector management)
```

### Task 1.4: apps/browser/downloads.h / downloads.cpp

- [ ] Download queue: pending/active/complete/failed status
- [ ] Progress bar per entry, clear completed

```cpp
// apps/browser/downloads.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <vector>

namespace straylight::browser {

enum class DownloadStatus { Pending, Active, Complete, Failed, Cancelled };
struct DownloadEntry {
    std::string url, filename, dest_path, error_msg;
    uint64_t bytes_received = 0, bytes_total = 0;
    DownloadStatus status = DownloadStatus::Pending;
};

class Downloads {
public:
    size_t add(const std::string& url, const std::string& dest_dir);
    void update_progress(size_t i, uint64_t recv, uint64_t total);
    void mark_complete(size_t i);
    void mark_failed(size_t i, const std::string& err);
    void cancel(size_t i);
    const std::vector<DownloadEntry>& entries() const { return entries_; }
    bool draw_panel();  // returns true if "clear completed" clicked
    Result<void, SLError> save_history() const;
    Result<void, SLError> load_history();
private:
    std::vector<DownloadEntry> entries_;
};
} // namespace straylight::browser
```

```cpp
// apps/browser/downloads.cpp — key logic
size_t Downloads::add(const std::string& url, const std::string& dest_dir) {
    DownloadEntry e; e.url = url;
    auto pos = url.rfind('/');
    e.filename = (pos != std::string::npos) ? url.substr(pos + 1) : "download";
    e.dest_path = (fs::path(dest_dir) / e.filename).string();
    entries_.push_back(std::move(e));
    return entries_.size() - 1;
}
// draw_panel: for each entry → Text + ProgressBar + cancel button
// save/load: JSON to ~/.config/straylight/browser-downloads.json
// ... (standard ImGui list + JSON persistence patterns)
```

### Task 1.5: apps/browser/main.cpp + CMakeLists.txt

- [ ] ImGui layout: tab bar | nav bar (back/fwd/reload/URL) | web texture | downloads
- [ ] Shortcuts: Ctrl+T new tab, Ctrl+W close, Ctrl+L focus URL

```cpp
// apps/browser/main.cpp
#include "engine.h"
#include "tab_manager.h"
#include "downloads.h"
#include <straylight/log.h>
#include <imgui.h>
using namespace straylight::browser;

int main() {
    Log::init("straylight-browser");
    TabManager tabs; Downloads downloads; downloads.load_history();
    char url_buf[2048] = "https://duckduckgo.com";
    bool show_dl = false;
    tabs.new_tab(url_buf, 1280, 720);

    // Wayland + EGL + ImGui setup (AppBase pattern per Plan 9A)
    // ... (standard init: wl_display, xdg_surface, EGL, ImGui)

    while (/*running*/ true) {
        // Poll Wayland, handle resize — standard event loop
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        auto& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T)) tabs.new_tab("", 1280, 720);
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W) && !tabs.empty()) tabs.close_tab(tabs.active_index());

        tabs.draw_tab_bar();
        if (!tabs.empty()) {
            auto& tab = tabs.active_tab();
            auto info = tab.engine->page_info();
            // Nav bar: [<] [>] [R/X] [URL input............] [Downloads]
            // Progress bar if loading
            // Web content: ImGui::Image(render_to_texture(), avail)
            // Forward mouse/key to engine when hovered
            // Downloads sidebar if show_dl
            // ... (standard ImGui layout — buttons, InputText, Image, child windows)
        }
        ImGui::End();
    }
    downloads.save_history();
    return 0;
}
```

```cmake
# apps/browser/CMakeLists.txt
add_executable(straylight-browser main.cpp engine.cpp tab_manager.cpp downloads.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)
target_include_directories(straylight-browser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib/common/include ${WEBKIT_INCLUDE_DIRS})
target_link_libraries(straylight-browser PRIVATE
    straylight-common imgui ${WEBKIT_LIBRARIES} ${WAYLAND_LIBRARIES} ${EGL_LIBRARIES} ${GLES_LIBRARIES} nlohmann_json)
target_compile_features(straylight-browser PRIVATE cxx_std_20)
install(TARGETS straylight-browser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 1.6: apps/browser/tests/browser_test.cpp

```cpp
#include <gtest/gtest.h>
#include "../tab_manager.h"
#include "../downloads.h"
using namespace straylight::browser;

TEST(Downloads, AddAndProgress) {
    Downloads dl;
    auto idx = dl.add("https://example.com/file.tar.gz", "/tmp");
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
    dl.cancel(idx);
    EXPECT_EQ(dl.entries()[0].status, DownloadStatus::Cancelled);
}
```

---

## Chunk 2: Text Editor

### Task 2.1: apps/editor/buffer.h

- [ ] Gap-buffer text storage with O(1) insert/delete at cursor
- [ ] Line index for O(1) line lookup
- [ ] Undo/redo stack (command pattern)

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

class Buffer {
public:
    Buffer();
    static Result<Buffer, SLError> from_file(const std::filesystem::path& path);
    Result<void, SLError> save(const std::filesystem::path& path) const;
    Result<void, SLError> save() const;

    void insert(Position pos, std::string_view text);
    void erase(Position start, Position end);
    std::string text() const;
    std::string_view line(int index) const;
    int line_count() const;
    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();
    bool modified() const { return modified_; }
    const std::filesystem::path& file_path() const { return path_; }

private:
    std::vector<char> data_;
    size_t gap_start_ = 0, gap_end_ = 0;
    std::vector<size_t> line_starts_;
    struct Edit { enum class Kind { Insert, Erase }; Kind kind; Position pos; std::string text; };
    std::vector<Edit> undo_stack_, redo_stack_;
    std::filesystem::path path_;
    bool modified_ = false;

    void move_gap(size_t offset);
    void rebuild_line_index();
    size_t pos_to_offset(Position pos) const;
    size_t content_size() const;
    void push_undo(Edit edit);
};

} // namespace straylight::editor
```

### Task 2.2: apps/editor/buffer.cpp

- [ ] Gap buffer: `move_gap`, grow gap on insert, record undo
- [ ] File I/O: read entire file into gap buffer; atomic write (tmp + rename)

```cpp
// apps/editor/buffer.cpp
#include "buffer.h"
#include <fstream>
#include <cstring>
namespace straylight::editor {
namespace fs = std::filesystem;
static constexpr size_t INITIAL_GAP = 4096;

Buffer::Buffer() : data_(INITIAL_GAP), gap_start_(0), gap_end_(INITIAL_GAP) {
    line_starts_.push_back(0);
}

Result<Buffer, SLError> Buffer::from_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return SLError("cannot open: " + path.string());
    auto size = static_cast<size_t>(f.tellg()); f.seekg(0);
    Buffer buf; buf.data_.resize(size + INITIAL_GAP);
    f.read(buf.data_.data() + INITIAL_GAP, static_cast<std::streamsize>(size));
    buf.gap_start_ = 0; buf.gap_end_ = INITIAL_GAP;
    buf.path_ = path; buf.rebuild_line_index();
    return buf;
}

Result<void, SLError> Buffer::save(const fs::path& path) const {
    auto tmp = path.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary);
      if (!f) return SLError("cannot write: " + tmp);
      f.write(data_.data(), static_cast<std::streamsize>(gap_start_));
      f.write(data_.data() + gap_end_, static_cast<std::streamsize>(data_.size() - gap_end_)); }
    std::error_code ec; fs::rename(tmp, path, ec);
    if (ec) return SLError("rename failed: " + ec.message());
    return {};
}

void Buffer::insert(Position pos, std::string_view text) {
    size_t offset = pos_to_offset(pos);
    move_gap(offset);
    if (text.size() > (gap_end_ - gap_start_)) {
        size_t new_gap = text.size() + INITIAL_GAP;
        data_.insert(data_.begin() + static_cast<ptrdiff_t>(gap_end_),
                     new_gap - (gap_end_ - gap_start_), '\0');
        gap_end_ = gap_start_ + new_gap;
    }
    std::memcpy(data_.data() + gap_start_, text.data(), text.size());
    gap_start_ += text.size(); modified_ = true;
    push_undo({Edit::Kind::Insert, pos, std::string(text)});
    redo_stack_.clear(); rebuild_line_index();
}

// erase: move_gap to start, expand gap_end, capture text for undo
// undo/redo: pop stack, apply inverse operation
// move_gap: memmove content across gap
// rebuild_line_index: scan for '\n' in logical content
// ... (standard gap-buffer patterns)
} // namespace straylight::editor
```

### Task 2.3: apps/editor/syntax.h / syntax.cpp

- [ ] Token-based syntax highlighting: keyword, string, comment, number, type, operator, preprocessor
- [ ] Language detection by extension (C++, Python, Rust, JS, Bash)
- [ ] Line-at-a-time tokenization for incremental updates

```cpp
// apps/editor/syntax.h
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>
namespace straylight::editor {

enum class TokenKind { Plain, Keyword, String, Comment, Number, Type, Function, Operator, Preprocessor };
struct Token { int start, length; TokenKind kind; };
enum class Language { None, Cpp, Python, Rust, JavaScript, Bash, Json, Markdown };

class Syntax {
public:
    static Language detect(const std::string& filename);
    void set_language(Language lang) { lang_ = lang; }
    std::vector<Token> highlight_line(std::string_view line) const;
    static ImU32 token_color(TokenKind kind);
private:
    Language lang_ = Language::None;
    std::vector<Token> highlight_cpp(std::string_view line) const;
    // highlight_python, highlight_rust, etc. — same signature
    static bool is_keyword(std::string_view word, Language lang);
};
} // namespace straylight::editor
```

```cpp
// apps/editor/syntax.cpp — core C++ highlighter
#include "syntax.h"
#include <unordered_set>
#include <cctype>
namespace straylight::editor {

Language Syntax::detect(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return Language::None;
    auto ext = filename.substr(dot);
    if (ext == ".cpp" || ext == ".cc" || ext == ".h") return Language::Cpp;
    if (ext == ".py") return Language::Python;
    if (ext == ".rs") return Language::Rust;
    if (ext == ".js" || ext == ".ts") return Language::JavaScript;
    if (ext == ".sh") return Language::Bash;
    return Language::None;
}

std::vector<Token> Syntax::highlight_cpp(std::string_view line) const {
    std::vector<Token> tokens;
    size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) { i++; continue; }
        // "//" → Comment to end of line
        if (i+1 < line.size() && line[i]=='/' && line[i+1]=='/') {
            tokens.push_back({int(i), int(line.size()-i), TokenKind::Comment}); break; }
        // String literal (double/single quote, skip escapes)
        if (line[i]=='"' || line[i]=='\'') {
            char q = line[i]; size_t s = i++;
            while (i < line.size() && line[i]!=q) { if (line[i]=='\\') i++; i++; }
            if (i < line.size()) i++;
            tokens.push_back({int(s), int(i-s), TokenKind::String}); continue; }
        // Number
        if (std::isdigit((unsigned char)line[i])) {
            size_t s = i;
            while (i < line.size() && std::isalnum((unsigned char)line[i])||line[i]=='.') i++;
            tokens.push_back({int(s), int(i-s), TokenKind::Number}); continue; }
        // Preprocessor
        if (line[i]=='#') {
            tokens.push_back({int(i), int(line.size()-i), TokenKind::Preprocessor}); break; }
        // Identifier → keyword / type / plain
        if (std::isalpha((unsigned char)line[i]) || line[i]=='_') {
            size_t s = i;
            while (i < line.size() && (std::isalnum((unsigned char)line[i])||line[i]=='_')) i++;
            auto w = line.substr(s, i-s);
            auto k = is_keyword(w, Language::Cpp) ? TokenKind::Keyword : TokenKind::Plain;
            tokens.push_back({int(s), int(i-s), k}); continue; }
        tokens.push_back({int(i), 1, TokenKind::Operator}); i++;
    }
    return tokens;
}

// highlight_python, highlight_rust, etc. — same structure with language-specific keywords
// is_keyword: static unordered_set per language
// token_color: cyberpunk palette (purple keywords, green strings, grey comments, etc.)
// ... (standard per-language patterns, identical structure to highlight_cpp)

ImU32 Syntax::token_color(TokenKind kind) {
    switch (kind) {
    case TokenKind::Keyword:      return IM_COL32(198,120,221,255);
    case TokenKind::String:       return IM_COL32(152,195,121,255);
    case TokenKind::Comment:      return IM_COL32(92,99,112,255);
    case TokenKind::Number:       return IM_COL32(209,154,102,255);
    case TokenKind::Type:         return IM_COL32(86,182,194,255);
    case TokenKind::Preprocessor: return IM_COL32(224,108,117,255);
    case TokenKind::Operator:     return IM_COL32(190,80,70,255);
    default:                      return IM_COL32(204,204,204,255);
    }
}
} // namespace straylight::editor
```

### Task 2.4: apps/editor/search.h / search.cpp

- [ ] Find all / find next (plain text + regex, case sensitive toggle)
- [ ] Replace / replace all (reverse order to preserve positions)

```cpp
// apps/editor/search.h
#pragma once
#include "buffer.h"
#include <vector>
#include <optional>
namespace straylight::editor {

struct Match { Position start, end; };
struct SearchOpts { bool case_sensitive = true; bool use_regex = false; };

class Search {
public:
    std::vector<Match> find_all(const Buffer& buf, const std::string& pattern, SearchOpts opts = {}) const;
    std::optional<Match> find_next(const Buffer& buf, const std::string& pattern, Position from, SearchOpts opts = {}) const;
    static void replace(Buffer& buf, const Match& m, const std::string& repl);
    static int replace_all(Buffer& buf, const std::string& pattern, const std::string& repl, SearchOpts opts = {});
};
} // namespace straylight::editor
```

```cpp
// apps/editor/search.cpp — key logic
int Search::replace_all(Buffer& buf, const std::string& pattern,
                        const std::string& repl, SearchOpts opts) {
    Search s;
    auto matches = s.find_all(buf, pattern, opts);
    for (auto it = matches.rbegin(); it != matches.rend(); ++it)
        replace(buf, *it, repl);
    return static_cast<int>(matches.size());
}
// find_all: scan buf.text() for pattern (plain or std::regex), convert byte offsets to Position
// find_next: find_all from `from`, return first match
// ... (standard text search patterns)
```

### Task 2.5: apps/editor/main.cpp + CMakeLists.txt

- [ ] ImGui layout: menu bar | find bar | gutter + syntax-highlighted text | status bar
- [ ] Shortcuts: Ctrl+S save, Ctrl+Z/Y undo/redo, Ctrl+F find

```cpp
// apps/editor/main.cpp
#include "buffer.h"
#include "syntax.h"
#include "search.h"
#include <straylight/log.h>
#include <imgui.h>
using namespace straylight::editor;

int main(int argc, char* argv[]) {
    Log::init("straylight-editor");
    Buffer buffer; Syntax syntax; Search search;
    Position cursor{0,0}; bool show_find = false;
    char find_buf[256]={}; char replace_buf[256]={};

    if (argc > 1) {
        auto r = Buffer::from_file(argv[1]);
        if (r) { buffer = std::move(*r); syntax.set_language(Syntax::detect(argv[1])); }
    }
    // Wayland + EGL + ImGui setup (AppBase pattern)

    while (true) {
        // Standard event loop
        ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar);

        // Menu bar: File (Open/Save/Quit), Edit (Undo/Redo/Find)
        // Keyboard shortcuts: Ctrl+S/Z/Y/F
        // Find bar: InputText Find + Replace + Next/ReplaceAll/Close buttons

        // Editor pane: for each visible line:
        //   1. Draw line number in gutter (grey)
        //   2. Tokenize via syntax.highlight_line(buffer.line(i))
        //   3. Draw each token span with Syntax::token_color() via AddText
        // Handle text input, arrow keys, backspace, mouse click → cursor position

        // Status bar: "Ln X, Col Y | Modified/Saved | filename"
        ImGui::End();
    }
    return 0;
}
```

```cmake
# apps/editor/CMakeLists.txt
add_executable(straylight-editor main.cpp buffer.cpp syntax.cpp search.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)
target_include_directories(straylight-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib/common/include)
target_link_libraries(straylight-editor PRIVATE
    straylight-common imgui ${WAYLAND_LIBRARIES} ${EGL_LIBRARIES} ${GLES_LIBRARIES})
target_compile_features(straylight-editor PRIVATE cxx_std_20)
install(TARGETS straylight-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 2.6: apps/editor/tests/editor_test.cpp

```cpp
#include <gtest/gtest.h>
#include "../buffer.h"
#include "../syntax.h"
#include "../search.h"
using namespace straylight::editor;

TEST(Buffer, InsertAndLine) {
    Buffer buf; buf.insert({0,0}, "hello\nworld\n");
    EXPECT_EQ(buf.line_count(), 3);
    EXPECT_EQ(buf.line(0), "hello");
    EXPECT_EQ(buf.line(1), "world");
}
TEST(Buffer, EraseAndUndo) {
    Buffer buf; buf.insert({0,0}, "abcdef");
    buf.erase({0,2}, {0,4}); EXPECT_EQ(buf.line(0), "abef");
    buf.undo(); EXPECT_EQ(buf.line(0), "abcdef");
}
TEST(Syntax, Detect) {
    EXPECT_EQ(Syntax::detect("main.cpp"), Language::Cpp);
    EXPECT_EQ(Syntax::detect("script.py"), Language::Python);
    EXPECT_EQ(Syntax::detect("unknown"), Language::None);
}
TEST(Syntax, CppTokens) {
    Syntax s; s.set_language(Language::Cpp);
    auto t = s.highlight_line("int x = 42;");
    ASSERT_GE(t.size(), 3u);
}
TEST(Search, FindAll) {
    Buffer buf; buf.insert({0,0}, "foo bar foo baz foo");
    Search s; EXPECT_EQ(s.find_all(buf, "foo").size(), 3u);
}
```

---

## Chunk 3: Media Player

### Task 3.1: apps/player/pipeline.h

- [ ] GStreamer playbin3 wrapper: play, pause, stop, seek
- [ ] Stream metadata: duration, position, title, artist
- [ ] Video frame → GL texture via appsink

```cpp
// apps/player/pipeline.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <functional>
#include <cstdint>

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

namespace straylight::player {

enum class PlayState { Stopped, Playing, Paused, Buffering };

struct MediaInfo {
    std::string title, artist, album;
    int64_t duration_ns = 0, position_ns = 0;
    int video_width = 0, video_height = 0;
    bool has_video = false, has_audio = false;
};

class Pipeline {
public:
    static Result<Pipeline, SLError> create();
    Result<void, SLError> load(const std::string& uri);
    void play();
    void pause();
    void stop();
    void toggle_play_pause();
    Result<void, SLError> seek(int64_t position_ns);
    void seek_relative(int64_t delta_ns);
    void set_volume(double vol);
    double volume() const;
    void set_muted(bool m);
    bool muted() const;
    void poll();  // drain bus messages, update state + video texture
    uint32_t video_texture() const { return video_tex_; }
    PlayState state() const { return state_; }
    MediaInfo info() const;
    ~Pipeline();
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    Pipeline(const Pipeline&) = delete;

private:
    GstElement* pipeline_ = nullptr;
    GstBus* bus_ = nullptr;
    uint32_t video_tex_ = 0;
    PlayState state_ = PlayState::Stopped;
    MediaInfo info_;
    double volume_ = 1.0;
    explicit Pipeline(GstElement*, GstBus*);
    void update_info();
    void upload_video_frame();
};

} // namespace straylight::player
```

### Task 3.2: apps/player/pipeline.cpp

- [ ] `create()` — `gst_init`, `playbin3`, appsink with RGBA videoconvert
- [ ] `load()` — set URI, transition to PAUSED for preroll, create GL texture
- [ ] `poll()` — drain bus: ERROR, EOS, TAG (title/artist/album); update position/duration
- [ ] `upload_video_frame()` — pull sample from appsink, `glTexImage2D`

```cpp
// apps/player/pipeline.cpp
#include "pipeline.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <GLES3/gl3.h>
namespace straylight::player {

Result<Pipeline, SLError> Pipeline::create() {
    gst_init(nullptr, nullptr);
    auto* p = gst_element_factory_make("playbin3", "player");
    if (!p) return SLError("failed to create playbin3");
    // Set video-sink to: videoconvert ! video/x-raw,format=RGBA ! appsink
    auto* bus = gst_element_get_bus(p);
    return Pipeline(p, bus);
}

Result<void, SLError> Pipeline::load(const std::string& uri) {
    std::string full = uri;
    if (!uri.empty() && uri[0] == '/') full = "file://" + uri;
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    g_object_set(pipeline_, "uri", full.c_str(), nullptr);
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    state_ = PlayState::Paused;
    if (!video_tex_) { glGenTextures(1, &video_tex_); /* set filter params */ }
    return {};
}

void Pipeline::play()  { gst_element_set_state(pipeline_, GST_STATE_PLAYING); state_ = PlayState::Playing; }
void Pipeline::pause() { gst_element_set_state(pipeline_, GST_STATE_PAUSED);  state_ = PlayState::Paused; }
void Pipeline::stop()  { gst_element_set_state(pipeline_, GST_STATE_NULL);    state_ = PlayState::Stopped; }
void Pipeline::toggle_play_pause() { (state_ == PlayState::Playing) ? pause() : play(); }

void Pipeline::poll() {
    GstMessage* msg;
    while ((msg = gst_bus_pop(bus_))) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: { GError* e=nullptr; gst_message_parse_error(msg,&e,nullptr);
            state_ = PlayState::Stopped; if(e) g_error_free(e); break; }
        case GST_MESSAGE_EOS: state_ = PlayState::Stopped; break;
        case GST_MESSAGE_TAG: {
            GstTagList* tags=nullptr; gst_message_parse_tag(msg,&tags);
            // Extract GST_TAG_TITLE, GST_TAG_ARTIST, GST_TAG_ALBUM → info_
            if(tags) gst_tag_list_unref(tags); break; }
        default: break;
        }
        gst_message_unref(msg);
    }
    update_info(); upload_video_frame();
}

// seek: gst_element_seek_simple with FLUSH|KEY_UNIT
// seek_relative: clamp(position + delta, 0, duration), call seek
// update_info: gst_element_query_position/duration
// upload_video_frame: gst_app_sink_try_pull_sample → gst_buffer_map → glTexSubImage2D
// destructor: set NULL state, gst_object_unref pipeline+bus, glDeleteTextures
// ... (standard GStreamer patterns)

} // namespace straylight::player
```

### Task 3.3: apps/player/playlist.h / playlist.cpp

- [ ] Ordered URI list, next/prev/shuffle/repeat modes
- [ ] Directory scanning for media extensions

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
struct PlaylistEntry { std::string uri, title, artist; int64_t duration_ns = 0; };

class Playlist {
public:
    void add(const std::string& uri);
    void add_directory(const std::filesystem::path& dir);
    void remove(size_t i);
    void clear();
    std::optional<std::string> current_uri() const;
    std::optional<std::string> next();
    std::optional<std::string> previous();
    void set_repeat(RepeatMode m) { repeat_ = m; }
    void set_shuffle(bool on);
    const std::vector<PlaylistEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    std::optional<size_t> draw_panel();  // ImGui list, returns double-clicked index
    Result<void, SLError> save(const std::filesystem::path& p) const;
    Result<void, SLError> load(const std::filesystem::path& p);
private:
    std::vector<PlaylistEntry> entries_;
    std::vector<size_t> shuffle_order_;
    size_t current_ = 0;
    RepeatMode repeat_ = RepeatMode::None;
    bool shuffle_ = false;
    static bool is_media_file(const std::filesystem::path& p);
};
} // namespace straylight::player
```

```cpp
// apps/player/playlist.cpp — key logic
void Playlist::add(const std::string& uri) {
    PlaylistEntry e; e.uri = uri;
    auto pos = uri.rfind('/');
    e.title = (pos != std::string::npos) ? uri.substr(pos+1) : uri;
    entries_.push_back(std::move(e));
}

std::optional<std::string> Playlist::next() {
    if (entries_.empty()) return std::nullopt;
    if (repeat_ == RepeatMode::One) return entries_[current_].uri;
    size_t next_idx = current_ + 1;
    if (next_idx >= entries_.size()) {
        if (repeat_ == RepeatMode::All) next_idx = 0;
        else return std::nullopt;
    }
    current_ = next_idx;
    return entries_[current_].uri;
}
// previous: mirror of next
// add_directory: scan for .mp3/.flac/.ogg/.mp4/.mkv/.webm etc.
// draw_panel: for each entry → Selectable, double-click sets current
// save/load: JSON array of {uri, title}
// ... (standard patterns)
```

### Task 3.4: apps/player/controls.h / controls.cpp

- [ ] Transport UI: play/pause, stop, prev, next buttons
- [ ] Seek slider, volume slider, time display (mm:ss)

```cpp
// apps/player/controls.h
#pragma once
#include "pipeline.h"
#include <imgui.h>
#include <string>
namespace straylight::player {

class Controls {
public:
    struct Action {
        bool play_pause=false, stop=false, next=false, previous=false;
        int64_t seek_to = -1;
        float volume_set = -1.0f;
    };
    Action draw(const Pipeline& pipeline, const MediaInfo& info);
private:
    bool seeking_ = false;
    float seek_pos_ = 0.0f;
    static std::string format_time(int64_t ns);
};
} // namespace straylight::player
```

```cpp
// apps/player/controls.cpp
#include "controls.h"
namespace straylight::player {

std::string Controls::format_time(int64_t ns) {
    int64_t sec = ns / 1'000'000'000;
    char buf[16]; snprintf(buf, sizeof(buf), "%d:%02d", int(sec/60), int(sec%60));
    return buf;
}

Controls::Action Controls::draw(const Pipeline& pipe, const MediaInfo& info) {
    Action a;
    if (ImGui::Button(pipe.state()==PlayState::Playing ? "||" : ">")) a.play_pause = true;
    ImGui::SameLine(); if (ImGui::Button("[]")) a.stop = true;
    ImGui::SameLine(); if (ImGui::Button("|<")) a.previous = true;
    ImGui::SameLine(); if (ImGui::Button(">|")) a.next = true;

    // Seek slider
    ImGui::SameLine();
    float dur = float(info.duration_ns), pos = seeking_ ? seek_pos_ : float(info.position_ns);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200);
    if (ImGui::SliderFloat("##seek", &pos, 0, dur, "")) { seeking_=true; seek_pos_=pos; }
    if (seeking_ && ImGui::IsMouseReleased(0)) { a.seek_to = int64_t(seek_pos_); seeking_=false; }

    ImGui::SameLine();
    ImGui::Text("%s / %s", format_time(info.position_ns).c_str(), format_time(info.duration_ns).c_str());

    // Volume
    ImGui::SameLine(); float vol = float(pipe.volume());
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("##vol", &vol, 0, 1, "")) a.volume_set = vol;

    // Track info line
    if (!info.title.empty()) { ImGui::Text("%s", info.title.c_str());
        if (!info.artist.empty()) { ImGui::SameLine(); ImGui::TextDisabled("- %s", info.artist.c_str()); } }
    return a;
}
} // namespace straylight::player
```

### Task 3.5: apps/player/main.cpp + CMakeLists.txt

- [ ] ImGui layout: video/album art area | transport controls | playlist sidebar
- [ ] Shortcuts: Space play/pause, Left/Right seek +/-10s, auto-advance on EOS

```cpp
// apps/player/main.cpp
#include "pipeline.h"
#include "playlist.h"
#include "controls.h"
#include <straylight/log.h>
#include <imgui.h>
using namespace straylight::player;

int main(int argc, char* argv[]) {
    Log::init("straylight-player");
    auto pipe = Pipeline::create();
    if (!pipe) return 1;
    auto pipeline = std::move(*pipe);
    Playlist playlist; Controls controls;

    for (int i = 1; i < argc; i++) {
        if (std::filesystem::is_directory(argv[i])) playlist.add_directory(argv[i]);
        else playlist.add(argv[i]);
    }
    if (!playlist.empty()) { auto u = playlist.current_uri(); if (u) pipeline.load(*u); }

    // Wayland + EGL + ImGui setup (AppBase pattern)

    while (true) {
        pipeline.poll();
        ImGui::Begin("Player", nullptr, ImGuiWindowFlags_NoDecoration);
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) pipeline.toggle_play_pause();
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  pipeline.seek_relative(-10'000'000'000LL);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) pipeline.seek_relative( 10'000'000'000LL);

        auto info = pipeline.info();
        // Video area: ImGui::Image(video_texture(), size) if has_video, else centered track info
        // Transport controls: auto action = controls.draw(pipeline, info); dispatch actions
        // Playlist sidebar: auto clicked = playlist.draw_panel(); load+play if clicked
        // Auto-advance: if Stopped && !empty → playlist.next() → load+play
        ImGui::End();
    }
    return 0;
}
```

```cmake
# apps/player/CMakeLists.txt
add_executable(straylight-player main.cpp pipeline.cpp playlist.cpp controls.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)
target_include_directories(straylight-player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib/common/include ${GST_INCLUDE_DIRS})
target_link_libraries(straylight-player PRIVATE
    straylight-common imgui ${GST_LIBRARIES} ${WAYLAND_LIBRARIES} ${EGL_LIBRARIES} ${GLES_LIBRARIES} nlohmann_json)
target_compile_features(straylight-player PRIVATE cxx_std_20)
install(TARGETS straylight-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 3.6: apps/player/tests/player_test.cpp

```cpp
#include <gtest/gtest.h>
#include "../playlist.h"
using namespace straylight::player;

TEST(Playlist, AddAndNavigate) {
    Playlist pl;
    pl.add("/a.mp3"); pl.add("/b.mp3"); pl.add("/c.mp3");
    EXPECT_EQ(pl.entries().size(), 3u);
    EXPECT_EQ(*pl.current_uri(), "/a.mp3");
    EXPECT_EQ(*pl.next(), "/b.mp3");
    EXPECT_EQ(*pl.previous(), "/a.mp3");
}
TEST(Playlist, RepeatAll) {
    Playlist pl; pl.add("/a.mp3"); pl.add("/b.mp3");
    pl.set_repeat(RepeatMode::All);
    pl.next(); // b
    EXPECT_EQ(*pl.next(), "/a.mp3"); // wrap
}
TEST(Playlist, RepeatNoneStops) {
    Playlist pl; pl.add("/a.mp3");
    pl.set_repeat(RepeatMode::None);
    EXPECT_FALSE(pl.next().has_value());
}
```

---

## Chunk 4: Image Viewer

### Task 4.1: apps/image_viewer/loader.h / loader.cpp

- [ ] stb_image loading (PNG, JPG, BMP, GIF, TGA, HDR) → RGBA
- [ ] GL texture upload with mipmaps
- [ ] Directory listing of supported images

```cpp
// apps/image_viewer/loader.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <vector>
#include <cstdint>
namespace straylight::viewer {

struct ImageData {
    uint32_t texture = 0;
    int width = 0, height = 0, channels = 0;
    std::string filename, format;
    size_t file_size = 0;
};

class Loader {
public:
    static Result<ImageData, SLError> load(const std::filesystem::path& path);
    static void unload(ImageData& img);
    static std::vector<std::filesystem::path> list_images(const std::filesystem::path& dir);
    static bool is_supported(const std::filesystem::path& path);
};
} // namespace straylight::viewer
```

```cpp
// apps/image_viewer/loader.cpp
#include "loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <unordered_set>
namespace straylight::viewer {
namespace fs = std::filesystem;

static const std::unordered_set<std::string> exts = {
    ".png",".jpg",".jpeg",".bmp",".gif",".tga",".hdr"};

bool Loader::is_supported(const fs::path& p) {
    auto e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return exts.contains(e);
}

Result<ImageData, SLError> Loader::load(const fs::path& path) {
    int w, h, ch;
    auto* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) return SLError("stbi_load: " + std::string(stbi_failure_reason()));

    uint32_t tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    std::error_code ec;
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    return ImageData{tex, w, h, ch, path.filename().string(), ext.substr(1), fs::file_size(path, ec)};
}

void Loader::unload(ImageData& img) {
    if (img.texture) { glDeleteTextures(1, &img.texture); img.texture = 0; }
}

std::vector<fs::path> Loader::list_images(const fs::path& dir) {
    std::vector<fs::path> r; std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file() && is_supported(e.path())) r.push_back(e.path());
    std::sort(r.begin(), r.end());
    return r;
}
} // namespace straylight::viewer
```

### Task 4.2: apps/image_viewer/viewer.h / viewer.cpp

- [ ] Pan/zoom/rotate viewport (scroll=zoom, drag=pan, 90-degree rotate)
- [ ] Fit modes: FitWindow, Original, Custom
- [ ] UV-based rotation for zero-copy display

```cpp
// apps/image_viewer/viewer.h
#pragma once
#include "loader.h"
#include <imgui.h>
namespace straylight::viewer {

class Viewer {
public:
    enum class FitMode { FitWindow, Original, Custom };
    void set_image(const ImageData& img);
    void reset();
    void rotate_cw();
    void rotate_ccw();
    void set_zoom(float z);
    float zoom() const { return zoom_; }
    FitMode fit_mode() const { return fit_mode_; }
    void set_fit_mode(FitMode m) { fit_mode_ = m; }
    void draw(ImVec2 available_size);
    void handle_input();
    void draw_info_overlay(const ImageData& img);
private:
    uint32_t texture_ = 0;
    int img_w_ = 0, img_h_ = 0;
    float zoom_ = 1.0f, pan_x_ = 0, pan_y_ = 0;
    int rotation_ = 0;
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
    texture_ = img.texture; img_w_ = img.width; img_h_ = img.height; reset();
}
void Viewer::reset() { zoom_=1; pan_x_=pan_y_=0; rotation_=0; fit_mode_=FitMode::FitWindow; }
void Viewer::rotate_cw()  { rotation_ = (rotation_+90)%360; }
void Viewer::rotate_ccw() { rotation_ = (rotation_+270)%360; }
void Viewer::set_zoom(float z) { zoom_ = std::clamp(z, 0.1f, 20.0f); fit_mode_ = FitMode::Custom; }

ImVec2 Viewer::compute_display_size(ImVec2 avail) const {
    float iw = float(img_w_), ih = float(img_h_);
    if (rotation_==90||rotation_==270) std::swap(iw, ih);
    switch (fit_mode_) {
    case FitMode::FitWindow: { float s = std::min(avail.x/iw, avail.y/ih); return {iw*s, ih*s}; }
    case FitMode::Original: return {iw, ih};
    case FitMode::Custom: return {iw*zoom_, ih*zoom_};
    } return {iw,ih};
}

void Viewer::draw(ImVec2 avail) {
    if (!texture_) { ImGui::TextDisabled("No image loaded"); return; }
    auto sz = compute_display_size(avail);
    float ox = (avail.x-sz.x)*0.5f+pan_x_, oy = (avail.y-sz.y)*0.5f+pan_y_;
    ImVec2 cur = ImGui::GetCursorScreenPos();
    ImVec2 p = {cur.x+ox, cur.y+oy};
    auto* dl = ImGui::GetWindowDrawList();

    // UV rotation: 0°→normal, 90°→{0,1},{0,0},{1,0},{1,1}, etc.
    ImVec2 uv[4];
    switch (rotation_) {
    case 0:   uv[0]={0,0}; uv[1]={1,0}; uv[2]={1,1}; uv[3]={0,1}; break;
    case 90:  uv[0]={0,1}; uv[1]={0,0}; uv[2]={1,0}; uv[3]={1,1}; break;
    case 180: uv[0]={1,1}; uv[1]={0,1}; uv[2]={0,0}; uv[3]={1,0}; break;
    default:  uv[0]={1,0}; uv[1]={1,1}; uv[2]={0,1}; uv[3]={0,0}; break;
    }
    auto tid = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture_));
    dl->AddImageQuad(tid, p, {p.x+sz.x,p.y}, {p.x+sz.x,p.y+sz.y}, {p.x,p.y+sz.y},
                     uv[0], uv[1], uv[2], uv[3]);
    ImGui::InvisibleButton("viewer", avail);
}

void Viewer::handle_input() {
    auto& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0)
        set_zoom(zoom_ * (1.0f + io.MouseWheel * 0.1f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        auto d = ImGui::GetMouseDragDelta(0);
        pan_x_ += d.x - (dragging_?drag_start_.x:0); pan_y_ += d.y - (dragging_?drag_start_.y:0);
        drag_start_ = d; dragging_ = true; fit_mode_ = FitMode::Custom;
    } else { dragging_ = false; drag_start_ = {}; }
}

void Viewer::draw_info_overlay(const ImageData& img) {
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::BeginChild("Info", {220,100}, true);
    ImGui::Text("%s", img.filename.c_str());
    ImGui::Text("%dx%d  %s", img.width, img.height, img.format.c_str());
    ImGui::Text("%.1f KB  Zoom: %.0f%%", float(img.file_size)/1024.f, zoom_*100);
    ImGui::EndChild();
}
} // namespace straylight::viewer
```

### Task 4.3: apps/image_viewer/main.cpp + CMakeLists.txt

- [ ] ImGui layout: toolbar | image viewport | info overlay
- [ ] Navigate directory images with arrows; zoom +/-, R rotate, F fit, 1 original

```cpp
// apps/image_viewer/main.cpp
#include "loader.h"
#include "viewer.h"
#include <straylight/log.h>
#include <imgui.h>
using namespace straylight::viewer;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    Log::init("straylight-image-viewer");
    Viewer viewer; ImageData cur_img;
    std::vector<fs::path> images; int idx = -1;
    bool show_info = true;

    auto load = [&](int i) {
        if (i<0 || i>=int(images.size())) return;
        Loader::unload(cur_img);
        auto r = Loader::load(images[i]);
        if (r) { cur_img = std::move(*r); viewer.set_image(cur_img); idx = i; }
    };

    if (argc > 1) {
        fs::path arg = argv[1];
        if (fs::is_directory(arg)) { images = Loader::list_images(arg); if (!images.empty()) load(0); }
        else if (Loader::is_supported(arg)) {
            images = Loader::list_images(arg.parent_path());
            auto it = std::find(images.begin(), images.end(), fs::canonical(arg));
            idx = (it != images.end()) ? int(it-images.begin()) : 0;
            load(idx);
        }
    }

    // Wayland + EGL + ImGui setup (AppBase pattern)

    while (true) {
        ImGui::Begin("ImageViewer", nullptr, ImGuiWindowFlags_NoDecoration);

        // Toolbar: [<] [>] [+] [-] [Fit] [1:1] [CW] [CCW] [Info] "N / Total"
        // Keyboard: Left/Right prev/next, +/- zoom, R rotate, F fit, 1 original, I info
        // ... (standard ImGui Button + IsKeyPressed dispatch)

        ImVec2 avail = ImGui::GetContentRegionAvail();
        viewer.draw(avail);
        viewer.handle_input();

        if (show_info && cur_img.texture) {
            ImGui::SetCursorPos({ImGui::GetIO().DisplaySize.x - 240, 44});
            viewer.draw_info_overlay(cur_img);
        }
        ImGui::End();
    }
    Loader::unload(cur_img);
    return 0;
}
```

```cmake
# apps/image_viewer/CMakeLists.txt
add_executable(straylight-image-viewer main.cpp loader.cpp viewer.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLES REQUIRED glesv2)
target_include_directories(straylight-image-viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib/common/include ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(straylight-image-viewer PRIVATE
    straylight-common imgui ${WAYLAND_LIBRARIES} ${EGL_LIBRARIES} ${GLES_LIBRARIES})
target_compile_features(straylight-image-viewer PRIVATE cxx_std_20)
install(TARGETS straylight-image-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 4.4: apps/image_viewer/tests/viewer_test.cpp

```cpp
#include <gtest/gtest.h>
#include "../loader.h"
#include "../viewer.h"
using namespace straylight::viewer;

TEST(Loader, SupportedExtensions) {
    EXPECT_TRUE(Loader::is_supported("photo.png"));
    EXPECT_TRUE(Loader::is_supported("image.JPG"));
    EXPECT_FALSE(Loader::is_supported("doc.pdf"));
    EXPECT_FALSE(Loader::is_supported("noext"));
}
TEST(Viewer, ZoomClamp) {
    Viewer v; ImageData img{1,100,100,4,"t","PNG",0};
    v.set_image(img);
    v.set_zoom(0.01f); EXPECT_GE(v.zoom(), 0.1f);
    v.set_zoom(100.f); EXPECT_LE(v.zoom(), 20.0f);
}
TEST(Viewer, RotationCycle) {
    Viewer v; ImageData img{1,200,100,4,"t","PNG",0};
    v.set_image(img);
    v.rotate_cw(); v.rotate_cw(); v.rotate_cw(); v.rotate_cw(); // back to 0
}
TEST(Viewer, Reset) {
    Viewer v; ImageData img{1,500,300,4,"t","PNG",0};
    v.set_image(img); v.set_zoom(3.0f); v.rotate_cw();
    v.reset();
    EXPECT_FLOAT_EQ(v.zoom(), 1.0f);
    EXPECT_EQ(v.fit_mode(), Viewer::FitMode::FitWindow);
}
```

---

## Summary

| Chunk | Scope | Key Files | Tasks |
|-------|-------|-----------|-------|
| 1 | Web Browser | `engine.h/cpp`, `tab_manager.h/cpp`, `downloads.h/cpp`, `main.cpp` | 1.1–1.6 |
| 2 | Text Editor | `buffer.h/cpp`, `syntax.h/cpp`, `search.h/cpp`, `main.cpp` | 2.1–2.6 |
| 3 | Media Player | `pipeline.h/cpp`, `playlist.h/cpp`, `controls.h/cpp`, `main.cpp` | 3.1–3.6 |
| 4 | Image Viewer | `loader.h/cpp`, `viewer.h/cpp`, `main.cpp` | 4.1–4.4 |

### External Dependencies

| Dependency | Version | Used By |
|-----------|---------|---------|
| WebKitGTK | 6.0+ | Browser |
| GStreamer | 1.24+ | Player |
| stb_image | latest | Image Viewer |
| nlohmann/json | 3.11+ | Browser, Player |
| Dear ImGui | 1.90+ | All |
| wayland-client | 1.22+ | All |
| EGL + GLESv2 | 3.0 | All |
| GTest | 1.14+ | All |
