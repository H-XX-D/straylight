# Plan 13: ML/HPC Widget Panel — The StrayLight Differentiator

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement 20 ImGui widget panels that make StrayLight OS the first desktop OS purpose-built for ML/HPC practitioners. Each widget renders real data from `/proc`, `/sys`, `nvidia-smi`, subsystem daemons, or filesystem state. Widgets can be embedded in the shell dock or run as standalone windows.

**Architecture:** All widgets live under `apps/widgets/` organized by domain. Every widget implements a common `WidgetBase` interface with `render()`, `update()`, `name()`. Widgets communicate with subsystem daemons (straylight-core, straylight-agent, straylight-bus) via Unix socket JSON IPC using the same framing protocol as the compositor. Data that does not require a daemon is read directly from procfs/sysfs.

**Tech Stack:** C++20, Dear ImGui 1.90+, nlohmann/json 3.11+, POSIX (Linux x86_64)

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common), Plan 2 (core daemons — bus, registry), Plan 3 (compositor — for embedded mode), Plan 4 (shell — dock integration)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Reads `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/sys/class/thermal/`, `/sys/devices/system/cpu/`, `nvidia-smi`. macOS cannot build.

---

## Common Widget Base Pattern

Defined here; all 20 widgets inherit from this. Lives in `lib/common/include/straylight/widget.h`.

### WidgetBase Interface

```cpp
// lib/common/include/straylight/widget.h
#pragma once
#include <straylight/common.h>
#include <straylight/ipc_client.h>
#include "imgui.h"
#include <string>
#include <chrono>

namespace straylight {

class WidgetBase {
public:
    virtual ~WidgetBase() = default;

    // Human-readable name for window title and dock label
    virtual const char* name() const = 0;

    // Called once per frame at the configured poll interval.
    // Reads /proc, /sys, nvidia-smi, or sends IPC requests.
    // Must NOT block for more than 5ms; use background threads for slow I/O.
    virtual void update() = 0;

    // Called every frame. Renders the ImGui panel content.
    // Receives p_open to allow the user to close the widget.
    virtual void render(bool* p_open) = 0;

    // Default poll interval in seconds (widgets override as needed)
    virtual float poll_interval() const { return 1.0f; }

    // Whether this widget supports embedding in a dock panel
    virtual bool supports_embed() const { return true; }

protected:
    // Shared IPC client for daemon communication
    IpcClient ipc_;

    // Poll timer
    std::chrono::steady_clock::time_point last_update_{};

    bool should_update() {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_update_).count();
        if (dt >= poll_interval()) {
            last_update_ = now;
            return true;
        }
        return false;
    }
};

} // namespace straylight
```

### IPC Client (shared by all widgets)

```cpp
// lib/common/include/straylight/ipc_client.h
#pragma once
#include <straylight/common.h>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace straylight {

class IpcClient {
public:
    // Connect to a subsystem daemon's Unix socket
    Result<void, SLError> connect(const std::string& socket_path) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd_ < 0)
            return Result<void, SLError>::error(
                SLError{SLErrorCode::IpcFailed, "socket() failed"});

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
            ::close(fd_);
            fd_ = -1;
            return Result<void, SLError>::error(
                SLError{SLErrorCode::IpcFailed, "connect() failed: " + socket_path});
        }
        connected_ = true;
        return Result<void, SLError>::ok({});
    }

    // Send JSON request, receive JSON response (non-blocking with timeout)
    Result<nlohmann::json, SLError> request(const nlohmann::json& req) {
        if (!connected_)
            return Result<nlohmann::json, SLError>::error(
                SLError{SLErrorCode::IpcFailed, "not connected"});

        std::string payload = req.dump() + "\n";
        ssize_t written = ::write(fd_, payload.data(), payload.size());
        if (written < 0)
            return Result<nlohmann::json, SLError>::error(
                SLError{SLErrorCode::IpcFailed, "write failed"});

        // Read response with 50ms timeout via poll()
        struct pollfd pfd{fd_, POLLIN, 0};
        if (::poll(&pfd, 1, 50) <= 0)
            return Result<nlohmann::json, SLError>::error(
                SLError{SLErrorCode::Timeout, "IPC timeout"});

        char buf[8192];
        ssize_t n = ::read(fd_, buf, sizeof(buf) - 1);
        if (n <= 0)
            return Result<nlohmann::json, SLError>::error(
                SLError{SLErrorCode::IpcFailed, "read failed"});
        buf[n] = '\0';

        try {
            return Result<nlohmann::json, SLError>::ok(nlohmann::json::parse(buf));
        } catch (const nlohmann::json::exception& e) {
            return Result<nlohmann::json, SLError>::error(
                SLError{SLErrorCode::ParseError, e.what()});
        }
    }

    void disconnect() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        connected_ = false;
    }

    bool is_connected() const { return connected_; }

    ~IpcClient() { disconnect(); }

private:
    int fd_ = -1;
    bool connected_ = false;
};

} // namespace straylight
```

### File Structure (per widget)

```
apps/widgets/<domain>/
├── CMakeLists.txt
├── <name>_widget.h
└── <name>_widget.cpp
```

### CMakeLists.txt template (all widgets)

```cmake
# apps/widgets/<domain>/CMakeLists.txt
add_library(widget-<name> STATIC <name>_widget.cpp)
target_link_libraries(widget-<name> PRIVATE straylight-common imgui)
target_include_directories(widget-<name> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

### Shared Color Palette

All ML/HPC widgets share a consistent neon-ice aesthetic:

```cpp
// lib/common/include/straylight/widget_colors.h
#pragma once
#include "imgui.h"

namespace straylight::colors {
    constexpr ImVec4 Cyan       = {0.0f,  0.941f, 1.0f,  1.0f};
    constexpr ImVec4 Ice        = {0.55f, 0.8f,   0.95f, 1.0f};
    constexpr ImVec4 Frost      = {0.35f, 0.55f,  0.8f,  1.0f};
    constexpr ImVec4 Neon       = {0.0f,  1.0f,   0.5f,  1.0f};
    constexpr ImVec4 Amber      = {1.0f,  0.757f, 0.027f,1.0f};
    constexpr ImVec4 Hot        = {1.0f,  0.25f,  0.15f, 1.0f};
    constexpr ImVec4 Warm       = {1.0f,  0.7f,   0.2f,  1.0f};
    constexpr ImVec4 Green      = {0.18f, 0.80f,  0.44f, 1.0f};
    constexpr ImVec4 Red        = {0.93f, 0.23f,  0.23f, 1.0f};
    constexpr ImVec4 Dim        = {0.35f, 0.38f,  0.48f, 1.0f};
    constexpr ImVec4 Ghost      = {0.2f,  0.22f,  0.3f,  1.0f};
    constexpr ImVec4 Pink       = {0.886f,0.169f, 0.886f,1.0f};
    constexpr ImVec4 Purple     = {0.7f,  0.3f,   0.9f,  1.0f};
    constexpr ImVec4 White      = {1.0f,  1.0f,   1.0f,  1.0f};
    constexpr ImVec4 WindowBg   = {0.03f, 0.04f,  0.08f, 0.97f};

    inline ImVec4 util_color(float pct) {
        if (pct > 90) return Hot;
        if (pct > 70) return Warm;
        if (pct > 40) return Neon;
        return Ice;
    }

    inline ImVec4 temp_color(float c) {
        if (c > 85) return Hot;
        if (c > 65) return Warm;
        return Ice;
    }
}
```

### Shared Draw Helpers

```cpp
// lib/common/include/straylight/widget_draw.h
#pragma once
#include "imgui.h"
#include <straylight/widget_colors.h>
#include <deque>
#include <cstdio>

namespace straylight::draw {

// Horizontal utilization bar with label
inline void util_bar(const char* label, float pct, ImVec4 color, float width = -1) {
    if (width < 0) width = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = 16.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h),
                      IM_COL32(20, 25, 40, 255), 4.0f);
    float fw = width * (pct / 100.0f);
    if (fw > 0)
        dl->AddRectFilled(p, ImVec2(p.x + fw, p.y + h),
                          ImGui::ColorConvertFloat4ToU32(color), 4.0f);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s  %.0f%%", label, pct);
    dl->AddText(ImVec2(p.x + 6, p.y + 1), IM_COL32(220, 230, 255, 255), buf);
    ImGui::Dummy(ImVec2(width, h + 2));
}

// Sparkline (rolling time-series)
inline void sparkline(const std::deque<float>& data, ImVec4 color,
                      float width, float height, float max_val = 100.0f) {
    if (data.empty()) return;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height),
                      IM_COL32(15, 18, 30, 200), 3.0f);

    int n = static_cast<int>(data.size());
    float step = width / static_cast<float>(n > 1 ? n - 1 : 1);
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
    for (int i = 1; i < n; i++) {
        float x0 = p.x + (i - 1) * step;
        float y0 = p.y + height - (data[i - 1] / max_val * height);
        float x1 = p.x + i * step;
        float y1 = p.y + height - (data[i] / max_val * height);
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, 1.5f);
    }
    ImGui::Dummy(ImVec2(width, height + 2));
}

// Status dot (green/yellow/red)
inline void status_dot(bool ok) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ok ? IM_COL32(46, 204, 113, 255) : IM_COL32(237, 59, 59, 255);
    dl->AddCircleFilled(ImVec2(p.x + 6, p.y + 8), 5.0f, col);
    ImGui::Dummy(ImVec2(14, 16));
}

