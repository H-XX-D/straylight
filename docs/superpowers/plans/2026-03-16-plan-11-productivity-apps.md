# Plan 11: Desktop Apps — Browser, Editor, Media Player & Image Viewer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan.

**Goal:** Four productivity apps as standalone ImGui Wayland clients under `apps/`.

**Architecture:** `wl_egl_window + EGL + ImGui` (Plan 9A AppBase pattern). C++20, `Result<T,E>::ok/error`.

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor), Plan 9A (AppBase)

---

## File Structure

```
apps/browser/    — CMakeLists.txt main.cpp engine.h/.cpp tab_manager.h/.cpp downloads.h/.cpp tests/
apps/editor/     — CMakeLists.txt main.cpp buffer.h/.cpp syntax.h/.cpp search.h/.cpp tests/
apps/player/     — CMakeLists.txt main.cpp pipeline.h/.cpp playlist.h/.cpp controls.h/.cpp tests/
apps/image_viewer/ — CMakeLists.txt main.cpp loader.h/.cpp viewer.h/.cpp tests/
```

---

## Chunk 1: Web Browser

### Task 1.1: engine.h — WebKit embedding

- [ ] Off-screen WebKitGTK → GL texture
- [ ] Navigate, back, forward, reload, stop, eval JS
- [ ] Inject mouse/keyboard, extract PageInfo (title, progress, TLS)

```cpp
// apps/browser/engine.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <cstdint>
typedef struct _WebKitWebView WebKitWebView;

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
    static Result<Engine, SLError> create(int w, int h);
    Result<void, SLError> navigate(const std::string& url);
    void go_back();
    void go_forward();
    void reload();
    void stop();
    void resize(int w, int h);
    uint32_t render_to_texture();  // pump WebKit, snapshot → GL, return tex
    void inject_mouse(float x, float y, bool lmb, bool rmb, float scroll);
    void inject_key(uint32_t keycode, uint32_t mods, bool pressed);
    PageInfo page_info() const;
    ~Engine(); Engine(Engine&&) noexcept; Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
private:
    WebKitWebView* view_ = nullptr;
    uint32_t gl_texture_ = 0;
    int width_, height_;
    PageInfo info_;
    explicit Engine(WebKitWebView*, uint32_t, int, int);
};
} // namespace straylight::browser
```

### Task 1.2: engine.cpp

- [ ] `create()` — `gtk_init_check`, `webkit_web_view_new_with_settings`, GL texture alloc
- [ ] `navigate()` — normalize URL (auto `https://`, search fallback → DuckDuckGo)
- [ ] `render_to_texture()` — `g_main_context_iteration` loop, snapshot → cairo → `glTexSubImage2D`

```cpp
// apps/browser/engine.cpp — key implementation
static std::string normalize_url(const std::string& in) {
    if (in.find("://") != std::string::npos) return in;
    if (in.find('.') != std::string::npos) return "https://" + in;
    return "https://duckduckgo.com/?q=" + in;
}

Result<Engine, SLError> Engine::create(int w, int h) {
    if (!gtk_init_check(nullptr, nullptr)) return SLError("gtk_init failed");
    auto* s = webkit_settings_new_with_settings("enable-javascript", TRUE, nullptr);
    auto* v = WEBKIT_WEB_VIEW(webkit_web_view_new_with_settings(s));
    uint32_t tex = 0; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    return Engine(v, tex, w, h);
}

Result<void, SLError> Engine::navigate(const std::string& url) {
    auto n = normalize_url(url);
    webkit_web_view_load_uri(view_, n.c_str());
    info_.url = n; info_.is_loading = true; return {};
}

uint32_t Engine::render_to_texture() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    // webkit snapshot → cairo surface → glTexSubImage2D(gl_texture_)
    // update info_ from webkit_web_view_get_title/progress/can_go_back
    return gl_texture_;
}
// go_back/forward/reload/stop: single webkit_web_view_* call each
// inject_mouse/key: synthesize GDK events
// dtor: g_object_unref + glDeleteTextures; move: swap members
```

### Task 1.3: tab_manager.h/.cpp

- [ ] Each `Tab` owns a `unique_ptr<Engine>`; `TabManager` holds vector
- [ ] ImGui tab bar: reorder, close (X), new (+)

