# Plan 9A: Desktop Apps — straylight-terminal & straylight-files

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement two core desktop applications as ImGui Wayland clients: a full terminal emulator with real PTY/VT100 support, and a file manager with real filesystem operations.

**Architecture:** Both apps use `wl_egl_window + EGL + ImGui` (same pattern as `shell::Renderer`). No GLFW. Each app is a standalone binary under `apps/`.

**Tech Stack:** C++20, ImGui 1.90+, EGL/OpenGL ES 3.0, Wayland client, libstraylight-common (Result<T,E>, SLError)

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor/Wayland), Plan 4 (shell renderer pattern)

**Development environment:** Linux x86_64 required. Uses POSIX PTY APIs, `/dev/ptmx`, Wayland protocol, Linux VFS.

---

## File Structure

```
apps/terminal/
├── CMakeLists.txt
├── main.cpp
├── pty.h / pty.cpp
├── vte.h / vte.cpp
├── renderer.h / renderer.cpp
└── config.h / config.cpp

apps/file_manager/
├── CMakeLists.txt
├── main.cpp
├── browser.h / browser.cpp
├── operations.h / operations.cpp
├── preview.h / preview.cpp
└── bookmarks.h / bookmarks.cpp
```

---

## Chunk 1: Terminal — PTY Management & Configuration

### Task 1.1: apps/terminal/pty.h

- [ ] PTY master/slave pair via `posix_openpt` + `forkpty`
- [ ] Non-blocking read from master fd
- [ ] Write input to master fd
- [ ] Child process lifecycle (waitpid, SIGCHLD)

```cpp
// apps/terminal/pty.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <functional>

namespace straylight::terminal {

struct PtySize { uint16_t rows = 24; uint16_t cols = 80; };

class Pty {
public:
    /// Fork a child shell and return a Pty connected to it.
    static Result<Pty, SLError> spawn(const std::string& shell_path,
                                       PtySize size);

    /// Non-blocking read from PTY master. Returns bytes read (may be empty).
    Result<std::string, SLError> read();

    /// Write user input to PTY master.
    Result<void, SLError> write(std::string_view data);

    /// Resize the PTY (sends SIGWINCH to child).
    Result<void, SLError> resize(PtySize size);

    /// Check if child process is still alive.
    bool alive() const;

    int master_fd() const { return master_fd_; }

    ~Pty();
    Pty(Pty&& other) noexcept;
    Pty& operator=(Pty&& other) noexcept;
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

private:
    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    explicit Pty(int master_fd, pid_t child_pid);
};

} // namespace straylight::terminal
```

### Task 1.2: apps/terminal/pty.cpp

- [ ] `spawn()` — `forkpty()`, set `TERM=xterm-256color`, `execvp` shell
- [ ] `read()` — `poll()` + `::read()` with `O_NONBLOCK`
- [ ] `write()` — `::write()` in loop until all bytes sent
- [ ] `resize()` — `ioctl(TIOCSWINSZ)` + `kill(SIGWINCH)`
- [ ] `alive()` — `waitpid(WNOHANG)`, cache exit status
- [ ] Destructor — kill child, close master fd

```cpp
// apps/terminal/pty.cpp — key implementation
#include "pty.h"
#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <cstring>

namespace straylight::terminal {

Result<Pty, SLError> Pty::spawn(const std::string& shell_path, PtySize size) {
    struct winsize ws{};
    ws.ws_row = size.rows;
    ws.ws_col = size.cols;

    int master_fd;
    pid_t pid = forkpty(&master_fd, nullptr, nullptr, &ws);
    if (pid < 0)
        return SLError::from_errno("forkpty");
    if (pid == 0) {
        // Child: set TERM, exec shell
        setenv("TERM", "xterm-256color", 1);
        const char* sh = shell_path.c_str();
        execlp(sh, sh, nullptr);
        _exit(127);
    }
    // Parent: set non-blocking
    fcntl(master_fd, F_SETFL, fcntl(master_fd, F_GETFL) | O_NONBLOCK);
    return Pty(master_fd, pid);
}

Result<std::string, SLError> Pty::read() {
    struct pollfd pfd{master_fd_, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0) return std::string{};

    char buf[4096];
    ssize_t n = ::read(master_fd_, buf, sizeof(buf));
    if (n < 0 && errno == EAGAIN) return std::string{};
    if (n < 0) return SLError::from_errno("pty read");
    return std::string(buf, n);
}

// write(), resize(), alive(), destructor — standard POSIX patterns
// ... (standard pattern: write loop, ioctl TIOCSWINSZ, waitpid WNOHANG)

} // namespace straylight::terminal
```