// Human-readable byte formatting
inline std::string human_bytes(uint64_t bytes) {
    if (bytes >= (1ULL << 30)) {
        uint64_t gb  = bytes >> 30;
        uint64_t rem = ((bytes % (1ULL << 30)) * 10) >> 30;
        return std::to_string(gb) + "." + std::to_string(rem) + " GB";
    }
    if (bytes >= (1ULL << 20)) return std::to_string(bytes >> 20) + " MB";
    if (bytes >= (1ULL << 10)) return std::to_string(bytes >> 10) + " KB";
    return std::to_string(bytes) + " B";
}

} // namespace straylight::draw
```

---

## Chunk 1: alice_widget + model_serve (AI Core)

`apps/widgets/ai/` — Alice AI assistant chat panel and model serving dashboard. These two form the core user-facing AI interface.

### File Structure

```
apps/widgets/ai/
├── CMakeLists.txt
├── alice_widget.h
├── alice_widget.cpp
├── model_serve_widget.h
└── model_serve_widget.cpp
```

### Task 1: Failing tests for alice_widget

**File:** `tests/unit/widgets/test_alice_widget.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "alice_widget.h"

TEST(AliceWidget, NameReturnsExpected) {
    straylight::AliceWidget w;
    EXPECT_STREQ(w.name(), "ALICE");
}

TEST(AliceWidget, SanitizeRejectsMetachars) {
    straylight::AliceWidget w;
    EXPECT_FALSE(w.sanitize_input("hello; rm -rf /").has_value());
    EXPECT_FALSE(w.sanitize_input("test | cat").has_value());
    EXPECT_FALSE(w.sanitize_input("x$(cmd)").has_value());
    EXPECT_FALSE(w.sanitize_input("a`b`c").has_value());
}

TEST(AliceWidget, SanitizeAcceptsClean) {
    straylight::AliceWidget w;
    auto r = w.sanitize_input("what is the weather");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "what is the weather");
}

TEST(AliceWidget, HistoryTracksMessages) {
    straylight::AliceWidget w;
    w.add_message("user", "hello");
    w.add_message("alice", "hi there");
    EXPECT_EQ(w.history_size(), 2u);
}
```

Run: `cmake --build build --target test_alice_widget && ctest --test-dir build -R test_alice_widget` -> expect 4 failures.

---

### Task 2: Implement alice_widget

**Files:** `apps/widgets/ai/alice_widget.h`, `apps/widgets/ai/alice_widget.cpp`

- [ ] **Step 1: Header**

```cpp
// alice_widget.h
#pragma once
#include <straylight/widget.h>
#include <straylight/widget_colors.h>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace straylight {

struct ChatMessage {
    std::string role;     // "user", "alice", "system"
    std::string content;
    time_t timestamp;
};

class AliceWidget : public WidgetBase {
public:
    AliceWidget();
    ~AliceWidget() override;

    const char* name() const override { return "ALICE"; }
    void update() override;
    void render(bool* p_open) override;
    float poll_interval() const override { return 0.5f; }

    // Public for testing
    std::optional<std::string> sanitize_input(const std::string& input) const;
    void add_message(const std::string& role, const std::string& content);
    size_t history_size() const;

private:
    char input_buf_[512] = "";
    std::vector<ChatMessage> history_;
    mutable std::mutex mutex_;
    bool auto_scroll_ = true;
    bool scroll_to_bottom_ = false;
    bool awaiting_response_ = false;

    // IPC to straylight-agent daemon
    static constexpr const char* AGENT_SOCKET = "/run/straylight/agent.sock";

    void send_query(const std::string& query);
    void poll_response();

    void render_chat_area();
    void render_input_bar();
    void render_toolbar();
};

} // namespace straylight
```

- [ ] **Step 2: Implementation**

```cpp
// alice_widget.cpp
#include "alice_widget.h"
#include <straylight/widget_draw.h>
#include <ctime>
#include <cstring>

namespace straylight {

AliceWidget::AliceWidget() {
    add_message("system", "[STRAYLIGHT ALICE] Conversational AI Agent Initialized.");
    add_message("alice", "How may I assist you today, Administrator?");
    ipc_.connect(AGENT_SOCKET); // non-fatal if agent not running
}

AliceWidget::~AliceWidget() {
    ipc_.disconnect();
}

std::optional<std::string> AliceWidget::sanitize_input(const std::string& input) const {
    for (char c : input) {
        if (c == '\'' || c == ';' || c == '&' || c == '|' ||
            c == '$'  || c == '`' || c == '\\' || c == '"')
            return std::nullopt;
    }
    return input;
}

void AliceWidget::add_message(const std::string& role, const std::string& content) {
    std::lock_guard lock(mutex_);
    history_.push_back({role, content, std::time(nullptr)});
    scroll_to_bottom_ = true;
}

size_t AliceWidget::history_size() const {
    std::lock_guard lock(mutex_);
    return history_.size();
}

void AliceWidget::send_query(const std::string& query) {
    nlohmann::json req = {
        {"method", "alice.query"},
        {"params", {{"text", query}}}
    };
    awaiting_response_ = true;
    auto r = ipc_.request(req);
    if (r) {
        auto& resp = *r;
        if (resp.contains("result") && resp["result"].contains("text")) {
            add_message("alice", resp["result"]["text"].get<std::string>());
        } else if (resp.contains("error")) {
            add_message("system", "[error] " + resp["error"].get<std::string>());
        }
    } else {
        // Fallback: execute via popen if IPC unavailable
        std::string cmd = "straylight alice '" + query + "' 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buf[1024];
            std::string output;
            while (fgets(buf, sizeof(buf), pipe))
                output += buf;
            pclose(pipe);
            if (!output.empty())
                add_message("alice", output);
            else
                add_message("system", "[error] No response from ALICE.");
        } else {
            add_message("system", "[error] ALICE offline.");
        }
    }
    awaiting_response_ = false;
}

void AliceWidget::update() {
    // Periodic: check if agent daemon is alive
    if (!ipc_.is_connected()) {
        ipc_.connect(AGENT_SOCKET);
    }
}

void AliceWidget::render(bool* p_open) {
    if (should_update()) update();

    ImGui::SetNextWindowSize(ImVec2(650, 450), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors::WindowBg);

    if (!ImGui::Begin("ALICE (AI Assistant)", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    render_toolbar();
    ImGui::Separator();
    render_chat_area();
    ImGui::Separator();
    render_input_bar();

    ImGui::End();
    ImGui::PopStyleColor();
}

void AliceWidget::render_toolbar() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.05f, 0.1f, 0.2f, 1.0f));

    if (ImGui::Button("Wake ALICE")) {
        send_query("--wake");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Context")) {
        send_query("--clear-context");
        std::lock_guard lock(mutex_);
        history_.clear();
        history_.push_back({"system", "Context Cleared.", std::time(nullptr)});
    }
    ImGui::SameLine();

    // Connection status indicator
    if (ipc_.is_connected()) {
        ImGui::TextColored(colors::Neon, "[CONNECTED]");
    } else {
        ImGui::TextColored(colors::Red, "[OFFLINE — fallback to CLI]");
    }

    ImGui::PopStyleColor();
}

