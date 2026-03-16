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

## Chunk 4: straylight-compiler — Backends (CUDA, ROCm, CPU) + Cache

Extends the compiler with code generation backends that take a lowered schedule and emit kernel code or dispatch to runtime APIs. The cache module avoids redundant compilations by hashing the graph topology + shapes.

### File Structure

```
bin/compiler/
├── backends/
│   ├── backend.h
│   ├── cuda.h
│   ├── cuda.cpp
│   ├── rocm.h
│   ├── rocm.cpp
│   ├── cpu.h
│   └── cpu.cpp
├── cache.h
├── cache.cpp
└── main.cpp
tests/unit/subsystems/
└── test_compiler_backends.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_compiler_backends.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_compiler_backends.cpp
#include <gtest/gtest.h>
#include "backends/backend.h"
#include "backends/cpu.h"
#include "backends/cuda.h"
#include "backends/rocm.h"
#include "cache.h"
#include "ir/graph.h"
#include "ir/lowering.h"
#include "ir/passes.h"

using namespace straylight::compiler;

// --- CPU backend ---

TEST(CpuBackend, GenerateSimpleKernel) {
    IRGraph g("cpu_test");
    auto inp = g.add_input("x", {1, 10}, DType::Float32);
    auto relu = g.add_op("ReLU", {inp}, "relu");
    g.mark_output(relu);

    auto sched = lower_to_schedule(g);
    ASSERT_TRUE(sched.has_value());

    CpuBackend cpu;
    auto code = cpu.codegen(g, sched.value());
    ASSERT_TRUE(code.has_value());
    EXPECT_FALSE(code.value().empty());
    // Should contain a ReLU kernel
    EXPECT_NE(code.value().find("relu"), std::string::npos);
}

TEST(CpuBackend, GenerateMatMulKernel) {
    IRGraph g("matmul_test");
    auto a = g.add_input("a", {4, 8}, DType::Float32);
    auto b = g.add_input("b", {8, 3}, DType::Float32);
    auto mm = g.add_op("MatMul", {a, b}, "mm");
    g.mark_output(mm);

    auto sched = lower_to_schedule(g);
    ASSERT_TRUE(sched.has_value());

    CpuBackend cpu;
    auto code = cpu.codegen(g, sched.value());
    ASSERT_TRUE(code.has_value());
    EXPECT_NE(code.value().find("matmul"), std::string::npos);
}

// --- CUDA backend ---

TEST(CudaBackend, GeneratePTXStub) {
    IRGraph g("cuda_test");
    auto inp = g.add_input("x", {32, 128}, DType::Float32);
    auto relu = g.add_op("ReLU", {inp}, "relu");
    g.mark_output(relu);

    auto sched = lower_to_schedule(g);
    ASSERT_TRUE(sched.has_value());

    CudaBackend cuda;
    auto code = cuda.codegen(g, sched.value());
    ASSERT_TRUE(code.has_value());
    // Should contain CUDA kernel syntax
    EXPECT_NE(code.value().find("__global__"), std::string::npos);
}

// --- ROCm backend ---

TEST(RocmBackend, GenerateHIPKernel) {
    IRGraph g("rocm_test");
    auto inp = g.add_input("x", {16, 64}, DType::Float32);
    auto sigmoid = g.add_op("Sigmoid", {inp}, "sig");
    g.mark_output(sigmoid);

    auto sched = lower_to_schedule(g);
    ASSERT_TRUE(sched.has_value());

    RocmBackend rocm;
    auto code = rocm.codegen(g, sched.value());
    ASSERT_TRUE(code.has_value());
    EXPECT_NE(code.value().find("__global__"), std::string::npos);
    EXPECT_NE(code.value().find("hipLaunchKernelGGL"), std::string::npos);
}

// --- Cache ---

TEST(CompilerCache, CacheHitOnSameGraph) {
    CompilerCache cache("/tmp/straylight_compiler_test_cache");
    cache.clear();

    IRGraph g("cache_test");
    auto inp = g.add_input("x", {1, 10}, DType::Float32);
    auto relu = g.add_op("ReLU", {inp}, "relu");
    g.mark_output(relu);

    std::string key = cache.compute_key(g, "cpu");
    EXPECT_FALSE(cache.lookup(key).has_value());  // Miss

    cache.store(key, "generated_code_here");
    auto hit = cache.lookup(key);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit.value(), "generated_code_here");
}

TEST(CompilerCache, DifferentGraphsDifferentKeys) {
    CompilerCache cache("/tmp/straylight_compiler_test_cache2");
    cache.clear();

    IRGraph g1("a");
    g1.add_input("x", {1, 10}, DType::Float32);
    IRGraph g2("b");
    g2.add_input("x", {1, 20}, DType::Float32);

    std::string k1 = cache.compute_key(g1, "cpu");
    std::string k2 = cache.compute_key(g2, "cpu");
    EXPECT_NE(k1, k2);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_compiler_backends test_compiler_backends.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/graph.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/passes.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/lowering.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/cpu.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/cuda.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/rocm.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/cache.cpp)
target_include_directories(test_compiler_backends PRIVATE ${PROJECT_SOURCE_DIR}/bin/compiler)
target_link_libraries(test_compiler_backends PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_compiler_backends)
```

Run: expect 6 failures.

---

### Task 2: Implement backend interface and CPU backend

**Files:** `bin/compiler/backends/backend.h`, `bin/compiler/backends/cpu.h`, `bin/compiler/backends/cpu.cpp`

- [ ] **Step 1: Backend interface**

```cpp
// bin/compiler/backends/backend.h
#pragma once

#include <straylight/result.h>
#include "ir/graph.h"
#include "ir/lowering.h"

#include <string>
#include <vector>

namespace straylight::compiler {

/// Abstract backend that generates code from a lowered schedule.
class Backend {
public:
    virtual ~Backend() = default;

    /// The target name (e.g., "cpu", "cuda", "rocm").
    [[nodiscard]] virtual std::string target_name() const = 0;

    /// Generate code string from the graph and its lowered schedule.
    virtual Result<std::string, std::string> codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) = 0;
};

} // namespace straylight::compiler
```

- [ ] **Step 2: CPU header**

```cpp
// bin/compiler/backends/cpu.h
#pragma once

#include "backends/backend.h"

namespace straylight::compiler {

/// CPU backend: generates C++ kernel source code with vectorized loops.
class CpuBackend : public Backend {
public:
    [[nodiscard]] std::string target_name() const override { return "cpu"; }

    Result<std::string, std::string> codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) override;

private:
    std::string emit_relu(const IRNode& node, int slot_in, int slot_out) const;
    std::string emit_sigmoid(const IRNode& node, int slot_in, int slot_out) const;
    std::string emit_tanh(const IRNode& node, int slot_in, int slot_out) const;
    std::string emit_matmul(const IRNode& node, const IRGraph& g,
                             int slot_a, int slot_b, int slot_out) const;
    std::string emit_elementwise_binary(const IRNode& node,
                                         int slot_a, int slot_b, int slot_out) const;
};

} // namespace straylight::compiler
```

- [ ] **Step 3: CPU implementation**

```cpp
// bin/compiler/backends/cpu.cpp
#include "backends/cpu.h"

#include <sstream>
#include <unordered_map>

namespace straylight::compiler {

static size_t numel_of(const std::vector<int64_t>& shape) {
    size_t n = 1;
    for (auto s : shape) n *= static_cast<size_t>(s);
    return n;
}

Result<std::string, std::string> CpuBackend::codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) {
    std::ostringstream out;
    out << "// Auto-generated CPU kernels for graph: " << graph.name() << "\n";
    out << "#include <cmath>\n#include <cstddef>\n#include <cstring>\n\n";
    out << "namespace straylight::runtime {\n\n";

    // Emit memory pool declaration
    int max_slot = 0;
    for (const auto& e : schedule) {
        if (e.memory_slot > max_slot) max_slot = e.memory_slot;
    }
    out << "// Memory pool: " << (max_slot + 1) << " slots\n";
    out << "static thread_local float* slots[" << (max_slot + 1) << "];\n\n";

    // Build node→slot map
    std::unordered_map<IRNodeId, int> slot_map;
    for (const auto& e : schedule) slot_map[e.node_id] = e.memory_slot;

    // Emit kernels for each scheduled op
    for (const auto& entry : schedule) {
        const auto& node = graph.node(entry.node_id);
        if (node.op_type == "Input" || node.op_type == "Constant") {
            out << "// " << node.name << ": slot " << entry.memory_slot
                << " (external data)\n\n";
            continue;
        }

        int out_slot = entry.memory_slot;

        if (node.op_type == "ReLU") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << emit_relu(node, in_slot, out_slot);
        } else if (node.op_type == "Sigmoid") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << emit_sigmoid(node, in_slot, out_slot);
        } else if (node.op_type == "Tanh") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << emit_tanh(node, in_slot, out_slot);
        } else if (node.op_type == "MatMul" || node.op_type == "MatMulReLU") {
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            out << emit_matmul(node, graph, slot_a, slot_b, out_slot);
        } else if (node.op_type == "Add" || node.op_type == "Mul" || node.op_type == "Sub") {
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            out << emit_elementwise_binary(node, slot_a, slot_b, out_slot);
        } else {
            out << "// Unsupported op: " << node.op_type << " (" << node.name << ")\n\n";
        }
    }

    out << "} // namespace straylight::runtime\n";
    return Result<std::string, std::string>::ok(out.str());
}

std::string CpuBackend::emit_relu(const IRNode& node, int slot_in, int slot_out) const {
    size_t n = numel_of(node.output_shape);
    std::ostringstream s;
    s << "// relu: " << node.name << "\n";
    s << "inline void kernel_" << node.name << "() {\n";
    s << "    const float* in = slots[" << slot_in << "];\n";
    s << "    float* out = slots[" << slot_out << "];\n";
    s << "    for (size_t i = 0; i < " << n << "; ++i) {\n";
    s << "        out[i] = in[i] > 0.f ? in[i] : 0.f;\n";
    s << "    }\n}\n\n";
    return s.str();
}

std::string CpuBackend::emit_sigmoid(const IRNode& node, int slot_in, int slot_out) const {
    size_t n = numel_of(node.output_shape);
    std::ostringstream s;
    s << "// sigmoid: " << node.name << "\n";
    s << "inline void kernel_" << node.name << "() {\n";
    s << "    const float* in = slots[" << slot_in << "];\n";
    s << "    float* out = slots[" << slot_out << "];\n";
    s << "    for (size_t i = 0; i < " << n << "; ++i) {\n";
    s << "        out[i] = 1.f / (1.f + std::exp(-in[i]));\n";
    s << "    }\n}\n\n";
    return s.str();
}

std::string CpuBackend::emit_tanh(const IRNode& node, int slot_in, int slot_out) const {
    size_t n = numel_of(node.output_shape);
    std::ostringstream s;
    s << "// tanh: " << node.name << "\n";
    s << "inline void kernel_" << node.name << "() {\n";
    s << "    const float* in = slots[" << slot_in << "];\n";
    s << "    float* out = slots[" << slot_out << "];\n";
    s << "    for (size_t i = 0; i < " << n << "; ++i) {\n";
    s << "        out[i] = std::tanh(in[i]);\n";
    s << "    }\n}\n\n";
    return s.str();
}

std::string CpuBackend::emit_matmul(const IRNode& node, const IRGraph& g,
                                      int slot_a, int slot_b, int slot_out) const {
    const auto& a_shape = g.node(node.inputs[0]).output_shape;
    const auto& b_shape = g.node(node.inputs[1]).output_shape;
    int64_t M = a_shape[a_shape.size() - 2];
    int64_t K = a_shape[a_shape.size() - 1];
    int64_t N = b_shape[b_shape.size() - 1];

    bool fused_relu = (node.op_type == "MatMulReLU");

    std::ostringstream s;
    s << "// matmul: " << node.name << " [" << M << "x" << K << "] x ["
      << K << "x" << N << "]" << (fused_relu ? " + ReLU" : "") << "\n";
    s << "inline void kernel_" << node.name << "() {\n";
    s << "    const float* A = slots[" << slot_a << "];\n";
    s << "    const float* B = slots[" << slot_b << "];\n";
    s << "    float* C = slots[" << slot_out << "];\n";
    s << "    std::memset(C, 0, " << (M * N) << " * sizeof(float));\n";
    s << "    for (int64_t m = 0; m < " << M << "; ++m) {\n";
    s << "        for (int64_t k = 0; k < " << K << "; ++k) {\n";
    s << "            float a_mk = A[m * " << K << " + k];\n";
    s << "            for (int64_t n = 0; n < " << N << "; ++n) {\n";
    s << "                C[m * " << N << " + n] += a_mk * B[k * " << N << " + n];\n";
    s << "            }\n";
    s << "        }\n";
    if (fused_relu) {
        s << "        for (int64_t n = 0; n < " << N << "; ++n) {\n";
        s << "            float& v = C[m * " << N << " + n];\n";
        s << "            if (v < 0.f) v = 0.f;\n";
        s << "        }\n";
    }
    s << "    }\n}\n\n";
    return s.str();
}

std::string CpuBackend::emit_elementwise_binary(const IRNode& node,
                                                  int slot_a, int slot_b, int slot_out) const {
    size_t n = numel_of(node.output_shape);
    std::string op_char;
    if (node.op_type == "Add") op_char = "+";
    else if (node.op_type == "Sub") op_char = "-";
    else if (node.op_type == "Mul") op_char = "*";
    else op_char = "+";

    std::ostringstream s;
    s << "// " << node.op_type << ": " << node.name << "\n";
    s << "inline void kernel_" << node.name << "() {\n";
    s << "    const float* a = slots[" << slot_a << "];\n";
    s << "    const float* b = slots[" << slot_b << "];\n";
    s << "    float* out = slots[" << slot_out << "];\n";
    s << "    for (size_t i = 0; i < " << n << "; ++i) {\n";
    s << "        out[i] = a[i] " << op_char << " b[i];\n";
    s << "    }\n}\n\n";
    return s.str();
}

} // namespace straylight::compiler
```

---

### Task 3: Implement CUDA and ROCm backends

**Files:** `bin/compiler/backends/cuda.h`, `bin/compiler/backends/cuda.cpp`, `bin/compiler/backends/rocm.h`, `bin/compiler/backends/rocm.cpp`

- [ ] **Step 1: CUDA header**

```cpp
// bin/compiler/backends/cuda.h
#pragma once

#include "backends/backend.h"

namespace straylight::compiler {

/// CUDA backend: generates PTX-style kernel source code.
/// Does not require NVCC at compile time; produces source strings
/// that can be JIT-compiled via CUDA Driver API at runtime.
class CudaBackend : public Backend {
public:
    [[nodiscard]] std::string target_name() const override { return "cuda"; }

    Result<std::string, std::string> codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) override;
};

} // namespace straylight::compiler
```

- [ ] **Step 2: CUDA implementation**

```cpp
// bin/compiler/backends/cuda.cpp
#include "backends/cuda.h"

#include <sstream>
#include <unordered_map>

namespace straylight::compiler {

static size_t numel_of(const std::vector<int64_t>& shape) {
    size_t n = 1;
    for (auto s : shape) n *= static_cast<size_t>(s);
    return n;
}

Result<std::string, std::string> CudaBackend::codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) {
    std::ostringstream out;
    out << "// Auto-generated CUDA kernels for graph: " << graph.name() << "\n";
    out << "#include <cstddef>\n\n";

    std::unordered_map<IRNodeId, int> slot_map;
    for (const auto& e : schedule) slot_map[e.node_id] = e.memory_slot;

    for (const auto& entry : schedule) {
        const auto& node = graph.node(entry.node_id);
        if (node.op_type == "Input" || node.op_type == "Constant") continue;

        size_t n = numel_of(node.output_shape);
        int out_slot = entry.memory_slot;

        if (node.op_type == "ReLU") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << "__global__ void kernel_relu_" << node.name
                << "(const float* __restrict__ in, float* __restrict__ out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = in[idx] > 0.f ? in[idx] : 0.f;\n";
            out << "}\n";
            out << "// Launch: kernel_relu_" << node.name << "<<<("
                << n << "+255)/256, 256>>>(slots[" << in_slot
                << "], slots[" << out_slot << "], " << n << ");\n\n";
        } else if (node.op_type == "Sigmoid") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << "__global__ void kernel_sigmoid_" << node.name
                << "(const float* __restrict__ in, float* __restrict__ out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = 1.f / (1.f + expf(-in[idx]));\n";
            out << "}\n";
            out << "// Launch: kernel_sigmoid_" << node.name << "<<<("
                << n << "+255)/256, 256>>>(slots[" << in_slot
                << "], slots[" << out_slot << "], " << n << ");\n\n";
        } else if (node.op_type == "MatMul" || node.op_type == "MatMulReLU") {
            const auto& a_shape = graph.node(node.inputs[0]).output_shape;
            const auto& b_shape = graph.node(node.inputs[1]).output_shape;
            int64_t M = a_shape[a_shape.size() - 2];
            int64_t K = a_shape[a_shape.size() - 1];
            int64_t N_dim = b_shape[b_shape.size() - 1];
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            bool fused = (node.op_type == "MatMulReLU");

            out << "__global__ void kernel_matmul_" << node.name
                << "(const float* A, const float* B, float* C,\n"
                << "    int M, int K, int N) {\n";
            out << "    int row = blockIdx.y * blockDim.y + threadIdx.y;\n";
            out << "    int col = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (row < M && col < N) {\n";
            out << "        float sum = 0.f;\n";
            out << "        for (int k = 0; k < K; ++k)\n";
            out << "            sum += A[row * K + k] * B[k * N + col];\n";
            if (fused) {
                out << "        C[row * N + col] = sum > 0.f ? sum : 0.f;\n";
            } else {
                out << "        C[row * N + col] = sum;\n";
            }
            out << "    }\n}\n";
            out << "// Launch: dim3 block(16,16); dim3 grid(("
                << N_dim << "+15)/16, (" << M << "+15)/16);\n";
            out << "// kernel_matmul_" << node.name << "<<<grid, block>>>(slots["
                << slot_a << "], slots[" << slot_b << "], slots["
                << out_slot << "], " << M << ", " << K << ", " << N_dim << ");\n\n";
        } else if (node.op_type == "Add" || node.op_type == "Mul" || node.op_type == "Sub") {
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            std::string op = (node.op_type == "Add") ? "+" :
                             (node.op_type == "Sub") ? "-" : "*";
            out << "__global__ void kernel_" << node.op_type << "_" << node.name
                << "(const float* a, const float* b, float* out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = a[idx] " << op << " b[idx];\n";
            out << "}\n";
            out << "// Launch: <<<(" << n << "+255)/256, 256>>>(slots["
                << slot_a << "], slots[" << slot_b << "], slots["
                << out_slot << "], " << n << ");\n\n";
        }
    }

    return Result<std::string, std::string>::ok(out.str());
}

} // namespace straylight::compiler
```

