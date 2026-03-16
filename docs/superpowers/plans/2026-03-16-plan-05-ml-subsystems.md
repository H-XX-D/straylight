# Plan 5: ML Subsystems — agent, compiler, morph, snn, rhem

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 5 ML subsystem binaries that provide StrayLight's machine-learning infrastructure. One daemon (`straylight-agent`) and four on-demand tools (`straylight-compiler`, `straylight-morph`, `straylight-snn`, `straylight-rhem`). After this plan, the `straylight-ml` Debian package is fully populated.

**Architecture:** Five binaries under `bin/`. `straylight-agent` is a persistent daemon using `DaemonBase` (init/tick/shutdown). The other four are CLI tools with `main()` + arg parsing that run, do work, and exit. All link against `libstraylight-common` + `libstraylight-ml`. Agent additionally links `libstraylight-net`. Morph and rhem additionally link `libstraylight-hw`.

**Tech Stack:** C++20, nlohmann/json 3.11+, spdlog 1.13+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, libstraylight-ml, libstraylight-net, libstraylight-hw)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). macOS cannot build — uses epoll, `/proc`, Linux-specific threading, `SIGTERM`/`SIGINT` process lifecycle.

**Error handling rules:**
- Libraries (internal modules) return `Result<T, std::string>`
- The daemon (`straylight-agent`) translates to `Result<T, SLError>` at the DaemonBase boundary
- On-demand tools translate to `Result<T, SLError>` only in `main()` for exit codes
- Use `Result<T,E>::ok(value)` / `Result<T,E>::error(err)` — never `std::unexpected`

---

## Chunk 1: straylight-agent — Event Loop + Task Queue

`bin/agent/` — Persistent daemon that accepts ML tasks (inference, training steps, data preprocessing) and distributes them across available workers. Uses an epoll-based event loop and a priority task queue. Links `libstraylight-common` + `libstraylight-ml` + `libstraylight-net`.

### File Structure

```
bin/agent/
├── CMakeLists.txt
├── main.cpp
├── agent_daemon.h
├── agent_daemon.cpp
├── event_loop.h
├── event_loop.cpp
├── task_queue.h
└── task_queue.cpp
tests/unit/subsystems/
└── test_agent_queue.cpp
etc/systemd/system/
└── straylight-agent.service
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_agent_queue.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_agent_queue.cpp
#include <gtest/gtest.h>
#include "task_queue.h"

using namespace straylight::agent;

TEST(TaskQueue, PushAndPopFIFO) {
    TaskQueue q(128);
    Task t1{.id = 1, .priority = Priority::Normal, .type = TaskType::Inference,
             .payload = "model_a"};
    Task t2{.id = 2, .priority = Priority::Normal, .type = TaskType::Training,
             .payload = "model_b"};
    ASSERT_TRUE(q.push(t1).has_value());
    ASSERT_TRUE(q.push(t2).has_value());
    EXPECT_EQ(q.size(), 2u);

    auto r1 = q.pop();
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1.value().id, 1u);

    auto r2 = q.pop();
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2.value().id, 2u);
}

TEST(TaskQueue, HighPriorityFirst) {
    TaskQueue q(128);
    Task low{.id = 1, .priority = Priority::Low, .type = TaskType::Preprocess,
              .payload = "data_1"};
    Task high{.id = 2, .priority = Priority::High, .type = TaskType::Inference,
               .payload = "model_a"};
    Task critical{.id = 3, .priority = Priority::Critical, .type = TaskType::Inference,
                   .payload = "model_b"};
    q.push(low);
    q.push(high);
    q.push(critical);

    EXPECT_EQ(q.pop().value().id, 3u);  // Critical first
    EXPECT_EQ(q.pop().value().id, 2u);  // High second
    EXPECT_EQ(q.pop().value().id, 1u);  // Low last
}

TEST(TaskQueue, PopEmptyReturnsError) {
    TaskQueue q(128);
    auto r = q.pop();
    EXPECT_FALSE(r.has_value());
}

TEST(TaskQueue, CapacityEnforced) {
    TaskQueue q(2);
    Task t{.id = 1, .priority = Priority::Normal, .type = TaskType::Inference,
            .payload = "x"};
    ASSERT_TRUE(q.push(t).has_value());
    t.id = 2;
    ASSERT_TRUE(q.push(t).has_value());
    t.id = 3;
    EXPECT_FALSE(q.push(t).has_value());  // Full
}

TEST(TaskQueue, CancelRemovesTask) {
    TaskQueue q(128);
    Task t{.id = 42, .priority = Priority::Normal, .type = TaskType::Training,
            .payload = "m"};
    q.push(t);
    EXPECT_TRUE(q.cancel(42));
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.cancel(42));  // Already removed
}

TEST(TaskQueue, PeekDoesNotRemove) {
    TaskQueue q(128);
    Task t{.id = 7, .priority = Priority::High, .type = TaskType::Inference,
            .payload = "p"};
    q.push(t);
    auto p = q.peek();
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p.value().id, 7u);
    EXPECT_EQ(q.size(), 1u);  // Still there
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_agent_queue test_agent_queue.cpp
    ${PROJECT_SOURCE_DIR}/bin/agent/task_queue.cpp)
target_include_directories(test_agent_queue PRIVATE ${PROJECT_SOURCE_DIR}/bin/agent)
target_link_libraries(test_agent_queue PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_agent_queue)
```

Run: `cmake --build build --target test_agent_queue && ctest --test-dir build -R test_agent_queue` → expect 6 failures.

---

### Task 2: Implement task_queue

**Files:** `bin/agent/task_queue.h`, `bin/agent/task_queue.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/agent/task_queue.h
#pragma once

#include <straylight/result.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace straylight::agent {

enum class Priority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,
};

enum class TaskType : uint8_t {
    Inference = 0,
    Training = 1,
    Preprocess = 2,
    Compile = 3,
    Quantize = 4,
};

struct Task {
    uint64_t id = 0;
    Priority priority = Priority::Normal;
    TaskType type = TaskType::Inference;
    std::string payload;         // JSON-encoded task parameters
    uint64_t submitted_ns = 0;  // Monotonic clock timestamp
};

/// Thread-safe priority task queue with bounded capacity.
/// Higher Priority values are dequeued first. Within the same priority, FIFO order.
class TaskQueue {
public:
    explicit TaskQueue(size_t capacity);

    /// Push a task. Returns error if queue is full.
    Result<void, std::string> push(Task task);

    /// Pop the highest-priority task. Returns error if empty.
    Result<Task, std::string> pop();

    /// Peek at the highest-priority task without removing it.
    Result<Task, std::string> peek() const;

    /// Cancel a task by ID. Returns true if found and removed.
    bool cancel(uint64_t task_id);

    /// Current number of queued tasks.
    [[nodiscard]] size_t size() const;

    /// Maximum capacity.
    [[nodiscard]] size_t capacity() const { return capacity_; }

    /// Drain all tasks into a vector (for shutdown).
    std::vector<Task> drain();

private:
    size_t capacity_;
    mutable std::mutex mu_;

    // One queue per priority level, index = static_cast<uint8_t>(Priority)
    static constexpr size_t kNumPriorities = 4;
    std::vector<Task> buckets_[kNumPriorities];
    size_t total_size_ = 0;
};

} // namespace straylight::agent
```

- [ ] **Step 2: Implementation**

```cpp
// bin/agent/task_queue.cpp
#include "task_queue.h"

#include <algorithm>
#include <chrono>

namespace straylight::agent {

TaskQueue::TaskQueue(size_t capacity) : capacity_(capacity) {}

Result<void, std::string> TaskQueue::push(Task task) {
    std::lock_guard lock(mu_);
    if (total_size_ >= capacity_) {
        return Result<void, std::string>::error("task queue full (" +
            std::to_string(capacity_) + " tasks)");
    }
    if (task.submitted_ns == 0) {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        task.submitted_ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
    auto idx = static_cast<uint8_t>(task.priority);
    if (idx >= kNumPriorities) idx = static_cast<uint8_t>(Priority::Normal);
    buckets_[idx].push_back(std::move(task));
    ++total_size_;
    return Result<void, std::string>::ok();
}

Result<Task, std::string> TaskQueue::pop() {
    std::lock_guard lock(mu_);
    // Scan from highest priority (Critical=3) down to lowest (Low=0)
    for (int p = static_cast<int>(kNumPriorities) - 1; p >= 0; --p) {
        auto& bucket = buckets_[p];
        if (!bucket.empty()) {
            Task t = std::move(bucket.front());
            bucket.erase(bucket.begin());
            --total_size_;
            return Result<Task, std::string>::ok(std::move(t));
        }
    }
    return Result<Task, std::string>::error("task queue empty");
}

Result<Task, std::string> TaskQueue::peek() const {
    std::lock_guard lock(mu_);
    for (int p = static_cast<int>(kNumPriorities) - 1; p >= 0; --p) {
        const auto& bucket = buckets_[p];
        if (!bucket.empty()) {
            return Result<Task, std::string>::ok(bucket.front());
        }
    }
    return Result<Task, std::string>::error("task queue empty");
}

bool TaskQueue::cancel(uint64_t task_id) {
    std::lock_guard lock(mu_);
    for (auto& bucket : buckets_) {
        auto it = std::find_if(bucket.begin(), bucket.end(),
            [task_id](const Task& t) { return t.id == task_id; });
        if (it != bucket.end()) {
            bucket.erase(it);
            --total_size_;
            return true;
        }
    }
    return false;
}

size_t TaskQueue::size() const {
    std::lock_guard lock(mu_);
    return total_size_;
}

std::vector<Task> TaskQueue::drain() {
    std::lock_guard lock(mu_);
    std::vector<Task> all;
    all.reserve(total_size_);
    // Drain in priority order: critical first
    for (int p = static_cast<int>(kNumPriorities) - 1; p >= 0; --p) {
        auto& bucket = buckets_[p];
        for (auto& t : bucket) all.push_back(std::move(t));
        bucket.clear();
    }
    total_size_ = 0;
    return all;
}

} // namespace straylight::agent
```