void AliceWidget::render_chat_area() {
    float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("ChatScroll", ImVec2(0, -footer), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
    std::lock_guard lock(mutex_);

    for (const auto& msg : history_) {
        ImVec4 color;
        const char* prefix;
        if (msg.role == "user") {
            color = colors::Ice;
            prefix = "[YOU]";
        } else if (msg.role == "alice") {
            color = colors::Pink;
            prefix = "[ALICE]";
        } else {
            color = colors::Dim;
            prefix = "[SYS]";
        }

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s %s", prefix, msg.content.c_str());
        ImGui::PopStyleColor();
    }

    if (awaiting_response_) {
        ImGui::TextColored(colors::Amber, "[ALICE] thinking...");
    }

    ImGui::PopStyleVar();

    if (scroll_to_bottom_ || (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        ImGui::SetScrollHereY(1.0f);
    scroll_to_bottom_ = false;

    ImGui::EndChild();
}

void AliceWidget::render_input_bar() {
    ImGui::TextColored(colors::Pink, "[ALICE]>");
    ImGui::SameLine();

    ImGui::PushItemWidth(-1);
    bool reclaim = false;
    if (ImGui::InputText("##AliceInput", input_buf_, sizeof(input_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (input_buf_[0]) {
            std::string input(input_buf_);
            auto sanitized = sanitize_input(input);
            if (sanitized) {
                add_message("user", input);
                send_query(*sanitized);
            } else {
                add_message("system", "[error] Invalid metacharacters in query.");
            }
            input_buf_[0] = '\0';
            reclaim = true;
        }
    }
    ImGui::PopItemWidth();

    ImGui::SetItemDefaultFocus();
    if (reclaim) ImGui::SetKeyboardFocusHere(-1);
}

} // namespace straylight
```

---

### Task 3: Failing tests for model_serve_widget

**File:** `tests/unit/widgets/test_model_serve.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "model_serve_widget.h"

TEST(ModelServe, NameReturnsExpected) {
    straylight::ModelServeWidget w;
    EXPECT_STREQ(w.name(), "Model Serve");
}

TEST(ModelServe, DetectFormatGGUF) {
    EXPECT_EQ(straylight::ModelServeWidget::detect_format("/path/to/model.gguf"),
              straylight::ModelFormat::GGUF);
}

TEST(ModelServe, DetectFormatSafetensors) {
    EXPECT_EQ(straylight::ModelServeWidget::detect_format("/path/to/model.safetensors"),
              straylight::ModelFormat::Safetensors);
}

TEST(ModelServe, DetectFormatONNX) {
    EXPECT_EQ(straylight::ModelServeWidget::detect_format("/path/to/model.onnx"),
              straylight::ModelFormat::ONNX);
}

TEST(ModelServe, EndpointLifecycle) {
    straylight::ModelServeWidget w;
    auto id = w.add_endpoint("test-llm", "/models/test.gguf", "llamacpp", 8080);
    ASSERT_TRUE(id.has_value());
    auto info = w.get_endpoint(*id);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "test-llm");
    EXPECT_EQ(info->port, 8080);
}
```

---

### Task 4: Implement model_serve_widget

**Files:** `apps/widgets/ai/model_serve_widget.h`, `apps/widgets/ai/model_serve_widget.cpp`

- [ ] **Step 1: Header**

```cpp
// model_serve_widget.h
#pragma once
#include <straylight/widget.h>
#include <straylight/widget_colors.h>
#include <optional>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <sys/types.h>

namespace straylight {

enum class ModelFormat { GGUF, Safetensors, ONNX, TorchScript, PyTorchBin, Unknown };

struct ModelEndpoint {
    uint32_t id;
    std::string name;
    std::string model_path;
    std::string backend;     // "llamacpp", "vllm", "torchserve", "onnx"
    int port;
    pid_t pid = -1;
    std::string status;      // "stopped", "starting", "running", "failed"
    uint64_t total_requests = 0;
    uint64_t total_errors = 0;
    float avg_latency_ms = 0.0f;
    float throughput_rps = 0.0f;
    std::deque<float> latency_history;  // last 60 samples
    std::deque<float> rps_history;      // last 60 samples
    std::deque<std::string> logs;       // stdout/stderr tail
};

class ModelServeWidget : public WidgetBase {
public:
    ModelServeWidget();
    ~ModelServeWidget() override;

    const char* name() const override { return "Model Serve"; }
    void update() override;
    void render(bool* p_open) override;
    float poll_interval() const override { return 2.0f; }

    // Public API for testing
    static ModelFormat detect_format(const std::string& path);
    Result<uint32_t, SLError> add_endpoint(const std::string& name,
                                            const std::string& model_path,
                                            const std::string& backend, int port);
    std::optional<ModelEndpoint> get_endpoint(uint32_t id) const;

private:
    std::vector<ModelEndpoint> endpoints_;
    mutable std::mutex mutex_;
    uint32_t next_id_ = 1;
    int selected_tab_ = 0;

    // Deploy form state
    char deploy_name_[128] = "";
    char deploy_path_[512] = "";
    int deploy_backend_ = 0;
    int deploy_port_ = 8080;

    void render_models_tab();
    void render_deploy_tab();
    void render_logs_tab();
    void poll_endpoint_health();
    void poll_nvidia_smi();
};

} // namespace straylight
```

- [ ] **Step 2: Implementation** — Full rendering with 3 tabs (MODELS, DEPLOY, LOGS). MODELS tab shows ImGui table with endpoint name, backend, port, status, requests, latency, throughput, sparklines. Status uses color-coded dots. Each row has Start/Stop/Remove buttons. DEPLOY tab has text inputs for model path, backend combo, port slider. LOGS tab shows per-endpoint stdout/stderr in scrolling region. `update()` calls `poll_endpoint_health()` which reads `/proc/<pid>/stat` for each running endpoint and probes the HTTP port with a non-blocking connect(). `detect_format()` checks file extension: `.gguf` -> GGUF, `.safetensors` -> Safetensors, `.onnx` -> ONNX, `.pt`/`.pth` -> TorchScript, `.bin` -> PyTorchBin.

- [ ] **Step 3: Add CMakeLists.txt for ai/ widget group**

```cmake
add_library(widgets-ai STATIC
    alice_widget.cpp
    model_serve_widget.cpp
)
target_link_libraries(widgets-ai PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

Run: `ctest --test-dir build -R "test_alice_widget|test_model_serve"` -> all pass.
`git add apps/widgets/ai/ tests/unit/widgets/test_alice_widget.cpp tests/unit/widgets/test_model_serve.cpp`
`git commit -m "feat(widgets/ai): implement alice_widget and model_serve_widget"`

---

## Chunk 2: inference_hotswap + model_cache (Inference Pipeline)

`apps/widgets/ai/` — Live model hot-swapping UI and model cache viewer. These handle the serving infrastructure layer.

### File Structure

```
apps/widgets/ai/
├── inference_hotswap_widget.h
├── inference_hotswap_widget.cpp
├── model_cache_widget.h
└── model_cache_widget.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/widgets/test_inference_hotswap.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "inference_hotswap_widget.h"

TEST(InferenceHotswap, NameReturnsExpected) {
    straylight::InferenceHotswapWidget w;
    EXPECT_STREQ(w.name(), "Inference Hotswap");
}

TEST(InferenceHotswap, VersionLifecycle) {
    straylight::InferenceHotswapWidget w;
    auto eid = w.create_endpoint("test-ep");
    ASSERT_TRUE(eid.has_value());
    auto vid = w.add_version(*eid, "/models/v1.gguf", "llamacpp", 8080);
    ASSERT_TRUE(vid.has_value());
    auto ep = w.get_endpoint_info(*eid);
    ASSERT_TRUE(ep.has_value());
    EXPECT_EQ(ep->versions.size(), 1u);
}

TEST(InferenceHotswap, LatencyPercentiles) {
    std::deque<float> latencies = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto metrics = straylight::InferenceHotswapWidget::compute_percentiles(latencies);
    EXPECT_NEAR(metrics.p50_ms, 5.0f, 1.0f);
    EXPECT_NEAR(metrics.p95_ms, 9.5f, 1.0f);
}
```

---

### Task 2: Implement inference_hotswap_widget

- [ ] **Step 1: Header** — `InferenceHotswapWidget` with 5 tabs: ENDPOINTS (table with active version, status, actions), DEPLOY (new version deployment with A/B split), TRAFFIC (request routing sparklines, latency percentile bars), HISTORY (timestamped deployment log), HEALTH (auto-rollback configuration, health check failures). Data structures: `ModelVersion` (version number, model_path, backend_type, port, pid, status, deployed_at, health_failures, last_health_ms), `ServingEndpoint` (name, active_version, versions[], total_requests, latencies, rpm_history). `compute_percentiles()` is static for testing.

- [ ] **Step 2: Implementation** — ENDPOINTS tab renders ImGui table with status dots (warming=amber, active=green, draining=yellow, stopped=dim, failed=red). DEPLOY tab: user picks endpoint, uploads model path, selects split percentage (0-100 slider for canary), starts warming. TRAFFIC tab: per-endpoint sparklines via `draw::sparkline()`, percentile bars via `draw::util_bar()`. Health check runs non-blocking TCP connect to each version's port every 5s. If 3 consecutive failures, auto-rollback to previous version.

---

### Task 3: Failing tests + implement model_cache_widget

**File:** `tests/unit/widgets/test_model_cache.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "model_cache_widget.h"

TEST(ModelCache, NameReturnsExpected) {
    straylight::ModelCacheWidget w;
    EXPECT_STREQ(w.name(), "Model Cache");
}

TEST(ModelCache, HumanBytesFormatting) {
    EXPECT_EQ(straylight::ModelCacheWidget::format_bytes(1024), "1 KB");
    EXPECT_EQ(straylight::ModelCacheWidget::format_bytes(1048576), "1 MB");
    EXPECT_EQ(straylight::ModelCacheWidget::format_bytes(1073741824), "1.0 GB");
}

TEST(ModelCache, LruEvictionOrder) {
    straylight::ModelCacheWidget w;
    w.add_entry("model-a", 100, 1000);
    w.add_entry("model-b", 200, 2000);
    w.add_entry("model-c", 300, 3000);
    // Access model-a to make it most recently used
    w.touch_entry("model-a");
    auto evict_order = w.get_lru_eviction_order();
    ASSERT_GE(evict_order.size(), 2u);
    // model-b should be evicted first (oldest access, ts=2000)
    EXPECT_EQ(evict_order[0], "model-b");
}
```

- [ ] **Step 2: Implement model_cache_widget** — 5 tabs: CACHE (overview cards showing total cache size, model count, dedup savings, LRU hit rate; sortable table of all cached models with name, format, size, last accessed, source), SCAN (walk HuggingFace `~/.cache/huggingface/`, Torch Hub `~/.cache/torch/`, Ollama `~/.ollama/models/`, StrayLight unified store `/var/cache/straylight/models/`; detect duplicates by SHA-256 of first 4MB), LINK (create symlinks from framework-specific caches to unified blob store), CLEAN (LRU eviction with size cap, age filter, dry-run toggle, estimated savings preview), SETTINGS (cache dir paths, size cap slider, auto-link toggle). `update()` reads directory sizes via `stat()` and `readdir()`.

- [ ] **Step 3: Update ai/ CMakeLists.txt**

```cmake
add_library(widgets-ai STATIC
    alice_widget.cpp
    model_serve_widget.cpp
    inference_hotswap_widget.cpp
    model_cache_widget.cpp
)
target_link_libraries(widgets-ai PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/ai): implement inference_hotswap and model_cache widgets"`

---

## Chunk 3: experiment_journal + checkpoint_dedup (Training & Experiments)

`apps/widgets/training/` — Experiment tracking and checkpoint management. Core ML workflow widgets.

### File Structure

```
apps/widgets/training/
├── CMakeLists.txt
├── experiment_journal_widget.h
├── experiment_journal_widget.cpp
├── checkpoint_dedup_widget.h
└── checkpoint_dedup_widget.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/widgets/test_experiment_journal.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "experiment_journal_widget.h"

TEST(ExperimentJournal, NameReturnsExpected) {
    straylight::ExperimentJournalWidget w;
    EXPECT_STREQ(w.name(), "Experiment Journal");
}

TEST(ExperimentJournal, RecordExperiment) {
    straylight::ExperimentJournalWidget w;
    straylight::ExperimentRecord rec;
    rec.command = "python train.py --lr 0.001";
    rec.exit_code = 0;
    rec.duration_sec = 3600;
    rec.gpu_peak_mb = 24000;
    auto id = w.record(rec);
    ASSERT_TRUE(id.has_value());
    EXPECT_FALSE(id->empty());
}

TEST(ExperimentJournal, SearchByCommand) {
    straylight::ExperimentJournalWidget w;
    straylight::ExperimentRecord r1;
    r1.command = "python train.py --lr 0.001";
    r1.exit_code = 0;
    w.record(r1);

    straylight::ExperimentRecord r2;
    r2.command = "python eval.py --checkpoint best";
    r2.exit_code = 0;
    w.record(r2);

    auto results = w.search("train");
    EXPECT_EQ(results.size(), 1u);
}

TEST(ExperimentJournal, StatsComputation) {
    straylight::ExperimentJournalWidget w;
    for (int i = 0; i < 10; i++) {
        straylight::ExperimentRecord r;
        r.command = "python train.py";
        r.exit_code = (i < 8) ? 0 : 1;
        r.duration_sec = 3600;
        r.gpu_peak_mb = 8000;
        w.record(r);
    }
    auto stats = w.compute_stats();
    EXPECT_NEAR(stats.success_rate, 0.8f, 0.01f);
    EXPECT_EQ(stats.total_experiments, 10u);
}
```

---

### Task 2: Implement experiment_journal_widget

- [ ] **Step 1: Header**

```cpp
// experiment_journal_widget.h
#pragma once
#include <straylight/widget.h>
#include <optional>
#include <string>
#include <vector>

namespace straylight {

struct ExperimentRecord {
    std::string uuid;
    std::string command;
    std::string cwd;
    int exit_code = -1;
    time_t start_time = 0;
    time_t end_time = 0;
    float duration_sec = 0;
    float gpu_peak_mb = 0;
    std::string git_hash;
    std::string git_branch;
    std::string python_version;
    std::string stdout_tail;   // last 50 lines
    std::string stderr_tail;
};

struct ExperimentStats {
    size_t total_experiments = 0;
    float success_rate = 0;
    float total_gpu_hours = 0;
    float avg_duration_sec = 0;
    std::vector<std::pair<std::string, int>> command_frequency;  // top commands
    std::vector<std::pair<std::string, int>> experiments_per_day; // last 30 days
};

class ExperimentJournalWidget : public WidgetBase {
public:
    ExperimentJournalWidget();
    const char* name() const override { return "Experiment Journal"; }
    void update() override;
    void render(bool* p_open) override;
    float poll_interval() const override { return 30.0f; }

    // Public API for testing
    Result<std::string, SLError> record(ExperimentRecord& rec);
    std::vector<ExperimentRecord> search(const std::string& query) const;
    ExperimentStats compute_stats() const;

private:
    std::vector<ExperimentRecord> experiments_;
    mutable std::mutex mutex_;
    int selected_idx_ = -1;
    int selected_tab_ = 0;

    // Search filter state
    char search_buf_[256] = "";
    int filter_exit_code_ = -1;  // -1 = all
    float filter_gpu_min_ = 0;
    float filter_gpu_max_ = 100000;

    static constexpr const char* EXPERIMENTS_DIR = "/var/log/straylight/experiments";

    void render_experiments_tab();
    void render_details_tab();
    void render_record_tab();
    void render_search_tab();
    void render_stats_tab();

    void load_from_disk();
    void save_record(const ExperimentRecord& rec);
    std::string generate_uuid();
};

} // namespace straylight
```

- [ ] **Step 2: Implementation** — 5 tabs matching the old codebase design. EXPERIMENTS tab: ImGui table with columns UUID (short), Command, Exit Code (color-coded: 0=green, else=red), Duration, GPU Peak MB, Timestamp. Click row to select. DETAILS tab: full metadata for selected experiment. RECORD tab: text input for command, "Run" button executes via `fork()`/`execvp()`, polls GPU memory via `nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits` every 2s, saves `metadata.json` to `EXPERIMENTS_DIR/<uuid>/`. STATS tab: success rate bar, GPU-hours total, average duration, experiments-per-day histogram via `ImGui::PlotHistogram()`.

- [ ] **Step 3: `load_from_disk()`** reads `/var/log/straylight/experiments/*/metadata.json`, parses each with nlohmann::json, populates `experiments_` vector.

---

### Task 3: Failing tests + implement checkpoint_dedup_widget

**File:** `tests/unit/widgets/test_checkpoint_dedup.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "checkpoint_dedup_widget.h"

TEST(CheckpointDedup, NameReturnsExpected) {
    straylight::CheckpointDedupWidget w;
    EXPECT_STREQ(w.name(), "Checkpoint Dedup");
}

TEST(CheckpointDedup, ChunkHashDeterministic) {
    std::vector<uint8_t> data(4096, 0x42);
    auto h1 = straylight::CheckpointDedupWidget::sha256_chunk(data.data(), data.size());
    auto h2 = straylight::CheckpointDedupWidget::sha256_chunk(data.data(), data.size());
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 64u); // hex string
}

TEST(CheckpointDedup, DedupRatioComputation) {
    // 10 chunks, 3 unique
    auto ratio = straylight::CheckpointDedupWidget::compute_dedup_ratio(10, 3);
    EXPECT_NEAR(ratio, 0.7f, 0.01f);
}
```

- [ ] **Step 2: Implement checkpoint_dedup_widget** — 4 tabs: SCAN (walk user-specified directories for `.pt`, `.pth`, `.safetensors`, `.gguf`, `.ckpt` files, hash 4MB chunks with SHA-256, report per-file dedup ratio), DEDUPLICATE (execute CAS: create `.straylight-cas/chunks/<ab>/<hash>`, write manifest JSON, replace originals with symlinks; progress bar during operation), STATUS (per-directory dedup state showing original size, deduped size, savings), HISTORY (timestamped operation log). SHA-256 implementation is the minimal public-domain version from the old codebase (~100 lines, no OpenSSL).

- [ ] **Step 3: CMakeLists.txt**

```cmake
add_library(widgets-training STATIC
    experiment_journal_widget.cpp
    checkpoint_dedup_widget.cpp
)
target_link_libraries(widgets-training PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/training): implement experiment_journal and checkpoint_dedup widgets"`

---

## Chunk 4: job_queue + task_monitor (Job Management)

`apps/widgets/training/` — Job queue viewer and per-task resource monitoring.

### File Structure

```
apps/widgets/training/
├── job_queue_widget.h
├── job_queue_widget.cpp
├── task_monitor_widget.h
└── task_monitor_widget.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/widgets/test_job_queue.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "job_queue_widget.h"

TEST(JobQueue, NameReturnsExpected) {
    straylight::JobQueueWidget w;
    EXPECT_STREQ(w.name(), "Job Queue");
}

TEST(JobQueue, SubmitAndList) {
    straylight::JobQueueWidget w;
    straylight::JobSpec spec;
    spec.command = "python train.py";
    spec.gpu_mem_required_mb = 8000;
    spec.priority = 5;
    auto id = w.submit(spec);
    ASSERT_TRUE(id.has_value());

    auto jobs = w.list_jobs();
    EXPECT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].status, "pending");
}

TEST(JobQueue, PriorityOrdering) {
    straylight::JobQueueWidget w;
    straylight::JobSpec s1{.command = "low", .priority = 1};
    straylight::JobSpec s2{.command = "high", .priority = 10};
    w.submit(s1);
    w.submit(s2);

    auto jobs = w.list_jobs();
    ASSERT_EQ(jobs.size(), 2u);
    // Higher priority should be first
    EXPECT_EQ(jobs[0].spec.priority, 10);
}

TEST(JobQueue, CancelJob) {
    straylight::JobQueueWidget w;
    straylight::JobSpec spec{.command = "test"};
    auto id = w.submit(spec);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(w.cancel(*id).has_value());
    auto jobs = w.list_jobs();
    EXPECT_EQ(jobs[0].status, "cancelled");
}
```

---

### Task 2: Implement job_queue_widget

- [ ] **Step 1: Header** — `JobQueueWidget` with data structures: `JobSpec` (command, gpu_mem_required_mb, priority, schedule_after, env_vars), `JobEntry` (id, spec, status ["pending","running","completed","failed","cancelled"], pid, submitted_at, started_at, completed_at, exit_code, gpu_peak_mb, stdout_tail, stderr_tail).

- [ ] **Step 2: Implementation** — 5 tabs: QUEUE (sortable table by priority/status/submitted_at, color-coded status, Start/Pause/Kill/Cancel buttons per row), ADD JOB (text input for command, GPU memory slider, priority spinner, optional schedule-after datetime), MONITOR (selected running job's live stdout/stderr in scrolling child, GPU metrics sparkline from `nvidia-smi`), HISTORY (completed/failed jobs with duration, exit code, peak GPU, CSV export button), STATS (total GPU-hours, success rate, queue depth over time bar chart). `update()` reads GPU state via `nvidia-smi --query-gpu=memory.free,memory.total,utilization.gpu,temperature.gpu --format=csv,noheader,nounits`, checks `/proc/<pid>/stat` for running jobs, reaps completed children via `waitpid(WNOHANG)`.

---

### Task 3: Failing tests + implement task_monitor_widget

**File:** `tests/unit/widgets/test_task_monitor.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "task_monitor_widget.h"

TEST(TaskMonitor, NameReturnsExpected) {
    straylight::TaskMonitorWidget w;
    EXPECT_STREQ(w.name(), "Task Monitor");
}

TEST(TaskMonitor, ParseProcStat) {
    // Simulated /proc/<pid>/stat line
    std::string stat_line = "1234 (python) S 1000 1234 1234 0 -1 4194304 "
                            "500 0 0 0 100 50 0 0 20 0 1 0 1000 12345678 3000 "
                            "18446744073709551615";
    auto info = straylight::TaskMonitorWidget::parse_proc_stat(stat_line);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->pid, 1234);
    EXPECT_EQ(info->name, "python");
    EXPECT_EQ(info->state, 'S');
    EXPECT_EQ(info->utime, 100u);
    EXPECT_EQ(info->stime, 50u);
}

TEST(TaskMonitor, CpuPercentComputation) {
    auto pct = straylight::TaskMonitorWidget::compute_cpu_percent(
        100, 50,   // prev utime, stime
        200, 100,  // curr utime, stime
        1.0f       // elapsed seconds
    );
    // 150 ticks in 1 second, at 100 Hz = 150%
    EXPECT_GT(pct, 0.0f);
}
```

- [ ] **Step 2: Implement task_monitor_widget** — 3 tabs: PROCESSES (live table from `/proc/` iteration: PID, Name, User, CPU%, MEM%, GPU%, VRAM, State; sorted by CPU or MEM; kill/signal buttons), TASKS (straylight-agent scheduled tasks via IPC query), SYSTEM (aggregate CPU/RAM/GPU sparklines, top-10-by-memory breakdown, load average from `/proc/loadavg`). GPU per-process data from `nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits`. Process enumeration reads `/proc/[0-9]*/stat` and `/proc/[0-9]*/status` for memory RSS.

- [ ] **Step 3: Update training/ CMakeLists.txt**

```cmake
add_library(widgets-training STATIC
    experiment_journal_widget.cpp
    checkpoint_dedup_widget.cpp
    job_queue_widget.cpp
    task_monitor_widget.cpp
)
target_link_libraries(widgets-training PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/training): implement job_queue and task_monitor widgets"`

---

## Chunk 5: doctor_widget + observer_widget (System Health)

`apps/widgets/system/` — Health diagnostics and telemetry observation panels.

### File Structure

```
apps/widgets/system/
├── CMakeLists.txt
├── doctor_widget.h
├── doctor_widget.cpp
├── observer_widget.h
└── observer_widget.cpp
```

### Task 1: Failing tests for doctor_widget

**File:** `tests/unit/widgets/test_doctor.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "doctor_widget.h"

TEST(Doctor, NameReturnsExpected) {
    straylight::DoctorWidget w;
    EXPECT_STREQ(w.name(), "Doctor");
}

TEST(Doctor, SubsystemCheckStructure) {
    straylight::DoctorWidget w;
    auto checks = w.run_fast_check();
    // Should have checks for at least: bus, registry, scheduler, core, compositor
    EXPECT_GE(checks.size(), 5u);
    for (const auto& c : checks) {
        EXPECT_FALSE(c.subsystem.empty());
        EXPECT_TRUE(c.status == "ok" || c.status == "warn" || c.status == "fail");
    }
}

TEST(Doctor, MemoryCheckReadsProc) {
    auto mem = straylight::DoctorWidget::read_meminfo();
    EXPECT_GT(mem.total_kb, 0u);
    EXPECT_GT(mem.available_kb, 0u);
}
```

---

### Task 2: Implement doctor_widget

- [ ] **Step 1: Header** — `DoctorWidget` with `SubsystemCheck` (subsystem name, status, message, latency_ms), `MemInfo` (total_kb, available_kb, buffers_kb, cached_kb, swap_total_kb, swap_free_kb). `run_fast_check()` queries each StrayLight daemon socket with a `{"method": "health.ping"}` request and checks response within 100ms timeout. `run_deep_check()` additionally reads `/proc/meminfo` for memory pressure, checks GPU health via `nvidia-smi --query-gpu=ecc.errors.corrected.aggregate.total --format=csv,noheader`, verifies filesystem mounts.

- [ ] **Step 2: Implementation** — 4 tabs: OVERVIEW (subsystem status grid with color-coded cards: green=ok, amber=warn, red=fail; each card shows subsystem name, PID, uptime, last health check), MEMORY (memory pressure bars from `/proc/meminfo`, swap usage, page faults from `/proc/vmstat`), GPU (ECC errors, temperature, power draw, fan speed, all from nvidia-smi), LOG (scrolling diagnostic output area, command input for manual `straylight doctor` invocations). Fast check button runs all health pings in parallel. Deep check button adds memory scan, GPU diagnostics, filesystem checks.

---

### Task 3: Failing tests + implement observer_widget

**File:** `tests/unit/widgets/test_observer.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "observer_widget.h"

TEST(Observer, NameReturnsExpected) {
    straylight::ObserverWidget w;
    EXPECT_STREQ(w.name(), "Observer");
}

TEST(Observer, ParseLoadAvg) {
    auto load = straylight::ObserverWidget::parse_loadavg("1.50 2.30 3.10 2/500 12345");
    ASSERT_TRUE(load.has_value());
    EXPECT_NEAR(load->load_1m, 1.5f, 0.01f);
    EXPECT_NEAR(load->load_5m, 2.3f, 0.01f);
    EXPECT_NEAR(load->load_15m, 3.1f, 0.01f);
    EXPECT_EQ(load->running_tasks, 2);
    EXPECT_EQ(load->total_tasks, 500);
}

TEST(Observer, MetricHistoryRollover) {
    straylight::ObserverWidget w;
    for (int i = 0; i < 200; i++) {
        w.push_metric("cpu", static_cast<float>(i));
    }
    // History should be capped at 120 samples
    EXPECT_LE(w.get_metric_history("cpu").size(), 120u);
}
```

- [ ] **Step 2: Implement observer_widget** — Full telemetry dashboard with 5 tabs: VITALS (CPU, RAM, Swap, Load Average with sparklines, uptime, hostname, kernel version from `uname`), NETWORK (per-interface bandwidth from `/proc/net/dev`, connection count from `/proc/net/tcp`), GPU (utilization%, VRAM%, temperature, power draw sparklines from nvidia-smi), DISK (per-mount I/O from `/proc/diskstats`, read/write rates), PROCESSES (top 5 by CPU, top 5 by MEM, running count). All metrics stored in rolling deques of 120 samples. `update()` reads all data sources and pushes to metric history.

- [ ] **Step 3: CMakeLists.txt**

```cmake
add_library(widgets-system STATIC
    doctor_widget.cpp
    observer_widget.cpp
)
target_link_libraries(widgets-system PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/system): implement doctor and observer widgets"`

---

## Chunk 6: swarm_hud + sysadmin_hud (Infrastructure HUDs)

`apps/widgets/system/` — Multi-node swarm status and system administration overview.

### File Structure

```
apps/widgets/system/
├── swarm_hud_widget.h
├── swarm_hud_widget.cpp
├── sysadmin_hud_widget.h
└── sysadmin_hud_widget.cpp
```

### Task 1: Failing tests for swarm_hud

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "swarm_hud_widget.h"

TEST(SwarmHud, NameReturnsExpected) {
    straylight::SwarmHudWidget w;
    EXPECT_STREQ(w.name(), "Swarm HUD");
}

TEST(SwarmHud, AddAndRemoveNode) {
    straylight::SwarmHudWidget w;
    straylight::SwarmNode node;
    node.hostname = "jetson-01";
    node.ip = "10.0.0.101";
    node.hardware_profile = "NVIDIA Jetson AGX Orin";
    node.is_online = true;
    node.vram_total_mb = 65536;
    node.vram_usage_mb = 14200;
    w.add_node(node);

    EXPECT_EQ(w.node_count(), 1u);
    EXPECT_EQ(w.online_count(), 1u);

    w.remove_node("jetson-01");
    EXPECT_EQ(w.node_count(), 0u);
}

TEST(SwarmHud, AggregateVram) {
    straylight::SwarmHudWidget w;
    straylight::SwarmNode n1{.hostname="a", .vram_total_mb=1000, .vram_usage_mb=500, .is_online=true};
    straylight::SwarmNode n2{.hostname="b", .vram_total_mb=2000, .vram_usage_mb=800, .is_online=true};
    w.add_node(n1);
    w.add_node(n2);
    auto agg = w.aggregate_vram();
    EXPECT_FLOAT_EQ(agg.total_mb, 3000.0f);
    EXPECT_FLOAT_EQ(agg.used_mb, 1300.0f);
}
```

---

### Task 2: Implement swarm_hud_widget

- [ ] **Step 1: Header** — `SwarmHudWidget` with `SwarmNode` (hostname, ip, hardware_profile, vram_usage_mb, vram_total_mb, cpu_load_pct, temp_c, is_online, tx_mbps, rx_mbps, last_seen), `VramAggregate` (total_mb, used_mb). Methods: `add_node()`, `remove_node()`, `node_count()`, `online_count()`, `aggregate_vram()`.

- [ ] **Step 2: Implementation** — Single-page grid layout (matching old codebase pattern). Header: online count, aggregate VRAM bar, fabric bandwidth totals. 3-column grid of node cards, each card renders: status dot + hostname + IP, hardware profile text, VRAM progress bar (color shifts red at >90%), CPU% + temperature (fire icon above 80C), network I/O rates, action buttons (SSH Shell, Bifrost Evict for online; Wake-on-LAN for offline). `update()` queries straylight-bus for registered swarm nodes via IPC `{"method": "swarm.list_nodes"}`. Falls back to reading `/etc/straylight/swarm.json` if bus unavailable.

---

### Task 3: Failing tests + implement sysadmin_hud

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "sysadmin_hud_widget.h"

TEST(SysadminHud, NameReturnsExpected) {
    straylight::SysadminHudWidget w;
    EXPECT_STREQ(w.name(), "Sysadmin HUD");
}

TEST(SysadminHud, ParseDfOutput) {
    std::string line = "/dev/sda1       ext4   500G  200G  280G  42% /";
    auto disk = straylight::SysadminHudWidget::parse_df_line(line);
    ASSERT_TRUE(disk.has_value());
    EXPECT_EQ(disk->mount_point, "/");
    EXPECT_EQ(disk->use_percent, 42);
}

TEST(SysadminHud, ServiceStatusParsing) {
    auto status = straylight::SysadminHudWidget::parse_systemctl_status("active (running)");
    EXPECT_EQ(status, straylight::ServiceStatus::Running);
    status = straylight::SysadminHudWidget::parse_systemctl_status("inactive (dead)");
    EXPECT_EQ(status, straylight::ServiceStatus::Stopped);
}
```

- [ ] **Step 2: Implement sysadmin_hud_widget** — 6 tabs matching old codebase design: VITALS (CPU/RAM/Swap/Disk bars from `/proc/stat`, `/proc/meminfo`, `statvfs()`, load average sparkline, uptime from `/proc/uptime`, hostname/kernel/arch from `uname`), PROCESSES (live table from `/proc/` enumeration, sorted by CPU/MEM, kill button sends `SIGTERM`/`SIGKILL`), DISK (mount table from `statvfs()` on each mount in `/proc/mounts`, I/O rates from `/proc/diskstats`), SERVICES (StrayLight service list: bus, registry, scheduler, core, compositor, agent; status from `systemctl is-active straylight-<name>`; start/stop/restart buttons via IPC), LOGS (live system log from `journalctl -f --no-pager -n 100` with severity color-coding and search filter), COMMAND (interactive command output area with sanitized input, history).

- [ ] **Step 3: Update system/ CMakeLists.txt**

```cmake
add_library(widgets-system STATIC
    doctor_widget.cpp
    observer_widget.cpp
    swarm_hud_widget.cpp
    sysadmin_hud_widget.cpp
)
target_link_libraries(widgets-system PRIVATE straylight-common imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/system): implement swarm_hud and sysadmin_hud widgets"`

---

## Chunk 7: power_scheduler + device_settings (Hardware Management)

`apps/widgets/system/` — Power/thermal management and GPU/accelerator configuration.

### File Structure

```
apps/widgets/system/
├── power_scheduler_widget.h
├── power_scheduler_widget.cpp
├── device_settings_widget.h
└── device_settings_widget.cpp
```

### Task 1: Failing tests for power_scheduler

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "power_scheduler_widget.h"

TEST(PowerScheduler, NameReturnsExpected) {
    straylight::PowerSchedulerWidget w;
    EXPECT_STREQ(w.name(), "Power Scheduler");
}

TEST(PowerScheduler, CostComputation) {
    // 100W for 1 hour at $0.12/kWh = $0.012
    float cost = straylight::PowerSchedulerWidget::compute_cost(100.0f, 3600.0f, 0.12f);
    EXPECT_NEAR(cost, 0.012f, 0.001f);
}

TEST(PowerScheduler, CheapestWindow) {
    std::vector<straylight::RatePeriod> rates = {
        {0, 6, 0.05f},    // midnight-6am: $0.05
        {6, 18, 0.15f},   // 6am-6pm: $0.15
        {18, 24, 0.10f},  // 6pm-midnight: $0.10
    };
    auto window = straylight::PowerSchedulerWidget::find_cheapest_window(rates, 4);
    // 4-hour cheapest should start at hour 0 (midnight-4am)
    EXPECT_EQ(window.start_hour, 0);
    EXPECT_NEAR(window.rate, 0.05f, 0.001f);
}

TEST(PowerScheduler, ReadRaplEnergy) {
    // This test validates the parsing logic, not actual RAPL access
    auto energy = straylight::PowerSchedulerWidget::parse_rapl_energy("123456789");
    EXPECT_EQ(energy, 123456789ULL);
}
```

---

### Task 2: Implement power_scheduler_widget

- [ ] **Step 1: Header** — `PowerSchedulerWidget` with `RatePeriod` (start_hour, end_hour, rate_per_kwh), `CheapestWindow` (start_hour, rate), `PowerSample` (timestamp, gpu_watts, cpu_watts, total_watts, cost_accumulated). Methods: `compute_cost()`, `find_cheapest_window()`, `parse_rapl_energy()`.

- [ ] **Step 2: Implementation** — 6 tabs: POWER (real-time GPU power from `nvidia-smi --query-gpu=power.draw --format=csv,noheader,nounits`, CPU power from Intel RAPL `/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj`, system total estimate, cost accumulator, sparklines for power draw), RATES (editable rate periods table with add/remove, save to `/etc/straylight/power-rates.json`, 24h timeline visualization), FORECAST (24h cost projection table showing each hour's estimated cost, cheapest 4h window highlighted in green), SOLAR (optional solar panel config: peak watts, latitude/longitude for sunrise/sunset calculation, estimated daily production kWh), HISTORY (30-day bar chart of daily kWh and cost via `ImGui::PlotHistogram()`, CSV export), SCHEDULE (deferred job list: jobs that wait for off-peak rates, cost limit per job, thermal-safe toggle that pauses when GPU >85C).

---

### Task 3: Failing tests + implement device_settings_widget

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "device_settings_widget.h"

TEST(DeviceSettings, NameReturnsExpected) {
    straylight::DeviceSettingsWidget w;
    EXPECT_STREQ(w.name(), "Device Settings");
}

TEST(DeviceSettings, ParseNvidiaSmiGpuInfo) {
    std::string csv = "NVIDIA GeForce RTX 4090, 24564, 350.00, 55, 89";
    auto info = straylight::DeviceSettingsWidget::parse_gpu_info(csv);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->name, "NVIDIA GeForce RTX 4090");
    EXPECT_EQ(info->vram_total_mb, 24564);
    EXPECT_NEAR(info->power_limit_w, 350.0f, 0.1f);
    EXPECT_EQ(info->temperature, 55);
    EXPECT_EQ(info->fan_percent, 89);
}

TEST(DeviceSettings, ClockProfileNames) {
    auto profiles = straylight::DeviceSettingsWidget::get_clock_profiles();
    EXPECT_GE(profiles.size(), 3u); // at least: default, max-perf, power-save
}
```

- [ ] **Step 2: Implement device_settings_widget** — 4 tabs: GPU (nvidia-smi data: name, VRAM total/used, power limit, temperature, fan speed, clock speeds; adjustable power limit slider, persistence mode toggle, compute mode combo), CPU (governor from `/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`, available governors, turbo boost toggle from `/sys/devices/system/cpu/intel_pstate/no_turbo`), MEMORY (hugepages from `/proc/sys/vm/nr_hugepages`, transparent hugepages from `/sys/kernel/mm/transparent_hugepage/enabled`, NUMA policy), PROFILES (named configurations that set all the above in one click: "ML Training", "Inference", "Power Save", "Maximum Performance").

- [ ] **Step 3: Update system/ CMakeLists.txt, tests pass + commit**

`git commit -m "feat(widgets/system): implement power_scheduler and device_settings widgets"`

---

## Chunk 8: log_viewer + quantum_ide (Specialized Tools)

`apps/widgets/tools/` — Live log viewer and quantum circuit composer.

### File Structure

```
apps/widgets/tools/
├── CMakeLists.txt
├── log_viewer_widget.h
├── log_viewer_widget.cpp
├── quantum_ide_widget.h
└── quantum_ide_widget.cpp
```

### Task 1: Failing tests for log_viewer

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "log_viewer_widget.h"

TEST(LogViewer, NameReturnsExpected) {
    straylight::LogViewerWidget w;
    EXPECT_STREQ(w.name(), "Log Viewer");
}

TEST(LogViewer, ParseJournalLine) {
    std::string line = "Mar 16 10:30:45 straylight straylight-core[1234]: "
                       "scheduler tick completed in 2ms";
    auto entry = straylight::LogViewerWidget::parse_journal_line(line);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->unit, "straylight-core");
    EXPECT_EQ(entry->pid, 1234);
    EXPECT_TRUE(entry->message.find("scheduler tick") != std::string::npos);
}

TEST(LogViewer, FilterBySeverity) {
    straylight::LogViewerWidget w;
    w.add_entry(straylight::LogSeverity::Info, "test", "info message");
    w.add_entry(straylight::LogSeverity::Error, "test", "error message");
    w.add_entry(straylight::LogSeverity::Debug, "test", "debug message");

    w.set_min_severity(straylight::LogSeverity::Info);
    auto filtered = w.get_filtered_entries();
    // Should include Info and Error, exclude Debug
    EXPECT_EQ(filtered.size(), 2u);
}

TEST(LogViewer, SearchFilter) {
    straylight::LogViewerWidget w;
    w.add_entry(straylight::LogSeverity::Info, "core", "scheduler tick");
    w.add_entry(straylight::LogSeverity::Info, "bus", "signal forwarded");
    w.add_entry(straylight::LogSeverity::Info, "core", "scheduler timeout");

    auto results = w.search("scheduler");
    EXPECT_EQ(results.size(), 2u);
}
```

---

### Task 2: Implement log_viewer_widget

- [ ] **Step 1: Header** — `LogViewerWidget` with `LogSeverity` enum (Debug, Info, Warning, Error, Critical), `LogEntry` (timestamp, severity, unit, pid, message), `JournalParsed` (unit, pid, message). Methods: `parse_journal_line()`, `add_entry()`, `set_min_severity()`, `get_filtered_entries()`, `search()`.

- [ ] **Step 2: Implementation** — Live journal viewer that reads from `journalctl -f --no-pager -o short-precise -n 500 2>/dev/null` via `popen()` in a background thread. Entries parsed and pushed to a thread-safe ring buffer (max 10000 entries). Render: top control bar with severity combo (Debug/Info/Warning/Error/Critical), unit filter combo (populated from seen units), search text input, auto-scroll toggle, clear button. Main area: scrolling child with color-coded lines (Debug=dim, Info=ice, Warning=amber, Error=red, Critical=hot+bold). Each line shows timestamp, severity badge, unit tag, message. Supports pause/resume of the log stream.

---

### Task 3: Failing tests + implement quantum_ide_widget

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "quantum_ide_widget.h"

TEST(QuantumIDE, NameReturnsExpected) {
    straylight::QuantumIDEWidget w;
    EXPECT_STREQ(w.name(), "Quantum IDE");
}

TEST(QuantumIDE, DefaultCircuitNotEmpty) {
    straylight::QuantumIDEWidget w;
    auto src = w.get_source();
    EXPECT_FALSE(src.empty());
    EXPECT_TRUE(src.find("qreg") != std::string::npos);
}

TEST(QuantumIDE, ParseGateCount) {
    std::string qasm = "qreg q[2];\ncreg c[2];\nh q[0];\ncx q[0],q[1];\n"
                       "measure q[0] -> c[0];\nmeasure q[1] -> c[1];\n";
    auto count = straylight::QuantumIDEWidget::count_gates(qasm);
    // h + cx + 2 measure = 4 gates
    EXPECT_EQ(count, 4u);
}
```

- [ ] **Step 2: Implement quantum_ide_widget** — Split-pane layout (50/50): left pane is OpenQASM 2.0 text editor (`ImGui::InputTextMultiline()` with `AllowTabInput`), right pane shows simulation results. "Simulate" button writes source to `/tmp/straylight_ide_qasm.qasm`, invokes the straylight-quantum QasmParser and QVM (linked from `lib/quantum/`). Results: probability histogram via `ImGui::PlotHistogram()`, amplitude matrix with color-coded entries (higher probability = warmer color), execution time display. Gate count and qubit count displayed below editor. Visual gate editor: below the text editor, render a circuit diagram using ImGui DrawList — horizontal qubit lines with gate boxes (H, X, CX, etc.) drawn at the appropriate positions by parsing the QASM source. Click gates to select/delete.

- [ ] **Step 3: CMakeLists.txt**

```cmake
add_library(widgets-tools STATIC
    log_viewer_widget.cpp
    quantum_ide_widget.cpp
)
target_link_libraries(widgets-tools PRIVATE straylight-common straylight-quantum imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/tools): implement log_viewer and quantum_ide widgets"`

---

## Chunk 9: neon_ice_firewall + topology_tuner (Network & Hardware Visualization)

`apps/widgets/tools/` — Firewall viewer/editor and NUMA/CPU topology visualizer.

### File Structure

```
apps/widgets/tools/
├── neon_ice_firewall_widget.h
├── neon_ice_firewall_widget.cpp
├── topology_tuner_widget.h
└── topology_tuner_widget.cpp
```

### Task 1: Failing tests for neon_ice_firewall

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "neon_ice_firewall_widget.h"

TEST(NeonIceFirewall, NameReturnsExpected) {
    straylight::NeonIceFirewallWidget w;
    EXPECT_STREQ(w.name(), "Firewall");
}

TEST(NeonIceFirewall, ParseProcNetTcp) {
    // Simulated /proc/net/tcp line (hex encoded)
    std::string line = "  0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 "
                       "00:00000000 00000000   1000        0 12345 1";
    auto conn = straylight::NeonIceFirewallWidget::parse_proc_tcp_line(line);
    ASSERT_TRUE(conn.has_value());
    EXPECT_EQ(conn->local_ip, "127.0.0.1");
    EXPECT_EQ(conn->local_port, 8080);
    EXPECT_EQ(conn->state, "LISTEN");
    EXPECT_EQ(conn->uid, 1000u);
    EXPECT_EQ(conn->inode, 12345u);
}

TEST(NeonIceFirewall, RuleCreation) {
    straylight::NeonIceFirewallWidget w;
    auto r = w.add_rule("python3", straylight::FirewallAction::Block);
    ASSERT_TRUE(r.has_value());
    auto rules = w.get_rules();
    EXPECT_EQ(rules.size(), 1u);
    EXPECT_EQ(rules[0].binary, "python3");
    EXPECT_EQ(rules[0].action, straylight::FirewallAction::Block);
}

TEST(NeonIceFirewall, ThreatScoreComputation) {
    // 50 connections in 1 second from same source = high threat
    auto score = straylight::NeonIceFirewallWidget::compute_threat_score(50, 1.0f);
    EXPECT_GT(score, 0.7f);
}
```

---

### Task 2: Implement neon_ice_firewall_widget

- [ ] **Step 1: Header** — `NeonIceFirewallWidget` with `TcpConnection` (local_ip, local_port, remote_ip, remote_port, state, uid, inode, pid, binary), `FirewallRule` (binary, action [Allow/Block], created_at), `FirewallAction` enum. Methods: `parse_proc_tcp_line()`, `add_rule()`, `get_rules()`, `compute_threat_score()`.

- [ ] **Step 2: Implementation** — 7 tabs: PROFILES (per-binary connection tracking from `/proc/net/tcp` + inode-to-PID resolution via `/proc/[pid]/fd/` readlink, block/allow toggle per binary), CONNECTIONS (live socket table: protocol, local addr:port, remote addr:port, state, PID, binary name; sorted by state/binary; color-coded states), STATS (bandwidth estimation, connection count sparkline, threat score gauge), THREATS (port scan detection: if >20 SYN_SENT to different ports from same PID within 5s, alert; rapid connection alerts), RULES (presets: ML-Safe blocks everything except model hub domains, Paranoid blocks all outbound, Permissive allows all), HISTORY (timestamped connection log, max 10000 entries), EXPORT (dump rules as JSON to `/etc/straylight/firewall-rules.json`). Blocking enforcement via `iptables -A OUTPUT -m owner --uid-owner <uid> -j DROP` command execution.

---

### Task 3: Failing tests + implement topology_tuner_widget

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "topology_tuner_widget.h"

TEST(TopologyTuner, NameReturnsExpected) {
    straylight::TopologyTunerWidget w;
    EXPECT_STREQ(w.name(), "Topology Tuner");
}

TEST(TopologyTuner, ParseCpuinfo) {
    std::string cpuinfo = "model name\t: AMD EPYC 7742 64-Core Processor\n"
                          "processor\t: 0\n"
                          "processor\t: 1\n";
    auto info = straylight::TopologyTunerWidget::parse_cpuinfo(cpuinfo);
    EXPECT_EQ(info.model, "AMD EPYC 7742 64-Core Processor");
    EXPECT_EQ(info.thread_count, 2);
}

TEST(TopologyTuner, MemoryTierHierarchy) {
    straylight::TopologyTunerWidget w;
    w.poll_memory();
    auto tiers = w.get_memory_tiers();
    // Should have at least L1, L2, L3, RAM
    EXPECT_GE(tiers.size(), 4u);
    // Verify ordering: L1 < L2 < L3 < RAM capacity
    for (size_t i = 1; i < tiers.size() && i < 4; i++) {
        EXPECT_GT(tiers[i].capacity_kb, tiers[i-1].capacity_kb);
    }
}

TEST(TopologyTuner, ProfileSaveLoad) {
    straylight::TopologyTunerWidget w;
    straylight::TuningProfile p;
    p.name = "test-profile";
    p.cpu_governor = 1;
    p.turbo_boost = true;
    w.save_profile(p);
    auto profiles = w.get_profiles();
    bool found = false;
    for (const auto& pr : profiles) {
        if (pr.name == "test-profile") { found = true; break; }
    }
    EXPECT_TRUE(found);
}
```

- [ ] **Step 2: Implement topology_tuner_widget** — Direct port of the old codebase TopologyTuner class but using WidgetBase interface. 6 tabs: OVERVIEW (system summary: CPU model from `/proc/cpuinfo`, core/thread count, RAM from `/proc/meminfo`, VRAM from nvidia-smi; unified memory pressure bar; CPU/RAM/GPU sparkline histories), MEMORY MAP (L1/L2/L3/RAM/VRAM hierarchy table with capacity, used, bandwidth, latency; visual flow diagram), COMPUTE GRID (per-core utilization cells using ImGui DrawList, frequency from sysfs, NUMA grouping), NETWORK MAP (interface table from `/proc/net/dev`, RDMA peer discovery), TUNING (CPU governor combo, GPU power mode, NUMA policy, I/O scheduler; Apply button writes to sysfs), PROFILES (save/load/delete named configurations). Background poll thread at 1Hz.

- [ ] **Step 3: Update tools/ CMakeLists.txt**

```cmake
add_library(widgets-tools STATIC
    log_viewer_widget.cpp
    quantum_ide_widget.cpp
    neon_ice_firewall_widget.cpp
    topology_tuner_widget.cpp
)
target_link_libraries(widgets-tools PRIVATE straylight-common straylight-quantum imgui)
```

- [ ] **Step 4: Tests pass + commit**

`git commit -m "feat(widgets/tools): implement neon_ice_firewall and topology_tuner widgets"`

---

## Chunk 10: orbital_storage_analyzer + media_browser + Widget Registry (Final)

`apps/widgets/tools/` — Storage visualizer, media browser, and the widget registry that ties everything together.

### File Structure

```
apps/widgets/tools/
├── orbital_storage_widget.h
├── orbital_storage_widget.cpp
├── media_browser_widget.h
├── media_browser_widget.cpp
apps/widgets/
├── widget_registry.h
├── widget_registry.cpp
└── CMakeLists.txt          # top-level, links all widget groups
```

### Task 1: Failing tests for orbital_storage_analyzer

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "orbital_storage_widget.h"

TEST(OrbitalStorage, NameReturnsExpected) {
    straylight::OrbitalStorageWidget w;
    EXPECT_STREQ(w.name(), "Storage Analyzer");
}

TEST(OrbitalStorage, ScanTmp) {
    straylight::OrbitalStorageWidget w;
    auto root = w.scan("/tmp");
    EXPECT_TRUE(root.is_directory);
    EXPECT_FALSE(root.name.empty());
}

TEST(OrbitalStorage, CategoryDetection) {
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("model.py"), "code");
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("photo.jpg"), "images");
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("data.csv"), "data");
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("readme.md"), "docs");
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("video.mp4"), "media");
    EXPECT_EQ(straylight::OrbitalStorageWidget::categorize("model.gguf"), "ml-models");
}