```cpp
// apps/browser/tab_manager.h
#pragma once
#include "engine.h"
#include <vector>
#include <memory>
namespace straylight::browser {
struct Tab { std::unique_ptr<Engine> engine; std::string title, url; };
class TabManager {
public:
    Result<void, SLError> new_tab(const std::string& url, int w, int h);
    void close_tab(size_t i);
    void switch_to(size_t i);
    size_t active_index() const { return active_; }
    Tab& active_tab() { return tabs_[active_]; }
    bool empty() const { return tabs_.empty(); }
    bool draw_tab_bar();
private:
    std::vector<Tab> tabs_; size_t active_ = 0;
};
} // namespace straylight::browser
```

```cpp
// tab_manager.cpp — draw_tab_bar is the only non-trivial method
bool TabManager::draw_tab_bar() {
    bool changed = false;
    if (ImGui::BeginTabBar("Tabs", ImGuiTabBarFlags_Reorderable)) {
        for (size_t i = 0; i < tabs_.size(); i++) {
            bool open = true;
            if (ImGui::BeginTabItem((tabs_[i].title+"##"+std::to_string(i)).c_str(), &open)) {
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
```

### Task 1.4: downloads.h/.cpp

- [ ] Download queue with status enum, progress, cancel
- [ ] JSON persistence to `~/.config/straylight/browser-downloads.json`

```cpp
// apps/browser/downloads.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <vector>
namespace straylight::browser {
enum class DlStatus { Pending, Active, Complete, Failed, Cancelled };
struct DlEntry { std::string url, filename, dest; uint64_t recv=0, total=0; DlStatus status=DlStatus::Pending; };
class Downloads {
public:
    size_t add(const std::string& url, const std::string& dest_dir);
    void update_progress(size_t i, uint64_t r, uint64_t t);
    void mark_complete(size_t i);
    void cancel(size_t i);
    bool draw_panel();
    const std::vector<DlEntry>& entries() const { return e_; }
private:
    std::vector<DlEntry> e_;
};
} // namespace straylight::browser
```

### Task 1.5: main.cpp + CMakeLists.txt

- [ ] Layout: tab bar | nav (back/fwd/reload/URL) | web texture | optional download sidebar
- [ ] Shortcuts: Ctrl+T new, Ctrl+W close, Ctrl+L focus URL

```cpp
// apps/browser/main.cpp — abbreviated, follows Plan 9A AppBase loop pattern
// Wayland+EGL+ImGui init, then:
//   tabs.draw_tab_bar();
//   Nav bar: Button("<") Button(">") Button("R") InputText("##url") Button("DL")
//   ImGui::Image(tab.engine->render_to_texture(), avail)
//   Forward mouse/key to engine when hovered
//   Optional download sidebar via downloads.draw_panel()
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

### Task 1.6: tests/browser_test.cpp

```cpp
TEST(Downloads, AddAndProgress) {
    Downloads dl; auto i = dl.add("https://example.com/file.tar.gz", "/tmp");
    EXPECT_EQ(dl.entries()[0].filename, "file.tar.gz");
    dl.update_progress(i, 500, 1000); dl.mark_complete(i);
    EXPECT_EQ(dl.entries()[0].status, DlStatus::Complete);
}
TEST(Downloads, Cancel) {
    Downloads dl; auto i = dl.add("https://example.com/x.iso", "/tmp");
    dl.cancel(i); EXPECT_EQ(dl.entries()[0].status, DlStatus::Cancelled);
}
```

---

## Chunk 2: Text Editor

### Task 2.1: buffer.h — Gap buffer with undo

- [ ] Gap-buffer storage, O(1) insert/delete at cursor
- [ ] Line index, undo/redo stacks, atomic file save

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
struct Position { int line=0; int col=0; };

class Buffer {
public:
    Buffer();
    static Result<Buffer, SLError> from_file(const std::filesystem::path& p);
    Result<void, SLError> save(const std::filesystem::path& p) const;
    Result<void, SLError> save() const;
    void insert(Position pos, std::string_view text);
    void erase(Position start, Position end);
    std::string text() const;
    std::string_view line(int i) const;
    int line_count() const;
    void undo(); void redo();
    bool can_undo() const; bool can_redo() const;
    bool modified() const { return modified_; }
    const std::filesystem::path& file_path() const { return path_; }
private:
    std::vector<char> data_; size_t gap_start_=0, gap_end_=0;
    std::vector<size_t> line_starts_;
    struct Edit { enum Kind{Ins,Del}; Kind kind; Position pos; std::string text; };
    std::vector<Edit> undo_, redo_;
    std::filesystem::path path_; bool modified_=false;
    void move_gap(size_t off); void rebuild_lines(); size_t pos_to_off(Position) const;
};
} // namespace straylight::editor
```