### Task 1.3: apps/terminal/config.h / config.cpp

- [ ] Terminal configuration: font, colors, scrollback, shell path
- [ ] Load/save from JSON (`~/.config/straylight/terminal.json`)

```cpp
// apps/terminal/config.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <array>

namespace straylight::terminal {

struct Color { uint8_t r, g, b, a; };

struct TermConfig {
    std::string font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
    float font_size = 14.0f;
    std::string shell_path = "/bin/bash";
    uint32_t scrollback_lines = 10000;
    Color fg = {204, 204, 204, 255};
    Color bg = {30, 30, 30, 255};
    std::array<Color, 256> palette;  // xterm-256color palette

    static Result<TermConfig, SLError> load();
    Result<void, SLError> save() const;
};

} // namespace straylight::terminal
```

```cpp
// apps/terminal/config.cpp — key logic
// Load from ~/.config/straylight/terminal.json via nlohmann/json.
// Populate palette with standard xterm-256color defaults.
// save() writes JSON atomically (write tmp + rename).
// ... (standard JSON load/save pattern per Plan 1 conventions)
```

---

## Chunk 2: Terminal — VTE Parser & Renderer

### Task 2.1: apps/terminal/vte.h / vte.cpp

- [ ] VT100/xterm escape sequence state machine
- [ ] Cell grid with character, fg/bg color, attributes (bold, underline, inverse)
- [ ] Scrollback ring buffer
- [ ] Cursor position tracking

```cpp
// apps/terminal/vte.h
#pragma once
#include "config.h"
#include <string>
#include <vector>
#include <cstdint>

namespace straylight::terminal {

enum class Attr : uint8_t {
    None      = 0,
    Bold      = 1 << 0,
    Underline = 1 << 1,
    Inverse   = 1 << 2,
    Blink     = 1 << 3,
};

struct Cell {
    char32_t codepoint = U' ';
    uint8_t fg_index = 7;    // palette index or RGB
    uint8_t bg_index = 0;
    Attr attrs = Attr::None;
};

struct CursorState {
    int row = 0, col = 0;
    bool visible = true;
};

class Vte {
public:
    explicit Vte(uint16_t rows, uint16_t cols, uint32_t scrollback_max);

    /// Feed raw bytes from PTY into the parser.
    void feed(std::string_view data);

    /// Resize the terminal grid (reflows content).
    void resize(uint16_t rows, uint16_t cols);

    const Cell& cell_at(int row, int col) const;
    CursorState cursor() const { return cursor_; }
    uint16_t rows() const { return rows_; }
    uint16_t cols() const { return cols_; }
    int scroll_offset() const { return scroll_offset_; }
    void scroll(int delta);

private:
    uint16_t rows_, cols_;
    std::vector<Cell> grid_;           // rows_ * cols_ active cells
    std::vector<std::vector<Cell>> scrollback_;
    uint32_t scrollback_max_;
    int scroll_offset_ = 0;
    CursorState cursor_;

    // Parser state machine
    enum class State { Ground, Escape, CSI, OSC };
    State state_ = State::Ground;
    std::string csi_buf_;

    void emit_char(char32_t ch);
    void parse_csi(std::string_view seq);
    void newline();
    void scroll_up();
    // ... (additional escape handlers: set_color, set_attr, erase_line, etc.)
};

} // namespace straylight::terminal
```