Run: `ctest --test-dir build -R test_agent_queue` → all 6 pass.

---

### Task 3: Implement event_loop

**Files:** `bin/agent/event_loop.h`, `bin/agent/event_loop.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/agent/event_loop.h
#pragma once

#include <straylight/result.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight::agent {

/// Callback invoked when a file descriptor is ready.
using FdCallback = std::function<void(int fd, uint32_t events)>;

/// Timer callback invoked when a timer fires.
using TimerCallback = std::function<void()>;

using TimerId = uint64_t;

/// epoll-based event loop for the agent daemon.
/// Supports file descriptor watches and repeating timers.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Non-copyable, non-movable (owns epoll fd)
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// Initialize epoll. Must be called before add_fd/run.
    Result<void, std::string> init();

    /// Add a file descriptor to watch for events (EPOLLIN, EPOLLOUT, etc.).
    Result<void, std::string> add_fd(int fd, uint32_t events, FdCallback cb);

    /// Remove a file descriptor from the watch set.
    void remove_fd(int fd);

    /// Add a repeating timer that fires every interval_ms milliseconds.
    TimerId add_timer(uint64_t interval_ms, TimerCallback cb);

    /// Remove a timer.
    void remove_timer(TimerId id);

    /// Run one iteration: epoll_wait with timeout, dispatch callbacks, fire timers.
    /// Returns error only on fatal epoll failures.
    Result<void, std::string> poll_once(int timeout_ms = 100);

    /// Signal the loop to stop (thread-safe).
    void request_stop() { stop_.store(true); }

    /// Check if stop was requested.
    [[nodiscard]] bool stopped() const { return stop_.load(); }

private:
    int epoll_fd_ = -1;
    std::atomic<bool> stop_{false};

    struct FdEntry {
        int fd;
        uint32_t events;
        FdCallback callback;
    };
    std::unordered_map<int, FdEntry> fd_map_;

    struct TimerEntry {
        TimerId id;
        uint64_t interval_ms;
        uint64_t next_fire_ms;
        TimerCallback callback;
    };
    std::vector<TimerEntry> timers_;
    TimerId next_timer_id_ = 1;

    uint64_t now_ms() const;
};

} // namespace straylight::agent
```

- [ ] **Step 2: Implementation**

```cpp
// bin/agent/event_loop.cpp
#include "event_loop.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

namespace straylight::agent {

EventLoop::EventLoop() = default;

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
}

Result<void, std::string> EventLoop::init() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        return Result<void, std::string>::error(
            std::string("epoll_create1 failed: ") + std::strerror(errno));
    }
    return Result<void, std::string>::ok();
}

Result<void, std::string> EventLoop::add_fd(int fd, uint32_t events, FdCallback cb) {
    if (epoll_fd_ < 0) {
        return Result<void, std::string>::error("event loop not initialized");
    }

    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    int op = fd_map_.contains(fd) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epoll_fd_, op, fd, &ev) < 0) {
        return Result<void, std::string>::error(
            std::string("epoll_ctl failed: ") + std::strerror(errno));
    }

    fd_map_[fd] = FdEntry{fd, events, std::move(cb)};
    return Result<void, std::string>::ok();
}

void EventLoop::remove_fd(int fd) {
    if (epoll_fd_ >= 0 && fd_map_.contains(fd)) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        fd_map_.erase(fd);
    }
}

TimerId EventLoop::add_timer(uint64_t interval_ms, TimerCallback cb) {
    TimerId id = next_timer_id_++;
    uint64_t now = now_ms();
    timers_.push_back(TimerEntry{
        .id = id,
        .interval_ms = interval_ms,
        .next_fire_ms = now + interval_ms,
        .callback = std::move(cb),
    });
    return id;
}

void EventLoop::remove_timer(TimerId id) {
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
            [id](const TimerEntry& e) { return e.id == id; }),
        timers_.end());
}

Result<void, std::string> EventLoop::poll_once(int timeout_ms) {
    if (epoll_fd_ < 0) {
        return Result<void, std::string>::error("event loop not initialized");
    }

    // Compute minimum timeout based on pending timers
    uint64_t now = now_ms();
    int effective_timeout = timeout_ms;
    for (const auto& timer : timers_) {
        if (timer.next_fire_ms <= now) {
            effective_timeout = 0;
            break;
        }
        int delta = static_cast<int>(timer.next_fire_ms - now);
        if (delta < effective_timeout) effective_timeout = delta;
    }

    constexpr int kMaxEvents = 64;
    struct epoll_event events[kMaxEvents];
    int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, effective_timeout);
    if (n < 0) {
        if (errno == EINTR) return Result<void, std::string>::ok();  // Signal, not fatal
        return Result<void, std::string>::error(
            std::string("epoll_wait failed: ") + std::strerror(errno));
    }

    // Dispatch fd events
    for (int i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        if (auto it = fd_map_.find(fd); it != fd_map_.end()) {
            it->second.callback(fd, events[i].events);
        }
    }

    // Fire timers
    now = now_ms();
    for (auto& timer : timers_) {
        if (timer.next_fire_ms <= now) {
            timer.callback();
            // Reschedule
            timer.next_fire_ms = now + timer.interval_ms;
        }
    }

    return Result<void, std::string>::ok();
}

uint64_t EventLoop::now_ms() const {
    auto tp = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(tp).count());
}

} // namespace straylight::agent
```

---

### Task 4: Implement agent_daemon

**Files:** `bin/agent/agent_daemon.h`, `bin/agent/agent_daemon.cpp`, `bin/agent/main.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/agent/agent_daemon.h
#pragma once

#include <straylight/daemon.h>
#include <straylight/error.h>
#include <straylight/result.h>

#include "event_loop.h"
#include "task_queue.h"

#include <memory>
#include <string>

namespace straylight::agent {

/// Agent daemon: accepts task submissions over a Unix domain socket,
/// queues them by priority, and distributes to workers (Chunk 2).
class AgentDaemon : public DaemonBase {
public:
    AgentDaemon();
    ~AgentDaemon() override;

    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    /// Direct API for submitting tasks (used by tests and IPC handler).
    Result<uint64_t, SLError> submit_task(TaskType type, Priority prio,
                                           std::string payload);

    /// Cancel a pending task.
    Result<void, SLError> cancel_task(uint64_t task_id);

    /// Get queue depth for monitoring.
    [[nodiscard]] size_t queue_depth() const;

    /// Access the task queue (for worker_pool integration in Chunk 2).
    TaskQueue& task_queue() { return queue_; }

private:
    Result<void, SLError> setup_listener(const std::string& socket_path);
    void handle_client(int client_fd);
    void process_command(int client_fd, const std::string& data);

    EventLoop loop_;
    TaskQueue queue_;
    int listen_fd_ = -1;
    uint64_t next_task_id_ = 1;
    std::string socket_path_;
};

} // namespace straylight::agent
```

- [ ] **Step 2: Implementation**