- [ ] **Step 3: ROCm header**

```cpp
// bin/compiler/backends/rocm.h
#pragma once

#include "backends/backend.h"

namespace straylight::compiler {

/// ROCm/HIP backend: generates HIP kernel source code.
class RocmBackend : public Backend {
public:
    [[nodiscard]] std::string target_name() const override { return "rocm"; }

    Result<std::string, std::string> codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) override;
};

} // namespace straylight::compiler
```

- [ ] **Step 4: ROCm implementation**

```cpp
// bin/compiler/backends/rocm.cpp
#include "backends/rocm.h"

#include <sstream>
#include <unordered_map>

namespace straylight::compiler {

static size_t numel_of(const std::vector<int64_t>& shape) {
    size_t n = 1;
    for (auto s : shape) n *= static_cast<size_t>(s);
    return n;
}

Result<std::string, std::string> RocmBackend::codegen(
        const IRGraph& graph,
        const std::vector<ScheduleEntry>& schedule) {
    std::ostringstream out;
    out << "// Auto-generated HIP kernels for graph: " << graph.name() << "\n";
    out << "#include <hip/hip_runtime.h>\n#include <cstddef>\n\n";

    std::unordered_map<IRNodeId, int> slot_map;
    for (const auto& e : schedule) slot_map[e.node_id] = e.memory_slot;

    for (const auto& entry : schedule) {
        const auto& node = graph.node(entry.node_id);
        if (node.op_type == "Input" || node.op_type == "Constant") continue;

        size_t n = numel_of(node.output_shape);
        int out_slot = entry.memory_slot;

        if (node.op_type == "ReLU") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << "__global__ void kernel_relu_" << node.name
                << "(const float* __restrict__ in, float* __restrict__ out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = in[idx] > 0.f ? in[idx] : 0.f;\n";
            out << "}\n";
            out << "// hipLaunchKernelGGL(kernel_relu_" << node.name
                << ", dim3((" << n << "+255)/256), dim3(256), 0, 0, slots["
                << in_slot << "], slots[" << out_slot << "], " << n << ");\n\n";
        } else if (node.op_type == "Sigmoid") {
            int in_slot = slot_map.at(node.inputs[0]);
            out << "__global__ void kernel_sigmoid_" << node.name
                << "(const float* __restrict__ in, float* __restrict__ out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = 1.f / (1.f + expf(-in[idx]));\n";
            out << "}\n";
            out << "// hipLaunchKernelGGL(kernel_sigmoid_" << node.name
                << ", dim3((" << n << "+255)/256), dim3(256), 0, 0, slots["
                << in_slot << "], slots[" << out_slot << "], " << n << ");\n\n";
        } else if (node.op_type == "MatMul" || node.op_type == "MatMulReLU") {
            const auto& a_shape = graph.node(node.inputs[0]).output_shape;
            const auto& b_shape = graph.node(node.inputs[1]).output_shape;
            int64_t M = a_shape[a_shape.size() - 2];
            int64_t K = a_shape[a_shape.size() - 1];
            int64_t N_dim = b_shape[b_shape.size() - 1];
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            bool fused = (node.op_type == "MatMulReLU");

            out << "__global__ void kernel_matmul_" << node.name
                << "(const float* A, const float* B, float* C,\n"
                << "    int M, int K, int N) {\n";
            out << "    int row = blockIdx.y * blockDim.y + threadIdx.y;\n";
            out << "    int col = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (row < M && col < N) {\n";
            out << "        float sum = 0.f;\n";
            out << "        for (int k = 0; k < K; ++k)\n";
            out << "            sum += A[row * K + k] * B[k * N + col];\n";
            if (fused) {
                out << "        C[row * N + col] = sum > 0.f ? sum : 0.f;\n";
            } else {
                out << "        C[row * N + col] = sum;\n";
            }
            out << "    }\n}\n";
            out << "// hipLaunchKernelGGL(kernel_matmul_" << node.name
                << ", dim3((" << N_dim << "+15)/16, (" << M << "+15)/16), dim3(16,16), 0, 0, slots["
                << slot_a << "], slots[" << slot_b << "], slots["
                << out_slot << "], " << M << ", " << K << ", " << N_dim << ");\n\n";
        } else if (node.op_type == "Add" || node.op_type == "Mul" || node.op_type == "Sub") {
            int slot_a = slot_map.at(node.inputs[0]);
            int slot_b = slot_map.at(node.inputs[1]);
            std::string op = (node.op_type == "Add") ? "+" :
                             (node.op_type == "Sub") ? "-" : "*";
            out << "__global__ void kernel_" << node.op_type << "_" << node.name
                << "(const float* a, const float* b, float* out, size_t N) {\n";
            out << "    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
            out << "    if (idx < N) out[idx] = a[idx] " << op << " b[idx];\n";
            out << "}\n";
            out << "// hipLaunchKernelGGL(kernel_" << node.op_type << "_" << node.name
                << ", dim3((" << n << "+255)/256), dim3(256), 0, 0, slots["
                << slot_a << "], slots[" << slot_b << "], slots["
                << out_slot << "], " << n << ");\n\n";
        }
    }

    return Result<std::string, std::string>::ok(out.str());
}

} // namespace straylight::compiler
```

---

### Task 4: Implement cache

**Files:** `bin/compiler/cache.h`, `bin/compiler/cache.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/compiler/cache.h
#pragma once

#include <straylight/result.h>
#include "ir/graph.h"

#include <filesystem>
#include <optional>
#include <string>

namespace straylight::compiler {

/// Filesystem-backed compilation cache.
/// Keys are SHA-256 hashes of (graph topology + shapes + target).
/// Values are generated code strings stored as files.
class CompilerCache {
public:
    explicit CompilerCache(std::string cache_dir);

    /// Compute a cache key for the given graph + target combo.
    [[nodiscard]] std::string compute_key(const IRGraph& graph,
                                           const std::string& target) const;

    /// Look up a cached result. Returns nullopt on miss.
    [[nodiscard]] std::optional<std::string> lookup(const std::string& key) const;

    /// Store a compiled result in the cache.
    void store(const std::string& key, const std::string& code);

    /// Remove all cached entries.
    void clear();

    /// Number of cached entries.
    [[nodiscard]] size_t size() const;

private:
    std::filesystem::path cache_dir_;
    std::filesystem::path key_to_path(const std::string& key) const;
};

} // namespace straylight::compiler
```

- [ ] **Step 2: Implementation**

```cpp
// bin/compiler/cache.cpp
#include "cache.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>

namespace straylight::compiler {

// Simple FNV-1a 64-bit hash (no external crypto dependency needed for cache keys)
static uint64_t fnv1a(const std::string& data) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : data) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::string to_hex(uint64_t val) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(val));
    return std::string(buf);
}

CompilerCache::CompilerCache(std::string cache_dir)
    : cache_dir_(std::move(cache_dir)) {
    std::filesystem::create_directories(cache_dir_);
}

std::string CompilerCache::compute_key(const IRGraph& graph,
                                        const std::string& target) const {
    // Build a canonical representation of the graph topology
    std::ostringstream repr;
    repr << "graph:" << graph.name() << "|target:" << target << "|";
    auto order = graph.topological_order();
    for (auto id : order) {
        const auto& n = graph.node(id);
        repr << n.op_type << "(";
        for (size_t i = 0; i < n.inputs.size(); ++i) {
            if (i > 0) repr << ",";
            repr << n.inputs[i];
        }
        repr << ")[";
        for (size_t i = 0; i < n.output_shape.size(); ++i) {
            if (i > 0) repr << "x";
            repr << n.output_shape[i];
        }
        repr << "]" << static_cast<int>(n.dtype) << ";";
    }

    uint64_t h1 = fnv1a(repr.str());
    // Double-hash for collision resistance
    uint64_t h2 = fnv1a(repr.str() + std::to_string(h1));
    return to_hex(h1) + to_hex(h2);
}

std::filesystem::path CompilerCache::key_to_path(const std::string& key) const {
    // Two-level directory structure to avoid too many files in one dir
    return cache_dir_ / key.substr(0, 2) / (key + ".cc");
}

std::optional<std::string> CompilerCache::lookup(const std::string& key) const {
    auto path = key_to_path(key);
    if (!std::filesystem::exists(path)) return std::nullopt;

    std::ifstream f(path);
    if (!f.is_open()) return std::nullopt;

    std::string content{std::istreambuf_iterator<char>(f), {}};
    return content;
}

void CompilerCache::store(const std::string& key, const std::string& code) {
    auto path = key_to_path(key);
    std::filesystem::create_directories(path.parent_path());

    std::ofstream f(path);
    f << code;
}

void CompilerCache::clear() {
    if (std::filesystem::exists(cache_dir_)) {
        std::filesystem::remove_all(cache_dir_);
        std::filesystem::create_directories(cache_dir_);
    }
}

size_t CompilerCache::size() const {
    size_t count = 0;
    if (!std::filesystem::exists(cache_dir_)) return 0;
    for (auto& entry : std::filesystem::recursive_directory_iterator(cache_dir_)) {
        if (entry.is_regular_file()) ++count;
    }
    return count;
}

} // namespace straylight::compiler
```

---

### Task 5: Implement compiler main.cpp

**File:** `bin/compiler/main.cpp`

- [ ] **Step 1: Create main**

```cpp
// bin/compiler/main.cpp
// straylight-compiler: on-demand graph compilation tool
// Usage: straylight-compiler --input <graph.json> --target <cpu|cuda|rocm> [--output <file>]

#include <straylight/config.h>
#include <straylight/error.h>
#include <straylight/log.h>

#include "backends/cpu.h"
#include "backends/cuda.h"
#include "backends/rocm.h"
#include "cache.h"
#include "ir/graph.h"
#include "ir/lowering.h"
#include "ir/passes.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

using namespace straylight;
using namespace straylight::compiler;

static void print_usage() {
    std::cerr << "Usage: straylight-compiler [OPTIONS]\n"
              << "  --input  <file>   Input graph JSON file\n"
              << "  --target <name>   Target backend: cpu, cuda, rocm\n"
              << "  --output <file>   Output file (default: stdout)\n"
              << "  --no-cache        Disable compilation cache\n"
              << "  --cache-dir <dir> Cache directory (default: /var/cache/straylight/compiler)\n"
              << "  --no-opt          Skip optimization passes\n"
              << "  --help            Show this help\n";
}

static Result<IRGraph, std::string> load_graph_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        return Result<IRGraph, std::string>::error("cannot open file: " + path);
    }

    try {
        auto j = nlohmann::json::parse(f);
        std::string name = j.value("name", "unnamed");
        IRGraph graph(name);

        // Parse nodes
        std::unordered_map<std::string, IRNodeId> name_to_id;
        for (const auto& node_json : j["nodes"]) {
            std::string nname = node_json.value("name", "");
            std::string op = node_json.value("op", "");
            std::vector<int64_t> shape;
            if (node_json.contains("shape")) {
                shape = node_json["shape"].get<std::vector<int64_t>>();
            }
            DType dtype = DType::Float32;
            if (node_json.contains("dtype")) {
                int dt = node_json["dtype"].get<int>();
                dtype = static_cast<DType>(dt);
            }

            IRNodeId id;
            if (op == "Input") {
                id = graph.add_input(nname, shape, dtype);
            } else if (op == "Constant") {
                id = graph.add_constant(nname, shape, dtype, nullptr);
            } else {
                std::vector<IRNodeId> inputs;
                if (node_json.contains("inputs")) {
                    for (const auto& inp_name : node_json["inputs"]) {
                        auto it = name_to_id.find(inp_name.get<std::string>());
                        if (it == name_to_id.end()) {
                            return Result<IRGraph, std::string>::error(
                                "unknown input: " + inp_name.get<std::string>());
                        }
                        inputs.push_back(it->second);
                    }
                }
                id = graph.add_op(op, inputs, nname);
            }
            name_to_id[nname] = id;

            if (node_json.value("is_output", false)) {
                graph.mark_output(id);
            }
        }

        return Result<IRGraph, std::string>::ok(std::move(graph));
    } catch (const nlohmann::json::exception& e) {
        return Result<IRGraph, std::string>::error(
            std::string("JSON parse error: ") + e.what());
    }
}

int main(int argc, char* argv[]) {
    SL_INIT("straylight-compiler", "info");

    std::string input_path;
    std::string target = "cpu";
    std::string output_path;
    std::string cache_dir = "/var/cache/straylight/compiler";
    bool use_cache = true;
    bool run_opt = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (std::strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (std::strcmp(argv[i], "--cache-dir") == 0 && i + 1 < argc) {
            cache_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--no-cache") == 0) {
            use_cache = false;
        } else if (std::strcmp(argv[i], "--no-opt") == 0) {
            run_opt = false;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
    }

    if (input_path.empty()) {
        std::cerr << "error: --input is required\n";
        print_usage();
        return 1;
    }

    // Load graph
    auto graph_r = load_graph_json(input_path);
    if (!graph_r.has_value()) {
        SL_ERROR("compiler: {}", graph_r.error());
        return 1;
    }
    auto graph = std::move(graph_r).value();

    // Check cache
    CompilerCache cache(cache_dir);
    if (use_cache) {
        std::string key = cache.compute_key(graph, target);
        if (auto hit = cache.lookup(key)) {
            SL_INFO("compiler: cache hit for {}", key);
            if (output_path.empty()) {
                std::cout << hit.value();
            } else {
                std::ofstream out(output_path);
                out << hit.value();
            }
            return 0;
        }
    }

    // Run optimization passes
    if (run_opt) {
        auto r = passes::run_all(graph);
        if (!r.has_value()) {
            SL_ERROR("compiler: optimization failed: {}", r.error());
            return 1;
        }
    }

    // Lower to schedule
    auto sched_r = lower_to_schedule(graph);
    if (!sched_r.has_value()) {
        SL_ERROR("compiler: lowering failed: {}", sched_r.error());
        return 1;
    }

    // Select backend
    std::unique_ptr<Backend> backend;
    if (target == "cpu") {
        backend = std::make_unique<CpuBackend>();
    } else if (target == "cuda") {
        backend = std::make_unique<CudaBackend>();
    } else if (target == "rocm") {
        backend = std::make_unique<RocmBackend>();
    } else {
        SL_ERROR("compiler: unknown target: {}", target);
        return 1;
    }

    // Generate code
    auto code_r = backend->codegen(graph, sched_r.value());
    if (!code_r.has_value()) {
        SL_ERROR("compiler: codegen failed: {}", code_r.error());
        return 1;
    }

    // Store in cache
    if (use_cache) {
        std::string key = cache.compute_key(graph, target);
        cache.store(key, code_r.value());
        SL_INFO("compiler: cached result for {}", key);
    }

    // Output
    if (output_path.empty()) {
        std::cout << code_r.value();
    } else {
        std::ofstream out(output_path);
        out << code_r.value();
    }

    SL_INFO("compiler: done (target={}, {} nodes)", target, graph.num_nodes());
    return 0;
}
```

---

### Task 6: CMakeLists.txt + commit

- [ ] **Step 1: Create `bin/compiler/CMakeLists.txt`**

```cmake
add_executable(straylight-compiler
    main.cpp
    ir/graph.cpp
    ir/passes.cpp
    ir/lowering.cpp
    backends/cpu.cpp
    backends/cuda.cpp
    backends/rocm.cpp
    cache.cpp)
target_link_libraries(straylight-compiler PRIVATE straylight-common straylight-ml)
target_include_directories(straylight-compiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-compiler DESTINATION bin)
```

- [ ] **Step 2: Add `add_subdirectory(bin/compiler)` to root `CMakeLists.txt`**
- [ ] Run: `ctest --test-dir build -R test_compiler` → all 13 pass (7 IR + 6 backends)
- [ ] `git add bin/compiler/ tests/unit/subsystems/test_compiler_backends.cpp`
- [ ] `git commit -m "feat(compiler): implement CUDA/ROCm/CPU backends and compilation cache"`