### Task 2.2: buffer.cpp

```cpp
// apps/editor/buffer.cpp — key methods
Result<Buffer, SLError> Buffer::from_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary|std::ios::ate);
    if (!f) return SLError("cannot open: " + p.string());
    auto sz = size_t(f.tellg()); f.seekg(0);
    Buffer b; b.data_.resize(sz + 4096);
    f.read(b.data_.data() + 4096, std::streamsize(sz));
    b.gap_start_ = 0; b.gap_end_ = 4096; b.path_ = p; b.rebuild_lines();
    return b;
}
Result<void, SLError> Buffer::save(const fs::path& p) const {
    auto tmp = p.string() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary);
      f.write(data_.data(), std::streamsize(gap_start_));
      f.write(data_.data()+gap_end_, std::streamsize(data_.size()-gap_end_)); }
    std::error_code ec; fs::rename(tmp, p, ec);
    if (ec) return SLError(ec.message()); return {};
}
void Buffer::insert(Position pos, std::string_view text) {
    auto off = pos_to_off(pos); move_gap(off);
    // Grow gap if needed, memcpy text into gap, advance gap_start_
    // Push undo record, clear redo, rebuild_lines
}
// erase: move_gap, expand gap_end, capture text for undo
// undo/redo: pop stack, apply inverse
// move_gap: memmove across gap boundary
```

### Task 2.3: syntax.h/.cpp — Token highlighting

- [ ] Language detection by extension, line-at-a-time tokenizer
- [ ] Token kinds: Keyword, String, Comment, Number, Type, Operator, Preprocessor

```cpp
// apps/editor/syntax.h
#pragma once
#include <string_view>
#include <vector>
#include <imgui.h>
namespace straylight::editor {
enum class TokenKind { Plain, Keyword, String, Comment, Number, Type, Operator, Preprocessor };
struct Token { int start, length; TokenKind kind; };
enum class Language { None, Cpp, Python, Rust, JavaScript, Bash };

class Syntax {
public:
    static Language detect(const std::string& filename);
    void set_language(Language l) { lang_=l; }
    std::vector<Token> highlight_line(std::string_view line) const;
    static ImU32 token_color(TokenKind k);
private:
    Language lang_ = Language::None;
    std::vector<Token> hl_cpp(std::string_view) const;
    // hl_python, hl_rust — identical structure, different keyword sets
};
} // namespace straylight::editor
```

```cpp
// syntax.cpp — C++ highlighter (other languages follow same pattern)
std::vector<Token> Syntax::hl_cpp(std::string_view line) const {
    std::vector<Token> toks; size_t i = 0;
    while (i < line.size()) {
        if (std::isspace((unsigned char)line[i])) { i++; continue; }
        if (i+1<line.size() && line[i]=='/' && line[i+1]=='/') {
            toks.push_back({int(i), int(line.size()-i), TokenKind::Comment}); break; }
        if (line[i]=='"'||line[i]=='\'') { /* scan string literal, skip escapes */ }
        if (std::isdigit((unsigned char)line[i])) { /* scan number */ }
        if (line[i]=='#') { toks.push_back({int(i),int(line.size()-i),TokenKind::Preprocessor}); break; }
        if (std::isalpha((unsigned char)line[i])||line[i]=='_') { /* scan ident, check keyword set */ }
        // else: single-char Operator
        // Each branch: push Token, advance i — standard lexer pattern
    }
    return toks;
}
ImU32 Syntax::token_color(TokenKind k) {
    switch(k) {
    case TokenKind::Keyword: return IM_COL32(198,120,221,255);
    case TokenKind::String:  return IM_COL32(152,195,121,255);
    case TokenKind::Comment: return IM_COL32(92,99,112,255);
    case TokenKind::Number:  return IM_COL32(209,154,102,255);
    case TokenKind::Type:    return IM_COL32(86,182,194,255);
    default:                 return IM_COL32(204,204,204,255);
    }
}
```