```cpp
// bin/agent/agent_daemon.cpp
#include "agent_daemon.h"

#include <straylight/log.h>

#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace straylight::agent {

AgentDaemon::AgentDaemon() : queue_(4096) {}

AgentDaemon::~AgentDaemon() {
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

Result<void, SLError> AgentDaemon::init(const Config& cfg) {
    // Initialize event loop
    auto lr = loop_.init();
    if (!lr.has_value()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal, lr.error()});
    }

    // Set up listening socket
    socket_path_ = cfg.get("agent.socket",
                            "/run/straylight/agent.sock");
    auto sr = setup_listener(socket_path_);
    if (!sr.has_value()) return sr;

    // Add a periodic timer for queue stats logging (every 10 seconds)
    loop_.add_timer(10000, [this]() {
        SL_DEBUG("agent: queue depth = {}", queue_.size());
    });

    SL_INFO("agent: listening on {}", socket_path_);
    return Result<void, SLError>::ok();
}

Result<void, SLError> AgentDaemon::tick() {
    auto r = loop_.poll_once(100);
    if (!r.has_value()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal, r.error()});
    }
    return Result<void, SLError>::ok();
}

void AgentDaemon::shutdown() {
    SL_INFO("agent: shutting down, draining {} tasks", queue_.size());
    loop_.request_stop();
    if (listen_fd_ >= 0) {
        loop_.remove_fd(listen_fd_);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    ::unlink(socket_path_.c_str());

    // Drain remaining tasks and log them
    auto remaining = queue_.drain();
    for (const auto& t : remaining) {
        SL_WARN("agent: dropped task id={} type={} on shutdown",
                t.id, static_cast<int>(t.type));
    }
}

Result<uint64_t, SLError> AgentDaemon::submit_task(TaskType type, Priority prio,
                                                     std::string payload) {
    uint64_t id = next_task_id_++;
    Task task{
        .id = id,
        .priority = prio,
        .type = type,
        .payload = std::move(payload),
    };
    auto r = queue_.push(std::move(task));
    if (!r.has_value()) {
        return Result<uint64_t, SLError>::error(
            SLError{SLErrorCode::Internal, r.error()});
    }
    SL_DEBUG("agent: queued task id={} prio={}", id, static_cast<int>(prio));
    return Result<uint64_t, SLError>::ok(id);
}

Result<void, SLError> AgentDaemon::cancel_task(uint64_t task_id) {
    if (queue_.cancel(task_id)) {
        return Result<void, SLError>::ok();
    }
    return Result<void, SLError>::error(
        SLError{SLErrorCode::NotFound,
                "task " + std::to_string(task_id) + " not found"});
}

size_t AgentDaemon::queue_depth() const {
    return queue_.size();
}

Result<void, SLError> AgentDaemon::setup_listener(const std::string& path) {
    // Remove stale socket
    ::unlink(path.c_str());

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError,
                    std::string("socket() failed: ") + std::strerror(errno)});
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError,
                    std::string("bind() failed: ") + std::strerror(errno)});
    }

    if (::listen(listen_fd_, 32) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError,
                    std::string("listen() failed: ") + std::strerror(errno)});
    }

    // Register accept handler
    auto accept_r = loop_.add_fd(listen_fd_, EPOLLIN,
        [this](int /*fd*/, uint32_t /*events*/) {
            struct sockaddr_un peer{};
            socklen_t len = sizeof(peer);
            int client = ::accept4(listen_fd_,
                reinterpret_cast<struct sockaddr*>(&peer), &len,
                SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (client < 0) return;

            // Register the client fd for reading
            loop_.add_fd(client, EPOLLIN,
                [this, client](int fd, uint32_t ev) {
                    if (ev & (EPOLLHUP | EPOLLERR)) {
                        loop_.remove_fd(fd);
                        ::close(fd);
                        return;
                    }
                    handle_client(fd);
                });
        });

    if (!accept_r.has_value()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal, accept_r.error()});
    }
    return Result<void, SLError>::ok();
}

void AgentDaemon::handle_client(int client_fd) {
    char buf[4096];
    ssize_t n = ::read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) {
        loop_.remove_fd(client_fd);
        ::close(client_fd);
        return;
    }
    buf[n] = '\0';
    process_command(client_fd, std::string(buf, static_cast<size_t>(n)));
}

void AgentDaemon::process_command(int client_fd, const std::string& data) {
    // Protocol: JSON messages {"cmd":"submit"|"cancel"|"status", ...}
    nlohmann::json response;
    try {
        auto j = nlohmann::json::parse(data);
        std::string cmd = j.value("cmd", "");

        if (cmd == "submit") {
            auto type_str = j.value("type", "inference");
            TaskType type = TaskType::Inference;
            if (type_str == "training") type = TaskType::Training;
            else if (type_str == "preprocess") type = TaskType::Preprocess;
            else if (type_str == "compile") type = TaskType::Compile;
            else if (type_str == "quantize") type = TaskType::Quantize;

            auto prio_str = j.value("priority", "normal");
            Priority prio = Priority::Normal;
            if (prio_str == "low") prio = Priority::Low;
            else if (prio_str == "high") prio = Priority::High;
            else if (prio_str == "critical") prio = Priority::Critical;

            std::string payload = j.value("payload", "{}");
            auto r = submit_task(type, prio, std::move(payload));
            if (r.has_value()) {
                response = {{"ok", true}, {"task_id", r.value()}};
            } else {
                response = {{"ok", false}, {"error", r.error().message()}};
            }
        } else if (cmd == "cancel") {
            uint64_t task_id = j.value("task_id", uint64_t{0});
            auto r = cancel_task(task_id);
            response = {{"ok", r.has_value()}};
            if (!r.has_value()) response["error"] = r.error().message();
        } else if (cmd == "status") {
            response = {{"ok", true}, {"queue_depth", queue_depth()}};
        } else {
            response = {{"ok", false}, {"error", "unknown command: " + cmd}};
        }
    } catch (const nlohmann::json::exception& e) {
        response = {{"ok", false}, {"error", std::string("parse error: ") + e.what()}};
    }

    std::string resp_str = response.dump() + "\n";
    // Best-effort write; non-blocking socket may partial-write
    ::write(client_fd, resp_str.data(), resp_str.size());
}

} // namespace straylight::agent
```

- [ ] **Step 3: main.cpp**

```cpp
// bin/agent/main.cpp
#include <straylight/config.h>
#include <straylight/log.h>

#include "agent_daemon.h"

int main(int argc, char* argv[]) {
    using namespace straylight;
    auto cfg = Config::from_file("/etc/straylight/agent.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-agent", cfg.log_level());
    agent::AgentDaemon daemon;
    return daemon.run(cfg);
}
```

---

### Task 5: systemd service file

**File:** `etc/systemd/system/straylight-agent.service`

- [ ] **Step 1: Create service file**

```ini
[Unit]
Description=StrayLight ML Agent Task Distributor
Documentation=https://straylight.dev/docs/agent
After=straylight-scheduler.service straylight-registry.service
Wants=straylight-scheduler.service straylight-registry.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-agent
Restart=on-failure
RestartSec=2s
User=root
RuntimeDirectory=straylight
LimitNOFILE=65536
WatchdogSec=30s

[Install]
WantedBy=multi-user.target
```

---

### Task 6: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_agent_queue` → all 6 pass
- [ ] `git add bin/agent/main.cpp bin/agent/agent_daemon.h bin/agent/agent_daemon.cpp bin/agent/event_loop.h bin/agent/event_loop.cpp bin/agent/task_queue.h bin/agent/task_queue.cpp bin/agent/CMakeLists.txt tests/unit/subsystems/test_agent_queue.cpp etc/systemd/system/straylight-agent.service`
- [ ] `git commit -m "feat(agent): implement straylight-agent event loop and task queue"`

---

## Chunk 2: straylight-agent — Worker Pool + Distribution

Extends the agent daemon with a thread pool that dequeues tasks and dispatches them to local or remote workers. Distribution strategy uses round-robin with load-aware weighting.

### File Structure

```
bin/agent/
├── worker_pool.h
├── worker_pool.cpp
├── distribution.h
└── distribution.cpp
tests/unit/subsystems/
├── test_agent_worker.cpp
└── test_agent_distribution.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_agent_worker.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_agent_worker.cpp
#include <gtest/gtest.h>
#include "worker_pool.h"
#include "task_queue.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace straylight::agent;

TEST(WorkerPool, CreateAndShutdown) {
    WorkerPool pool(4);
    EXPECT_EQ(pool.num_workers(), 4u);
    EXPECT_EQ(pool.active_tasks(), 0u);
    pool.shutdown();
}

TEST(WorkerPool, ExecuteTask) {
    WorkerPool pool(2);
    std::atomic<int> counter{0};
    pool.set_handler([&](const Task& t) -> Result<void, std::string> {
        counter.fetch_add(1);
        return Result<void, std::string>::ok();
    });

    TaskQueue q(128);
    q.push(Task{.id = 1, .priority = Priority::Normal,
                 .type = TaskType::Inference, .payload = "test"});
    q.push(Task{.id = 2, .priority = Priority::Normal,
                 .type = TaskType::Inference, .payload = "test2"});

    pool.dispatch_from(q);
    // Give workers time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter.load(), 2);
    EXPECT_EQ(q.size(), 0u);
    pool.shutdown();
}

TEST(WorkerPool, HandlerErrorDoesNotCrash) {
    WorkerPool pool(1);
    std::atomic<int> attempts{0};
    pool.set_handler([&](const Task&) -> Result<void, std::string> {
        attempts.fetch_add(1);
        return Result<void, std::string>::error("simulated failure");
    });

    TaskQueue q(128);
    q.push(Task{.id = 1, .priority = Priority::Normal,
                 .type = TaskType::Inference, .payload = "fail"});
    pool.dispatch_from(q);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_GE(attempts.load(), 1);
    pool.shutdown();
}

TEST(WorkerPool, CompletedCount) {
    WorkerPool pool(2);
    pool.set_handler([](const Task&) -> Result<void, std::string> {
        return Result<void, std::string>::ok();
    });

    TaskQueue q(128);
    for (uint64_t i = 1; i <= 10; ++i) {
        q.push(Task{.id = i, .priority = Priority::Normal,
                     .type = TaskType::Inference, .payload = "x"});
    }
    pool.dispatch_from(q);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    EXPECT_EQ(pool.completed_count(), 10u);
    EXPECT_EQ(pool.failed_count(), 0u);
    pool.shutdown();
}
```