### Task 2.2: apps/terminal/vte.cpp

- [ ] `feed()` — byte-by-byte state machine: Ground → Escape → CSI/OSC
- [ ] CSI dispatch: cursor movement (A/B/C/D/H), erase (J/K), SGR colors (m)
- [ ] SGR parser: 16-color, 256-color (`38;5;N`), true color (`38;2;R;G;B`)
- [ ] `scroll_up()` — push top row to scrollback ring buffer

```cpp
// apps/terminal/vte.cpp — core parser loop
void Vte::feed(std::string_view data) {
    for (char c : data) {
        switch (state_) {
        case State::Ground:
            if (c == '\x1b')       { state_ = State::Escape; }
            else if (c == '\n')    { newline(); }
            else if (c == '\r')    { cursor_.col = 0; }
            else if (c == '\b')    { if (cursor_.col > 0) cursor_.col--; }
            else if (c == '\t')    { cursor_.col = (cursor_.col + 8) & ~7; }
            else if (c >= 0x20)    { emit_char(static_cast<char32_t>(c)); }
            break;

        case State::Escape:
            if (c == '[')          { state_ = State::CSI; csi_buf_.clear(); }
            else if (c == ']')     { state_ = State::OSC; csi_buf_.clear(); }
            else                   { state_ = State::Ground; } // unsupported
            break;

        case State::CSI:
            if (c >= 0x40 && c <= 0x7e) {
                parse_csi(csi_buf_);  // final byte determines command
                state_ = State::Ground;
            } else {
                csi_buf_ += c;
            }
            break;

        case State::OSC:
            if (c == '\x07' || c == '\x1b') { state_ = State::Ground; }
            break;
        }
    }
}

void Vte::parse_csi(std::string_view seq) {
    // Parse semicolon-separated numeric params, dispatch on final byte:
    // 'A' = cursor up, 'B' = down, 'C' = forward, 'D' = back,
    // 'H' = cursor position, 'J' = erase display, 'K' = erase line,
    // 'm' = SGR (colors/attributes)
    // ... (standard CSI dispatch table)
}
```

### Task 2.3: apps/terminal/renderer.h / renderer.cpp

- [ ] ImGui-based terminal grid renderer (monospaced font, cell grid)
- [ ] Cursor blink (500ms toggle)
- [ ] Selection highlighting (click-drag, copy to clipboard)
- [ ] Scrollbar mapped to scrollback

```cpp
// apps/terminal/renderer.h
#pragma once
#include "vte.h"
#include "config.h"
#include <imgui.h>

namespace straylight::terminal {

class TermRenderer {
public:
    explicit TermRenderer(const TermConfig& config);

    /// Render the terminal grid into the current ImGui window.
    /// Call between Renderer::begin_frame() and end_frame().
    void draw(Vte& vte, float dt);

    /// Handle keyboard input, return bytes to send to PTY.
    std::string handle_input();

    void update_config(const TermConfig& config);

private:
    TermConfig config_;
    ImFont* mono_font_ = nullptr;
    float cell_w_ = 0, cell_h_ = 0;
    float blink_timer_ = 0;
    bool cursor_visible_ = true;

    ImU32 palette_to_color(uint8_t index) const;
    void draw_cell(ImDrawList* dl, const Cell& cell,
                   float x, float y) const;
};

} // namespace straylight::terminal
```