TEST(OrbitalStorage, HeatmapColor) {
    // Largest fraction -> red, smallest -> green
    auto red = straylight::OrbitalStorageWidget::heatmap_color(1.0f);
    EXPECT_GT(red.x, 0.8f);  // red channel
    auto green = straylight::OrbitalStorageWidget::heatmap_color(0.0f);
    EXPECT_GT(green.y, 0.5f); // green channel
}
```

---

### Task 2: Implement orbital_storage_widget

- [ ] **Step 1: Header** — `OrbitalStorageWidget` with `DiskNode` (name, path, size, file_count, dir_count, is_directory, children[]). Methods: `scan()`, `categorize()`, `heatmap_color()`.

- [ ] **Step 2: Implementation** — 5 tabs: RADIAL (DaisyDisk-style pie chart rendered with ImGui DrawList `AddTriangleFilled()` to compose arc segments; heatmap coloring where largest slice = red, smallest = green; center label shows current folder total; click slice to drill in; right-click context menu to open terminal at location or move to trash), TREE (sortable table with name, size bar, file count, type; click to expand/collapse directories), FILE TYPES (category breakdown: code, images, data, docs, media, ml-models, other; stacked bar chart), LARGE FILES (top 50 biggest files with path, size, last modified), DUPLICATES (files with identical size + extension pair, grouped). Scanning runs in background thread via `std::async`, reports progress via atomic counter. ML-model category added for `.gguf`, `.safetensors`, `.onnx`, `.pt`, `.pth`, `.bin` files.

---

### Task 3: Failing tests + implement media_browser_widget

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "media_browser_widget.h"

TEST(MediaBrowser, NameReturnsExpected) {
    straylight::MediaBrowserWidget w;
    EXPECT_STREQ(w.name(), "Media Browser");
}

TEST(MediaBrowser, IsMediaFile) {
    EXPECT_TRUE(straylight::MediaBrowserWidget::is_media_file("photo.jpg"));
    EXPECT_TRUE(straylight::MediaBrowserWidget::is_media_file("video.mp4"));
    EXPECT_TRUE(straylight::MediaBrowserWidget::is_media_file("audio.wav"));
    EXPECT_TRUE(straylight::MediaBrowserWidget::is_media_file("IMAGE.PNG"));
    EXPECT_FALSE(straylight::MediaBrowserWidget::is_media_file("code.py"));
    EXPECT_FALSE(straylight::MediaBrowserWidget::is_media_file("model.gguf"));
}

TEST(MediaBrowser, DirectoryListing) {
    straylight::MediaBrowserWidget w;
    w.set_directory("/tmp");
    auto dir = w.get_current_directory();
    EXPECT_EQ(dir, "/tmp");
}
```