**File:** `tests/unit/subsystems/test_agent_distribution.cpp`

- [ ] **Step 2: Write distribution tests**

```cpp
// tests/unit/subsystems/test_agent_distribution.cpp
#include <gtest/gtest.h>
#include "distribution.h"

using namespace straylight::agent;

TEST(Distribution, RoundRobinEvenSpread) {
    DistributionStrategy dist;
    dist.add_endpoint(Endpoint{.id = "worker-0", .address = "local",
                                .capacity = 4, .current_load = 0});
    dist.add_endpoint(Endpoint{.id = "worker-1", .address = "local",
                                .capacity = 4, .current_load = 0});

    // 8 tasks should distribute 4/4
    std::map<std::string, int> counts;
    for (int i = 0; i < 8; ++i) {
        auto ep = dist.select_endpoint();
        ASSERT_TRUE(ep.has_value());
        counts[ep.value().id]++;
    }
    EXPECT_EQ(counts["worker-0"], 4);
    EXPECT_EQ(counts["worker-1"], 4);
}

TEST(Distribution, LoadAwareSkipsOverloaded) {
    DistributionStrategy dist;
    dist.add_endpoint(Endpoint{.id = "w0", .address = "local",
                                .capacity = 2, .current_load = 2});
    dist.add_endpoint(Endpoint{.id = "w1", .address = "local",
                                .capacity = 4, .current_load = 1});

    auto ep = dist.select_endpoint();
    ASSERT_TRUE(ep.has_value());
    EXPECT_EQ(ep.value().id, "w1");  // w0 is full, pick w1
}

TEST(Distribution, NoEndpointsReturnsError) {
    DistributionStrategy dist;
    auto ep = dist.select_endpoint();
    EXPECT_FALSE(ep.has_value());
}

TEST(Distribution, RemoveEndpoint) {
    DistributionStrategy dist;
    dist.add_endpoint(Endpoint{.id = "w0", .address = "local",
                                .capacity = 4, .current_load = 0});
    dist.remove_endpoint("w0");
    EXPECT_FALSE(dist.select_endpoint().has_value());
}

TEST(Distribution, UpdateLoad) {
    DistributionStrategy dist;
    dist.add_endpoint(Endpoint{.id = "w0", .address = "local",
                                .capacity = 4, .current_load = 0});
    dist.add_endpoint(Endpoint{.id = "w1", .address = "local",
                                .capacity = 4, .current_load = 0});

    // Make w0 fully loaded
    dist.update_load("w0", 4);
    auto ep = dist.select_endpoint();
    ASSERT_TRUE(ep.has_value());
    EXPECT_EQ(ep.value().id, "w1");
}
```

- [ ] **Step 3: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_agent_worker test_agent_worker.cpp
    ${PROJECT_SOURCE_DIR}/bin/agent/worker_pool.cpp
    ${PROJECT_SOURCE_DIR}/bin/agent/task_queue.cpp)
target_include_directories(test_agent_worker PRIVATE ${PROJECT_SOURCE_DIR}/bin/agent)
target_link_libraries(test_agent_worker PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_agent_worker)

add_executable(test_agent_distribution test_agent_distribution.cpp
    ${PROJECT_SOURCE_DIR}/bin/agent/distribution.cpp)
target_include_directories(test_agent_distribution PRIVATE ${PROJECT_SOURCE_DIR}/bin/agent)
target_link_libraries(test_agent_distribution PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_agent_distribution)
```

Run: expect 9 failures total across both test binaries.

---

### Task 2: Implement worker_pool

**Files:** `bin/agent/worker_pool.h`, `bin/agent/worker_pool.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/agent/worker_pool.h
#pragma once

#include <straylight/result.h>
#include "task_queue.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace straylight::agent {

/// Callback to process a single task. Returns ok on success, error string on failure.
using TaskHandler = std::function<Result<void, std::string>(const Task&)>;

/// Thread pool that pulls tasks from a TaskQueue and executes them.
class WorkerPool {
public:
    /// Create a pool with the given number of worker threads.
    /// Threads start immediately but idle until dispatch_from() is called.
    explicit WorkerPool(size_t num_workers);
    ~WorkerPool();

    // Non-copyable
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /// Set the handler that processes each task.
    void set_handler(TaskHandler handler);

    /// Begin dispatching tasks from the given queue.
    /// Can be called multiple times to drain additional tasks.
    void dispatch_from(TaskQueue& queue);

    /// Gracefully stop all workers. Blocks until all threads join.
    void shutdown();

    /// Number of worker threads.
    [[nodiscard]] size_t num_workers() const { return workers_.size(); }

    /// Number of tasks currently being executed.
    [[nodiscard]] size_t active_tasks() const { return active_.load(); }

    /// Total tasks completed successfully.
    [[nodiscard]] uint64_t completed_count() const { return completed_.load(); }

    /// Total tasks that returned an error.
    [[nodiscard]] uint64_t failed_count() const { return failed_.load(); }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    TaskHandler handler_;

    // Shared work queue: workers pull from here
    std::mutex work_mu_;
    std::condition_variable work_cv_;
    std::vector<Task> pending_;
    bool stop_ = false;

    std::atomic<size_t> active_{0};
    std::atomic<uint64_t> completed_{0};
    std::atomic<uint64_t> failed_{0};
};

} // namespace straylight::agent
```

- [ ] **Step 2: Implementation**

```cpp
// bin/agent/worker_pool.cpp
#include "worker_pool.h"

#include <spdlog/spdlog.h>

namespace straylight::agent {

WorkerPool::WorkerPool(size_t num_workers) {
    workers_.reserve(num_workers);
    for (size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back([this]() { worker_loop(); });
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::set_handler(TaskHandler handler) {
    handler_ = std::move(handler);
}

void WorkerPool::dispatch_from(TaskQueue& queue) {
    // Transfer all tasks from the priority queue into the internal work queue
    auto tasks = queue.drain();
    if (tasks.empty()) return;

    {
        std::lock_guard lock(work_mu_);
        for (auto& t : tasks) {
            pending_.push_back(std::move(t));
        }
    }
    work_cv_.notify_all();
}

void WorkerPool::shutdown() {
    {
        std::lock_guard lock(work_mu_);
        if (stop_) return;  // Already shut down
        stop_ = true;
    }
    work_cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void WorkerPool::worker_loop() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(work_mu_);
            work_cv_.wait(lock, [this]() {
                return stop_ || !pending_.empty();
            });
            if (stop_ && pending_.empty()) return;
            if (pending_.empty()) continue;
            task = std::move(pending_.front());
            pending_.erase(pending_.begin());
        }

        active_.fetch_add(1);
        if (handler_) {
            auto r = handler_(task);
            if (r.has_value()) {
                completed_.fetch_add(1);
            } else {
                failed_.fetch_add(1);
                spdlog::warn("agent: task {} failed: {}", task.id, r.error());
            }
        } else {
            spdlog::warn("agent: no handler set, dropping task {}", task.id);
            failed_.fetch_add(1);
        }
        active_.fetch_sub(1);
    }
}

} // namespace straylight::agent
```

---

### Task 3: Implement distribution

**Files:** `bin/agent/distribution.h`, `bin/agent/distribution.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/agent/distribution.h
#pragma once

#include <straylight/result.h>

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace straylight::agent {

struct Endpoint {
    std::string id;
    std::string address;   // "local" for this machine, or "host:port"
    uint32_t capacity;     // Max concurrent tasks
    uint32_t current_load; // Current tasks running
};

/// Load-aware round-robin distribution strategy.
/// Selects endpoints by rotating through available workers,
/// skipping any that are at capacity.
class DistributionStrategy {
public:
    /// Register an endpoint.
    void add_endpoint(Endpoint ep);

    /// Remove an endpoint by ID.
    void remove_endpoint(const std::string& id);

    /// Update the current load for an endpoint.
    void update_load(const std::string& id, uint32_t load);

    /// Select the next endpoint for task dispatch.
    /// Uses round-robin, skipping overloaded endpoints.
    /// Returns error if no endpoints are available.
    Result<Endpoint, std::string> select_endpoint();

    /// Number of registered endpoints.
    [[nodiscard]] size_t num_endpoints() const;

    /// Get all endpoints (for monitoring).
    [[nodiscard]] std::vector<Endpoint> endpoints() const;

private:
    mutable std::mutex mu_;
    std::vector<Endpoint> endpoints_;
    size_t rr_index_ = 0;
};

} // namespace straylight::agent
```

- [ ] **Step 2: Implementation**

```cpp
// bin/agent/distribution.cpp
#include "distribution.h"