```cpp
// apps/terminal/renderer.cpp — core draw loop
void TermRenderer::draw(Vte& vte, float dt) {
    blink_timer_ += dt;
    if (blink_timer_ > 0.5f) { cursor_visible_ = !cursor_visible_; blink_timer_ = 0; }

    ImGui::PushFont(mono_font_);
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    for (int r = 0; r < vte.rows(); r++) {
        for (int c = 0; c < vte.cols(); c++) {
            const auto& cell = vte.cell_at(r, c);
            float x = origin.x + c * cell_w_;
            float y = origin.y + r * cell_h_;
            draw_cell(dl, cell, x, y);
        }
    }
    // Draw cursor block
    auto cur = vte.cursor();
    if (cur.visible && cursor_visible_) {
        float cx = origin.x + cur.col * cell_w_;
        float cy = origin.y + cur.row * cell_h_;
        dl->AddRectFilled({cx, cy}, {cx + cell_w_, cy + cell_h_},
                          IM_COL32(200, 200, 200, 180));
    }
    ImGui::PopFont();

    // Scrollbar: map mouse wheel to vte.scroll()
    // ... (standard ImGui scroll handling)
}
```

### Task 2.4: apps/terminal/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (wl_display, xdg_surface, wl_egl_window)
- [ ] EGL + ImGui renderer (same pattern as shell::Renderer)
- [ ] Event loop: poll Wayland fd + PTY fd, feed/draw/present

```cpp
// apps/terminal/main.cpp — application entry
#include "pty.h"
#include "vte.h"
#include "renderer.h"
#include "config.h"
#include <straylight/result.h>
#include <wayland-client.h>

using namespace straylight::terminal;

int main() {
    auto config = TermConfig::load().value_or(TermConfig{});
    PtySize size{24, 80};

    auto pty = Pty::spawn(config.shell_path, size);
    if (!pty) { fprintf(stderr, "PTY: %s\n", pty.error().message().c_str()); return 1; }

    Vte vte(size.rows, size.cols, config.scrollback_lines);
    TermRenderer term_renderer(config);

    // Wayland + EGL + ImGui setup (same pattern as shell::Renderer::create)
    // ... (wl_display_connect, xdg_surface, wl_egl_window, EGL init, ImGui init)

    while (pty->alive()) {
        // Poll both Wayland fd and PTY master fd
        // ... (poll with 16ms timeout for ~60fps)

        // Read PTY output → feed VTE
        auto data = pty->read();
        if (data && !data->empty()) vte.feed(*data);

        renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Terminal", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        term_renderer.draw(vte, ImGui::GetIO().DeltaTime);

        // Keyboard → PTY
        auto input = term_renderer.handle_input();
        if (!input.empty()) pty->write(input);

        ImGui::End();
        renderer.end_frame();
    }
    return 0;
}
```

```cmake
# apps/terminal/CMakeLists.txt
add_executable(straylight-terminal
    main.cpp pty.cpp vte.cpp renderer.cpp config.cpp)
target_link_libraries(straylight-terminal PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2 util)
target_compile_features(straylight-terminal PRIVATE cxx_std_20)
install(TARGETS straylight-terminal RUNTIME DESTINATION bin)
```

---

## Chunk 3: File Manager — Browser & Operations

### Task 3.1: apps/file_manager/browser.h / browser.cpp

- [ ] Directory listing via `std::filesystem::directory_iterator`
- [ ] Sort by name, size, date, type
- [ ] Filter by extension or name pattern
- [ ] Track current path, navigation history (back/forward)

```cpp
// apps/file_manager/browser.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <vector>
#include <string>

namespace straylight::files {
namespace fs = std::filesystem;

enum class SortField { Name, Size, Date, Type };
enum class SortOrder { Asc, Desc };

struct DirEntry {
    fs::path path;
    std::string name;
    bool is_dir;
    uintmax_t size;
    fs::file_time_type mtime;
    fs::perms permissions;
};

class Browser {
public:
    explicit Browser(fs::path initial = fs::path(getenv("HOME")));

    Result<void, SLError> navigate(const fs::path& dir);
    void go_back();
    void go_forward();
    void go_up();

    void set_sort(SortField field, SortOrder order);
    void set_filter(const std::string& pattern);
    Result<void, SLError> refresh();

    const fs::path& current_path() const { return current_; }
    const std::vector<DirEntry>& entries() const { return entries_; }
    bool can_go_back() const { return !history_back_.empty(); }
    bool can_go_forward() const { return !history_fwd_.empty(); }

private:
    fs::path current_;
    std::vector<DirEntry> entries_;
    std::vector<fs::path> history_back_, history_fwd_;
    SortField sort_field_ = SortField::Name;
    SortOrder sort_order_ = SortOrder::Asc;
    std::string filter_;

    void apply_sort();
    void apply_filter();
};

} // namespace straylight::files
```