---

## Chunk 5: straylight-morph — Quantize, Prune, Distill, Adapt

`bin/morph/` — On-demand tool for model transformation. Implements real quantization math (symmetric/asymmetric INT8/INT4), magnitude pruning, knowledge distillation loss, and LoRA-style adaptation. Links `libstraylight-common` + `libstraylight-ml` + `libstraylight-hw`.

### File Structure

```
bin/morph/
├── CMakeLists.txt
├── main.cpp
├── quantize.h
├── quantize.cpp
├── prune.h
├── prune.cpp
├── distill.h
├── distill.cpp
├── adapt.h
└── adapt.cpp
tests/unit/subsystems/
└── test_morph.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_morph.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_morph.cpp
#include <gtest/gtest.h>
#include "quantize.h"
#include "prune.h"
#include "distill.h"
#include "adapt.h"

#include <straylight/ml/tensor.h>

#include <cmath>
#include <numeric>

using namespace straylight;
using namespace straylight::morph;
using namespace straylight::ml;

// --- Quantization tests ---

TEST(Quantize, SymmetricInt8RoundTrip) {
    Tensor t({4}, DType::Float32);
    auto* d = t.typed_data<float>();
    d[0] = -1.0f; d[1] = 0.0f; d[2] = 0.5f; d[3] = 1.0f;

    auto r = quantize_symmetric_int8(t);
    ASSERT_TRUE(r.has_value());
    auto& [qtensor, scale] = r.value();
    EXPECT_EQ(qtensor.dtype(), DType::Int8);
    EXPECT_GT(scale, 0.0f);

    // Dequantize and check accuracy
    auto deq = dequantize_int8(qtensor, scale);
    ASSERT_TRUE(deq.has_value());
    auto* dd = deq.value().typed_data<float>();
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(dd[i], d[i], 0.02f);  // Within quantization error
    }
}

TEST(Quantize, AsymmetricInt8) {
    Tensor t({3}, DType::Float32);
    auto* d = t.typed_data<float>();
    d[0] = 0.1f; d[1] = 0.5f; d[2] = 0.9f;

    auto r = quantize_asymmetric_int8(t);
    ASSERT_TRUE(r.has_value());
    auto& [qtensor, scale, zero_point] = r.value();
    EXPECT_EQ(qtensor.dtype(), DType::Int8);
    EXPECT_GT(scale, 0.0f);

    auto deq = dequantize_int8(qtensor, scale, zero_point);
    ASSERT_TRUE(deq.has_value());
    auto* dd = deq.value().typed_data<float>();
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(dd[i], d[i], 0.02f);
    }
}

TEST(Quantize, Int4Packing) {
    Tensor t({8}, DType::Float32);
    auto* d = t.typed_data<float>();
    for (int i = 0; i < 8; ++i) d[i] = static_cast<float>(i) / 7.0f;

    auto r = quantize_int4(t);
    ASSERT_TRUE(r.has_value());
    // INT4 packs 2 values per byte
    EXPECT_EQ(r.value().packed.nbytes(), 4u);
}

// --- Pruning tests ---

TEST(Prune, MagnitudePrune50Percent) {
    Tensor t({10}, DType::Float32);
    auto* d = t.typed_data<float>();
    for (int i = 0; i < 10; ++i) d[i] = static_cast<float>(i);

    auto r = magnitude_prune(t, 0.5f);
    ASSERT_TRUE(r.has_value());
    auto& [pruned, mask] = r.value();
    auto* pd = pruned.typed_data<float>();
    auto* md = mask.typed_data<uint8_t>();

    // 50% of elements should be zero
    int zeros = 0;
    for (int i = 0; i < 10; ++i) {
        if (md[i] == 0) {
            EXPECT_EQ(pd[i], 0.0f);
            zeros++;
        }
    }
    EXPECT_EQ(zeros, 5);  // Bottom 50% by magnitude
}

TEST(Prune, StructuredPrune) {
    // 4 rows, 3 cols — prune 50% of rows (by L2 norm)
    Tensor t({4, 3}, DType::Float32);
    auto* d = t.typed_data<float>();
    // Row 0: [0,0,0], Row 1: [1,1,1], Row 2: [2,2,2], Row 3: [3,3,3]
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            d[r * 3 + c] = static_cast<float>(r);

    auto res = structured_prune(t, 0.5f, 0);  // Prune along dim 0
    ASSERT_TRUE(res.has_value());
    // Rows 0 and 1 have smallest norms → pruned
    auto& pruned = res.value();
    EXPECT_EQ(pruned.shape()[0], 2);  // 2 rows remain
}

// --- Distillation tests ---

TEST(Distill, KLDivergenceLoss) {
    // Teacher softmax output
    Tensor teacher({4}, DType::Float32);
    auto* td = teacher.typed_data<float>();
    td[0] = 0.7f; td[1] = 0.1f; td[2] = 0.1f; td[3] = 0.1f;

    // Student softmax output
    Tensor student({4}, DType::Float32);
    auto* sd = student.typed_data<float>();
    sd[0] = 0.4f; sd[1] = 0.2f; sd[2] = 0.2f; sd[3] = 0.2f;

    auto r = kl_divergence(teacher, student);
    ASSERT_TRUE(r.has_value());
    EXPECT_GT(r.value(), 0.0f);  // Non-zero divergence

    // Same distribution should give ~0
    auto r2 = kl_divergence(teacher, teacher);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NEAR(r2.value(), 0.0f, 1e-6f);
}

TEST(Distill, SoftmaxWithTemperature) {
    Tensor logits({3}, DType::Float32);
    auto* d = logits.typed_data<float>();
    d[0] = 2.0f; d[1] = 1.0f; d[2] = 0.1f;

    auto r = softmax_temperature(logits, 2.0f);
    ASSERT_TRUE(r.has_value());
    auto* out = r.value().typed_data<float>();
    // Higher temperature → more uniform
    float sum = out[0] + out[1] + out[2];
    EXPECT_NEAR(sum, 1.0f, 1e-5f);
    // With T=2, distribution should be softer than T=1
    EXPECT_LT(out[0] - out[2], 0.5f);  // Less peaked
}

// --- Adaptation (LoRA) tests ---

TEST(Adapt, LoRADecomposition) {
    // Original weight: [8, 16]
    Tensor W({8, 16}, DType::Float32);
    auto* wd = W.typed_data<float>();
    for (int i = 0; i < 128; ++i) wd[i] = static_cast<float>(i) * 0.01f;

    int rank = 4;
    auto r = lora_decompose(W, rank);
    ASSERT_TRUE(r.has_value());
    auto& [A, B] = r.value();
    // A: [8, 4], B: [4, 16]
    EXPECT_EQ(A.shape()[0], 8);
    EXPECT_EQ(A.shape()[1], rank);
    EXPECT_EQ(B.shape()[0], rank);
    EXPECT_EQ(B.shape()[1], 16);
}

TEST(Adapt, LoRAMerge) {
    Tensor W({4, 4}, DType::Float32);
    auto* wd = W.typed_data<float>();
    for (int i = 0; i < 16; ++i) wd[i] = 1.0f;

    Tensor A({4, 2}, DType::Float32);
    auto* ad = A.typed_data<float>();
    for (int i = 0; i < 8; ++i) ad[i] = 0.1f;

    Tensor B({2, 4}, DType::Float32);
    auto* bd = B.typed_data<float>();
    for (int i = 0; i < 8; ++i) bd[i] = 0.1f;

    float alpha = 1.0f;
    auto r = lora_merge(W, A, B, alpha);
    ASSERT_TRUE(r.has_value());
    // W_merged = W + alpha * A @ B
    auto* md = r.value().typed_data<float>();
    // A @ B = [4,2] x [2,4] = [4,4], each element = 2 * 0.01 = 0.02
    // So merged = 1.0 + 0.02 = 1.02
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(md[i], 1.02f, 1e-5f);
    }
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_morph test_morph.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/quantize.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/prune.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/distill.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/adapt.cpp)
target_include_directories(test_morph PRIVATE ${PROJECT_SOURCE_DIR}/bin/morph)
target_link_libraries(test_morph PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_morph)
```

Run: expect 9 failures.

---

### Task 2: Implement quantize

**Files:** `bin/morph/quantize.h`, `bin/morph/quantize.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/morph/quantize.h
#pragma once

#include <straylight/result.h>
#include <straylight/ml/tensor.h>

#include <string>
#include <tuple>

namespace straylight::morph {

using straylight::ml::Tensor;

/// Symmetric INT8 quantization result: (quantized_tensor, scale).
/// q = round(clamp(x / scale, -127, 127))
/// x_approx = q * scale
using SymmetricInt8Result = std::pair<Tensor, float>;

/// Asymmetric INT8 quantization result: (quantized_tensor, scale, zero_point).
/// q = round(clamp(x / scale + zero_point, -128, 127))
/// x_approx = (q - zero_point) * scale
struct AsymmetricInt8Result {
    Tensor qtensor;
    float scale;
    int8_t zero_point;
};

/// INT4 quantization result (2 values packed per byte).
struct Int4Result {
    Tensor packed;      // UInt8 tensor, half the original length
    float scale;
    int8_t zero_point;
};

/// Quantize a Float32 tensor to symmetric INT8.
Result<SymmetricInt8Result, std::string> quantize_symmetric_int8(const Tensor& input);

/// Quantize a Float32 tensor to asymmetric INT8.
Result<AsymmetricInt8Result, std::string> quantize_asymmetric_int8(const Tensor& input);

/// Quantize a Float32 tensor to packed INT4.
Result<Int4Result, std::string> quantize_int4(const Tensor& input);

/// Dequantize an INT8 tensor back to Float32 (symmetric: zero_point=0).
Result<Tensor, std::string> dequantize_int8(const Tensor& qtensor, float scale,
                                              int8_t zero_point = 0);

} // namespace straylight::morph
```

- [ ] **Step 2: Implementation**

```cpp
// bin/morph/quantize.cpp
#include "quantize.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace straylight::morph {

Result<SymmetricInt8Result, std::string> quantize_symmetric_int8(const Tensor& input) {
    if (input.dtype() != straylight::DType::Float32) {
        return Result<SymmetricInt8Result, std::string>::error(
            "quantize_symmetric_int8 requires Float32 input");
    }

    int64_t n = input.numel();
    const float* data = static_cast<const float*>(input.data());

    // Find max absolute value
    float max_abs = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        float a = std::fabs(data[i]);
        if (a > max_abs) max_abs = a;
    }

    // Scale: map [-max_abs, max_abs] to [-127, 127]
    float scale = (max_abs > 0.0f) ? (max_abs / 127.0f) : 1.0f;

    // Quantize
    Tensor qtensor(input.shape(), straylight::DType::Int8);
    auto* qdata = qtensor.typed_data<int8_t>();

    for (int64_t i = 0; i < n; ++i) {
        float q = std::round(data[i] / scale);
        q = std::clamp(q, -127.0f, 127.0f);
        qdata[i] = static_cast<int8_t>(q);
    }

    return Result<SymmetricInt8Result, std::string>::ok(
        SymmetricInt8Result{std::move(qtensor), scale});
}

Result<AsymmetricInt8Result, std::string> quantize_asymmetric_int8(const Tensor& input) {
    if (input.dtype() != straylight::DType::Float32) {
        return Result<AsymmetricInt8Result, std::string>::error(
            "quantize_asymmetric_int8 requires Float32 input");
    }

    int64_t n = input.numel();
    const float* data = static_cast<const float*>(input.data());

    // Find min and max
    float min_val = data[0], max_val = data[0];
    for (int64_t i = 1; i < n; ++i) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    // Scale and zero point: map [min_val, max_val] to [-128, 127]
    float scale = (max_val - min_val) / 255.0f;
    if (scale == 0.0f) scale = 1.0f;
    float zp_float = -128.0f - min_val / scale;
    int8_t zero_point = static_cast<int8_t>(std::clamp(std::round(zp_float), -128.0f, 127.0f));

    Tensor qtensor(input.shape(), straylight::DType::Int8);
    auto* qdata = qtensor.typed_data<int8_t>();

    for (int64_t i = 0; i < n; ++i) {
        float q = std::round(data[i] / scale) + static_cast<float>(zero_point);
        q = std::clamp(q, -128.0f, 127.0f);
        qdata[i] = static_cast<int8_t>(q);
    }

    return Result<AsymmetricInt8Result, std::string>::ok(
        AsymmetricInt8Result{std::move(qtensor), scale, zero_point});
}

Result<Int4Result, std::string> quantize_int4(const Tensor& input) {
    if (input.dtype() != straylight::DType::Float32) {
        return Result<Int4Result, std::string>::error(
            "quantize_int4 requires Float32 input");
    }

    int64_t n = input.numel();
    const float* data = static_cast<const float*>(input.data());

    // Find range
    float max_abs = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        float a = std::fabs(data[i]);
        if (a > max_abs) max_abs = a;
    }

    // INT4 range: [-8, 7]
    float scale = (max_abs > 0.0f) ? (max_abs / 7.0f) : 1.0f;

    // Pack 2 INT4 values per byte
    int64_t packed_bytes = (n + 1) / 2;
    Tensor packed({packed_bytes}, straylight::DType::UInt8);
    auto* pdata = packed.typed_data<uint8_t>();

    for (int64_t i = 0; i < n; i += 2) {
        float q0 = std::round(data[i] / scale);
        q0 = std::clamp(q0, -8.0f, 7.0f);
        int8_t v0 = static_cast<int8_t>(q0);

        int8_t v1 = 0;
        if (i + 1 < n) {
            float q1 = std::round(data[i + 1] / scale);
            q1 = std::clamp(q1, -8.0f, 7.0f);
            v1 = static_cast<int8_t>(q1);
        }

        // Pack: low nibble = v0, high nibble = v1 (both in 2's complement 4-bit)
        uint8_t byte = (static_cast<uint8_t>(v0 & 0x0F)) |
                       (static_cast<uint8_t>(v1 & 0x0F) << 4);
        pdata[i / 2] = byte;
    }

    return Result<Int4Result, std::string>::ok(
        Int4Result{std::move(packed), scale, 0});
}

Result<Tensor, std::string> dequantize_int8(const Tensor& qtensor, float scale,
                                              int8_t zero_point) {
    if (qtensor.dtype() != straylight::DType::Int8) {
        return Result<Tensor, std::string>::error("dequantize_int8 requires Int8 input");
    }

    int64_t n = qtensor.numel();
    Tensor output(qtensor.shape(), straylight::DType::Float32);
    auto* odata = output.typed_data<float>();
    const auto* qdata = static_cast<const int8_t*>(qtensor.data());

    for (int64_t i = 0; i < n; ++i) {
        odata[i] = (static_cast<float>(qdata[i]) - static_cast<float>(zero_point)) * scale;
    }

    return Result<Tensor, std::string>::ok(std::move(output));
}

} // namespace straylight::morph
```

---

### Task 3: Implement prune

**Files:** `bin/morph/prune.h`, `bin/morph/prune.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/morph/prune.h
#pragma once

#include <straylight/result.h>
#include <straylight/ml/tensor.h>

#include <string>
#include <utility>

namespace straylight::morph {

using straylight::ml::Tensor;

/// Unstructured magnitude pruning result: (pruned_tensor, binary_mask).
using PruneResult = std::pair<Tensor, Tensor>;

/// Prune elements below the given sparsity threshold (0.0 to 1.0).
/// E.g., sparsity=0.5 zeros out the 50% of elements with smallest magnitude.
/// Returns (pruned_tensor [Float32], mask [UInt8, 0=pruned, 1=kept]).
Result<PruneResult, std::string> magnitude_prune(const Tensor& weights, float sparsity);

/// Structured pruning: remove entire rows/columns along the given dimension.
/// sparsity fraction of rows/cols with smallest L2 norm are removed.
/// Returns a new tensor with reduced size along the pruned dimension.
Result<Tensor, std::string> structured_prune(const Tensor& weights, float sparsity,
                                               int dim);

} // namespace straylight::morph
```

- [ ] **Step 2: Implementation**