#include <algorithm>

namespace straylight::agent {

void DistributionStrategy::add_endpoint(Endpoint ep) {
    std::lock_guard lock(mu_);
    // Replace if same ID exists
    auto it = std::find_if(endpoints_.begin(), endpoints_.end(),
        [&](const Endpoint& e) { return e.id == ep.id; });
    if (it != endpoints_.end()) {
        *it = std::move(ep);
    } else {
        endpoints_.push_back(std::move(ep));
    }
}

void DistributionStrategy::remove_endpoint(const std::string& id) {
    std::lock_guard lock(mu_);
    endpoints_.erase(
        std::remove_if(endpoints_.begin(), endpoints_.end(),
            [&](const Endpoint& e) { return e.id == id; }),
        endpoints_.end());
    if (!endpoints_.empty() && rr_index_ >= endpoints_.size()) {
        rr_index_ = 0;
    }
}

void DistributionStrategy::update_load(const std::string& id, uint32_t load) {
    std::lock_guard lock(mu_);
    auto it = std::find_if(endpoints_.begin(), endpoints_.end(),
        [&](const Endpoint& e) { return e.id == id; });
    if (it != endpoints_.end()) {
        it->current_load = load;
    }
}

Result<Endpoint, std::string> DistributionStrategy::select_endpoint() {
    std::lock_guard lock(mu_);
    if (endpoints_.empty()) {
        return Result<Endpoint, std::string>::error("no endpoints registered");
    }

    // Try round-robin, skipping overloaded endpoints.
    // Full pass = endpoints_.size() attempts.
    size_t n = endpoints_.size();
    for (size_t attempt = 0; attempt < n; ++attempt) {
        size_t idx = rr_index_ % n;
        rr_index_ = (rr_index_ + 1) % n;
        auto& ep = endpoints_[idx];
        if (ep.current_load < ep.capacity) {
            ep.current_load++;  // Optimistically increment
            return Result<Endpoint, std::string>::ok(ep);
        }
    }

    // All endpoints at capacity — pick the one with the most headroom
    // (least load relative to capacity) as a fallback
    Endpoint* best = nullptr;
    float best_ratio = 2.0f;
    for (auto& ep : endpoints_) {
        float ratio = (ep.capacity > 0)
            ? static_cast<float>(ep.current_load) / static_cast<float>(ep.capacity)
            : 1.0f;
        if (ratio < best_ratio) {
            best_ratio = ratio;
            best = &ep;
        }
    }
    if (best) {
        best->current_load++;
        return Result<Endpoint, std::string>::ok(*best);
    }

    return Result<Endpoint, std::string>::error("all endpoints overloaded");
}

size_t DistributionStrategy::num_endpoints() const {
    std::lock_guard lock(mu_);
    return endpoints_.size();
}

std::vector<Endpoint> DistributionStrategy::endpoints() const {
    std::lock_guard lock(mu_);
    return endpoints_;
}

} // namespace straylight::agent
```

---

### Task 4: Update agent CMakeLists.txt

- [ ] **Step 1: Update `bin/agent/CMakeLists.txt`**

```cmake
add_executable(straylight-agent
    main.cpp
    agent_daemon.cpp
    event_loop.cpp
    task_queue.cpp
    worker_pool.cpp
    distribution.cpp)
target_link_libraries(straylight-agent PRIVATE straylight-common straylight-ml straylight-net)
target_include_directories(straylight-agent PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-agent DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../etc/systemd/system/straylight-agent.service
        DESTINATION lib/systemd/system)
```

- [ ] **Step 2: Add `add_subdirectory(bin/agent)` to root `CMakeLists.txt`**

---

### Task 5: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_agent` → all 15 pass (6 queue + 4 worker + 5 distribution)
- [ ] `git add bin/agent/ tests/unit/subsystems/test_agent_worker.cpp tests/unit/subsystems/test_agent_distribution.cpp`
- [ ] `git commit -m "feat(agent): implement worker pool and load-aware distribution"`

---

## Chunk 3: straylight-compiler — IR (Graph, Passes, Lowering)