```cpp
// apps/file_manager/browser.cpp — key logic
Result<void, SLError> Browser::navigate(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return SLError("not a directory: " + dir.string());

    history_back_.push_back(current_);
    history_fwd_.clear();
    current_ = fs::canonical(dir, ec);
    return refresh();
}

Result<void, SLError> Browser::refresh() {
    entries_.clear();
    std::error_code ec;
    for (auto& it : fs::directory_iterator(current_, ec)) {
        auto st = it.status(ec);
        entries_.push_back({
            .path = it.path(),
            .name = it.path().filename().string(),
            .is_dir = fs::is_directory(st),
            .size = fs::is_regular_file(st) ? fs::file_size(it.path(), ec) : 0,
            .mtime = fs::last_write_time(it.path(), ec),
            .permissions = st.permissions(),
        });
    }
    apply_filter();
    apply_sort();
    return {};
}

// go_back/go_forward: swap between history stacks
// apply_sort: std::sort on entries_ by sort_field_
// apply_filter: std::erase_if entries not matching glob
// ... (standard pattern)
```

### Task 3.2: apps/file_manager/operations.h / operations.cpp

- [ ] Async file operations: copy, move, delete, rename, mkdir
- [ ] Progress callback for large copies
- [ ] Error accumulation for batch operations

```cpp
// apps/file_manager/operations.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <functional>
#include <future>

namespace straylight::files {
namespace fs = std::filesystem;

using ProgressFn = std::function<void(uintmax_t bytes_done, uintmax_t bytes_total)>;

struct OpResult {
    bool success;
    std::vector<SLError> errors;  // per-file errors for batch ops
};

class Operations {
public:
    /// Copy file or directory recursively. Reports progress for large files.
    static std::future<Result<OpResult, SLError>>
    copy(const fs::path& src, const fs::path& dst, ProgressFn progress = {});

    /// Move (rename if same filesystem, copy+delete otherwise).
    static Result<void, SLError> move(const fs::path& src, const fs::path& dst);

    /// Delete file or directory recursively.
    static Result<void, SLError> remove(const fs::path& target);

    /// Rename in place.
    static Result<void, SLError> rename(const fs::path& old_path,
                                         const std::string& new_name);

    /// Create directory (including parents).
    static Result<void, SLError> mkdir(const fs::path& dir);
};

} // namespace straylight::files
```

```cpp
// apps/file_manager/operations.cpp — key logic
Result<void, SLError> Operations::remove(const fs::path& target) {
    std::error_code ec;
    fs::remove_all(target, ec);
    if (ec) return SLError("remove failed: " + ec.message());
    return {};
}

Result<void, SLError> Operations::mkdir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return SLError("mkdir failed: " + ec.message());
    return {};
}

std::future<Result<OpResult, SLError>>
Operations::copy(const fs::path& src, const fs::path& dst, ProgressFn progress) {
    return std::async(std::launch::async, [=]() -> Result<OpResult, SLError> {
        OpResult result{true, {}};
        std::error_code ec;
        if (fs::is_directory(src)) {
            for (auto& entry : fs::recursive_directory_iterator(src, ec)) {
                auto rel = fs::relative(entry.path(), src, ec);
                auto target = dst / rel;
                if (entry.is_directory()) fs::create_directories(target, ec);
                else fs::copy_file(entry.path(), target,
                                   fs::copy_options::overwrite_existing, ec);
                if (ec) result.errors.emplace_back(ec.message());
            }
        } else {
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) return SLError("copy failed: " + ec.message());
        }
        // Progress reporting via sendfile/splice for large files
        // ... (standard pattern)
        return result;
    });
}

// move: try fs::rename first, fall back to copy+delete
// rename: fs::rename(old_path, old_path.parent_path() / new_name)
// ... (standard pattern)
```