```cpp
// bin/morph/prune.cpp
#include "prune.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace straylight::morph {

Result<PruneResult, std::string> magnitude_prune(const Tensor& weights, float sparsity) {
    if (weights.dtype() != straylight::DType::Float32) {
        return Result<PruneResult, std::string>::error("magnitude_prune requires Float32");
    }
    if (sparsity < 0.0f || sparsity > 1.0f) {
        return Result<PruneResult, std::string>::error("sparsity must be in [0, 1]");
    }

    int64_t n = weights.numel();
    const float* data = static_cast<const float*>(weights.data());

    // Collect magnitudes and sort to find threshold
    std::vector<float> magnitudes(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) magnitudes[static_cast<size_t>(i)] = std::fabs(data[i]);

    std::vector<float> sorted_mags = magnitudes;
    std::sort(sorted_mags.begin(), sorted_mags.end());

    int64_t num_prune = static_cast<int64_t>(std::round(sparsity * static_cast<float>(n)));
    float threshold = (num_prune > 0 && num_prune <= n)
        ? sorted_mags[static_cast<size_t>(num_prune - 1)]
        : -1.0f;

    // Build mask and pruned tensor
    Tensor pruned(weights.shape(), straylight::DType::Float32);
    Tensor mask(weights.shape(), straylight::DType::UInt8);
    auto* pd = pruned.typed_data<float>();
    auto* md = mask.typed_data<uint8_t>();

    // To prune exactly num_prune elements, we track count
    int64_t pruned_count = 0;
    for (int64_t i = 0; i < n; ++i) {
        if (magnitudes[static_cast<size_t>(i)] <= threshold &&
            pruned_count < num_prune) {
            pd[i] = 0.0f;
            md[i] = 0;
            pruned_count++;
        } else {
            pd[i] = data[i];
            md[i] = 1;
        }
    }

    return Result<PruneResult, std::string>::ok(
        PruneResult{std::move(pruned), std::move(mask)});
}

Result<Tensor, std::string> structured_prune(const Tensor& weights, float sparsity,
                                               int dim) {
    if (weights.dtype() != straylight::DType::Float32) {
        return Result<Tensor, std::string>::error("structured_prune requires Float32");
    }
    if (weights.ndim() < 2) {
        return Result<Tensor, std::string>::error("structured_prune requires at least 2D tensor");
    }
    if (dim < 0 || dim >= static_cast<int>(weights.ndim())) {
        return Result<Tensor, std::string>::error("invalid dimension");
    }

    const auto& shape = weights.shape();
    const float* data = static_cast<const float*>(weights.data());

    // For dim=0: compute L2 norm of each row
    int64_t num_slices = shape[static_cast<size_t>(dim)];
    int64_t slice_size = 1;
    for (size_t d = static_cast<size_t>(dim) + 1; d < shape.size(); ++d) {
        slice_size *= shape[d];
    }
    int64_t outer_size = 1;
    for (int d = 0; d < dim; ++d) {
        outer_size *= shape[static_cast<size_t>(d)];
    }

    // Compute norms for each slice
    std::vector<std::pair<float, int64_t>> norms(static_cast<size_t>(num_slices));
    for (int64_t s = 0; s < num_slices; ++s) {
        float norm_sq = 0.0f;
        for (int64_t o = 0; o < outer_size; ++o) {
            for (int64_t i = 0; i < slice_size; ++i) {
                int64_t idx = o * (num_slices * slice_size) + s * slice_size + i;
                float v = data[idx];
                norm_sq += v * v;
            }
        }
        norms[static_cast<size_t>(s)] = {std::sqrt(norm_sq), s};
    }

    // Sort by norm (ascending) and determine which to keep
    std::sort(norms.begin(), norms.end());
    int64_t num_prune = static_cast<int64_t>(std::round(sparsity * static_cast<float>(num_slices)));
    int64_t num_keep = num_slices - num_prune;
    if (num_keep <= 0) num_keep = 1;

    // Collect kept indices (sorted by original index for contiguous output)
    std::vector<int64_t> kept_indices;
    kept_indices.reserve(static_cast<size_t>(num_keep));
    for (size_t i = static_cast<size_t>(num_prune); i < norms.size(); ++i) {
        kept_indices.push_back(norms[i].second);
    }
    std::sort(kept_indices.begin(), kept_indices.end());

    // Build output tensor with reduced dimension
    auto new_shape = shape;
    new_shape[static_cast<size_t>(dim)] = num_keep;
    Tensor output(std::vector<int64_t>(new_shape.begin(), new_shape.end()),
                  straylight::DType::Float32);
    auto* od = output.typed_data<float>();

    int64_t out_idx = 0;
    for (int64_t o = 0; o < outer_size; ++o) {
        for (int64_t ki = 0; ki < num_keep; ++ki) {
            int64_t s = kept_indices[static_cast<size_t>(ki)];
            for (int64_t i = 0; i < slice_size; ++i) {
                int64_t in_idx = o * (num_slices * slice_size) + s * slice_size + i;
                od[out_idx++] = data[in_idx];
            }
        }
    }

    return Result<Tensor, std::string>::ok(std::move(output));
}

} // namespace straylight::morph
```

---

### Task 4: Implement distill

**Files:** `bin/morph/distill.h`, `bin/morph/distill.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/morph/distill.h
#pragma once

#include <straylight/result.h>
#include <straylight/ml/tensor.h>

#include <string>

namespace straylight::morph {

using straylight::ml::Tensor;

/// Compute KL divergence: D_KL(teacher || student).
/// Both tensors must be probability distributions (same shape, sum to 1).
Result<float, std::string> kl_divergence(const Tensor& teacher, const Tensor& student);

/// Apply softmax with temperature scaling.
/// output_i = exp(logits_i / T) / sum(exp(logits_j / T))
Result<Tensor, std::string> softmax_temperature(const Tensor& logits, float temperature);

/// Compute distillation loss: weighted combination of hard loss and soft loss.
/// loss = alpha * CE(student_logits, labels) + (1-alpha) * T^2 * KL(teacher_soft, student_soft)
Result<float, std::string> distillation_loss(
    const Tensor& student_logits, const Tensor& teacher_logits,
    const Tensor& labels, float temperature, float alpha);

} // namespace straylight::morph
```

- [ ] **Step 2: Implementation**

```cpp
// bin/morph/distill.cpp
#include "distill.h"

#include <cmath>
#include <algorithm>

namespace straylight::morph {

Result<float, std::string> kl_divergence(const Tensor& teacher, const Tensor& student) {
    if (teacher.dtype() != straylight::DType::Float32 ||
        student.dtype() != straylight::DType::Float32) {
        return Result<float, std::string>::error("kl_divergence requires Float32 tensors");
    }
    if (teacher.numel() != student.numel()) {
        return Result<float, std::string>::error("tensor size mismatch");
    }

    int64_t n = teacher.numel();
    const float* t = static_cast<const float*>(teacher.data());
    const float* s = static_cast<const float*>(student.data());

    float kl = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        if (t[i] > 1e-10f) {
            float s_clamped = std::max(s[i], 1e-10f);
            kl += t[i] * std::log(t[i] / s_clamped);
        }
    }

    return Result<float, std::string>::ok(kl);
}

Result<Tensor, std::string> softmax_temperature(const Tensor& logits, float temperature) {
    if (logits.dtype() != straylight::DType::Float32) {
        return Result<Tensor, std::string>::error("softmax_temperature requires Float32");
    }
    if (temperature <= 0.0f) {
        return Result<Tensor, std::string>::error("temperature must be > 0");
    }

    int64_t n = logits.numel();
    const float* data = static_cast<const float*>(logits.data());

    Tensor output(logits.shape(), straylight::DType::Float32);
    auto* out = output.typed_data<float>();

    // Numerically stable softmax: subtract max before exp
    float max_val = data[0];
    for (int64_t i = 1; i < n; ++i) {
        if (data[i] > max_val) max_val = data[i];
    }

    float sum_exp = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        out[i] = std::exp((data[i] - max_val) / temperature);
        sum_exp += out[i];
    }

    for (int64_t i = 0; i < n; ++i) {
        out[i] /= sum_exp;
    }

    return Result<Tensor, std::string>::ok(std::move(output));
}

Result<float, std::string> distillation_loss(
        const Tensor& student_logits, const Tensor& teacher_logits,
        const Tensor& labels, float temperature, float alpha) {
    if (student_logits.numel() != teacher_logits.numel()) {
        return Result<float, std::string>::error("logit size mismatch");
    }

    // Soft targets
    auto teacher_soft_r = softmax_temperature(teacher_logits, temperature);
    if (!teacher_soft_r.has_value()) return Result<float, std::string>::error(teacher_soft_r.error());
    auto student_soft_r = softmax_temperature(student_logits, temperature);
    if (!student_soft_r.has_value()) return Result<float, std::string>::error(student_soft_r.error());

    auto kl_r = kl_divergence(teacher_soft_r.value(), student_soft_r.value());
    if (!kl_r.has_value()) return Result<float, std::string>::error(kl_r.error());

    float soft_loss = temperature * temperature * kl_r.value();

    // Hard loss: cross-entropy with labels (labels as one-hot Float32)
    auto student_hard_r = softmax_temperature(student_logits, 1.0f);
    if (!student_hard_r.has_value()) return Result<float, std::string>::error(student_hard_r.error());

    int64_t n = labels.numel();
    const float* lbl = static_cast<const float*>(labels.data());
    const float* pred = static_cast<const float*>(student_hard_r.value().data());

    float hard_loss = 0.0f;
    for (int64_t i = 0; i < n; ++i) {
        if (lbl[i] > 0.5f) {
            hard_loss -= std::log(std::max(pred[i], 1e-10f));
        }
    }

    float total = alpha * hard_loss + (1.0f - alpha) * soft_loss;
    return Result<float, std::string>::ok(total);
}

} // namespace straylight::morph
```

---

### Task 5: Implement adapt

**Files:** `bin/morph/adapt.h`, `bin/morph/adapt.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/morph/adapt.h
#pragma once

#include <straylight/result.h>
#include <straylight/ml/tensor.h>

#include <string>
#include <utility>

namespace straylight::morph {

using straylight::ml::Tensor;

/// LoRA decomposition: given weight matrix W [M,N] and rank r,
/// produce A [M,r] and B [r,N] via truncated SVD approximation.
/// W ≈ A @ B (not exact unless rank == min(M,N))
Result<std::pair<Tensor, Tensor>, std::string> lora_decompose(
    const Tensor& W, int rank);

/// LoRA merge: W_merged = W + alpha * (A @ B)
Result<Tensor, std::string> lora_merge(
    const Tensor& W, const Tensor& A, const Tensor& B, float alpha);

/// Apply adapter by adding a low-rank update to all weight tensors in a model.
/// This is a batch operation that calls lora_decompose on each weight.
struct AdapterConfig {
    int rank = 4;
    float alpha = 1.0f;
    float dropout = 0.0f;  // Not applied during inference
};

} // namespace straylight::morph
```

- [ ] **Step 2: Implementation**

```cpp
// bin/morph/adapt.cpp
#include "adapt.h"

#include <cmath>
#include <cstring>
#include <random>

namespace straylight::morph {

Result<std::pair<Tensor, Tensor>, std::string> lora_decompose(
        const Tensor& W, int rank) {
    if (W.dtype() != straylight::DType::Float32) {
        return Result<std::pair<Tensor, Tensor>, std::string>::error(
            "lora_decompose requires Float32");
    }
    if (W.ndim() != 2) {
        return Result<std::pair<Tensor, Tensor>, std::string>::error(
            "lora_decompose requires 2D tensor");
    }

    int64_t M = W.shape()[0];
    int64_t N = W.shape()[1];
    if (rank <= 0 || rank > std::min(M, N)) {
        return Result<std::pair<Tensor, Tensor>, std::string>::error(
            "rank must be in [1, min(M, N)]");
    }

    const float* wd = static_cast<const float*>(W.data());

    // Power iteration method for approximate truncated SVD.
    // Initialize B randomly, then iterate: A = W @ B^T, B = W^T @ A
    // (simplified: we do a few iterations to converge to the top-r singular vectors)

    std::mt19937 gen(42);  // Deterministic seed for reproducibility
    std::normal_distribution<float> dist(0.0f, 1.0f / std::sqrt(static_cast<float>(rank)));

    Tensor B({static_cast<int64_t>(rank), N}, straylight::DType::Float32);
    auto* bd = B.typed_data<float>();
    for (int64_t i = 0; i < rank * N; ++i) bd[i] = dist(gen);

    Tensor A({M, static_cast<int64_t>(rank)}, straylight::DType::Float32);
    auto* ad = A.typed_data<float>();

    // Power iterations (5 iterations is typically sufficient)
    for (int iter = 0; iter < 5; ++iter) {
        // A = W @ B^T: [M,N] @ [N,r] → [M,r]
        // B is [r,N], so B^T is [N,r]
        std::memset(ad, 0, static_cast<size_t>(M * rank) * sizeof(float));
        for (int64_t m = 0; m < M; ++m) {
            for (int64_t r = 0; r < rank; ++r) {
                float sum = 0.0f;
                for (int64_t n = 0; n < N; ++n) {
                    sum += wd[m * N + n] * bd[r * N + n];
                }
                ad[m * rank + r] = sum;
            }
        }

        // QR-like normalization of A columns (Gram-Schmidt)
        for (int64_t r = 0; r < rank; ++r) {
            // Orthogonalize against previous columns
            for (int64_t prev = 0; prev < r; ++prev) {
                float dot = 0.0f;
                for (int64_t m = 0; m < M; ++m) {
                    dot += ad[m * rank + r] * ad[m * rank + prev];
                }
                for (int64_t m = 0; m < M; ++m) {
                    ad[m * rank + r] -= dot * ad[m * rank + prev];
                }
            }
            // Normalize
            float norm = 0.0f;
            for (int64_t m = 0; m < M; ++m) {
                norm += ad[m * rank + r] * ad[m * rank + r];
            }
            norm = std::sqrt(norm);
            if (norm > 1e-10f) {
                for (int64_t m = 0; m < M; ++m) {
                    ad[m * rank + r] /= norm;
                }
            }
        }

        // B = A^T @ W: [r,M] @ [M,N] → [r,N]
        std::memset(bd, 0, static_cast<size_t>(rank * N) * sizeof(float));
        for (int64_t r = 0; r < rank; ++r) {
            for (int64_t n = 0; n < N; ++n) {
                float sum = 0.0f;
                for (int64_t m = 0; m < M; ++m) {
                    sum += ad[m * rank + r] * wd[m * N + n];
                }
                bd[r * N + n] = sum;
            }
        }
    }

    return Result<std::pair<Tensor, Tensor>, std::string>::ok(
        std::make_pair(std::move(A), std::move(B)));
}

Result<Tensor, std::string> lora_merge(
        const Tensor& W, const Tensor& A, const Tensor& B, float alpha) {
    if (W.dtype() != straylight::DType::Float32 ||
        A.dtype() != straylight::DType::Float32 ||
        B.dtype() != straylight::DType::Float32) {
        return Result<Tensor, std::string>::error("lora_merge requires Float32 tensors");
    }
    if (W.ndim() != 2 || A.ndim() != 2 || B.ndim() != 2) {
        return Result<Tensor, std::string>::error("lora_merge requires 2D tensors");
    }

    int64_t M = W.shape()[0];
    int64_t N = W.shape()[1];
    int64_t R = A.shape()[1];

    if (A.shape()[0] != M || B.shape()[0] != R || B.shape()[1] != N) {
        return Result<Tensor, std::string>::error("shape mismatch for LoRA merge");
    }

    const float* wd = static_cast<const float*>(W.data());
    const float* ad = static_cast<const float*>(A.data());
    const float* bd = static_cast<const float*>(B.data());

    Tensor output(W.shape(), straylight::DType::Float32);
    auto* od = output.typed_data<float>();

    // W_merged = W + alpha * (A @ B)
    for (int64_t m = 0; m < M; ++m) {
        for (int64_t n = 0; n < N; ++n) {
            float ab = 0.0f;
            for (int64_t r = 0; r < R; ++r) {
                ab += ad[m * R + r] * bd[r * N + n];
            }
            od[m * N + n] = wd[m * N + n] + alpha * ab;
        }
    }

    return Result<Tensor, std::string>::ok(std::move(output));
}

} // namespace straylight::morph
```

---

### Task 6: Implement morph main.cpp + CMakeLists.txt

- [ ] **Step 1: Create `bin/morph/main.cpp`**