### Task 2.4: search.h/.cpp — Find/replace

```cpp
// apps/editor/search.h
#pragma once
#include "buffer.h"
#include <optional>
namespace straylight::editor {
struct Match { Position start, end; };
struct SearchOpts { bool case_sensitive=true; bool regex=false; };
class Search {
public:
    std::vector<Match> find_all(const Buffer&, const std::string& pat, SearchOpts={}) const;
    std::optional<Match> find_next(const Buffer&, const std::string& pat, Position from, SearchOpts={}) const;
    static void replace(Buffer&, const Match&, const std::string& repl);
    static int replace_all(Buffer&, const std::string& pat, const std::string& repl, SearchOpts={});
};
} // namespace straylight::editor
```

### Task 2.5: main.cpp + CMakeLists.txt

- [ ] Layout: menu bar | find bar | gutter + syntax-colored text | status bar
- [ ] Per visible line: draw line number, tokenize, draw colored spans via `AddText`
- [ ] Shortcuts: Ctrl+S/Z/Y/F

```cmake
add_executable(straylight-editor main.cpp buffer.cpp syntax.cpp search.cpp)
target_link_libraries(straylight-editor PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2)
target_compile_features(straylight-editor PRIVATE cxx_std_20)
install(TARGETS straylight-editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 2.6: tests/editor_test.cpp

```cpp
TEST(Buffer, InsertAndLine) {
    Buffer b; b.insert({0,0}, "hello\nworld\n");
    EXPECT_EQ(b.line_count(), 3); EXPECT_EQ(b.line(0), "hello");
}
TEST(Buffer, EraseUndo) {
    Buffer b; b.insert({0,0}, "abcdef");
    b.erase({0,2},{0,4}); EXPECT_EQ(b.line(0), "abef");
    b.undo(); EXPECT_EQ(b.line(0), "abcdef");
}
TEST(Syntax, Detect) {
    EXPECT_EQ(Syntax::detect("main.cpp"), Language::Cpp);
    EXPECT_EQ(Syntax::detect("x.py"), Language::Python);
}
TEST(Search, FindAll) {
    Buffer b; b.insert({0,0}, "foo bar foo");
    EXPECT_EQ(Search().find_all(b, "foo").size(), 2u);
}
```

---

## Chunk 3: Media Player

### Task 3.1: pipeline.h — GStreamer playbin3

- [ ] Play, pause, stop, seek, volume/mute
- [ ] Tag extraction (title, artist, album)
- [ ] Video frame → GL texture via appsink

```cpp
// apps/player/pipeline.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <cstdint>
typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