---

## Chunk 4: File Manager — Preview, Bookmarks & Main

### Task 4.1: apps/file_manager/preview.h / preview.cpp

- [ ] Text file preview (first N lines)
- [ ] Image thumbnail (load via stb_image, display as ImGui texture)
- [ ] File metadata display (size, permissions, dates)

```cpp
// apps/file_manager/preview.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <string>
#include <imgui.h>

namespace straylight::files {

struct PreviewData {
    enum class Kind { None, Text, Image, Metadata };
    Kind kind = Kind::None;
    std::string text_content;          // for text preview
    ImTextureID texture = nullptr;     // for image preview
    int img_w = 0, img_h = 0;
    std::string metadata_summary;      // size, perms, dates
};

class Preview {
public:
    /// Generate preview for the given path.
    Result<PreviewData, SLError> generate(const std::filesystem::path& path);

    /// Render preview in current ImGui window region.
    void draw(const PreviewData& data);

    /// Free GPU texture if loaded.
    void clear();

private:
    ImTextureID current_texture_ = nullptr;
    Result<std::string, SLError> read_text_head(const std::filesystem::path& p,
                                                 size_t max_lines = 50);
    Result<ImTextureID, SLError> load_image_texture(const std::filesystem::path& p,
                                                     int& w, int& h);
};

} // namespace straylight::files
```

```cpp
// apps/file_manager/preview.cpp — key logic
Result<PreviewData, SLError> Preview::generate(const fs::path& path) {
    PreviewData data;
    std::error_code ec;
    auto st = fs::status(path, ec);

    // Metadata always available
    data.metadata_summary = fmt::format("Size: {}  Perms: {:o}  Modified: ...",
        fs::file_size(path, ec), static_cast<int>(st.permissions()));

    auto ext = path.extension().string();
    if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
        data.kind = PreviewData::Kind::Image;
        auto tex = load_image_texture(path, data.img_w, data.img_h);
        if (tex) data.texture = *tex;
    } else if (fs::is_regular_file(st) && fs::file_size(path, ec) < 1'000'000) {
        data.kind = PreviewData::Kind::Text;
        auto text = read_text_head(path);
        if (text) data.text_content = *text;
    } else {
        data.kind = PreviewData::Kind::Metadata;
    }
    return data;
}

// draw(): switch on kind, render text with ImGui::TextWrapped or image with ImGui::Image
// load_image_texture(): stb_image_load + glTexImage2D → ImTextureID
// ... (standard pattern)
```

### Task 4.2: apps/file_manager/bookmarks.h / bookmarks.cpp

- [ ] Sidebar bookmarks: Home, Documents, Downloads, Desktop, custom
- [ ] Persist bookmarks to JSON
- [ ] Add/remove favorites

```cpp
// apps/file_manager/bookmarks.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <vector>
#include <string>

namespace straylight::files {

struct Bookmark {
    std::string label;
    std::filesystem::path path;
    bool removable = true;  // system bookmarks are not removable
};

class Bookmarks {
public:
    Bookmarks();

    const std::vector<Bookmark>& items() const { return items_; }

    Result<void, SLError> add(const std::string& label,
                               const std::filesystem::path& path);
    Result<void, SLError> remove(size_t index);

    Result<void, SLError> load();  // from ~/.config/straylight/files-bookmarks.json
    Result<void, SLError> save() const;

    /// Draw sidebar with ImGui. Returns path if user clicked a bookmark.
    std::optional<std::filesystem::path> draw_sidebar();

private:
    std::vector<Bookmark> items_;
    void populate_defaults();
};

} // namespace straylight::files
```