```cpp
// bin/morph/main.cpp
// straylight-morph: on-demand model transformation tool
// Usage: straylight-morph <command> [options]
// Commands: quantize, prune, distill, adapt

#include <straylight/config.h>
#include <straylight/log.h>
#include <straylight/ml/tensor.h>

#include "quantize.h"
#include "prune.h"
#include "distill.h"
#include "adapt.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>

using namespace straylight;
using namespace straylight::morph;

static void print_usage() {
    std::cerr << "Usage: straylight-morph <command> [options]\n"
              << "Commands:\n"
              << "  quantize  --input <file> --method <sym8|asym8|int4> --output <file>\n"
              << "  prune     --input <file> --sparsity <0-1> [--structured --dim <d>] --output <file>\n"
              << "  distill   --teacher <file> --student <file> --temp <T> --alpha <a>\n"
              << "  adapt     --input <file> --rank <r> --alpha <a> --output <file>\n";
}

static Result<ml::Tensor, std::string> load_tensor_binary(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        return Result<ml::Tensor, std::string>::error("cannot open: " + path);
    }

    // Binary format: [ndim:uint32] [shape:int64*ndim] [dtype:uint8] [data:bytes]
    uint32_t ndim;
    f.read(reinterpret_cast<char*>(&ndim), sizeof(ndim));

    std::vector<int64_t> shape(ndim);
    f.read(reinterpret_cast<char*>(shape.data()),
           static_cast<std::streamsize>(ndim * sizeof(int64_t)));

    uint8_t dtype_raw;
    f.read(reinterpret_cast<char*>(&dtype_raw), sizeof(dtype_raw));
    auto dtype = static_cast<DType>(dtype_raw);

    ml::Tensor t(shape, dtype);
    f.read(static_cast<char*>(t.data()), static_cast<std::streamsize>(t.nbytes()));

    if (!f) {
        return Result<ml::Tensor, std::string>::error("truncated file: " + path);
    }
    return Result<ml::Tensor, std::string>::ok(std::move(t));
}

static void save_tensor_binary(const std::string& path, const ml::Tensor& t) {
    std::ofstream f(path, std::ios::binary);
    uint32_t ndim = static_cast<uint32_t>(t.ndim());
    f.write(reinterpret_cast<const char*>(&ndim), sizeof(ndim));
    f.write(reinterpret_cast<const char*>(t.shape().data()),
            static_cast<std::streamsize>(ndim * sizeof(int64_t)));
    uint8_t dtype_raw = static_cast<uint8_t>(t.dtype());
    f.write(reinterpret_cast<const char*>(&dtype_raw), sizeof(dtype_raw));
    f.write(static_cast<const char*>(t.data()), static_cast<std::streamsize>(t.nbytes()));
}

int main(int argc, char* argv[]) {
    SL_INIT("straylight-morph", "info");

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "quantize") {
        std::string input, output, method = "sym8";
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
            else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
            else if (std::strcmp(argv[i], "--method") == 0 && i + 1 < argc) method = argv[++i];
        }
        if (input.empty() || output.empty()) {
            std::cerr << "error: --input and --output required\n";
            return 1;
        }

        auto t_r = load_tensor_binary(input);
        if (!t_r.has_value()) { SL_ERROR("{}", t_r.error()); return 1; }

        if (method == "sym8") {
            auto r = quantize_symmetric_int8(t_r.value());
            if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
            save_tensor_binary(output, r.value().first);
            SL_INFO("morph: quantized {} -> {} (sym8, scale={})",
                    input, output, r.value().second);
        } else if (method == "asym8") {
            auto r = quantize_asymmetric_int8(t_r.value());
            if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
            save_tensor_binary(output, r.value().qtensor);
            SL_INFO("morph: quantized {} -> {} (asym8, scale={}, zp={})",
                    input, output, r.value().scale, r.value().zero_point);
        } else if (method == "int4") {
            auto r = quantize_int4(t_r.value());
            if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
            save_tensor_binary(output, r.value().packed);
            SL_INFO("morph: quantized {} -> {} (int4)", input, output);
        }
    } else if (command == "prune") {
        std::string input, output;
        float sparsity = 0.5f;
        bool structured = false;
        int dim = 0;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
            else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
            else if (std::strcmp(argv[i], "--sparsity") == 0 && i + 1 < argc) sparsity = std::stof(argv[++i]);
            else if (std::strcmp(argv[i], "--structured") == 0) structured = true;
            else if (std::strcmp(argv[i], "--dim") == 0 && i + 1 < argc) dim = std::stoi(argv[++i]);
        }
        if (input.empty() || output.empty()) {
            std::cerr << "error: --input and --output required\n";
            return 1;
        }

        auto t_r = load_tensor_binary(input);
        if (!t_r.has_value()) { SL_ERROR("{}", t_r.error()); return 1; }

        if (structured) {
            auto r = structured_prune(t_r.value(), sparsity, dim);
            if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
            save_tensor_binary(output, r.value());
        } else {
            auto r = magnitude_prune(t_r.value(), sparsity);
            if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
            save_tensor_binary(output, r.value().first);
        }
        SL_INFO("morph: pruned {} -> {} (sparsity={})", input, output, sparsity);
    } else if (command == "adapt") {
        std::string input, output;
        int rank = 4;
        float alpha = 1.0f;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
            else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
            else if (std::strcmp(argv[i], "--rank") == 0 && i + 1 < argc) rank = std::stoi(argv[++i]);
            else if (std::strcmp(argv[i], "--alpha") == 0 && i + 1 < argc) alpha = std::stof(argv[++i]);
        }
        if (input.empty() || output.empty()) {
            std::cerr << "error: --input and --output required\n";
            return 1;
        }

        auto t_r = load_tensor_binary(input);
        if (!t_r.has_value()) { SL_ERROR("{}", t_r.error()); return 1; }

        auto r = lora_decompose(t_r.value(), rank);
        if (!r.has_value()) { SL_ERROR("{}", r.error()); return 1; }
        // Save A and B as separate files
        save_tensor_binary(output + ".A", r.value().first);
        save_tensor_binary(output + ".B", r.value().second);
        SL_INFO("morph: adapted {} -> {}.A, {}.B (rank={})", input, output, output, rank);
    } else if (command == "--help" || command == "help") {
        print_usage();
    } else {
        std::cerr << "error: unknown command: " << command << "\n";
        print_usage();
        return 1;
    }

    return 0;
}
```

- [ ] **Step 2: Create `bin/morph/CMakeLists.txt`**

```cmake
add_executable(straylight-morph
    main.cpp
    quantize.cpp
    prune.cpp
    distill.cpp
    adapt.cpp)
target_link_libraries(straylight-morph PRIVATE straylight-common straylight-ml straylight-hw)
target_include_directories(straylight-morph PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-morph DESTINATION bin)
```

- [ ] **Step 3: Add `add_subdirectory(bin/morph)` to root `CMakeLists.txt`**

---

### Task 7: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_morph` → all 9 pass
- [ ] `git add bin/morph/ tests/unit/subsystems/test_morph.cpp`
- [ ] `git commit -m "feat(morph): implement quantization, pruning, distillation, and LoRA adaptation"`

---

## Chunk 6: straylight-snn — Neuron, Network, Plasticity, Simulator

`bin/snn/` — On-demand tool for spiking neural network simulation. Implements Leaky Integrate-and-Fire (LIF) neurons, spike-timing-dependent plasticity (STDP), network topology, and a discrete-time simulator. Links `libstraylight-common` + `libstraylight-ml`.

### File Structure

```
bin/snn/
├── CMakeLists.txt
├── main.cpp
├── neuron.h
├── neuron.cpp
├── network.h
├── network.cpp
├── plasticity.h
├── plasticity.cpp
├── simulator.h
└── simulator.cpp
tests/unit/subsystems/
└── test_snn.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_snn.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_snn.cpp
#include <gtest/gtest.h>
#include "neuron.h"
#include "network.h"
#include "plasticity.h"
#include "simulator.h"

using namespace straylight::snn;

// --- Neuron tests ---

TEST(LIFNeuron, SubthresholdDecay) {
    LIFNeuron n(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                            .v_reset = -75.0f, .tau_m = 20.0f,
                            .r_m = 10.0f});
    // Inject current below threshold
    n.inject_current(1.0f);
    float v0 = n.membrane_potential();
    n.step(1.0f);  // 1 ms timestep
    float v1 = n.membrane_potential();

    // Potential should increase but not spike
    EXPECT_GT(v1, v0);
    EXPECT_FALSE(n.has_spiked());
}

TEST(LIFNeuron, SpikesAboveThreshold) {
    LIFNeuron n(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                            .v_reset = -75.0f, .tau_m = 20.0f,
                            .r_m = 10.0f});
    // Inject large current to trigger spike
    n.inject_current(100.0f);
    for (int i = 0; i < 50; ++i) n.step(1.0f);

    EXPECT_TRUE(n.has_spiked());
}

TEST(LIFNeuron, ResetAfterSpike) {
    LIFNeuron n(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                            .v_reset = -75.0f, .tau_m = 20.0f,
                            .r_m = 10.0f});
    n.inject_current(100.0f);
    for (int i = 0; i < 100; ++i) n.step(1.0f);

    // After spike, potential should be at reset value
    if (n.has_spiked()) {
        EXPECT_NEAR(n.membrane_potential(), -75.0f, 1.0f);
    }
}

TEST(LIFNeuron, RefractoryPeriod) {
    LIFNeuron n(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                            .v_reset = -75.0f, .tau_m = 20.0f,
                            .r_m = 10.0f, .t_refract = 5.0f});
    n.inject_current(100.0f);
    // Drive to spike
    for (int i = 0; i < 100; ++i) n.step(1.0f);
    ASSERT_TRUE(n.has_spiked());

    // During refractory period, neuron should not respond
    n.clear_spike();
    n.inject_current(100.0f);
    n.step(1.0f);  // Still in refractory
    EXPECT_NEAR(n.membrane_potential(), -75.0f, 1.0f);
}

// --- Network tests ---

TEST(SpikeNetwork, ConnectAndPropagate) {
    SpikeNetwork net;
    auto n0 = net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                                         .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    auto n1 = net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                                         .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    net.connect(n0, n1, 50.0f, 1.0f);  // weight=50, delay=1ms

    EXPECT_EQ(net.num_neurons(), 2u);
    EXPECT_EQ(net.num_synapses(), 1u);
}

TEST(SpikeNetwork, RecordSpikes) {
    SpikeNetwork net;
    auto n0 = net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                                         .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    // Inject strong current into n0
    net.inject_current(n0, 100.0f);

    Simulator sim(net, 1.0f);
    sim.run(50.0f);  // 50 ms

    auto spikes = sim.spike_log();
    // n0 should have spiked at least once
    bool found = false;
    for (const auto& [time, nid] : spikes) {
        if (nid == n0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// --- Plasticity tests ---

TEST(STDP, PotentiationPreBeforePost) {
    STDPRule stdp(STDPParams{.a_plus = 0.01f, .a_minus = 0.012f,
                               .tau_plus = 20.0f, .tau_minus = 20.0f});
    float w = 0.5f;
    // Pre fires at t=10, post fires at t=15 → dt = 5ms → potentiation
    float dw = stdp.compute_dw(10.0f, 15.0f);
    EXPECT_GT(dw, 0.0f);
}

TEST(STDP, DepressionPostBeforePre) {
    STDPRule stdp(STDPParams{.a_plus = 0.01f, .a_minus = 0.012f,
                               .tau_plus = 20.0f, .tau_minus = 20.0f});
    // Post fires at t=10, pre fires at t=15 → dt = -5ms → depression
    float dw = stdp.compute_dw(15.0f, 10.0f);
    EXPECT_LT(dw, 0.0f);
}

TEST(STDP, LargeDelaySmallChange) {
    STDPRule stdp(STDPParams{.a_plus = 0.01f, .a_minus = 0.012f,
                               .tau_plus = 20.0f, .tau_minus = 20.0f});
    float dw_close = stdp.compute_dw(10.0f, 12.0f);    // 2ms apart
    float dw_far = stdp.compute_dw(10.0f, 50.0f);      // 40ms apart
    EXPECT_GT(std::abs(dw_close), std::abs(dw_far));
}

// --- Simulator tests ---

TEST(Simulator, RunReturnsOk) {
    SpikeNetwork net;
    net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                               .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    Simulator sim(net, 0.5f);
    auto r = sim.run(10.0f);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(sim.current_time(), 10.0f);
}

TEST(Simulator, STDPModifiesWeights) {
    SpikeNetwork net;
    auto n0 = net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                                         .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    auto n1 = net.add_neuron(LIFParams{.v_rest = -70.0f, .v_thresh = -55.0f,
                                         .v_reset = -75.0f, .tau_m = 20.0f, .r_m = 10.0f});
    net.connect(n0, n1, 0.5f, 1.0f);
    net.inject_current(n0, 80.0f);
    net.inject_current(n1, 80.0f);

    STDPRule stdp(STDPParams{.a_plus = 0.01f, .a_minus = 0.012f,
                               .tau_plus = 20.0f, .tau_minus = 20.0f});
    Simulator sim(net, 1.0f);
    sim.enable_stdp(stdp);
    float w_before = net.synapse_weight(n0, n1);
    sim.run(100.0f);
    float w_after = net.synapse_weight(n0, n1);

    // Weight should have changed due to STDP
    EXPECT_NE(w_before, w_after);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_snn test_snn.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/neuron.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/network.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/plasticity.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/simulator.cpp)
target_include_directories(test_snn PRIVATE ${PROJECT_SOURCE_DIR}/bin/snn)
target_link_libraries(test_snn PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_snn)
```

Run: expect 11 failures.

---

### Task 2: Implement neuron

**Files:** `bin/snn/neuron.h`, `bin/snn/neuron.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/snn/neuron.h
#pragma once

#include <cstdint>

namespace straylight::snn {

using NeuronId = uint32_t;

/// Parameters for a Leaky Integrate-and-Fire neuron.
struct LIFParams {
    float v_rest = -70.0f;     // Resting potential (mV)
    float v_thresh = -55.0f;   // Spike threshold (mV)
    float v_reset = -75.0f;    // Post-spike reset potential (mV)
    float tau_m = 20.0f;       // Membrane time constant (ms)
    float r_m = 10.0f;         // Membrane resistance (MOhm)
    float t_refract = 2.0f;    // Refractory period (ms)
};

/// Leaky Integrate-and-Fire neuron model.
/// dV/dt = -(V - V_rest)/tau_m + R_m * I / tau_m
/// When V >= V_thresh: spike, V = V_reset, enter refractory.
class LIFNeuron {
public:
    explicit LIFNeuron(LIFParams params);

    /// Inject current (nA) to be applied on the next step.
    void inject_current(float current);

    /// Advance by dt milliseconds using forward Euler integration.
    void step(float dt);

    /// Current membrane potential.
    [[nodiscard]] float membrane_potential() const { return v_; }

    /// Whether the neuron spiked during the last step.
    [[nodiscard]] bool has_spiked() const { return spiked_; }

    /// Clear the spike flag (call after processing spike events).
    void clear_spike() { spiked_ = false; }

    /// Last spike time in simulation time.
    [[nodiscard]] float last_spike_time() const { return last_spike_time_; }

    /// Set simulation time (called by simulator).
    void set_time(float t) { sim_time_ = t; }

    /// Get parameters (for introspection).
    [[nodiscard]] const LIFParams& params() const { return params_; }

private:
    LIFParams params_;
    float v_;                // Membrane potential
    float i_inject_ = 0.0f; // Injected current for this step
    bool spiked_ = false;
    float last_spike_time_ = -1000.0f;
    float sim_time_ = 0.0f;
    float refract_remaining_ = 0.0f;
};

} // namespace straylight::snn
```

- [ ] **Step 2: Implementation**

```cpp
// bin/snn/neuron.cpp
#include "neuron.h"

namespace straylight::snn {

LIFNeuron::LIFNeuron(LIFParams params)
    : params_(params), v_(params.v_rest) {}

void LIFNeuron::inject_current(float current) {
    i_inject_ += current;
}

void LIFNeuron::step(float dt) {
    spiked_ = false;

    // Refractory period: neuron is clamped at reset voltage
    if (refract_remaining_ > 0.0f) {
        refract_remaining_ -= dt;
        v_ = params_.v_reset;
        i_inject_ = 0.0f;
        return;
    }

    // Forward Euler integration of LIF equation:
    // dV/dt = (-(V - V_rest) + R_m * I) / tau_m
    float dv = (-(v_ - params_.v_rest) + params_.r_m * i_inject_) / params_.tau_m;
    v_ += dv * dt;

    // Clear injected current (consumed this step)
    i_inject_ = 0.0f;

    // Spike detection
    if (v_ >= params_.v_thresh) {
        spiked_ = true;
        last_spike_time_ = sim_time_;
        v_ = params_.v_reset;
        refract_remaining_ = params_.t_refract;
    }
}

} // namespace straylight::snn
```

---

### Task 3: Implement network

**Files:** `bin/snn/network.h`, `bin/snn/network.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/snn/network.h
#pragma once

#include "neuron.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight::snn {

struct Synapse {
    NeuronId pre;
    NeuronId post;
    float weight;   // Synaptic weight (current injected on pre-spike)
    float delay;    // Transmission delay (ms)
};

/// Spiking neural network: collection of neurons and synapses.
class SpikeNetwork {
public:
    /// Add a neuron with the given parameters. Returns its ID.
    NeuronId add_neuron(LIFParams params);

    /// Connect two neurons with a synapse.
    void connect(NeuronId pre, NeuronId post, float weight, float delay = 1.0f);

    /// Inject current into a specific neuron.
    void inject_current(NeuronId id, float current);

    /// Access a neuron by ID.
    [[nodiscard]] LIFNeuron& neuron(NeuronId id);
    [[nodiscard]] const LIFNeuron& neuron(NeuronId id) const;

    /// Get all synapses originating from a given neuron.
    [[nodiscard]] const std::vector<Synapse>& outgoing_synapses(NeuronId id) const;

    /// Get synapse weight between two neurons (0 if not connected).
    [[nodiscard]] float synapse_weight(NeuronId pre, NeuronId post) const;

    /// Modify a synapse weight (used by STDP).
    void set_synapse_weight(NeuronId pre, NeuronId post, float weight);

    [[nodiscard]] size_t num_neurons() const { return neurons_.size(); }
    [[nodiscard]] size_t num_synapses() const;

    /// Get all neuron IDs.
    [[nodiscard]] std::vector<NeuronId> neuron_ids() const;

    /// Get all synapses (for STDP iteration).
    [[nodiscard]] std::vector<Synapse*> all_synapses();

private:
    std::unordered_map<NeuronId, LIFNeuron> neurons_;
    std::unordered_map<NeuronId, std::vector<Synapse>> adjacency_;
    NeuronId next_id_ = 0;
    static const std::vector<Synapse> empty_synapses_;
};

} // namespace straylight::snn
```

- [ ] **Step 2: Implementation**