- [ ] **Step 2: Implement media_browser_widget** — 3 tabs: GALLERY (thumbnail grid layout using ImGui columns; scans directory for `.jpg`, `.jpeg`, `.png`, `.gif`, `.bmp`, `.svg`, `.mp4`, `.webm`, `.mov`, `.wav`, `.mp3`, `.flac`, `.ogg` files; renders filename + size under each cell; click to select, double-click to open with `xdg-open`), RECENT (list of recently accessed files with timestamps), SEARCH (recursive search with file extension filter, name substring filter, size range filter). Directory navigation: breadcrumb bar at top, click segments to navigate up. Thumbnail rendering: for images, load via `stb_image` into ImGui texture ID and render with `ImGui::Image()`; for video/audio, show format icon placeholder.

---

### Task 4: Widget Registry (ties all widgets together)

**File:** `apps/widgets/widget_registry.h`, `apps/widgets/widget_registry.cpp`

- [ ] **Step 1: Header**

```cpp
// widget_registry.h
#pragma once
#include <straylight/widget.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight {

class WidgetRegistry {
public:
    static WidgetRegistry& instance();

    // Register a widget by name
    void register_widget(std::unique_ptr<WidgetBase> widget);

    // Get widget by name
    WidgetBase* get(const std::string& name) const;

    // Get all registered widgets
    const std::vector<std::unique_ptr<WidgetBase>>& all() const { return widgets_; }

    // Initialize all ML/HPC widgets
    void init_all();

    // Update all visible widgets
    void update_all();

    // Render the widget selector panel (shows all available widgets with toggle buttons)
    void render_selector(bool* p_open);

private:
    WidgetRegistry() = default;
    std::vector<std::unique_ptr<WidgetBase>> widgets_;
    std::unordered_map<std::string, size_t> name_index_;
    std::unordered_map<std::string, bool> visibility_;
};

} // namespace straylight
```