namespace straylight::player {
enum class PlayState { Stopped, Playing, Paused };
struct MediaInfo {
    std::string title, artist, album;
    int64_t duration_ns=0, position_ns=0;
    bool has_video=false;
};

class Pipeline {
public:
    static Result<Pipeline, SLError> create();
    Result<void, SLError> load(const std::string& uri);
    void play(); void pause(); void stop(); void toggle();
    Result<void, SLError> seek(int64_t ns);
    void seek_relative(int64_t delta);
    void set_volume(double v); double volume() const;
    void poll();  // drain bus, update state + video
    uint32_t video_texture() const { return vtex_; }
    PlayState state() const { return state_; }
    MediaInfo info() const;
    ~Pipeline(); Pipeline(Pipeline&&) noexcept; Pipeline(const Pipeline&)=delete;
private:
    GstElement* pipe_=nullptr; GstBus* bus_=nullptr;
    uint32_t vtex_=0; PlayState state_=PlayState::Stopped; MediaInfo info_;
    explicit Pipeline(GstElement*, GstBus*);
};
} // namespace straylight::player
```

### Task 3.2: pipeline.cpp

```cpp
// apps/player/pipeline.cpp — key methods
Result<Pipeline, SLError> Pipeline::create() {
    gst_init(nullptr, nullptr);
    auto* p = gst_element_factory_make("playbin3", "player");
    if (!p) return SLError("no playbin3");
    // Set video-sink to videoconvert ! appsink (RGBA)
    return Pipeline(p, gst_element_get_bus(p));
}
Result<void, SLError> Pipeline::load(const std::string& uri) {
    std::string u = (uri[0]=='/') ? "file://"+uri : uri;
    gst_element_set_state(pipe_, GST_STATE_NULL);
    g_object_set(pipe_, "uri", u.c_str(), nullptr);
    gst_element_set_state(pipe_, GST_STATE_PAUSED);
    state_ = PlayState::Paused;
    if (!vtex_) { glGenTextures(1, &vtex_); /* params */ }
    return {};
}
void Pipeline::poll() {
    GstMessage* m;
    while ((m = gst_bus_pop(bus_))) {
        switch (GST_MESSAGE_TYPE(m)) {
        case GST_MESSAGE_EOS: state_ = PlayState::Stopped; break;
        case GST_MESSAGE_TAG: /* extract title/artist/album → info_ */ break;
        case GST_MESSAGE_ERROR: state_ = PlayState::Stopped; break;
        default: break;
        }
        gst_message_unref(m);
    }
    // query position/duration → info_
    // pull appsink sample → glTexSubImage2D(vtex_)
}
// play/pause/stop: set GST_STATE_*, update state_
// seek: gst_element_seek_simple FLUSH|KEY_UNIT
// dtor: set NULL, gst_object_unref, glDeleteTextures
```

### Task 3.3: playlist.h/.cpp

- [ ] URI list with next/prev, repeat (None/One/All), shuffle
- [ ] Directory scan for media extensions (.mp3/.flac/.ogg/.mp4/.mkv/.webm)

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
struct PlEntry { std::string uri, title; };
class Playlist {
public:
    void add(const std::string& uri);
    void add_directory(const std::filesystem::path& dir);
    std::optional<std::string> current_uri() const;
    std::optional<std::string> next();
    std::optional<std::string> previous();
    void set_repeat(RepeatMode m) { rep_=m; }
    void set_shuffle(bool on);
    bool empty() const { return e_.empty(); }
    std::optional<size_t> draw_panel(); // ImGui list, returns double-click idx
private:
    std::vector<PlEntry> e_; size_t cur_=0;
    RepeatMode rep_=RepeatMode::None; bool shuf_=false;
};
} // namespace straylight::player
```

### Task 3.4: controls.h/.cpp — Transport UI

```cpp
// apps/player/controls.h
#pragma once
#include "pipeline.h"
#include <imgui.h>
namespace straylight::player {
class Controls {
public:
    struct Action { bool play_pause=0,stop=0,next=0,prev=0; int64_t seek=-1; float vol=-1; };
    Action draw(const Pipeline&, const MediaInfo&);
private:
    bool seeking_=false; float spos_=0;
    static std::string fmt_time(int64_t ns);
};
} // namespace straylight::player
```

```cpp
// controls.cpp
Controls::Action Controls::draw(const Pipeline& p, const MediaInfo& i) {
    Action a;
    if (ImGui::Button(p.state()==PlayState::Playing?"||":">")) a.play_pause=true;
    ImGui::SameLine(); if (ImGui::Button("[]")) a.stop=true;
    ImGui::SameLine(); if (ImGui::Button("|<")) a.prev=true;
    ImGui::SameLine(); if (ImGui::Button(">|")) a.next=true;
    // Seek slider + time labels + volume slider — standard ImGui SliderFloat pattern
    return a;
}
```

### Task 3.5: main.cpp + CMakeLists.txt

- [ ] Layout: video/art area | controls | playlist sidebar
- [ ] Shortcuts: Space toggle, Left/Right +/-10s, auto-advance on EOS