`bin/compiler/` — On-demand tool that reads a computation graph (from `libstraylight-ml`'s `Graph` type), runs optimization passes, and lowers to backend-specific IR. This chunk covers the compiler's internal representation and optimization passes. No `DaemonBase`; this is a CLI tool.

### File Structure

```
bin/compiler/
├── CMakeLists.txt
├── main.cpp
├── ir/
│   ├── graph.h
│   ├── graph.cpp
│   ├── passes.h
│   ├── passes.cpp
│   ├── lowering.h
│   └── lowering.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_compiler_ir.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_compiler_ir.cpp
#include <gtest/gtest.h>
#include "ir/graph.h"
#include "ir/passes.h"
#include "ir/lowering.h"

using namespace straylight::compiler;

// --- IR Graph tests ---

TEST(CompilerGraph, CreateAndAddNodes) {
    IRGraph g("test_model");
    auto inp = g.add_input("x", {1, 784}, DType::Float32);
    auto w = g.add_constant("w", {784, 128}, DType::Float32, nullptr);
    auto mm = g.add_op("MatMul", {inp, w}, "fc1");
    auto relu = g.add_op("ReLU", {mm}, "relu1");

    EXPECT_EQ(g.num_nodes(), 4u);
    EXPECT_EQ(g.node(inp).op_type, "Input");
    EXPECT_EQ(g.node(w).op_type, "Constant");
    EXPECT_EQ(g.node(mm).op_type, "MatMul");
    EXPECT_EQ(g.node(relu).op_type, "ReLU");
}

TEST(CompilerGraph, TopologicalOrder) {
    IRGraph g("topo_test");
    auto a = g.add_input("a", {1, 10}, DType::Float32);
    auto b = g.add_input("b", {1, 10}, DType::Float32);
    auto add = g.add_op("Add", {a, b}, "add");
    auto relu = g.add_op("ReLU", {add}, "relu");

    auto order = g.topological_order();
    ASSERT_EQ(order.size(), 4u);
    // Inputs must come before their consumers
    auto pos_a = std::find(order.begin(), order.end(), a);
    auto pos_b = std::find(order.begin(), order.end(), b);
    auto pos_add = std::find(order.begin(), order.end(), add);
    auto pos_relu = std::find(order.begin(), order.end(), relu);
    EXPECT_LT(pos_a, pos_add);
    EXPECT_LT(pos_b, pos_add);
    EXPECT_LT(pos_add, pos_relu);
}

TEST(CompilerGraph, ShapeInference) {
    IRGraph g("shape_test");
    auto a = g.add_input("a", {2, 3}, DType::Float32);
    auto b = g.add_input("b", {3, 4}, DType::Float32);
    auto mm = g.add_op("MatMul", {a, b}, "mm");

    auto r = g.infer_shapes();
    ASSERT_TRUE(r.has_value());
    auto& shape = g.node(mm).output_shape;
    ASSERT_EQ(shape.size(), 2u);
    EXPECT_EQ(shape[0], 2);
    EXPECT_EQ(shape[1], 4);
}

// --- Pass tests ---

TEST(CompilerPasses, ConstantFolding) {
    IRGraph g("cf_test");
    // Two constants added together should fold to one constant
    auto c1 = g.add_constant("c1", {2, 2}, DType::Float32, nullptr);
    auto c2 = g.add_constant("c2", {2, 2}, DType::Float32, nullptr);
    auto add = g.add_op("Add", {c1, c2}, "add_const");
    auto inp = g.add_input("x", {2, 2}, DType::Float32);
    auto mul = g.add_op("Mul", {inp, add}, "mul");

    size_t before = g.num_nodes();
    auto r = passes::constant_fold(g);
    ASSERT_TRUE(r.has_value());
    // The Add of two constants should be folded into a single constant
    // Resulting graph: inp, folded_const, mul → 3 nodes (removed c1, c2, add; added folded)
    EXPECT_LT(g.num_nodes(), before);
    // The mul should still exist and reference the folded constant
    auto order = g.topological_order();
    bool has_mul = false;
    for (auto id : order) {
        if (g.node(id).op_type == "Mul") has_mul = true;
    }
    EXPECT_TRUE(has_mul);
}

TEST(CompilerPasses, DeadCodeElimination) {
    IRGraph g("dce_test");
    auto inp = g.add_input("x", {1, 10}, DType::Float32);
    auto relu = g.add_op("ReLU", {inp}, "relu");
    // Dead branch: not consumed by any output
    auto dead = g.add_op("Sigmoid", {inp}, "dead_sigmoid");
    g.mark_output(relu);

    size_t before = g.num_nodes();
    auto r = passes::dead_code_elimination(g);
    ASSERT_TRUE(r.has_value());
    EXPECT_LT(g.num_nodes(), before);

    // Sigmoid should be removed
    auto order = g.topological_order();
    for (auto id : order) {
        EXPECT_NE(g.node(id).op_type, "Sigmoid");
    }
}

TEST(CompilerPasses, FuseReLU) {
    IRGraph g("fuse_test");
    auto inp = g.add_input("x", {1, 784}, DType::Float32);
    auto w = g.add_constant("w", {784, 128}, DType::Float32, nullptr);
    auto mm = g.add_op("MatMul", {inp, w}, "fc");
    auto relu = g.add_op("ReLU", {mm}, "relu");
    g.mark_output(relu);

    auto r = passes::fuse_activations(g);
    ASSERT_TRUE(r.has_value());
    // MatMul + ReLU should fuse into MatMulReLU
    auto order = g.topological_order();
    bool found_fused = false;
    for (auto id : order) {
        if (g.node(id).op_type == "MatMulReLU") found_fused = true;
    }
    EXPECT_TRUE(found_fused);
}

// --- Lowering tests ---

TEST(CompilerLowering, LowerToLinearSchedule) {
    IRGraph g("lower_test");
    auto inp = g.add_input("x", {1, 10}, DType::Float32);
    auto relu = g.add_op("ReLU", {inp}, "relu");
    auto sigmoid = g.add_op("Sigmoid", {relu}, "sig");
    g.mark_output(sigmoid);

    auto r = lower_to_schedule(g);
    ASSERT_TRUE(r.has_value());
    auto& schedule = r.value();
    EXPECT_EQ(schedule.size(), 3u);  // input, relu, sigmoid
    // Each entry should have the node and its memory allocation
    for (auto& entry : schedule) {
        EXPECT_GT(entry.node_id, 0u);  // All valid IDs (1-based after input)
    }
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_compiler_ir test_compiler_ir.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/graph.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/passes.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/lowering.cpp)
target_include_directories(test_compiler_ir PRIVATE ${PROJECT_SOURCE_DIR}/bin/compiler)
target_link_libraries(test_compiler_ir PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_compiler_ir)
```

Run: expect 7 failures.

---

### Task 2: Implement IR graph

**Files:** `bin/compiler/ir/graph.h`, `bin/compiler/ir/graph.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/compiler/ir/graph.h
#pragma once

#include <straylight/result.h>
#include <straylight/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight::compiler {

using straylight::DType;
using IRNodeId = uint32_t;

struct IRNode {
    IRNodeId id = 0;
    std::string name;
    std::string op_type;
    std::vector<IRNodeId> inputs;
    std::vector<int64_t> output_shape;
    DType dtype = DType::Float32;

    // For constants: raw data pointer (owned by IRGraph)
    const void* constant_data = nullptr;
    size_t constant_bytes = 0;

    // Metadata for optimization passes
    bool is_output = false;
    bool marked_dead = false;
};

/// Extended computation graph for the compiler IR.
/// Builds on libstraylight-ml's Graph but adds constant data,
/// shape inference, output marking, and mutation for passes.
class IRGraph {
public:
    explicit IRGraph(std::string name);
    ~IRGraph();

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] size_t num_nodes() const;

    /// Add an input placeholder.
    IRNodeId add_input(std::string name, std::vector<int64_t> shape, DType dtype);

    /// Add a constant with optional data (copied internally).
    IRNodeId add_constant(std::string name, std::vector<int64_t> shape,
                          DType dtype, const void* data);

    /// Add an operation node.
    IRNodeId add_op(std::string op_type, std::vector<IRNodeId> inputs,
                    std::string name = "");

    /// Mark a node as a graph output (used by DCE).
    void mark_output(IRNodeId id);

    /// Access a node by ID.
    [[nodiscard]] IRNode& node(IRNodeId id);
    [[nodiscard]] const IRNode& node(IRNodeId id) const;

    /// Return all node IDs in topological order.
    [[nodiscard]] std::vector<IRNodeId> topological_order() const;

    /// Get the set of output node IDs.
    [[nodiscard]] const std::set<IRNodeId>& output_nodes() const { return outputs_; }

    /// Run shape inference on all nodes.
    Result<void, std::string> infer_shapes();

    /// Remove a node by ID (used by passes). Updates references.
    void remove_node(IRNodeId id);

    /// Replace all uses of old_id with new_id.
    void replace_all_uses(IRNodeId old_id, IRNodeId new_id);

    /// Get consumers of a node.
    [[nodiscard]] std::vector<IRNodeId> consumers(IRNodeId id) const;

    /// Check if all inputs of a node are constants.
    [[nodiscard]] bool all_inputs_constant(IRNodeId id) const;

    /// Allocate constant storage and return pointer. Graph owns the memory.
    void* alloc_constant(size_t bytes);

private:
    std::string name_;
    std::unordered_map<IRNodeId, IRNode> nodes_;
    IRNodeId next_id_ = 1;
    std::set<IRNodeId> outputs_;

    // Owned constant data buffers
    std::vector<std::unique_ptr<uint8_t[]>> constant_storage_;

    Result<std::vector<int64_t>, std::string> infer_shape_for(const IRNode& node) const;
};

} // namespace straylight::compiler
```

- [ ] **Step 2: Implementation**

```cpp
// bin/compiler/ir/graph.cpp
#include "ir/graph.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <stdexcept>

namespace straylight::compiler {

IRGraph::IRGraph(std::string name) : name_(std::move(name)) {}
IRGraph::~IRGraph() = default;

size_t IRGraph::num_nodes() const {
    size_t count = 0;
    for (const auto& [id, n] : nodes_) {
        if (!n.marked_dead) ++count;
    }
    return count;
}

IRNodeId IRGraph::add_input(std::string name, std::vector<int64_t> shape, DType dtype) {
    IRNodeId id = next_id_++;
    IRNode node;
    node.id = id;
    node.name = std::move(name);
    node.op_type = "Input";
    node.output_shape = std::move(shape);
    node.dtype = dtype;
    nodes_.emplace(id, std::move(node));
    return id;
}

IRNodeId IRGraph::add_constant(std::string name, std::vector<int64_t> shape,
                                DType dtype, const void* data) {
    IRNodeId id = next_id_++;
    IRNode node;
    node.id = id;
    node.name = std::move(name);
    node.op_type = "Constant";
    node.output_shape = std::move(shape);
    node.dtype = dtype;

    if (data) {
        size_t nbytes = 1;
        for (auto s : node.output_shape) nbytes *= static_cast<size_t>(s);
        nbytes *= straylight::dtype_size(dtype);
        void* buf = alloc_constant(nbytes);
        std::memcpy(buf, data, nbytes);
        node.constant_data = buf;
        node.constant_bytes = nbytes;
    }

    nodes_.emplace(id, std::move(node));
    return id;
}

IRNodeId IRGraph::add_op(std::string op_type, std::vector<IRNodeId> inputs,
                          std::string name) {
    IRNodeId id = next_id_++;
    IRNode node;
    node.id = id;
    node.name = name.empty() ? (op_type + "_" + std::to_string(id)) : std::move(name);
    node.op_type = std::move(op_type);
    node.inputs = std::move(inputs);
    nodes_.emplace(id, std::move(node));
    return id;
}

void IRGraph::mark_output(IRNodeId id) {
    if (nodes_.contains(id)) {
        nodes_[id].is_output = true;
        outputs_.insert(id);
    }
}

IRNode& IRGraph::node(IRNodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) throw std::out_of_range("node not found: " + std::to_string(id));
    return it->second;
}

const IRNode& IRGraph::node(IRNodeId id) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) throw std::out_of_range("node not found: " + std::to_string(id));
    return it->second;
}

std::vector<IRNodeId> IRGraph::topological_order() const {
    // Kahn's algorithm
    std::unordered_map<IRNodeId, int> in_degree;
    std::vector<IRNodeId> all_ids;
    for (const auto& [id, n] : nodes_) {
        if (n.marked_dead) continue;
        all_ids.push_back(id);
        if (!in_degree.contains(id)) in_degree[id] = 0;
        for (auto inp : n.inputs) {
            if (nodes_.contains(inp) && !nodes_.at(inp).marked_dead) {
                in_degree[id]++;
            }
        }
    }

    std::queue<IRNodeId> ready;
    for (auto id : all_ids) {
        if (in_degree[id] == 0) ready.push(id);
    }

    std::vector<IRNodeId> order;
    order.reserve(all_ids.size());
    while (!ready.empty()) {
        IRNodeId cur = ready.front();
        ready.pop();
        order.push_back(cur);
        // For each node that depends on cur, decrement in-degree
        for (auto id : all_ids) {
            const auto& n = nodes_.at(id);
            for (auto inp : n.inputs) {
                if (inp == cur) {
                    if (--in_degree[id] == 0) ready.push(id);
                    break;
                }
            }
        }
    }
    return order;
}

std::vector<IRNodeId> IRGraph::consumers(IRNodeId id) const {
    std::vector<IRNodeId> result;
    for (const auto& [nid, n] : nodes_) {
        if (n.marked_dead) continue;
        for (auto inp : n.inputs) {
            if (inp == id) {
                result.push_back(nid);
                break;
            }
        }
    }
    return result;
}

bool IRGraph::all_inputs_constant(IRNodeId id) const {
    const auto& n = nodes_.at(id);
    for (auto inp : n.inputs) {
        if (!nodes_.contains(inp)) return false;
        const auto& in_node = nodes_.at(inp);
        if (in_node.op_type != "Constant") return false;
    }
    return !n.inputs.empty();
}

void IRGraph::remove_node(IRNodeId id) {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        it->second.marked_dead = true;
        outputs_.erase(id);
    }
}

void IRGraph::replace_all_uses(IRNodeId old_id, IRNodeId new_id) {
    for (auto& [nid, n] : nodes_) {
        if (n.marked_dead) continue;
        for (auto& inp : n.inputs) {
            if (inp == old_id) inp = new_id;
        }
    }
    if (outputs_.contains(old_id)) {
        outputs_.erase(old_id);
        outputs_.insert(new_id);
        nodes_[new_id].is_output = true;
    }
}

void* IRGraph::alloc_constant(size_t bytes) {
    auto buf = std::make_unique<uint8_t[]>(bytes);
    std::memset(buf.get(), 0, bytes);
    void* ptr = buf.get();
    constant_storage_.push_back(std::move(buf));
    return ptr;
}

Result<void, std::string> IRGraph::infer_shapes() {
    auto order = topological_order();
    for (auto id : order) {
        auto& n = nodes_[id];
        if (n.op_type == "Input" || n.op_type == "Constant") continue;
        auto r = infer_shape_for(n);
        if (!r.has_value()) return Result<void, std::string>::error(r.error());
        n.output_shape = std::move(r).value();
    }
    return Result<void, std::string>::ok();
}

Result<std::vector<int64_t>, std::string> IRGraph::infer_shape_for(const IRNode& n) const {
    if (n.inputs.empty()) {
        return Result<std::vector<int64_t>, std::string>::error(
            "cannot infer shape for op with no inputs: " + n.name);
    }

    const auto& first_shape = nodes_.at(n.inputs[0]).output_shape;

    if (n.op_type == "MatMul" || n.op_type == "MatMulReLU") {
        if (n.inputs.size() < 2) {
            return Result<std::vector<int64_t>, std::string>::error(
                "MatMul requires 2 inputs");
        }
        const auto& a = nodes_.at(n.inputs[0]).output_shape;
        const auto& b = nodes_.at(n.inputs[1]).output_shape;
        if (a.size() < 2 || b.size() < 2) {
            return Result<std::vector<int64_t>, std::string>::error(
                "MatMul inputs must be at least 2D");
        }
        // [M, K] x [K, N] → [M, N]
        std::vector<int64_t> out = a;
        out.back() = b.back();
        return Result<std::vector<int64_t>, std::string>::ok(std::move(out));
    }

    if (n.op_type == "Conv2d") {
        // Simplified: assume same spatial dims (padding=same)
        return Result<std::vector<int64_t>, std::string>::ok(first_shape);
    }

    // Element-wise ops: ReLU, Sigmoid, Add, Mul, Sub, Tanh, etc.
    // Output shape = first input shape (broadcast not yet supported)
    return Result<std::vector<int64_t>, std::string>::ok(first_shape);
}

} // namespace straylight::compiler
```

---

### Task 3: Implement optimization passes

**Files:** `bin/compiler/ir/passes.h`, `bin/compiler/ir/passes.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/compiler/ir/passes.h
#pragma once

#include <straylight/result.h>
#include "ir/graph.h"

#include <string>

namespace straylight::compiler::passes {

/// Fold operations where all inputs are constants into a single constant.
/// E.g., Add(Const, Const) → Const.
Result<void, std::string> constant_fold(IRGraph& g);

/// Remove nodes that are not reachable from any output.
/// Requires at least one output to be marked via mark_output().
Result<void, std::string> dead_code_elimination(IRGraph& g);

/// Fuse element-wise activations into preceding ops.
/// MatMul + ReLU → MatMulReLU, Conv2d + ReLU → Conv2dReLU.
Result<void, std::string> fuse_activations(IRGraph& g);

/// Common subexpression elimination: merge duplicate ops.
Result<void, std::string> cse(IRGraph& g);

/// Run all passes in canonical order.
Result<void, std::string> run_all(IRGraph& g);

} // namespace straylight::compiler::passes
```

- [ ] **Step 2: Implementation**

```cpp
// bin/compiler/ir/passes.cpp
#include "ir/passes.h"

#include <cmath>
#include <cstring>
#include <set>
#include <unordered_set>

namespace straylight::compiler::passes {

// --- Constant folding ---

static void fold_elementwise(IRGraph& g, IRNodeId op_id) {
    auto& op = g.node(op_id);
    if (op.inputs.size() < 2) return;

    const auto& a_node = g.node(op.inputs[0]);
    const auto& b_node = g.node(op.inputs[1]);
    if (!a_node.constant_data || !b_node.constant_data) return;
    if (a_node.output_shape != b_node.output_shape) return;
    if (a_node.dtype != DType::Float32 || b_node.dtype != DType::Float32) return;

    size_t numel = 1;
    for (auto s : a_node.output_shape) numel *= static_cast<size_t>(s);
    size_t nbytes = numel * sizeof(float);

    void* buf = g.alloc_constant(nbytes);
    auto* out = static_cast<float*>(buf);
    const auto* a = static_cast<const float*>(a_node.constant_data);
    const auto* b = static_cast<const float*>(b_node.constant_data);

    if (op.op_type == "Add") {
        for (size_t i = 0; i < numel; ++i) out[i] = a[i] + b[i];
    } else if (op.op_type == "Sub") {
        for (size_t i = 0; i < numel; ++i) out[i] = a[i] - b[i];
    } else if (op.op_type == "Mul") {
        for (size_t i = 0; i < numel; ++i) out[i] = a[i] * b[i];
    } else {
        return;  // Unsupported op for folding
    }

    // Create folded constant
    auto folded_id = g.add_constant("folded_" + op.name, a_node.output_shape,
                                     DType::Float32, buf);
    // Replace all uses of this op with the folded constant
    g.replace_all_uses(op_id, folded_id);
    g.remove_node(op_id);
    // Also remove the consumed constants if they have no other consumers
    if (g.consumers(op.inputs[0]).empty()) g.remove_node(op.inputs[0]);
    if (g.consumers(op.inputs[1]).empty()) g.remove_node(op.inputs[1]);
}

static void fold_unary(IRGraph& g, IRNodeId op_id) {
    auto& op = g.node(op_id);
    if (op.inputs.size() != 1) return;

    const auto& in_node = g.node(op.inputs[0]);
    if (!in_node.constant_data) return;
    if (in_node.dtype != DType::Float32) return;

    size_t numel = 1;
    for (auto s : in_node.output_shape) numel *= static_cast<size_t>(s);
    size_t nbytes = numel * sizeof(float);

    void* buf = g.alloc_constant(nbytes);
    auto* out = static_cast<float*>(buf);
    const auto* a = static_cast<const float*>(in_node.constant_data);

    if (op.op_type == "ReLU") {
        for (size_t i = 0; i < numel; ++i) out[i] = a[i] > 0.f ? a[i] : 0.f;
    } else if (op.op_type == "Sigmoid") {
        for (size_t i = 0; i < numel; ++i) out[i] = 1.f / (1.f + std::exp(-a[i]));
    } else if (op.op_type == "Tanh") {
        for (size_t i = 0; i < numel; ++i) out[i] = std::tanh(a[i]);
    } else {
        return;
    }

    auto folded_id = g.add_constant("folded_" + op.name, in_node.output_shape,
                                     DType::Float32, buf);
    g.replace_all_uses(op_id, folded_id);
    g.remove_node(op_id);
    if (g.consumers(op.inputs[0]).empty()) g.remove_node(op.inputs[0]);
}

Result<void, std::string> constant_fold(IRGraph& g) {
    // Iterate in topological order; repeat until no more folds
    bool changed = true;
    int max_iters = 100;
    while (changed && max_iters-- > 0) {
        changed = false;
        auto order = g.topological_order();
        for (auto id : order) {
            const auto& n = g.node(id);
            if (n.marked_dead) continue;
            if (n.op_type == "Input" || n.op_type == "Constant") continue;

            if (!g.all_inputs_constant(id)) continue;

            size_t before = g.num_nodes();
            if (n.inputs.size() == 2) {
                fold_elementwise(g, id);
            } else if (n.inputs.size() == 1) {
                fold_unary(g, id);
            }
            if (g.num_nodes() < before) {
                changed = true;
                break;  // Restart iteration after mutation
            }
        }
    }
    return Result<void, std::string>::ok();
}

// --- Dead code elimination ---

Result<void, std::string> dead_code_elimination(IRGraph& g) {
    auto& outputs = g.output_nodes();
    if (outputs.empty()) {
        return Result<void, std::string>::ok();  // Nothing marked as output, skip
    }

    // BFS backward from outputs to find live nodes
    std::unordered_set<IRNodeId> live;
    std::queue<IRNodeId> worklist;
    for (auto out_id : outputs) {
        worklist.push(out_id);
        live.insert(out_id);
    }
    while (!worklist.empty()) {
        auto id = worklist.front();
        worklist.pop();
        const auto& n = g.node(id);
        for (auto inp : n.inputs) {
            if (!live.contains(inp)) {
                live.insert(inp);
                worklist.push(inp);
            }
        }
    }

    // Remove all non-live nodes
    auto order = g.topological_order();
    for (auto id : order) {
        if (!live.contains(id)) {
            g.remove_node(id);
        }
    }
    return Result<void, std::string>::ok();
}

// --- Activation fusion ---

Result<void, std::string> fuse_activations(IRGraph& g) {
    // Patterns: {preceding_op, activation} → fused_op
    struct FusePattern {
        std::string base_op;
        std::string activation;
        std::string fused_op;
    };
    std::vector<FusePattern> patterns = {
        {"MatMul", "ReLU", "MatMulReLU"},
        {"MatMul", "Sigmoid", "MatMulSigmoid"},
        {"Conv2d", "ReLU", "Conv2dReLU"},
        {"Conv2d", "Sigmoid", "Conv2dSigmoid"},
    };

    bool changed = true;
    int max_iters = 100;
    while (changed && max_iters-- > 0) {
        changed = false;
        auto order = g.topological_order();
        for (auto id : order) {
            auto& n = g.node(id);
            if (n.marked_dead || n.inputs.size() != 1) continue;

            auto& producer = g.node(n.inputs[0]);
            if (producer.marked_dead) continue;

            for (const auto& pat : patterns) {
                if (producer.op_type == pat.base_op && n.op_type == pat.activation) {
                    // Check the producer has only one consumer (this activation)
                    auto cons = g.consumers(producer.id);
                    if (cons.size() != 1) continue;

                    // Fuse: change producer's op_type, remove activation node
                    producer.op_type = pat.fused_op;
                    g.replace_all_uses(id, producer.id);
                    g.remove_node(id);
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }
    return Result<void, std::string>::ok();
}

// --- CSE ---

Result<void, std::string> cse(IRGraph& g) {
    // Hash ops by (op_type, inputs) and merge duplicates
    auto order = g.topological_order();

    struct OpKey {
        std::string op_type;
        std::vector<IRNodeId> inputs;
        bool operator==(const OpKey& o) const {
            return op_type == o.op_type && inputs == o.inputs;
        }
    };
    struct OpKeyHash {
        size_t operator()(const OpKey& k) const {
            size_t h = std::hash<std::string>{}(k.op_type);
            for (auto id : k.inputs) h ^= std::hash<uint32_t>{}(id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_map<OpKey, IRNodeId, OpKeyHash> seen;
    for (auto id : order) {
        const auto& n = g.node(id);
        if (n.marked_dead) continue;
        if (n.op_type == "Input" || n.op_type == "Constant") continue;

        OpKey key{n.op_type, n.inputs};
        if (auto it = seen.find(key); it != seen.end()) {
            g.replace_all_uses(id, it->second);
            g.remove_node(id);
        } else {
            seen[key] = id;
        }
    }
    return Result<void, std::string>::ok();
}

// --- Run all ---

Result<void, std::string> run_all(IRGraph& g) {
    auto r = constant_fold(g);
    if (!r.has_value()) return r;
    r = cse(g);
    if (!r.has_value()) return r;
    r = fuse_activations(g);
    if (!r.has_value()) return r;
    r = dead_code_elimination(g);
    return r;
}

} // namespace straylight::compiler::passes
```

---

### Task 4: Implement lowering

**Files:** `bin/compiler/ir/lowering.h`, `bin/compiler/ir/lowering.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/compiler/ir/lowering.h
#pragma once

#include <straylight/result.h>
#include "ir/graph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace straylight::compiler {

/// A scheduled operation with memory allocation metadata.
struct ScheduleEntry {
    IRNodeId node_id;
    std::string op_type;
    std::vector<int64_t> output_shape;
    size_t output_bytes;       // Bytes needed for this op's output buffer
    size_t workspace_bytes;    // Scratch memory needed (e.g., for GEMM)
    int memory_slot;           // Assigned memory slot for buffer reuse
};

/// Lower the IR graph into a linear execution schedule.
/// Shape inference must have been run before calling this.
/// Returns entries in execution order with memory slot assignments.
Result<std::vector<ScheduleEntry>, std::string> lower_to_schedule(IRGraph& g);

/// Estimate workspace bytes needed for an operation.
size_t estimate_workspace(const IRNode& node);

/// Perform memory planning: assign memory slots to schedule entries,
/// reusing slots where lifetimes don't overlap.
void assign_memory_slots(std::vector<ScheduleEntry>& schedule, const IRGraph& g);

} // namespace straylight::compiler
```

- [ ] **Step 2: Implementation**

```cpp
// bin/compiler/ir/lowering.cpp
#include "ir/lowering.h"

#include <straylight/types.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace straylight::compiler {

size_t estimate_workspace(const IRNode& node) {
    if (node.op_type == "MatMul" || node.op_type == "MatMulReLU") {
        // GEMM workspace: typically a fraction of output size for tiling
        size_t numel = 1;
        for (auto s : node.output_shape) numel *= static_cast<size_t>(s);
        return numel * straylight::dtype_size(node.dtype);  // 1x output for workspace
    }
    if (node.op_type == "Conv2d" || node.op_type == "Conv2dReLU") {
        // im2col workspace
        size_t numel = 1;
        for (auto s : node.output_shape) numel *= static_cast<size_t>(s);
        return numel * straylight::dtype_size(node.dtype) * 2;
    }
    return 0;  // Element-wise ops need no workspace
}

void assign_memory_slots(std::vector<ScheduleEntry>& schedule, const IRGraph& g) {
    // Compute last-use index for each node
    std::unordered_map<IRNodeId, size_t> last_use;
    for (size_t i = 0; i < schedule.size(); ++i) {
        auto& entry = schedule[i];
        // This node's output is "used" at its own index at minimum
        last_use[entry.node_id] = i;
        // Check all subsequent entries for uses of this node
        const auto& node = g.node(entry.node_id);
        (void)node;  // inputs are on the IRNode
    }
    // Second pass: for each entry, check if any later entry depends on it
    for (size_t i = 0; i < schedule.size(); ++i) {
        const auto& node = g.node(schedule[i].node_id);
        for (auto inp_id : node.inputs) {
            if (last_use.contains(inp_id)) {
                last_use[inp_id] = std::max(last_use[inp_id], i);
            }
        }
    }

    // Greedy slot assignment: reuse slots whose last_use < current index
    struct Slot {
        int id;
        size_t capacity;
        size_t last_use_idx;
    };
    std::vector<Slot> slots;
    int next_slot = 0;

    for (size_t i = 0; i < schedule.size(); ++i) {
        auto& entry = schedule[i];
        size_t needed = entry.output_bytes;

        // Find a free slot with sufficient capacity
        int assigned = -1;
        for (auto& slot : slots) {
            if (slot.last_use_idx < i && slot.capacity >= needed) {
                assigned = slot.id;
                slot.last_use_idx = last_use.value_or(entry.node_id, i);
                slot.capacity = std::max(slot.capacity, needed);
                break;
            }
        }
        if (assigned < 0) {
            assigned = next_slot++;
            size_t lu = i;
            if (last_use.contains(entry.node_id)) lu = last_use[entry.node_id];
            slots.push_back(Slot{assigned, needed, lu});
        }
        entry.memory_slot = assigned;
    }
}

Result<std::vector<ScheduleEntry>, std::string> lower_to_schedule(IRGraph& g) {
    // Run shape inference first
    auto sr = g.infer_shapes();
    if (!sr.has_value()) return Result<std::vector<ScheduleEntry>, std::string>::error(sr.error());

    auto order = g.topological_order();
    std::vector<ScheduleEntry> schedule;
    schedule.reserve(order.size());

    for (auto id : order) {
        const auto& node = g.node(id);
        if (node.marked_dead) continue;

        size_t numel = 1;
        for (auto s : node.output_shape) numel *= static_cast<size_t>(s);
        size_t output_bytes = numel * straylight::dtype_size(node.dtype);

        ScheduleEntry entry{
            .node_id = id,
            .op_type = node.op_type,
            .output_shape = node.output_shape,
            .output_bytes = output_bytes,
            .workspace_bytes = estimate_workspace(node),
            .memory_slot = -1,
        };
        schedule.push_back(std::move(entry));
    }

    assign_memory_slots(schedule, g);

    return Result<std::vector<ScheduleEntry>, std::string>::ok(std::move(schedule));
}

} // namespace straylight::compiler
```

---

### Task 5: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_compiler_ir` → all 7 pass
- [ ] `git add bin/compiler/ir/ tests/unit/subsystems/test_compiler_ir.cpp`
- [ ] `git commit -m "feat(compiler): implement IR graph, optimization passes, and lowering"`

---