```cpp
// bin/snn/network.cpp
#include "network.h"

#include <stdexcept>

namespace straylight::snn {

const std::vector<Synapse> SpikeNetwork::empty_synapses_;

NeuronId SpikeNetwork::add_neuron(LIFParams params) {
    NeuronId id = next_id_++;
    neurons_.emplace(id, LIFNeuron(params));
    return id;
}

void SpikeNetwork::connect(NeuronId pre, NeuronId post, float weight, float delay) {
    adjacency_[pre].push_back(Synapse{pre, post, weight, delay});
}

void SpikeNetwork::inject_current(NeuronId id, float current) {
    auto it = neurons_.find(id);
    if (it != neurons_.end()) {
        it->second.inject_current(current);
    }
}

LIFNeuron& SpikeNetwork::neuron(NeuronId id) {
    auto it = neurons_.find(id);
    if (it == neurons_.end()) throw std::out_of_range("neuron not found");
    return it->second;
}

const LIFNeuron& SpikeNetwork::neuron(NeuronId id) const {
    auto it = neurons_.find(id);
    if (it == neurons_.end()) throw std::out_of_range("neuron not found");
    return it->second;
}

const std::vector<Synapse>& SpikeNetwork::outgoing_synapses(NeuronId id) const {
    auto it = adjacency_.find(id);
    if (it == adjacency_.end()) return empty_synapses_;
    return it->second;
}

float SpikeNetwork::synapse_weight(NeuronId pre, NeuronId post) const {
    auto it = adjacency_.find(pre);
    if (it == adjacency_.end()) return 0.0f;
    for (const auto& s : it->second) {
        if (s.post == post) return s.weight;
    }
    return 0.0f;
}

void SpikeNetwork::set_synapse_weight(NeuronId pre, NeuronId post, float weight) {
    auto it = adjacency_.find(pre);
    if (it == adjacency_.end()) return;
    for (auto& s : it->second) {
        if (s.post == post) { s.weight = weight; return; }
    }
}

size_t SpikeNetwork::num_synapses() const {
    size_t count = 0;
    for (const auto& [id, syns] : adjacency_) count += syns.size();
    return count;
}

std::vector<NeuronId> SpikeNetwork::neuron_ids() const {
    std::vector<NeuronId> ids;
    ids.reserve(neurons_.size());
    for (const auto& [id, _] : neurons_) ids.push_back(id);
    return ids;
}

std::vector<Synapse*> SpikeNetwork::all_synapses() {
    std::vector<Synapse*> result;
    for (auto& [id, syns] : adjacency_) {
        for (auto& s : syns) result.push_back(&s);
    }
    return result;
}

} // namespace straylight::snn
```

---

### Task 4: Implement plasticity

**Files:** `bin/snn/plasticity.h`, `bin/snn/plasticity.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/snn/plasticity.h
#pragma once

namespace straylight::snn {

/// Parameters for spike-timing-dependent plasticity (STDP).
struct STDPParams {
    float a_plus = 0.01f;    // Max potentiation amplitude
    float a_minus = 0.012f;  // Max depression amplitude
    float tau_plus = 20.0f;  // Potentiation time constant (ms)
    float tau_minus = 20.0f; // Depression time constant (ms)
    float w_min = 0.0f;      // Minimum weight
    float w_max = 1.0f;      // Maximum weight
};

/// STDP learning rule.
/// If pre fires before post (dt > 0): potentiation → dw = a_plus * exp(-dt/tau_plus)
/// If post fires before pre (dt < 0): depression → dw = -a_minus * exp(dt/tau_minus)
class STDPRule {
public:
    explicit STDPRule(STDPParams params);

    /// Compute weight change given pre and post spike times.
    /// dt = t_post - t_pre (positive → potentiation, negative → depression)
    [[nodiscard]] float compute_dw(float t_pre, float t_post) const;

    /// Apply weight change with bounds enforcement.
    [[nodiscard]] float apply(float current_weight, float dw) const;

    [[nodiscard]] const STDPParams& params() const { return params_; }

private:
    STDPParams params_;
};

} // namespace straylight::snn
```

- [ ] **Step 2: Implementation**

```cpp
// bin/snn/plasticity.cpp
#include "plasticity.h"

#include <algorithm>
#include <cmath>

namespace straylight::snn {

STDPRule::STDPRule(STDPParams params) : params_(params) {}

float STDPRule::compute_dw(float t_pre, float t_post) const {
    float dt = t_post - t_pre;

    if (dt > 0.0f) {
        // Pre before post → potentiation (Long-Term Potentiation)
        return params_.a_plus * std::exp(-dt / params_.tau_plus);
    } else if (dt < 0.0f) {
        // Post before pre → depression (Long-Term Depression)
        return -params_.a_minus * std::exp(dt / params_.tau_minus);
    }
    return 0.0f;  // Simultaneous → no change
}

float STDPRule::apply(float current_weight, float dw) const {
    float new_weight = current_weight + dw;
    return std::clamp(new_weight, params_.w_min, params_.w_max);
}

} // namespace straylight::snn
```

---

### Task 5: Implement simulator

**Files:** `bin/snn/simulator.h`, `bin/snn/simulator.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/snn/simulator.h
#pragma once

#include <straylight/result.h>
#include "network.h"
#include "plasticity.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace straylight::snn {

/// Spike event: (time_ms, neuron_id).
using SpikeEvent = std::pair<float, NeuronId>;

/// Discrete-time spiking neural network simulator.
class Simulator {
public:
    /// Create a simulator for the given network with the specified timestep.
    Simulator(SpikeNetwork& network, float dt);

    /// Run the simulation for duration_ms milliseconds.
    Result<void, std::string> run(float duration_ms);

    /// Enable STDP learning.
    void enable_stdp(const STDPRule& rule);

    /// Get the spike log (all spikes recorded during simulation).
    [[nodiscard]] const std::vector<SpikeEvent>& spike_log() const { return spike_log_; }

    /// Current simulation time in ms.
    [[nodiscard]] float current_time() const { return time_; }

    /// Reset simulator state (but not network state).
    void reset();

private:
    void step_once();
    void deliver_spikes();
    void apply_stdp();

    SpikeNetwork& net_;
    float dt_;
    float time_ = 0.0f;

    std::optional<STDPRule> stdp_;

    // Spike log
    std::vector<SpikeEvent> spike_log_;

    // Pending spike deliveries: (delivery_time, post_neuron_id, weight)
    struct PendingSpike {
        float delivery_time;
        NeuronId post;
        float weight;
    };
    std::vector<PendingSpike> pending_spikes_;

    // Per-neuron persistent injection currents
    std::unordered_map<NeuronId, float> persistent_currents_;
};

} // namespace straylight::snn
```

- [ ] **Step 2: Implementation**

```cpp
// bin/snn/simulator.cpp
#include "simulator.h"

#include <algorithm>

namespace straylight::snn {

Simulator::Simulator(SpikeNetwork& network, float dt)
    : net_(network), dt_(dt) {
    // Capture any pre-set injection currents as persistent
    for (auto id : net_.neuron_ids()) {
        persistent_currents_[id] = 0.0f;
    }
}

void Simulator::enable_stdp(const STDPRule& rule) {
    stdp_ = rule;
}

Result<void, std::string> Simulator::run(float duration_ms) {
    if (dt_ <= 0.0f) {
        return Result<void, std::string>::error("timestep must be positive");
    }

    float end_time = time_ + duration_ms;
    while (time_ < end_time) {
        step_once();
        time_ += dt_;
    }
    // Snap to exact end time
    time_ = end_time;
    return Result<void, std::string>::ok();
}

void Simulator::step_once() {
    // 1. Deliver pending spikes that are due
    deliver_spikes();

    // 2. Re-inject persistent currents
    for (auto& [id, current] : persistent_currents_) {
        if (current != 0.0f) {
            net_.neuron(id).inject_current(current);
        }
    }

    // 3. Step all neurons
    auto ids = net_.neuron_ids();
    for (auto id : ids) {
        auto& n = net_.neuron(id);
        n.set_time(time_);
        n.step(dt_);
    }

    // 4. Process spikes
    for (auto id : ids) {
        auto& n = net_.neuron(id);
        if (n.has_spiked()) {
            spike_log_.emplace_back(time_, id);

            // Enqueue spike deliveries to post-synaptic neurons
            for (const auto& syn : net_.outgoing_synapses(id)) {
                pending_spikes_.push_back(PendingSpike{
                    .delivery_time = time_ + syn.delay,
                    .post = syn.post,
                    .weight = syn.weight,
                });
            }

            // Apply STDP if enabled
            if (stdp_) apply_stdp();

            n.clear_spike();
        }
    }
}

void Simulator::deliver_spikes() {
    auto it = pending_spikes_.begin();
    while (it != pending_spikes_.end()) {
        if (it->delivery_time <= time_) {
            net_.neuron(it->post).inject_current(it->weight);
            it = pending_spikes_.erase(it);
        } else {
            ++it;
        }
    }
}

void Simulator::apply_stdp() {
    if (!stdp_) return;

    // For each synapse, check if pre and post have recent spikes
    auto synapses = net_.all_synapses();
    for (auto* syn : synapses) {
        float t_pre = net_.neuron(syn->pre).last_spike_time();
        float t_post = net_.neuron(syn->post).last_spike_time();

        // Only apply if both have spiked recently (within 2*tau window)
        float max_tau = std::max(stdp_->params().tau_plus, stdp_->params().tau_minus);
        if (t_pre < 0.0f || t_post < 0.0f) continue;
        if (std::abs(t_post - t_pre) > 3.0f * max_tau) continue;

        float dw = stdp_->compute_dw(t_pre, t_post);
        syn->weight = stdp_->apply(syn->weight, dw);
    }
}

void Simulator::reset() {
    time_ = 0.0f;
    spike_log_.clear();
    pending_spikes_.clear();
}

} // namespace straylight::snn
```

---

### Task 6: Implement snn main.cpp + CMakeLists.txt

- [ ] **Step 1: Create `bin/snn/main.cpp`**

```cpp
// bin/snn/main.cpp
// straylight-snn: on-demand spiking neural network simulator
// Usage: straylight-snn --config <network.json> --duration <ms> [--output <spikes.csv>]

#include <straylight/config.h>
#include <straylight/log.h>

#include "neuron.h"
#include "network.h"
#include "plasticity.h"
#include "simulator.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

using namespace straylight;
using namespace straylight::snn;

static void print_usage() {
    std::cerr << "Usage: straylight-snn [OPTIONS]\n"
              << "  --config   <file>   Network configuration JSON\n"
              << "  --duration <ms>     Simulation duration in milliseconds\n"
              << "  --dt       <ms>     Timestep (default: 1.0)\n"
              << "  --output   <file>   Output spike log CSV (default: stdout)\n"
              << "  --stdp              Enable STDP learning\n"
              << "  --help              Show this help\n";
}

int main(int argc, char* argv[]) {
    SL_INIT("straylight-snn", "info");

    std::string config_path;
    std::string output_path;
    float duration = 100.0f;
    float dt = 1.0f;
    bool enable_stdp = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) config_path = argv[++i];
        else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) duration = std::stof(argv[++i]);
        else if (std::strcmp(argv[i], "--dt") == 0 && i + 1 < argc) dt = std::stof(argv[++i]);
        else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) output_path = argv[++i];
        else if (std::strcmp(argv[i], "--stdp") == 0) enable_stdp = true;
        else if (std::strcmp(argv[i], "--help") == 0) { print_usage(); return 0; }
    }

    if (config_path.empty()) {
        std::cerr << "error: --config required\n";
        print_usage();
        return 1;
    }

    // Load network configuration
    std::ifstream f(config_path);
    if (!f.is_open()) {
        SL_ERROR("snn: cannot open {}", config_path);
        return 1;
    }

    try {
        auto j = nlohmann::json::parse(f);
        SpikeNetwork net;

        // Parse neurons
        std::unordered_map<std::string, NeuronId> name_map;
        for (const auto& nj : j["neurons"]) {
            LIFParams p;
            p.v_rest = nj.value("v_rest", -70.0f);
            p.v_thresh = nj.value("v_thresh", -55.0f);
            p.v_reset = nj.value("v_reset", -75.0f);
            p.tau_m = nj.value("tau_m", 20.0f);
            p.r_m = nj.value("r_m", 10.0f);
            p.t_refract = nj.value("t_refract", 2.0f);

            auto id = net.add_neuron(p);
            std::string name = nj.value("name", std::to_string(id));
            name_map[name] = id;

            if (nj.contains("inject")) {
                net.inject_current(id, nj["inject"].get<float>());
            }
        }

        // Parse synapses
        if (j.contains("synapses")) {
            for (const auto& sj : j["synapses"]) {
                std::string pre = sj["pre"].get<std::string>();
                std::string post = sj["post"].get<std::string>();
                float weight = sj.value("weight", 1.0f);
                float delay = sj.value("delay", 1.0f);
                net.connect(name_map.at(pre), name_map.at(post), weight, delay);
            }
        }

        SL_INFO("snn: loaded {} neurons, {} synapses",
                net.num_neurons(), net.num_synapses());

        Simulator sim(net, dt);
        if (enable_stdp) {
            STDPParams sp;
            if (j.contains("stdp")) {
                sp.a_plus = j["stdp"].value("a_plus", 0.01f);
                sp.a_minus = j["stdp"].value("a_minus", 0.012f);
                sp.tau_plus = j["stdp"].value("tau_plus", 20.0f);
                sp.tau_minus = j["stdp"].value("tau_minus", 20.0f);
            }
            sim.enable_stdp(STDPRule(sp));
        }

        auto r = sim.run(duration);
        if (!r.has_value()) {
            SL_ERROR("snn: simulation failed: {}", r.error());
            return 1;
        }

        // Output spike log as CSV
        auto& spikes = sim.spike_log();
        std::ostream* out = &std::cout;
        std::ofstream outfile;
        if (!output_path.empty()) {
            outfile.open(output_path);
            out = &outfile;
        }

        *out << "time_ms,neuron_id\n";
        for (const auto& [t, nid] : spikes) {
            *out << t << "," << nid << "\n";
        }

        SL_INFO("snn: simulation complete, {} spikes in {}ms", spikes.size(), duration);

    } catch (const nlohmann::json::exception& e) {
        SL_ERROR("snn: JSON error: {}", e.what());
        return 1;
    }

    return 0;
}
```

- [ ] **Step 2: Create `bin/snn/CMakeLists.txt`**

```cmake
add_executable(straylight-snn
    main.cpp
    neuron.cpp
    network.cpp
    plasticity.cpp
    simulator.cpp)
target_link_libraries(straylight-snn PRIVATE straylight-common straylight-ml)
target_include_directories(straylight-snn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-snn DESTINATION bin)
```

- [ ] **Step 3: Add `add_subdirectory(bin/snn)` to root `CMakeLists.txt`**

---

### Task 7: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_snn` → all 11 pass
- [ ] `git add bin/snn/ tests/unit/subsystems/test_snn.cpp`
- [ ] `git commit -m "feat(snn): implement LIF neurons, STDP plasticity, and discrete-time simulator"`

---

## Chunk 7: straylight-rhem — Discovery, Allocator, Migration, Policy

`bin/rhem/` — On-demand tool for runtime heterogeneous execution management. Discovers compute devices (CPUs, GPUs, FPGAs), allocates workloads to them, handles live migration of tensors between devices, and enforces scheduling policies. Links `libstraylight-common` + `libstraylight-ml` + `libstraylight-hw`.

### File Structure