```cmake
add_executable(straylight-player main.cpp pipeline.cpp playlist.cpp controls.cpp)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
target_link_libraries(straylight-player PRIVATE
    straylight-common imgui ${GST_LIBRARIES} wayland-client wayland-egl EGL GLESv2 nlohmann_json)
target_compile_features(straylight-player PRIVATE cxx_std_20)
install(TARGETS straylight-player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 3.6: tests/player_test.cpp

```cpp
TEST(Playlist, Navigate) {
    Playlist pl; pl.add("/a.mp3"); pl.add("/b.mp3");
    EXPECT_EQ(*pl.current_uri(), "/a.mp3");
    EXPECT_EQ(*pl.next(), "/b.mp3");
    EXPECT_EQ(*pl.previous(), "/a.mp3");
}
TEST(Playlist, RepeatAll) {
    Playlist pl; pl.add("/a.mp3"); pl.add("/b.mp3");
    pl.set_repeat(RepeatMode::All);
    pl.next(); EXPECT_EQ(*pl.next(), "/a.mp3"); // wrap
}
TEST(Playlist, StopsAtEnd) {
    Playlist pl; pl.add("/a.mp3");
    EXPECT_FALSE(pl.next().has_value());
}
```

---

## Chunk 4: Image Viewer

### Task 4.1: loader.h/.cpp — stb_image + GL

- [ ] Decode PNG/JPG/BMP/GIF/TGA/HDR via `stbi_load`, upload RGBA to GL with mipmaps
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
    uint32_t texture=0; int width=0, height=0, channels=0;
    std::string filename, format; size_t file_size=0;
};
class Loader {
public:
    static Result<ImageData, SLError> load(const std::filesystem::path&);
    static void unload(ImageData&);
    static std::vector<std::filesystem::path> list_images(const std::filesystem::path& dir);
    static bool is_supported(const std::filesystem::path&);
};
} // namespace straylight::viewer
```

```cpp
// apps/image_viewer/loader.cpp
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <GLES3/gl3.h>

Result<ImageData, SLError> Loader::load(const fs::path& p) {
    int w, h, ch;
    auto* px = stbi_load(p.c_str(), &w, &h, &ch, 4);
    if (!px) return SLError(std::string("stbi: ") + stbi_failure_reason());
    uint32_t tex=0; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(px);
    std::error_code ec;
    auto ext = p.extension().string(); // uppercase for display
    return ImageData{tex, w, h, ch, p.filename().string(), ext.substr(1), fs::file_size(p,ec)};
}
// unload: glDeleteTextures; list_images: filter dir by is_supported; is_supported: check extension set
```

### Task 4.2: viewer.h/.cpp — Pan/zoom/rotate viewport

- [ ] FitWindow / Original / Custom zoom (clamped 0.1–20x)
- [ ] UV-based 90-degree rotation, mouse scroll zoom, drag pan

```cpp
// apps/image_viewer/viewer.h
#pragma once
#include "loader.h"
#include <imgui.h>
namespace straylight::viewer {
class Viewer {
public:
    enum class Fit { Window, Original, Custom };
    void set_image(const ImageData&);
    void reset();
    void rotate_cw();
    void rotate_ccw();
    void set_zoom(float z);
    float zoom() const { return zoom_; }
    Fit fit_mode() const { return fit_; }
    void set_fit_mode(Fit f) { fit_=f; }
    void draw(ImVec2 avail);
    void handle_input();
    void draw_info(const ImageData&);
private:
    uint32_t tex_=0; int iw_=0, ih_=0;
    float zoom_=1, px_=0, py_=0; int rot_=0;
    Fit fit_=Fit::Window; bool drag_=false; ImVec2 ds_{};
    ImVec2 display_size(ImVec2 avail) const;
};
} // namespace straylight::viewer
```