```cpp
// apps/file_manager/bookmarks.cpp — key logic
void Bookmarks::populate_defaults() {
    const char* home = getenv("HOME");
    items_ = {
        {"Home",      home,                            false},
        {"Documents", std::string(home) + "/Documents", false},
        {"Downloads", std::string(home) + "/Downloads", false},
        {"Desktop",   std::string(home) + "/Desktop",   false},
    };
}

std::optional<fs::path> Bookmarks::draw_sidebar() {
    std::optional<fs::path> clicked;
    ImGui::BeginChild("Bookmarks", {180, 0}, true);
    for (size_t i = 0; i < items_.size(); i++) {
        if (ImGui::Selectable(items_[i].label.c_str()))
            clicked = items_[i].path;
    }
    ImGui::EndChild();
    return clicked;
}

// load/save: standard JSON read/write pattern
// add/remove: push_back/erase with bounds check
// ... (standard pattern)
```

### Task 4.3: apps/file_manager/main.cpp + CMakeLists.txt

- [ ] Wayland client setup (same pattern as terminal)
- [ ] ImGui layout: sidebar bookmarks | file browser grid | preview pane
- [ ] Event loop: poll Wayland fd, handle input, render

```cpp
// apps/file_manager/main.cpp
#include "browser.h"
#include "operations.h"
#include "preview.h"
#include "bookmarks.h"
#include <straylight/result.h>
#include <imgui.h>
#include <wayland-client.h>

using namespace straylight::files;

int main() {
    Browser browser;
    Bookmarks bookmarks;
    Preview preview;
    bookmarks.load();
    PreviewData preview_data;
    int selected_idx = -1;

    // Wayland + EGL + ImGui setup (same pattern as shell::Renderer::create)
    // ... (wl_display_connect, xdg_surface, wl_egl_window, EGL init, ImGui init)

    bool running = true;
    while (running) {
        // Poll Wayland events
        // ... (wl_display_dispatch_pending, 16ms frame target)

        renderer.begin_frame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Files", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        // Left: bookmarks sidebar
        auto nav = bookmarks.draw_sidebar();
        if (nav) browser.navigate(*nav);

        ImGui::SameLine();

        // Center: file browser
        ImGui::BeginChild("Browser", {ImGui::GetContentRegionAvail().x * 0.6f, 0});
        // Path bar
        ImGui::Text("%s", browser.current_path().c_str());
        ImGui::Separator();

        for (int i = 0; i < (int)browser.entries().size(); i++) {
            auto& e = browser.entries()[i];
            auto label = (e.is_dir ? "[D] " : "    ") + e.name;
            if (ImGui::Selectable(label.c_str(), i == selected_idx,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_idx = i;
                preview_data = preview.generate(e.path).value_or(PreviewData{});
                if (ImGui::IsMouseDoubleClicked(0) && e.is_dir)
                    browser.navigate(e.path);
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // Right: preview pane
        ImGui::BeginChild("Preview", {0, 0});
        preview.draw(preview_data);
        ImGui::EndChild();

        ImGui::End();
        renderer.end_frame();
    }
    return 0;
}
```

```cmake
# apps/file_manager/CMakeLists.txt
add_executable(straylight-files
    main.cpp browser.cpp operations.cpp preview.cpp bookmarks.cpp)
target_link_libraries(straylight-files PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2
    stb_image nlohmann_json fmt)
target_compile_features(straylight-files PRIVATE cxx_std_20)
install(TARGETS straylight-files RUNTIME DESTINATION bin)
```

---

## Summary

| Chunk | Scope | Key Files | Tasks |
|-------|-------|-----------|-------|
| 1 | Terminal PTY + Config | `pty.h/cpp`, `config.h/cpp` | 1.1–1.3 |
| 2 | Terminal VTE + Renderer + Main | `vte.h/cpp`, `renderer.h/cpp`, `main.cpp` | 2.1–2.4 |
| 3 | File Manager Browser + Ops | `browser.h/cpp`, `operations.h/cpp` | 3.1–3.2 |
| 4 | File Manager Preview + Bookmarks + Main | `preview.h/cpp`, `bookmarks.h/cpp`, `main.cpp` | 4.1–4.3 |