```
bin/rhem/
├── CMakeLists.txt
├── main.cpp
├── discovery.h
├── discovery.cpp
├── allocator.h
├── allocator.cpp
├── migration.h
├── migration.cpp
├── policy.h
└── policy.cpp
tests/unit/subsystems/
└── test_rhem.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_rhem.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/unit/subsystems/test_rhem.cpp
#include <gtest/gtest.h>
#include "discovery.h"
#include "allocator.h"
#include "migration.h"
#include "policy.h"

using namespace straylight::rhem;

// --- Discovery tests ---

TEST(Discovery, RegisterAndEnumerate) {
    DeviceRegistry reg;
    DeviceInfo gpu0{.id = "gpu:0", .type = DeviceClass::GPU,
                     .memory_bytes = 8ULL * 1024 * 1024 * 1024,
                     .compute_units = 80, .vendor = "nvidia"};
    DeviceInfo cpu0{.id = "cpu:0", .type = DeviceClass::CPU,
                     .memory_bytes = 32ULL * 1024 * 1024 * 1024,
                     .compute_units = 16, .vendor = "amd"};
    reg.register_device(gpu0);
    reg.register_device(cpu0);

    EXPECT_EQ(reg.num_devices(), 2u);
    auto gpus = reg.devices_by_type(DeviceClass::GPU);
    EXPECT_EQ(gpus.size(), 1u);
    EXPECT_EQ(gpus[0].id, "gpu:0");
}

TEST(Discovery, DeregisterDevice) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.deregister_device("gpu:0");
    EXPECT_EQ(reg.num_devices(), 0u);
}

TEST(Discovery, UpdateDeviceStats) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.update_utilization("gpu:0", 0.75f, 6ULL << 30);

    auto dev = reg.device("gpu:0");
    ASSERT_TRUE(dev.has_value());
    EXPECT_NEAR(dev->utilization, 0.75f, 0.01f);
    EXPECT_EQ(dev->memory_used, 6ULL << 30);
}

// --- Allocator tests ---

TEST(Allocator, AllocateToBestDevice) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.register_device(DeviceInfo{.id = "gpu:1", .type = DeviceClass::GPU,
                                     .memory_bytes = 16ULL << 30, .compute_units = 80});

    DeviceAllocator alloc(reg);
    AllocationRequest req{.workload_id = "task_1", .memory_needed = 10ULL << 30,
                           .preferred_type = DeviceClass::GPU};

    auto r = alloc.allocate(req);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r.value().device_id, "gpu:1");  // Only gpu:1 has enough memory
}

TEST(Allocator, AllocationFailsInsufficientMemory) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 4ULL << 30, .compute_units = 80});

    DeviceAllocator alloc(reg);
    AllocationRequest req{.workload_id = "task_1", .memory_needed = 8ULL << 30,
                           .preferred_type = DeviceClass::GPU};

    auto r = alloc.allocate(req);
    EXPECT_FALSE(r.has_value());
}

TEST(Allocator, ReleaseFreesMemory) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});

    DeviceAllocator alloc(reg);
    AllocationRequest req{.workload_id = "t1", .memory_needed = 6ULL << 30,
                           .preferred_type = DeviceClass::GPU};
    auto r = alloc.allocate(req);
    ASSERT_TRUE(r.has_value());

    alloc.release("t1");

    // Now should be able to allocate again
    req.workload_id = "t2";
    auto r2 = alloc.allocate(req);
    EXPECT_TRUE(r2.has_value());
}

// --- Migration tests ---

TEST(Migration, PlanMigration) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.register_device(DeviceInfo{.id = "gpu:1", .type = DeviceClass::GPU,
                                     .memory_bytes = 16ULL << 30, .compute_units = 80});

    MigrationPlanner planner(reg);
    MigrationRequest req{.tensor_id = "model.layer1.weight",
                          .source_device = "gpu:0",
                          .target_device = "gpu:1",
                          .tensor_bytes = 1ULL << 30};

    auto plan = planner.plan(req);
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan.value().source, "gpu:0");
    EXPECT_EQ(plan.value().target, "gpu:1");
    EXPECT_GT(plan.value().estimated_time_ms, 0.0f);
}

TEST(Migration, RejectMigrationInsufficientTarget) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.register_device(DeviceInfo{.id = "gpu:1", .type = DeviceClass::GPU,
                                     .memory_bytes = 1ULL << 30, .compute_units = 40});

    MigrationPlanner planner(reg);
    MigrationRequest req{.tensor_id = "big_tensor",
                          .source_device = "gpu:0",
                          .target_device = "gpu:1",
                          .tensor_bytes = 4ULL << 30};

    auto plan = planner.plan(req);
    EXPECT_FALSE(plan.has_value());
}

// --- Policy tests ---

TEST(Policy, MemoryPressureTriggersEviction) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.update_utilization("gpu:0", 0.5f, 7ULL << 30);  // 87.5% memory used

    SchedulingPolicy policy(reg);
    policy.set_memory_threshold(0.85f);  // Trigger at 85%

    auto actions = policy.evaluate();
    EXPECT_FALSE(actions.empty());
    // Should recommend eviction from gpu:0
    bool found_evict = false;
    for (const auto& a : actions) {
        if (a.type == PolicyAction::Type::Evict && a.device_id == "gpu:0") {
            found_evict = true;
        }
    }
    EXPECT_TRUE(found_evict);
}

TEST(Policy, NoActionWhenHealthy) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.update_utilization("gpu:0", 0.3f, 2ULL << 30);  // 25% memory, low util

    SchedulingPolicy policy(reg);
    policy.set_memory_threshold(0.85f);

    auto actions = policy.evaluate();
    EXPECT_TRUE(actions.empty());
}

TEST(Policy, LoadBalanceAcrossDevices) {
    DeviceRegistry reg;
    reg.register_device(DeviceInfo{.id = "gpu:0", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.register_device(DeviceInfo{.id = "gpu:1", .type = DeviceClass::GPU,
                                     .memory_bytes = 8ULL << 30, .compute_units = 80});
    reg.update_utilization("gpu:0", 0.95f, 6ULL << 30);  // Overloaded
    reg.update_utilization("gpu:1", 0.1f, 1ULL << 30);   // Idle

    SchedulingPolicy policy(reg);
    policy.set_utilization_threshold(0.9f);

    auto actions = policy.evaluate();
    bool found_migrate = false;
    for (const auto& a : actions) {
        if (a.type == PolicyAction::Type::Migrate) {
            found_migrate = true;
        }
    }
    EXPECT_TRUE(found_migrate);
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_rhem test_rhem.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/discovery.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/allocator.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/migration.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/policy.cpp)
target_include_directories(test_rhem PRIVATE ${PROJECT_SOURCE_DIR}/bin/rhem)
target_link_libraries(test_rhem PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_rhem)
```

Run: expect 11 failures.

---

### Task 2: Implement discovery

**Files:** `bin/rhem/discovery.h`, `bin/rhem/discovery.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/rhem/discovery.h
#pragma once

#include <straylight/result.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight::rhem {

enum class DeviceClass : uint8_t {
    CPU = 0,
    GPU = 1,
    FPGA = 2,
    TPU = 3,
};

struct DeviceInfo {
    std::string id;
    DeviceClass type = DeviceClass::CPU;
    uint64_t memory_bytes = 0;
    uint32_t compute_units = 0;
    std::string vendor;

    // Runtime stats (updated periodically)
    float utilization = 0.0f;      // 0.0 to 1.0
    uint64_t memory_used = 0;
    float temperature_c = 0.0f;
    float bandwidth_gbps = 0.0f;   // PCIe/interconnect bandwidth
};

/// Device registry: tracks available compute devices and their runtime stats.
class DeviceRegistry {
public:
    void register_device(DeviceInfo info);
    void deregister_device(const std::string& id);

    [[nodiscard]] std::optional<DeviceInfo> device(const std::string& id) const;
    [[nodiscard]] std::vector<DeviceInfo> devices_by_type(DeviceClass type) const;
    [[nodiscard]] std::vector<DeviceInfo> all_devices() const;
    [[nodiscard]] size_t num_devices() const;

    void update_utilization(const std::string& id, float util, uint64_t mem_used);
    void update_temperature(const std::string& id, float temp_c);

    /// Probe system for available devices (reads /proc, sysfs).
    Result<void, std::string> auto_discover();

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, DeviceInfo> devices_;
};

} // namespace straylight::rhem
```

- [ ] **Step 2: Implementation**

```cpp
// bin/rhem/discovery.cpp
#include "discovery.h"

#include <algorithm>
#include <fstream>
#include <filesystem>

namespace straylight::rhem {

void DeviceRegistry::register_device(DeviceInfo info) {
    std::lock_guard lock(mu_);
    devices_[info.id] = std::move(info);
}

void DeviceRegistry::deregister_device(const std::string& id) {
    std::lock_guard lock(mu_);
    devices_.erase(id);
}

std::optional<DeviceInfo> DeviceRegistry::device(const std::string& id) const {
    std::lock_guard lock(mu_);
    auto it = devices_.find(id);
    if (it == devices_.end()) return std::nullopt;
    return it->second;
}

std::vector<DeviceInfo> DeviceRegistry::devices_by_type(DeviceClass type) const {
    std::lock_guard lock(mu_);
    std::vector<DeviceInfo> result;
    for (const auto& [id, dev] : devices_) {
        if (dev.type == type) result.push_back(dev);
    }
    return result;
}

std::vector<DeviceInfo> DeviceRegistry::all_devices() const {
    std::lock_guard lock(mu_);
    std::vector<DeviceInfo> result;
    result.reserve(devices_.size());
    for (const auto& [id, dev] : devices_) result.push_back(dev);
    return result;
}

size_t DeviceRegistry::num_devices() const {
    std::lock_guard lock(mu_);
    return devices_.size();
}

void DeviceRegistry::update_utilization(const std::string& id, float util, uint64_t mem_used) {
    std::lock_guard lock(mu_);
    auto it = devices_.find(id);
    if (it != devices_.end()) {
        it->second.utilization = util;
        it->second.memory_used = mem_used;
    }
}

void DeviceRegistry::update_temperature(const std::string& id, float temp_c) {
    std::lock_guard lock(mu_);
    auto it = devices_.find(id);
    if (it != devices_.end()) {
        it->second.temperature_c = temp_c;
    }
}

Result<void, std::string> DeviceRegistry::auto_discover() {
    // Discover CPUs from /proc/cpuinfo
    {
        std::ifstream f("/proc/cpuinfo");
        if (f.is_open()) {
            int core_count = 0;
            std::string line;
            while (std::getline(f, line)) {
                if (line.find("processor") == 0) core_count++;
            }
            if (core_count > 0) {
                // Get memory from /proc/meminfo
                uint64_t mem_bytes = 0;
                std::ifstream memf("/proc/meminfo");
                if (memf.is_open()) {
                    std::string key;
                    uint64_t val;
                    std::string unit;
                    while (memf >> key >> val) {
                        if (key == "MemTotal:") {
                            memf >> unit;
                            mem_bytes = val * 1024;
                            break;
                        }
                    }
                }

                register_device(DeviceInfo{
                    .id = "cpu:0",
                    .type = DeviceClass::CPU,
                    .memory_bytes = mem_bytes,
                    .compute_units = static_cast<uint32_t>(core_count),
                    .vendor = "system",
                });
            }
        }
    }

    // Discover NVIDIA GPUs from sysfs
    std::string gpu_base = "/proc/driver/nvidia/gpus";
    if (std::filesystem::exists(gpu_base)) {
        int gpu_idx = 0;
        for (auto& entry : std::filesystem::directory_iterator(gpu_base)) {
            if (entry.is_directory()) {
                std::string gpu_id = "gpu:" + std::to_string(gpu_idx);
                register_device(DeviceInfo{
                    .id = gpu_id,
                    .type = DeviceClass::GPU,
                    .memory_bytes = 0,  // Would need nvidia-smi or NVML for this
                    .compute_units = 0,
                    .vendor = "nvidia",
                });
                gpu_idx++;
            }
        }
    }

    // Discover AMD GPUs via /sys/class/drm
    std::string drm_base = "/sys/class/drm";
    if (std::filesystem::exists(drm_base)) {
        int amd_idx = 0;
        for (auto& entry : std::filesystem::directory_iterator(drm_base)) {
            auto name = entry.path().filename().string();
            if (name.find("card") == 0 && name.find("-") == std::string::npos) {
                auto vendor_path = entry.path() / "device" / "vendor";
                if (std::filesystem::exists(vendor_path)) {
                    std::ifstream vf(vendor_path);
                    std::string vendor_id;
                    vf >> vendor_id;
                    if (vendor_id == "0x1002") {  // AMD vendor ID
                        register_device(DeviceInfo{
                            .id = "gpu:" + std::to_string(amd_idx),
                            .type = DeviceClass::GPU,
                            .vendor = "amd",
                        });
                        amd_idx++;
                    }
                }
            }
        }
    }

    return Result<void, std::string>::ok();
}

} // namespace straylight::rhem
```

---

### Task 3: Implement allocator

**Files:** `bin/rhem/allocator.h`, `bin/rhem/allocator.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/rhem/allocator.h
#pragma once

#include <straylight/result.h>
#include "discovery.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace straylight::rhem {

struct AllocationRequest {
    std::string workload_id;
    uint64_t memory_needed = 0;
    DeviceClass preferred_type = DeviceClass::GPU;
    uint32_t min_compute_units = 0;
};

struct AllocationResult {
    std::string device_id;
    uint64_t memory_allocated;
};

/// Workload-to-device allocator using first-fit-decreasing strategy.
class DeviceAllocator {
public:
    explicit DeviceAllocator(DeviceRegistry& registry);

    /// Allocate a device for the given workload.
    Result<AllocationResult, std::string> allocate(const AllocationRequest& req);

    /// Release a previously allocated workload.
    void release(const std::string& workload_id);

    /// Get current allocations.
    [[nodiscard]] std::unordered_map<std::string, AllocationResult> allocations() const;

private:
    DeviceRegistry& registry_;
    mutable std::mutex mu_;
    // workload_id → allocation
    std::unordered_map<std::string, AllocationResult> allocations_;
    // device_id → total memory committed to workloads
    std::unordered_map<std::string, uint64_t> committed_memory_;
};

} // namespace straylight::rhem
```

- [ ] **Step 2: Implementation**

```cpp
// bin/rhem/allocator.cpp
#include "allocator.h"

#include <algorithm>

namespace straylight::rhem {

DeviceAllocator::DeviceAllocator(DeviceRegistry& registry)
    : registry_(registry) {}

Result<AllocationResult, std::string> DeviceAllocator::allocate(
        const AllocationRequest& req) {
    std::lock_guard lock(mu_);

    auto devices = registry_.devices_by_type(req.preferred_type);
    if (devices.empty()) {
        // Fall back to any device
        devices = registry_.all_devices();
    }

    // Sort by available memory (descending) — best fit
    std::sort(devices.begin(), devices.end(),
        [this](const DeviceInfo& a, const DeviceInfo& b) {
            uint64_t a_committed = 0, b_committed = 0;
            if (committed_memory_.contains(a.id)) a_committed = committed_memory_.at(a.id);
            if (committed_memory_.contains(b.id)) b_committed = committed_memory_.at(b.id);
            uint64_t a_avail = a.memory_bytes > a_committed ? a.memory_bytes - a_committed : 0;
            uint64_t b_avail = b.memory_bytes > b_committed ? b.memory_bytes - b_committed : 0;
            return a_avail > b_avail;
        });

    for (const auto& dev : devices) {
        if (req.min_compute_units > 0 && dev.compute_units < req.min_compute_units) continue;

        uint64_t committed = 0;
        if (committed_memory_.contains(dev.id)) committed = committed_memory_[dev.id];
        uint64_t available = (dev.memory_bytes > committed) ? dev.memory_bytes - committed : 0;

        if (available >= req.memory_needed) {
            committed_memory_[dev.id] = committed + req.memory_needed;
            AllocationResult result{dev.id, req.memory_needed};
            allocations_[req.workload_id] = result;
            return Result<AllocationResult, std::string>::ok(result);
        }
    }

    return Result<AllocationResult, std::string>::error(
        "no device with sufficient memory for workload " + req.workload_id +
        " (need " + std::to_string(req.memory_needed) + " bytes)");
}

void DeviceAllocator::release(const std::string& workload_id) {
    std::lock_guard lock(mu_);
    auto it = allocations_.find(workload_id);
    if (it != allocations_.end()) {
        auto& alloc = it->second;
        if (committed_memory_.contains(alloc.device_id)) {
            auto& committed = committed_memory_[alloc.device_id];
            committed = (committed > alloc.memory_allocated)
                ? committed - alloc.memory_allocated : 0;
        }
        allocations_.erase(it);
    }
}

std::unordered_map<std::string, AllocationResult> DeviceAllocator::allocations() const {
    std::lock_guard lock(mu_);
    return allocations_;
}

} // namespace straylight::rhem
```

---

### Task 4: Implement migration

**Files:** `bin/rhem/migration.h`, `bin/rhem/migration.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/rhem/migration.h
#pragma once

#include <straylight/result.h>
#include "discovery.h"

#include <string>

namespace straylight::rhem {

struct MigrationRequest {
    std::string tensor_id;
    std::string source_device;
    std::string target_device;
    uint64_t tensor_bytes;
};

struct MigrationPlan {
    std::string tensor_id;
    std::string source;
    std::string target;
    uint64_t bytes;
    float estimated_time_ms;    // Based on interconnect bandwidth
    bool requires_format_conversion;  // CPU↔GPU needs conversion
};

/// Plans tensor migrations between devices.
class MigrationPlanner {
public:
    explicit MigrationPlanner(const DeviceRegistry& registry);

    /// Create a migration plan. Validates source/target exist and target has capacity.
    Result<MigrationPlan, std::string> plan(const MigrationRequest& req) const;

    /// Estimate transfer time based on device interconnect bandwidth.
    [[nodiscard]] float estimate_transfer_time(
        const std::string& source, const std::string& target,
        uint64_t bytes) const;

private:
    const DeviceRegistry& registry_;
};

} // namespace straylight::rhem
```

- [ ] **Step 2: Implementation**

```cpp
// bin/rhem/migration.cpp
#include "migration.h"

namespace straylight::rhem {

MigrationPlanner::MigrationPlanner(const DeviceRegistry& registry)
    : registry_(registry) {}

Result<MigrationPlan, std::string> MigrationPlanner::plan(
        const MigrationRequest& req) const {
    auto src = registry_.device(req.source_device);
    if (!src.has_value()) {
        return Result<MigrationPlan, std::string>::error(
            "source device not found: " + req.source_device);
    }

    auto tgt = registry_.device(req.target_device);
    if (!tgt.has_value()) {
        return Result<MigrationPlan, std::string>::error(
            "target device not found: " + req.target_device);
    }

    // Check target has enough free memory
    uint64_t tgt_free = tgt->memory_bytes > tgt->memory_used
        ? tgt->memory_bytes - tgt->memory_used : 0;
    if (tgt_free < req.tensor_bytes) {
        return Result<MigrationPlan, std::string>::error(
            "target device " + req.target_device +
            " has insufficient memory: " + std::to_string(tgt_free) +
            " free, need " + std::to_string(req.tensor_bytes));
    }

    float time_ms = estimate_transfer_time(req.source_device, req.target_device,
                                            req.tensor_bytes);

    bool needs_conversion = (src->type != tgt->type);

    return Result<MigrationPlan, std::string>::ok(MigrationPlan{
        .tensor_id = req.tensor_id,
        .source = req.source_device,
        .target = req.target_device,
        .bytes = req.tensor_bytes,
        .estimated_time_ms = time_ms,
        .requires_format_conversion = needs_conversion,
    });
}

float MigrationPlanner::estimate_transfer_time(
        const std::string& source, const std::string& target,
        uint64_t bytes) const {
    // Estimate based on interconnect bandwidth
    auto src = registry_.device(source);
    auto tgt = registry_.device(target);

    float bandwidth_gbps = 16.0f;  // Default PCIe Gen4 x16

    if (src.has_value() && src->bandwidth_gbps > 0.0f) {
        bandwidth_gbps = src->bandwidth_gbps;
    }
    if (tgt.has_value() && tgt->bandwidth_gbps > 0.0f) {
        bandwidth_gbps = std::min(bandwidth_gbps, tgt->bandwidth_gbps);
    }

    // Same device type peer-to-peer may use NVLink (faster)
    if (src.has_value() && tgt.has_value() &&
        src->type == DeviceClass::GPU && tgt->type == DeviceClass::GPU &&
        src->vendor == tgt->vendor) {
        bandwidth_gbps *= 2.0f;  // Approximate NVLink advantage
    }

    // Convert: bytes / (Gbps * 1e9 / 8) * 1000 ms
    float bandwidth_bytes_per_ms = (bandwidth_gbps * 1e9f / 8.0f) / 1000.0f;
    if (bandwidth_bytes_per_ms <= 0.0f) bandwidth_bytes_per_ms = 1e6f;

    return static_cast<float>(bytes) / bandwidth_bytes_per_ms;
}

} // namespace straylight::rhem
```