```cpp
// apps/image_viewer/viewer.cpp — key methods
void Viewer::set_image(const ImageData& img) { tex_=img.texture; iw_=img.width; ih_=img.height; reset(); }
void Viewer::reset() { zoom_=1; px_=py_=0; rot_=0; fit_=Fit::Window; }
void Viewer::rotate_cw() { rot_=(rot_+90)%360; }
void Viewer::set_zoom(float z) { zoom_=std::clamp(z,0.1f,20.f); fit_=Fit::Custom; }

void Viewer::draw(ImVec2 avail) {
    if (!tex_) { ImGui::TextDisabled("No image"); return; }
    auto sz = display_size(avail);
    float ox=(avail.x-sz.x)*.5f+px_, oy=(avail.y-sz.y)*.5f+py_;
    ImVec2 p = ImGui::GetCursorScreenPos(); p.x+=ox; p.y+=oy;
    auto* dl = ImGui::GetWindowDrawList();
    // UV rotation: 0→{0,0}{1,0}{1,1}{0,1}, 90→{0,1}{0,0}{1,0}{1,1}, etc.
    ImVec2 uv[4]; /* set per rotation_ */
    auto tid = reinterpret_cast<ImTextureID>(uintptr_t(tex_));
    dl->AddImageQuad(tid, p, {p.x+sz.x,p.y}, {p.x+sz.x,p.y+sz.y}, {p.x,p.y+sz.y},
                     uv[0], uv[1], uv[2], uv[3]);
    ImGui::InvisibleButton("vw", avail);
}

void Viewer::handle_input() {
    auto& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && io.MouseWheel!=0) set_zoom(zoom_*(1+io.MouseWheel*.1f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        auto d=ImGui::GetMouseDragDelta(0);
        px_+=d.x-(drag_?ds_.x:0); py_+=d.y-(drag_?ds_.y:0);
        ds_=d; drag_=true; fit_=Fit::Custom;
    } else { drag_=false; ds_={}; }
}

void Viewer::draw_info(const ImageData& img) {
    ImGui::SetNextWindowBgAlpha(0.6f); ImGui::BeginChild("I",{220,80},true);
    ImGui::Text("%s  %dx%d  %s", img.filename.c_str(), img.width, img.height, img.format.c_str());
    ImGui::Text("%.1fKB  Zoom:%.0f%%", float(img.file_size)/1024.f, zoom_*100);
    ImGui::EndChild();
}
```

### Task 4.3: main.cpp + CMakeLists.txt

- [ ] Layout: toolbar (prev/next/+/-/fit/1:1/rotate/info) | viewport | info overlay
- [ ] Navigate directory with Left/Right; zoom +/-, R rotate, F fit, I info

```cpp
// apps/image_viewer/main.cpp — abbreviated
// Load image list from argv (directory or single file → scan parent dir)
// auto load = [&](int i) { Loader::unload(cur); cur = *Loader::load(images[i]); viewer.set_image(cur); };
// Toolbar: [<] [>] [+] [-] [Fit] [1:1] [CW] [CCW] [Info] "N/Total"
// Keyboard: Left/Right=prev/next, +/-=zoom, R=rotate, F=fit, 1=original, I=info
// viewer.draw(avail); viewer.handle_input();
// if (show_info) viewer.draw_info(cur);
```

```cmake
add_executable(straylight-image-viewer main.cpp loader.cpp viewer.cpp)
target_include_directories(straylight-image-viewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib/common/include ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(straylight-image-viewer PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2)
target_compile_features(straylight-image-viewer PRIVATE cxx_std_20)
install(TARGETS straylight-image-viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
```

### Task 4.4: tests/viewer_test.cpp

```cpp
TEST(Loader, Extensions) {
    EXPECT_TRUE(Loader::is_supported("x.png"));
    EXPECT_TRUE(Loader::is_supported("x.JPG"));
    EXPECT_FALSE(Loader::is_supported("x.pdf"));
}
TEST(Viewer, ZoomClamp) {
    Viewer v; v.set_image({1,100,100,4,"t","PNG",0});
    v.set_zoom(0.01f); EXPECT_GE(v.zoom(), 0.1f);
    v.set_zoom(100.f); EXPECT_LE(v.zoom(), 20.f);
}
TEST(Viewer, Reset) {
    Viewer v; v.set_image({1,500,300,4,"t","PNG",0});
    v.set_zoom(3); v.rotate_cw(); v.reset();
    EXPECT_FLOAT_EQ(v.zoom(), 1.f);
    EXPECT_EQ(v.fit_mode(), Viewer::Fit::Window);
}
```

---

## Summary

| Chunk | App | Key Files | Tasks |
|-------|-----|-----------|-------|
| 1 | Browser (WebKitGTK) | engine, tab_manager, downloads | 1.1–1.6 |
| 2 | Editor (gap buffer + syntax) | buffer, syntax, search | 2.1–2.6 |
| 3 | Player (GStreamer) | pipeline, playlist, controls | 3.1–3.6 |
| 4 | Image Viewer (stb_image) | loader, viewer | 4.1–4.4 |

**External deps:** WebKitGTK 6.0+, GStreamer 1.24+, stb_image, nlohmann/json 3.11+, ImGui 1.90+, wayland-client 1.22+, EGL/GLESv2, GTest 1.14+