- [ ] **Step 2: Implementation** — `init_all()` instantiates all 20 widgets and registers them. `render_selector()` draws a sidebar panel listing all widgets with toggleable visibility checkboxes, grouped by domain (AI & Inference, Training & Experiments, System & Infrastructure, Specialized). The shell's left dock calls `render_selector()` to let users pick which widgets to show.

- [ ] **Step 3: Top-level CMakeLists.txt**

```cmake
# apps/widgets/CMakeLists.txt
add_subdirectory(ai)
add_subdirectory(training)
add_subdirectory(system)
add_subdirectory(tools)

add_library(widgets-all STATIC widget_registry.cpp)
target_link_libraries(widgets-all PRIVATE
    widgets-ai widgets-training widgets-system widgets-tools
    straylight-common imgui
)
```

- [ ] **Step 4: All tests pass + final commit**

Run: `ctest --test-dir build -R "test_.*widget"` -> all pass.
`git commit -m "feat(widgets): complete ML/HPC widget panel with 20 widgets and registry"`

---

## Summary

| Chunk | Widgets | Domain | Key Data Sources |
|-------|---------|--------|------------------|
| 1 | alice_widget, model_serve | AI Core | straylight-agent IPC, popen, inotify |
| 2 | inference_hotswap, model_cache | Inference Pipeline | TCP health probes, ~/.cache/*, SHA-256 |
| 3 | experiment_journal, checkpoint_dedup | Training | /var/log/straylight/experiments/, CAS |
| 4 | job_queue, task_monitor | Job Management | nvidia-smi, /proc/[pid]/stat, fork/exec |
| 5 | doctor_widget, observer_widget | System Health | IPC health ping, /proc/meminfo, /proc/stat |
| 6 | swarm_hud, sysadmin_hud | Infrastructure | swarm IPC, systemctl, journalctl, /proc/ |
| 7 | power_scheduler, device_settings | Hardware | Intel RAPL, nvidia-smi, sysfs |
| 8 | log_viewer, quantum_ide | Specialized Tools | journalctl -f, straylight-quantum QVM |
| 9 | neon_ice_firewall, topology_tuner | Network & HW Viz | /proc/net/tcp, /proc/cpuinfo, iptables |
| 10 | orbital_storage, media_browser, registry | Final | std::filesystem, stb_image, all widgets |

**Total:** 20 widgets + 1 registry, organized in 4 domain libraries, 10 implementation chunks.

**What makes this unique:** No other desktop OS ships an integrated ML experiment tracker, model serving dashboard, GPU job queue, checkpoint deduplicator, inference hot-swap manager, quantum circuit composer, swarm HUD, and power-aware scheduler — all as first-class desktop widgets with real-time data from the same subsystem daemons that manage the OS. This is StrayLight's differentiator: the desktop IS the ML workstation.