---

### Task 5: Implement policy

**Files:** `bin/rhem/policy.h`, `bin/rhem/policy.cpp`

- [ ] **Step 1: Header**

```cpp
// bin/rhem/policy.h
#pragma once

#include "discovery.h"

#include <string>
#include <vector>

namespace straylight::rhem {

struct PolicyAction {
    enum class Type : uint8_t {
        NoOp = 0,
        Evict = 1,      // Evict least-recently-used tensors from device
        Migrate = 2,    // Migrate workload to less-loaded device
        Throttle = 3,   // Reduce new allocations on overloaded device
        Rebalance = 4,  // General rebalancing across devices
    };

    Type type = Type::NoOp;
    std::string device_id;
    std::string target_device_id;  // For Migrate actions
    std::string reason;
};

/// Scheduling policy engine that evaluates device health
/// and recommends corrective actions.
class SchedulingPolicy {
public:
    explicit SchedulingPolicy(const DeviceRegistry& registry);

    /// Set memory utilization threshold (0.0-1.0). Default 0.85.
    void set_memory_threshold(float threshold);

    /// Set compute utilization threshold (0.0-1.0). Default 0.90.
    void set_utilization_threshold(float threshold);

    /// Set thermal throttle temperature (Celsius). Default 85.
    void set_thermal_limit(float temp_c);

    /// Evaluate all devices and return recommended actions.
    [[nodiscard]] std::vector<PolicyAction> evaluate() const;

private:
    const DeviceRegistry& registry_;
    float memory_threshold_ = 0.85f;
    float utilization_threshold_ = 0.90f;
    float thermal_limit_ = 85.0f;

    std::vector<PolicyAction> check_memory_pressure() const;
    std::vector<PolicyAction> check_compute_balance() const;
    std::vector<PolicyAction> check_thermal() const;
};

} // namespace straylight::rhem
```

- [ ] **Step 2: Implementation**

```cpp
// bin/rhem/policy.cpp
#include "policy.h"

#include <algorithm>

namespace straylight::rhem {

SchedulingPolicy::SchedulingPolicy(const DeviceRegistry& registry)
    : registry_(registry) {}

void SchedulingPolicy::set_memory_threshold(float threshold) {
    memory_threshold_ = threshold;
}

void SchedulingPolicy::set_utilization_threshold(float threshold) {
    utilization_threshold_ = threshold;
}

void SchedulingPolicy::set_thermal_limit(float temp_c) {
    thermal_limit_ = temp_c;
}

std::vector<PolicyAction> SchedulingPolicy::evaluate() const {
    std::vector<PolicyAction> actions;

    auto mem_actions = check_memory_pressure();
    actions.insert(actions.end(), mem_actions.begin(), mem_actions.end());

    auto compute_actions = check_compute_balance();
    actions.insert(actions.end(), compute_actions.begin(), compute_actions.end());

    auto thermal_actions = check_thermal();
    actions.insert(actions.end(), thermal_actions.begin(), thermal_actions.end());

    return actions;
}

std::vector<PolicyAction> SchedulingPolicy::check_memory_pressure() const {
    std::vector<PolicyAction> actions;
    auto devices = registry_.all_devices();

    for (const auto& dev : devices) {
        if (dev.memory_bytes == 0) continue;
        float mem_ratio = static_cast<float>(dev.memory_used) /
                         static_cast<float>(dev.memory_bytes);
        if (mem_ratio > memory_threshold_) {
            actions.push_back(PolicyAction{
                .type = PolicyAction::Type::Evict,
                .device_id = dev.id,
                .reason = "memory pressure: " +
                    std::to_string(static_cast<int>(mem_ratio * 100)) +
                    "% used (threshold: " +
                    std::to_string(static_cast<int>(memory_threshold_ * 100)) + "%)",
            });
        }
    }
    return actions;
}

std::vector<PolicyAction> SchedulingPolicy::check_compute_balance() const {
    std::vector<PolicyAction> actions;
    auto devices = registry_.all_devices();

    // Find overloaded and underloaded devices of the same type
    struct DevUtil {
        std::string id;
        DeviceClass type;
        float util;
    };
    std::vector<DevUtil> utils;
    for (const auto& dev : devices) {
        utils.push_back({dev.id, dev.type, dev.utilization});
    }

    for (const auto& hot : utils) {
        if (hot.util < utilization_threshold_) continue;

        // Find a cold device of the same type
        for (const auto& cold : utils) {
            if (cold.id == hot.id) continue;
            if (cold.type != hot.type) continue;
            if (cold.util > 0.5f) continue;  // Not idle enough

            actions.push_back(PolicyAction{
                .type = PolicyAction::Type::Migrate,
                .device_id = hot.id,
                .target_device_id = cold.id,
                .reason = "load imbalance: " + hot.id + " at " +
                    std::to_string(static_cast<int>(hot.util * 100)) +
                    "%, " + cold.id + " at " +
                    std::to_string(static_cast<int>(cold.util * 100)) + "%",
            });
            break;  // One migration suggestion per hot device
        }
    }
    return actions;
}

std::vector<PolicyAction> SchedulingPolicy::check_thermal() const {
    std::vector<PolicyAction> actions;
    auto devices = registry_.all_devices();

    for (const auto& dev : devices) {
        if (dev.temperature_c > thermal_limit_) {
            actions.push_back(PolicyAction{
                .type = PolicyAction::Type::Throttle,
                .device_id = dev.id,
                .reason = "thermal: " + std::to_string(static_cast<int>(dev.temperature_c)) +
                    "C exceeds limit of " + std::to_string(static_cast<int>(thermal_limit_)) + "C",
            });
        }
    }
    return actions;
}

} // namespace straylight::rhem
```

---

### Task 6: Implement rhem main.cpp + CMakeLists.txt

- [ ] **Step 1: Create `bin/rhem/main.cpp`**

```cpp
// bin/rhem/main.cpp
// straylight-rhem: on-demand heterogeneous execution manager
// Usage: straylight-rhem <command> [options]
// Commands: discover, allocate, migrate, policy

#include <straylight/config.h>
#include <straylight/log.h>

#include "discovery.h"
#include "allocator.h"
#include "migration.h"
#include "policy.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <iostream>
#include <string>

using namespace straylight;
using namespace straylight::rhem;

static void print_usage() {
    std::cerr << "Usage: straylight-rhem <command> [options]\n"
              << "Commands:\n"
              << "  discover           Probe system for compute devices\n"
              << "  allocate           Allocate a workload to a device\n"
              << "    --workload <id>  Workload identifier\n"
              << "    --memory <bytes> Memory requirement\n"
              << "    --type <gpu|cpu> Preferred device type\n"
              << "  migrate            Plan a tensor migration\n"
              << "    --tensor <id>    Tensor identifier\n"
              << "    --from <device>  Source device\n"
              << "    --to <device>    Target device\n"
              << "    --bytes <n>      Tensor size in bytes\n"
              << "  policy             Evaluate scheduling policy\n"
              << "  status             Show device status\n";
}

int main(int argc, char* argv[]) {
    SL_INIT("straylight-rhem", "info");

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    DeviceRegistry registry;

    if (command == "discover") {
        auto r = registry.auto_discover();
        if (!r.has_value()) {
            SL_ERROR("rhem: discovery failed: {}", r.error());
            return 1;
        }

        auto devices = registry.all_devices();
        nlohmann::json out = nlohmann::json::array();
        for (const auto& dev : devices) {
            out.push_back({
                {"id", dev.id},
                {"type", static_cast<int>(dev.type)},
                {"memory_bytes", dev.memory_bytes},
                {"compute_units", dev.compute_units},
                {"vendor", dev.vendor},
            });
        }
        std::cout << out.dump(2) << "\n";
        SL_INFO("rhem: discovered {} devices", devices.size());
    } else if (command == "allocate") {
        registry.auto_discover();
        std::string workload;
        uint64_t memory = 0;
        std::string type_str = "gpu";

        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--workload") == 0 && i + 1 < argc) workload = argv[++i];
            else if (std::strcmp(argv[i], "--memory") == 0 && i + 1 < argc) memory = std::stoull(argv[++i]);
            else if (std::strcmp(argv[i], "--type") == 0 && i + 1 < argc) type_str = argv[++i];
        }

        DeviceClass pref = (type_str == "cpu") ? DeviceClass::CPU : DeviceClass::GPU;
        DeviceAllocator alloc(registry);
        auto r = alloc.allocate(AllocationRequest{workload, memory, pref});
        if (!r.has_value()) {
            SL_ERROR("rhem: {}", r.error());
            return 1;
        }
        std::cout << nlohmann::json({{"device", r.value().device_id},
                                       {"memory_allocated", r.value().memory_allocated}}).dump(2) << "\n";
    } else if (command == "migrate") {
        registry.auto_discover();
        std::string tensor, from, to;
        uint64_t bytes = 0;

        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--tensor") == 0 && i + 1 < argc) tensor = argv[++i];
            else if (std::strcmp(argv[i], "--from") == 0 && i + 1 < argc) from = argv[++i];
            else if (std::strcmp(argv[i], "--to") == 0 && i + 1 < argc) to = argv[++i];
            else if (std::strcmp(argv[i], "--bytes") == 0 && i + 1 < argc) bytes = std::stoull(argv[++i]);
        }

        MigrationPlanner planner(registry);
        auto r = planner.plan(MigrationRequest{tensor, from, to, bytes});
        if (!r.has_value()) {
            SL_ERROR("rhem: {}", r.error());
            return 1;
        }
        auto& plan = r.value();
        std::cout << nlohmann::json({
            {"tensor", plan.tensor_id}, {"source", plan.source},
            {"target", plan.target}, {"bytes", plan.bytes},
            {"estimated_ms", plan.estimated_time_ms},
            {"needs_conversion", plan.requires_format_conversion},
        }).dump(2) << "\n";
    } else if (command == "policy") {
        registry.auto_discover();
        SchedulingPolicy policy(registry);
        auto actions = policy.evaluate();
        nlohmann::json out = nlohmann::json::array();
        for (const auto& a : actions) {
            out.push_back({{"type", static_cast<int>(a.type)},
                            {"device", a.device_id},
                            {"target", a.target_device_id},
                            {"reason", a.reason}});
        }
        std::cout << out.dump(2) << "\n";
    } else if (command == "--help" || command == "help") {
        print_usage();
    } else {
        std::cerr << "error: unknown command: " << command << "\n";
        print_usage();
        return 1;
    }

    return 0;
}
```

- [ ] **Step 2: Create `bin/rhem/CMakeLists.txt`**

```cmake
add_executable(straylight-rhem
    main.cpp
    discovery.cpp
    allocator.cpp
    migration.cpp
    policy.cpp)
target_link_libraries(straylight-rhem PRIVATE straylight-common straylight-ml straylight-hw)
target_include_directories(straylight-rhem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-rhem DESTINATION bin)
```

- [ ] **Step 3: Add `add_subdirectory(bin/rhem)` to root `CMakeLists.txt`**

---

### Task 7: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_rhem` → all 11 pass
- [ ] `git add bin/rhem/ tests/unit/subsystems/test_rhem.cpp`
- [ ] `git commit -m "feat(rhem): implement device discovery, allocation, migration, and scheduling policy"`

---

## Chunk 8: Tests + CMake Integration for All 5 Subsystems

Final integration chunk: ensures all test targets compile together, adds subsystem subdirectories to root CMake, and runs the full test suite.

### Task 1: Update root CMakeLists.txt

- [ ] **Step 1: Add subsystem subdirectories to `CMakeLists.txt`**

Add the following lines to the `# Subsystem binaries` section:

```cmake
# ML subsystem binaries (Plan 5)
add_subdirectory(bin/agent)
add_subdirectory(bin/compiler)
add_subdirectory(bin/morph)
add_subdirectory(bin/snn)
add_subdirectory(bin/rhem)
```

---

### Task 2: Update tests/unit/subsystems/CMakeLists.txt

- [ ] **Step 1: Add all Plan 5 test targets** (gathered from chunks 1-7)

```cmake
# --- Plan 5: ML Subsystem tests ---

# straylight-agent tests
add_executable(test_agent_queue test_agent_queue.cpp
    ${PROJECT_SOURCE_DIR}/bin/agent/task_queue.cpp)
target_include_directories(test_agent_queue PRIVATE ${PROJECT_SOURCE_DIR}/bin/agent)
target_link_libraries(test_agent_queue PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_agent_queue)

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

# straylight-compiler tests
add_executable(test_compiler_ir test_compiler_ir.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/graph.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/passes.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/lowering.cpp)
target_include_directories(test_compiler_ir PRIVATE ${PROJECT_SOURCE_DIR}/bin/compiler)
target_link_libraries(test_compiler_ir PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_compiler_ir)

add_executable(test_compiler_backends test_compiler_backends.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/graph.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/passes.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/ir/lowering.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/cpu.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/cuda.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/backends/rocm.cpp
    ${PROJECT_SOURCE_DIR}/bin/compiler/cache.cpp)
target_include_directories(test_compiler_backends PRIVATE ${PROJECT_SOURCE_DIR}/bin/compiler)
target_link_libraries(test_compiler_backends PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_compiler_backends)

# straylight-morph tests
add_executable(test_morph test_morph.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/quantize.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/prune.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/distill.cpp
    ${PROJECT_SOURCE_DIR}/bin/morph/adapt.cpp)
target_include_directories(test_morph PRIVATE ${PROJECT_SOURCE_DIR}/bin/morph)
target_link_libraries(test_morph PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_morph)

# straylight-snn tests
add_executable(test_snn test_snn.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/neuron.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/network.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/plasticity.cpp
    ${PROJECT_SOURCE_DIR}/bin/snn/simulator.cpp)
target_include_directories(test_snn PRIVATE ${PROJECT_SOURCE_DIR}/bin/snn)
target_link_libraries(test_snn PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_snn)

# straylight-rhem tests
add_executable(test_rhem test_rhem.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/discovery.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/allocator.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/migration.cpp
    ${PROJECT_SOURCE_DIR}/bin/rhem/policy.cpp)
target_include_directories(test_rhem PRIVATE ${PROJECT_SOURCE_DIR}/bin/rhem)
target_link_libraries(test_rhem PRIVATE straylight-common straylight-ml GTest::gtest_main)
gtest_discover_tests(test_rhem)
```

---

### Task 3: Run full test suite

- [ ] Run: `cmake --build build` → all 5 binaries + 7 test binaries compile
- [ ] Run: `ctest --test-dir build -R "test_agent|test_compiler|test_morph|test_snn|test_rhem"` → all 56 tests pass:
  - test_agent_queue: 6 tests
  - test_agent_worker: 4 tests
  - test_agent_distribution: 5 tests
  - test_compiler_ir: 7 tests
  - test_compiler_backends: 6 tests
  - test_morph: 9 tests
  - test_snn: 11 tests
  - test_rhem: 11 tests (total: 59 tests)

---

### Task 4: Final commit

- [ ] `git add CMakeLists.txt tests/unit/subsystems/CMakeLists.txt`
- [ ] `git commit -m "feat(ml): integrate all 5 ML subsystem binaries and test suite"`

---

## Summary

| Chunk | Binary | Type | Files | Tests |
|-------|--------|------|-------|-------|
| 1 | straylight-agent (event loop + queue) | daemon | 7 | 6 |
| 2 | straylight-agent (worker pool + dist) | daemon | 4 | 9 |
| 3 | straylight-compiler (IR) | tool | 6 | 7 |
| 4 | straylight-compiler (backends + cache) | tool | 8 | 6 |
| 5 | straylight-morph | tool | 9 | 9 |
| 6 | straylight-snn | tool | 9 | 11 |
| 7 | straylight-rhem | tool | 9 | 11 |
| 8 | CMake integration | - | 2 | - |
| **Total** | **5 binaries** | | **54 files** | **59 tests** |

**Dependencies between chunks:**
- Chunk 2 depends on Chunk 1 (uses TaskQueue)
- Chunk 4 depends on Chunk 3 (uses IRGraph, passes, lowering)
- Chunks 5, 6, 7 are independent of each other
- Chunk 8 depends on all previous chunks
