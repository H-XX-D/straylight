# Plan 2: Core Daemons — bus, registry, entropy, scheduler, core

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 5 core daemons that form the StrayLight userspace backbone. Each daemon links against `libstraylight-common` (Plan 1). After this plan, `multi-user.target` is fully populated.

**Architecture:** Five binaries under `bin/` (per the spec's `subsystems/` mapping). All share a common daemon base pattern defined once in Task 1 and referenced thereafter. Startup order enforced by systemd: entropy → bus → registry → scheduler → core.

**Tech Stack:** C++20, sdbus-c++ 2.0+, nlohmann/json 3.11+, spdlog 1.13+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, libstraylight-hw)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). macOS cannot build — uses cgroup v2 `/sys/fs/cgroup`, D-Bus session management, `/dev/urandom`, `SIGTERM`/`SIGINT` process lifecycle.

---

## Common Daemon Base Pattern

Defined here; all five daemons use this without repetition.

### File Structure (shared pattern)

```
bin/<name>/
├── CMakeLists.txt
├── main.cpp           # Signal handling + DaemonBase subclass
├── <name>_daemon.h
└── <name>_daemon.cpp
tests/unit/subsystems/
└── test_<name>.cpp
etc/systemd/system/
└── straylight-<name>.service
```

### DaemonBase (lives in libstraylight-common)

Add to `lib/common/include/straylight/daemon.h`:

```cpp
class DaemonBase {
public:
    virtual ~DaemonBase() = default;

    // Called once after signal handlers are installed
    virtual Result<void, SLError> init(const Config& cfg) = 0;

    // Called in a loop until shutdown requested
    virtual Result<void, SLError> tick() = 0;

    // Called on SIGTERM/SIGINT before process exit
    virtual void shutdown() = 0;

    // Blocks until shutdown signal; calls tick() in loop
    int run(const Config& cfg);
};
```

`DaemonBase::run()` implementation (add to `lib/common/src/daemon.cpp`):

```cpp
// Thread-local flag set by signal handler
static std::atomic<bool> g_shutdown{false};

int DaemonBase::run(const Config& cfg) {
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_shutdown.store(true); };
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    if (auto r = init(cfg); !r) {
        SL_ERROR("daemon init failed: {}", r.error().message());
        return 1;
    }
    SL_INFO("daemon started (pid={})", getpid());

    while (!g_shutdown.load()) {
        if (auto r = tick(); !r) {
            SL_ERROR("tick error: {}", r.error().message());
            break;
        }
    }
    shutdown();
    SL_INFO("daemon stopped");
    return 0;
}
```

### main.cpp template (all daemons follow this)

```cpp
int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/<name>.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-<name>", cfg.log_level());
    <Name>Daemon daemon;
    return daemon.run(cfg);
}
```

### CMakeLists.txt template (all daemons follow this)

```cmake
add_executable(straylight-<name> main.cpp <name>_daemon.cpp)
target_link_libraries(straylight-<name> PRIVATE straylight-common)
target_include_directories(straylight-<name> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-<name> DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../etc/systemd/system/straylight-<name>.service
        DESTINATION lib/systemd/system)
```

---

## Chunk 1: straylight-bus

`bin/bus/` — D-Bus session management. Wraps sdbus-c++ to provide a StrayLight-specific bus abstraction. Other daemons register objects on this bus. Forwards signals between registered clients.

### File Structure

```
bin/bus/
├── CMakeLists.txt
├── main.cpp
├── bus_daemon.h
└── bus_daemon.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_bus.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(BusRegistry, RegisterAndLookup) {
    BusDaemon bus;
    EXPECT_TRUE(bus.register_service("org.straylight.Test", 1001).has_value());
    auto owner = bus.lookup_owner("org.straylight.Test");
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, 1001u);
}

TEST(BusRegistry, DuplicateRegisterFails) {
    BusDaemon bus;
    ASSERT_TRUE(bus.register_service("org.straylight.Test", 1001).has_value());
    auto r = bus.register_service("org.straylight.Test", 1002);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), SLErrorCode::AlreadyExists);
}

TEST(BusRegistry, UnregisterClearsOwner) {
    BusDaemon bus;
    ASSERT_TRUE(bus.register_service("org.straylight.Test", 1001).has_value());
    bus.unregister_service("org.straylight.Test");
    EXPECT_FALSE(bus.lookup_owner("org.straylight.Test").has_value());
}

TEST(BusSignal, ForwardSignalToSubscribers) {
    BusDaemon bus;
    std::vector<std::string> received;
    bus.subscribe("org.straylight.Test", "TestSignal",
                  [&](const std::string& payload) { received.push_back(payload); });
    bus.emit("org.straylight.Test", "TestSignal", "hello");
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], "hello");
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_bus test_bus.cpp)
target_link_libraries(test_bus PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_bus)
```

Run: `cmake --build build --target test_bus && ctest --test-dir build -R test_bus` → expect 4 failures.

---

### Task 2: Implement bus_daemon

**Files:** `bin/bus/bus_daemon.h`, `bin/bus/bus_daemon.cpp`

- [ ] **Step 1: Header**

```cpp
// bus_daemon.h
#pragma once
#include <straylight/common.h>
#include <straylight/daemon.h>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight {

using SignalHandler = std::function<void(const std::string&)>;

class BusDaemon : public DaemonBase {
public:
    Result<void, SLError> init(const Config& cfg) override;
    Result<void, SLError> tick() override;
    void shutdown() override;

    // Service registry (name → owner pid)
    Result<void, SLError> register_service(const std::string& name, pid_t owner);
    void unregister_service(const std::string& name);
    std::optional<pid_t> lookup_owner(const std::string& name) const;

    // Signal forwarding
    void subscribe(const std::string& service, const std::string& signal,
                   SignalHandler handler);
    void emit(const std::string& service, const std::string& signal,
              const std::string& payload);

private:
    std::unordered_map<std::string, pid_t> service_registry_;
    std::unordered_map<std::string, std::vector<SignalHandler>> subscriptions_;
    std::unique_ptr<sdbus::IConnection> dbus_conn_;
    mutable std::mutex mutex_;
};

} // namespace straylight
```

- [ ] **Step 2: Implementation**

```cpp
// bus_daemon.cpp  — unique logic only; boilerplate from DaemonBase
Result<void, SLError> BusDaemon::init(const Config& cfg) {
    try {
        dbus_conn_ = sdbus::createSystemBusConnection("org.straylight.Bus1");
    } catch (const sdbus::Error& e) {
        return std::unexpected(SLError{SLErrorCode::IpcFailed, e.getMessage()});
    }
    SL_INFO("bus: D-Bus connection established");
    return {};
}

Result<void, SLError> BusDaemon::tick() {
    // Process pending D-Bus events with 100ms timeout
    dbus_conn_->processPendingRequest();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return {};
}

void BusDaemon::shutdown() {
    SL_INFO("bus: releasing D-Bus connection");
    dbus_conn_.reset();
}

Result<void, SLError> BusDaemon::register_service(const std::string& name, pid_t owner) {
    std::lock_guard lock(mutex_);
    if (service_registry_.contains(name))
        return std::unexpected(SLError{SLErrorCode::AlreadyExists,
                                       "service already registered: " + name});
    service_registry_[name] = owner;
    SL_DEBUG("bus: registered service {} (pid={})", name, owner);
    return {};
}

void BusDaemon::unregister_service(const std::string& name) {
    std::lock_guard lock(mutex_);
    service_registry_.erase(name);
    subscriptions_.erase(name);
}

std::optional<pid_t> BusDaemon::lookup_owner(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = service_registry_.find(name);
    if (it == service_registry_.end()) return std::nullopt;
    return it->second;
}

void BusDaemon::subscribe(const std::string& service, const std::string& signal,
                           SignalHandler handler) {
    std::lock_guard lock(mutex_);
    subscriptions_[service + "." + signal].push_back(std::move(handler));
}

void BusDaemon::emit(const std::string& service, const std::string& signal,
                     const std::string& payload) {
    std::lock_guard lock(mutex_);
    auto key = service + "." + signal;
    if (auto it = subscriptions_.find(key); it != subscriptions_.end())
        for (auto& h : it->second) h(payload);
}
```

- [ ] **Step 3: Add `bin/bus/CMakeLists.txt`** (follow template, add `sdbus-c++` link)

```cmake
add_executable(straylight-bus main.cpp bus_daemon.cpp)
target_link_libraries(straylight-bus PRIVATE straylight-common sdbus-c++)
target_include_directories(straylight-bus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-bus DESTINATION bin)
```

- [ ] **Step 4: Add `add_subdirectory(bin/bus)` to root `CMakeLists.txt`**

Run: `cmake --build build --target straylight-bus` → expect success.

---

### Task 3: systemd service file

**File:** `etc/systemd/system/straylight-bus.service`

- [ ] **Step 1: Create service file**

```ini
[Unit]
Description=StrayLight D-Bus Session Manager
Documentation=https://straylight.dev/docs/bus
After=dbus.service
Requires=dbus.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-bus
Restart=on-failure
RestartSec=2s
User=root
LimitNOFILE=65536
WatchdogSec=30s

[Install]
WantedBy=multi-user.target
```

---

### Task 4: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_bus` → all 4 pass
- [ ] `git add bin/bus/ tests/unit/subsystems/test_bus.cpp etc/systemd/system/straylight-bus.service`
- [ ] `git commit -m "feat(bus): implement straylight-bus D-Bus session manager"`

---

## Chunk 2: straylight-registry

`bin/registry/` — Key-value configuration store. In-memory `std::map` with JSON persistence to `/var/lib/straylight/registry.json`. Exposes get/set/watch over D-Bus (single-node; no Raft yet).

### File Structure

```
bin/registry/
├── CMakeLists.txt
├── main.cpp
├── registry_daemon.h
├── registry_daemon.cpp
├── store.h
└── store.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_registry.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(RegistryStore, SetAndGet) {
    Store store;
    store.set("network.hostname", "straylight-dev");
    auto v = store.get("network.hostname");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "straylight-dev");
}

TEST(RegistryStore, GetMissingKey) {
    Store store;
    EXPECT_FALSE(store.get("does.not.exist").has_value());
}

TEST(RegistryStore, Watch) {
    Store store;
    std::string last_value;
    store.watch("ui.theme", [&](const std::string& v) { last_value = v; });
    store.set("ui.theme", "cyberpunk");
    EXPECT_EQ(last_value, "cyberpunk");
    store.set("ui.theme", "minimal");
    EXPECT_EQ(last_value, "minimal");
}

TEST(RegistryStore, JsonRoundtrip) {
    Store store;
    store.set("a.b", "1");
    store.set("a.c", "2");
    auto json_str = store.serialize();
    Store store2;
    ASSERT_TRUE(store2.deserialize(json_str).has_value());
    EXPECT_EQ(store2.get("a.b"), std::optional<std::string>{"1"});
    EXPECT_EQ(store2.get("a.c"), std::optional<std::string>{"2"});
}

TEST(RegistryStore, Delete) {
    Store store;
    store.set("tmp.key", "val");
    store.del("tmp.key");
    EXPECT_FALSE(store.get("tmp.key").has_value());
}
```

Run: `cmake --build build --target test_registry && ctest --test-dir build -R test_registry` → expect 5 failures.

---

### Task 2: Implement store and registry_daemon

**File:** `bin/registry/store.h`

```cpp
class Store {
public:
    void set(const std::string& key, std::string value);
    std::optional<std::string> get(const std::string& key) const;
    void del(const std::string& key);
    void watch(const std::string& key,
               std::function<void(const std::string&)> callback);

    std::string serialize() const;   // returns JSON string
    Result<void, SLError> deserialize(const std::string& json);

private:
    std::map<std::string, std::string> data_;
    std::unordered_map<std::string,
        std::vector<std::function<void(const std::string&)>>> watchers_;
    mutable std::shared_mutex mutex_;

    void notify_watchers(const std::string& key, const std::string& value);
};
```

- [ ] **Step 1: Implement `store.cpp`** (unique logic: watcher notification, JSON serde)

```cpp
void Store::set(const std::string& key, std::string value) {
    {
        std::unique_lock lock(mutex_);
        data_[key] = value;
    }
    notify_watchers(key, value);
}

std::string Store::serialize() const {
    std::shared_lock lock(mutex_);
    nlohmann::json j = data_;   // map → JSON object
    return j.dump(2);
}

Result<void, SLError> Store::deserialize(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        std::unique_lock lock(mutex_);
        data_ = j.get<std::map<std::string, std::string>>();
        return {};
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(SLError{SLErrorCode::ParseError, e.what()});
    }
}

void Store::notify_watchers(const std::string& key, const std::string& value) {
    std::shared_lock lock(mutex_);
    if (auto it = watchers_.find(key); it != watchers_.end())
        for (auto& cb : it->second) cb(value);
}
```

- [ ] **Step 2: Implement `registry_daemon.cpp`** (unique logic: load/persist, D-Bus exposure)

```cpp
Result<void, SLError> RegistryDaemon::init(const Config& cfg) {
    persist_path_ = cfg.get("registry.persist_path",
                            "/var/lib/straylight/registry.json");

    // Load existing state
    if (std::filesystem::exists(persist_path_)) {
        std::ifstream f(persist_path_);
        std::string content{std::istreambuf_iterator<char>(f), {}};
        if (auto r = store_.deserialize(content); !r)
            SL_WARN("registry: failed to load {}: {}", persist_path_, r.error().message());
        else
            SL_INFO("registry: loaded {} from disk", persist_path_);
    }

    // Expose D-Bus interface
    dbus_obj_ = sdbus::createObject(*dbus_conn_, "/org/straylight/Registry1");
    dbus_obj_->registerMethod("Get").onInterface("org.straylight.Registry1")
        .implementedAs([this](const std::string& key) -> std::string {
            return store_.get(key).value_or("");
        });
    dbus_obj_->registerMethod("Set").onInterface("org.straylight.Registry1")
        .implementedAs([this](const std::string& key, const std::string& val) {
            store_.set(key, val);
            persist();
        });
    dbus_obj_->finishRegistration();
    return {};
}

void RegistryDaemon::persist() {
    std::ofstream f(persist_path_);
    f << store_.serialize();
}
```

`tick()` flushes a dirty flag every 30 seconds (avoids per-set disk I/O).

---

### Task 3: systemd service file

**File:** `etc/systemd/system/straylight-registry.service`

```ini
[Unit]
Description=StrayLight Configuration Registry
After=straylight-bus.service
Requires=straylight-bus.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-registry
Restart=on-failure
RestartSec=2s
StateDirectory=straylight
WatchdogSec=30s

[Install]
WantedBy=multi-user.target
```

---

### Task 4: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_registry` → all 5 pass
- [ ] `git add bin/registry/ tests/unit/subsystems/test_registry.cpp etc/systemd/system/straylight-registry.service`
- [ ] `git commit -m "feat(registry): implement straylight-registry KV store with JSON persistence"`

---

## Chunk 3: straylight-entropy

`bin/entropy/` — Entropy daemon. Wraps `libstraylight-hw`'s `EntropySource`. Maintains a NIST SP 800-90A CTR-DRBG seeded from hardware. Runs health checks every 60s. Exposes `/dev/straylight-entropy` read interface via a Unix socket.

### File Structure

```
bin/entropy/
├── CMakeLists.txt
├── main.cpp
├── entropy_daemon.h
├── entropy_daemon.cpp
└── drbg.h / drbg.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_entropy.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(Drbg, GeneratesNonZeroBytes) {
    CtrDrbg drbg;
    std::array<uint8_t, 32> seed{};
    // Seed with known value
    std::fill(seed.begin(), seed.end(), 0xAB);
    ASSERT_TRUE(drbg.seed(seed).has_value());

    auto out = drbg.generate(32);
    ASSERT_TRUE(out.has_value());
    // Output should not be all zeros
    bool all_zero = std::all_of(out->begin(), out->end(),
                                [](uint8_t b) { return b == 0; });
    EXPECT_FALSE(all_zero);
}

TEST(Drbg, DifferentSeedsDifferentOutput) {
    CtrDrbg drbg1, drbg2;
    std::array<uint8_t, 32> seed1{}, seed2{};
    seed1.fill(0x11);
    seed2.fill(0x22);
    ASSERT_TRUE(drbg1.seed(seed1).has_value());
    ASSERT_TRUE(drbg2.seed(seed2).has_value());

    auto out1 = drbg1.generate(32);
    auto out2 = drbg2.generate(32);
    ASSERT_TRUE(out1.has_value());
    ASSERT_TRUE(out2.has_value());
    EXPECT_NE(*out1, *out2);
}

TEST(Drbg, ReseedChangesOutput) {
    CtrDrbg drbg;
    std::array<uint8_t, 32> seed{};
    seed.fill(0x55);
    ASSERT_TRUE(drbg.seed(seed).has_value());
    auto out1 = drbg.generate(16);

    seed.fill(0x66);
    ASSERT_TRUE(drbg.reseed(seed).has_value());
    auto out2 = drbg.generate(16);

    ASSERT_TRUE(out1.has_value());
    ASSERT_TRUE(out2.has_value());
    EXPECT_NE(*out1, *out2);
}

TEST(EntropyDaemon, HealthCheckPassesWithFakeSource) {
    // Mock EntropySource always returns 32 non-zero bytes
    MockEntropySource mock;
    EntropyDaemon daemon(std::make_unique<MockEntropySource>(mock));
    EXPECT_TRUE(daemon.run_health_check().has_value());
}
```

Run: `cmake --build build --target test_entropy && ctest --test-dir build -R test_entropy` → expect 4 failures.

---

### Task 2: Implement drbg and entropy_daemon

**File:** `bin/entropy/drbg.h`

```cpp
class CtrDrbg {
public:
    Result<void, SLError> seed(std::span<const uint8_t, 32> entropy);
    Result<void, SLError> reseed(std::span<const uint8_t, 32> additional);
    Result<std::vector<uint8_t>, SLError> generate(size_t n_bytes);

private:
    // AES-256 CTR state: key + counter (NIST SP 800-90A §10.2)
    std::array<uint8_t, 32> key_{};
    std::array<uint8_t, 16> counter_{};
    bool seeded_ = false;

    void update(std::span<const uint8_t, 32> provided_data);
    void increment_counter();
};
```

- [ ] **Step 1: Implement `drbg.cpp`** (key unique logic: CTR-DRBG update/generate)

```cpp
// Core DRBG generate — NIST SP 800-90A §10.2.1.5
Result<std::vector<uint8_t>, SLError> CtrDrbg::generate(size_t n_bytes) {
    if (!seeded_)
        return std::unexpected(SLError{SLErrorCode::NotInitialized, "DRBG not seeded"});

    std::vector<uint8_t> output;
    output.reserve(n_bytes);

    // Generate ceil(n_bytes/16) AES blocks
    while (output.size() < n_bytes) {
        increment_counter();
        // AES-256-ECB encrypt counter using key_ → 16 bytes of output
        // Using OpenSSL EVP (available on Debian without extra deps)
        std::array<uint8_t, 16> block{};
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        int len = 0;
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key_.data(), nullptr);
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        EVP_EncryptUpdate(ctx, block.data(), &len, counter_.data(), 16);
        EVP_CIPHER_CTX_free(ctx);
        output.insert(output.end(), block.begin(), block.end());
    }
    output.resize(n_bytes);

    // DRBG update with no additional input
    std::array<uint8_t, 32> zero{};
    update(zero);
    return output;
}
```

- [ ] **Step 2: Implement `entropy_daemon.cpp`** (unique logic: health check, periodic reseed)

```cpp
Result<void, SLError> EntropyDaemon::init(const Config& cfg) {
    reseed_interval_s_ = cfg.get_int("entropy.reseed_interval_s", 3600);
    health_interval_s_ = cfg.get_int("entropy.health_interval_s", 60);

    // Initial seed from hardware
    auto raw = source_->read(32);
    if (!raw) return std::unexpected(raw.error());
    return drbg_.seed(std::span<const uint8_t, 32>(raw->data(), 32));
}

Result<void, SLError> EntropyDaemon::run_health_check() {
    // NIST SP 800-90B continuous health test: read 64 bytes, check not all same
    auto raw = source_->read(64);
    if (!raw) return std::unexpected(raw.error());

    uint8_t first = (*raw)[0];
    bool stuck = std::all_of(raw->begin(), raw->end(),
                             [first](uint8_t b) { return b == first; });
    if (stuck)
        return std::unexpected(SLError{SLErrorCode::HardwareFault,
                                       "entropy source stuck (all bytes identical)"});
    return {};
}

Result<void, SLError> EntropyDaemon::tick() {
    auto now = std::chrono::steady_clock::now();

    if (now - last_health_ > std::chrono::seconds(health_interval_s_)) {
        if (auto r = run_health_check(); !r)
            SL_WARN("entropy: health check failed: {}", r.error().message());
        last_health_ = now;
    }
    if (now - last_reseed_ > std::chrono::seconds(reseed_interval_s_)) {
        if (auto raw = source_->read(32); raw)
            drbg_.reseed(std::span<const uint8_t, 32>(raw->data(), 32));
        last_reseed_ = now;
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    return {};
}
```

- [ ] **Step 3: `bin/entropy/CMakeLists.txt`** (links `straylight-hw` in addition to `straylight-common`)

```cmake
add_executable(straylight-entropy main.cpp entropy_daemon.cpp drbg.cpp)
target_link_libraries(straylight-entropy PRIVATE straylight-common straylight-hw ssl crypto)
```

---

### Task 3: systemd service file

**File:** `etc/systemd/system/straylight-entropy.service`

```ini
[Unit]
Description=StrayLight Hardware Entropy Daemon
Documentation=https://straylight.dev/docs/entropy
# Starts early — registry and bus both need entropy
DefaultDependencies=no
After=sysinit.target local-fs.target

[Service]
Type=notify
ExecStart=/usr/bin/straylight-entropy
Restart=on-failure
RestartSec=1s
WatchdogSec=120s
# Entropy daemon needs hardware access
PrivateTmp=true
ProtectSystem=strict
ReadWritePaths=/var/lib/straylight

[Install]
WantedBy=multi-user.target
```

---

### Task 4: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_entropy` → all 4 pass
- [ ] `git add bin/entropy/ tests/unit/subsystems/test_entropy.cpp etc/systemd/system/straylight-entropy.service`
- [ ] `git commit -m "feat(entropy): implement straylight-entropy CTR-DRBG with NIST health checks"`

---

## Chunk 4: straylight-scheduler

`bin/scheduler/` — Userspace task scheduler. Manages cgroup v2 resource allocation for StrayLight subsystems. Priority queue assigns CPU weight and memory limits per subsystem. Communicates with `straylight-scheduler.ko` via `/proc/straylight/sched` when the kernel module is loaded (graceful degradation if absent).

### File Structure

```
bin/scheduler/
├── CMakeLists.txt
├── main.cpp
├── scheduler_daemon.h
├── scheduler_daemon.cpp
├── topology.h / topology.cpp
└── cgroup.h / cgroup.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_scheduler.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST(Topology, ParsesCpuInfo) {
    // Feed synthetic /proc/cpuinfo-style string
    const std::string fake_cpuinfo = R"(
processor       : 0
core id         : 0
physical id     : 0

processor       : 1
core id         : 0
physical id     : 0

processor       : 2
core id         : 1
physical id     : 0
)";
    Topology topo;
    ASSERT_TRUE(topo.parse_cpuinfo(fake_cpuinfo).has_value());
    EXPECT_EQ(topo.logical_cpu_count(), 3u);
    EXPECT_EQ(topo.physical_core_count(), 2u);
}

TEST(Cgroup, ParsesCpuWeight) {
    // Uses a temp cgroup path for isolation
    std::filesystem::path tmp = std::filesystem::temp_directory_path() /
                                "sl_test_cgroup";
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / "cpu.weight") << "100\n";

    CgroupV2 cg(tmp);
    auto w = cg.read_cpu_weight();
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 100u);

    std::filesystem::remove_all(tmp);
}

TEST(Cgroup, SetsCpuWeight) {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() /
                                "sl_test_cgroup_set";
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / "cpu.weight") << "100\n";

    CgroupV2 cg(tmp);
    ASSERT_TRUE(cg.set_cpu_weight(200).has_value());

    std::ifstream f(tmp / "cpu.weight");
    std::string val;
    f >> val;
    EXPECT_EQ(val, "200");

    std::filesystem::remove_all(tmp);
}

TEST(SchedulerPriority, HigherPriorityGetsMoreWeight) {
    PriorityQueue pq;
    pq.enqueue("straylight-core",   Priority::High);
    pq.enqueue("straylight-agent",  Priority::Normal);
    pq.enqueue("straylight-fuse",   Priority::Low);

    EXPECT_GT(pq.cpu_weight("straylight-core"),
              pq.cpu_weight("straylight-agent"));
    EXPECT_GT(pq.cpu_weight("straylight-agent"),
              pq.cpu_weight("straylight-fuse"));
}
```

Run: `cmake --build build --target test_scheduler && ctest --test-dir build -R test_scheduler` → expect 4 failures.

---

### Task 2: Implement topology, cgroup, scheduler_daemon

- [ ] **Step 1: `topology.cpp`** (unique logic: parse `/proc/cpuinfo`, count P/E cores)

```cpp
Result<void, SLError> Topology::parse_cpuinfo(const std::string& content) {
    std::istringstream iss(content);
    std::string line;
    std::set<std::string> cores;  // "physical_id:core_id"
    int logical_count = 0;
    std::string cur_physical, cur_core;

    while (std::getline(iss, line)) {
        if (line.starts_with("processor")) {
            if (!cur_physical.empty() && !cur_core.empty())
                cores.insert(cur_physical + ":" + cur_core);
            cur_physical.clear(); cur_core.clear();
            ++logical_count;
        } else if (line.starts_with("physical id")) {
            cur_physical = line.substr(line.find(':') + 2);
        } else if (line.starts_with("core id")) {
            cur_core = line.substr(line.find(':') + 2);
        }
    }
    if (!cur_physical.empty() && !cur_core.empty())
        cores.insert(cur_physical + ":" + cur_core);

    logical_count_ = logical_count;
    physical_cores_ = static_cast<unsigned>(cores.size());
    return {};
}
```

- [ ] **Step 2: `cgroup.cpp`** (unique logic: read/write cgroup v2 knobs)

```cpp
// CgroupV2 wraps a /sys/fs/cgroup/<name>/ directory
Result<unsigned, SLError> CgroupV2::read_cpu_weight() const {
    std::ifstream f(path_ / "cpu.weight");
    if (!f) return std::unexpected(SLError{SLErrorCode::NotFound,
                                           "cpu.weight not found: " + path_.string()});
    unsigned w; f >> w;
    return w;
}

Result<void, SLError> CgroupV2::set_cpu_weight(unsigned weight) const {
    std::ofstream f(path_ / "cpu.weight");
    if (!f) return std::unexpected(SLError{SLErrorCode::PermissionDenied,
                                           "cannot write cpu.weight: " + path_.string()});
    f << weight << "\n";
    return {};
}

Result<void, SLError> CgroupV2::set_memory_max(size_t bytes) const {
    std::ofstream f(path_ / "memory.max");
    if (!f) return std::unexpected(SLError{SLErrorCode::PermissionDenied,
                                           "cannot write memory.max"});
    f << bytes << "\n";
    return {};
}
```

- [ ] **Step 3: `scheduler_daemon.cpp`** (unique logic: init cgroup hierarchy, apply profiles)

```cpp
static constexpr std::string_view CGROUP_ROOT = "/sys/fs/cgroup/straylight";

Result<void, SLError> SchedulerDaemon::init(const Config& cfg) {
    // Create StrayLight cgroup hierarchy
    std::filesystem::create_directories(CGROUP_ROOT);

    // Register default subsystems with priority
    register_subsystem("straylight-core",      Priority::High);
    register_subsystem("straylight-bus",       Priority::High);
    register_subsystem("straylight-registry",  Priority::Normal);
    register_subsystem("straylight-entropy",   Priority::Normal);
    register_subsystem("straylight-agent",     Priority::Normal);
    register_subsystem("straylight-fuse",      Priority::Low);

    apply_priorities();

    // Probe for kernel module
    kernel_module_available_ =
        std::filesystem::exists("/proc/straylight/sched");
    if (kernel_module_available_)
        SL_INFO("scheduler: straylight-scheduler.ko detected, enhanced mode active");
    else
        SL_WARN("scheduler: kernel module not loaded, using cgroup-only mode");

    return {};
}

void SchedulerDaemon::apply_priorities() {
    // cpu.weight values: High=800, Normal=100, Low=10 (Linux cgroup v2 range 1-10000)
    static const std::unordered_map<Priority, unsigned> weight_map = {
        {Priority::High,   800},
        {Priority::Normal, 100},
        {Priority::Low,    10},
    };
    for (auto& [name, prio] : subsystems_) {
        auto cg_path = std::filesystem::path(CGROUP_ROOT) / name;
        std::filesystem::create_directories(cg_path);
        CgroupV2 cg(cg_path);
        if (auto r = cg.set_cpu_weight(weight_map.at(prio)); !r)
            SL_WARN("scheduler: failed to set weight for {}: {}", name, r.error().message());
    }
}
```

`tick()` sleeps 5s, then re-reads cgroup stats and logs any out-of-bounds PIDs.

---

### Task 3: systemd service file

**File:** `etc/systemd/system/straylight-scheduler.service`

```ini
[Unit]
Description=StrayLight Userspace Task Scheduler
After=straylight-registry.service
Requires=straylight-registry.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-scheduler
Restart=on-failure
RestartSec=2s
# Needs to write cgroup hierarchy
AmbientCapabilities=CAP_SYS_ADMIN
WatchdogSec=60s

[Install]
WantedBy=multi-user.target
```

---

### Task 4: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_scheduler` → all 4 pass
- [ ] `git add bin/scheduler/ tests/unit/subsystems/test_scheduler.cpp etc/systemd/system/straylight-scheduler.service`
- [ ] `git commit -m "feat(scheduler): implement straylight-scheduler cgroup v2 priority management"`

---

## Chunk 5: straylight-core

`bin/core/` — Central orchestrator. Starts after bus and registry are up. Monitors health of all other daemons by polling their D-Bus `Health()` method. Manages subsystem lifecycle (start/stop/restart) via D-Bus signals and `sd_notify`. This is the "init" of StrayLight userspace.

### File Structure

```
bin/core/
├── CMakeLists.txt
├── main.cpp
├── core_daemon.h
├── core_daemon.cpp
├── pipeline.h / pipeline.cpp
└── doctor.h / doctor.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_core.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// Mock subsystem for testing
struct MockSubsystem {
    std::string name;
    bool healthy = true;
    int restart_count = 0;
};

TEST(Pipeline, SubsystemRegistration) {
    Pipeline pipeline;
    pipeline.register_subsystem("straylight-bus",      SubsystemPriority::Critical);
    pipeline.register_subsystem("straylight-registry", SubsystemPriority::Critical);
    pipeline.register_subsystem("straylight-entropy",  SubsystemPriority::Critical);
    pipeline.register_subsystem("straylight-agent",    SubsystemPriority::Normal);

    EXPECT_EQ(pipeline.subsystem_count(), 4u);
    EXPECT_EQ(pipeline.critical_count(), 3u);
}

TEST(Doctor, ReportsHealthyWhenAllUp) {
    Doctor doctor;
    doctor.record_health("straylight-bus",      HealthStatus::Healthy);
    doctor.record_health("straylight-registry", HealthStatus::Healthy);
    EXPECT_TRUE(doctor.all_healthy());
    EXPECT_EQ(doctor.unhealthy_count(), 0u);
}

TEST(Doctor, DetectsUnhealthy) {
    Doctor doctor;
    doctor.record_health("straylight-bus",      HealthStatus::Healthy);
    doctor.record_health("straylight-registry", HealthStatus::Degraded);
    EXPECT_FALSE(doctor.all_healthy());
    EXPECT_EQ(doctor.unhealthy_count(), 1u);
}

TEST(Doctor, RestartThresholdTracking) {
    Doctor doctor;
    // Simulate 3 consecutive failures → should flag for restart
    for (int i = 0; i < 3; ++i)
        doctor.record_health("straylight-entropy", HealthStatus::Failed);
    EXPECT_TRUE(doctor.needs_restart("straylight-entropy"));
}

TEST(CoreDaemon, StartupOrderIsEnforced) {
    // Core should not mark itself ready until all Critical subsystems healthy
    CoreDaemon core;
    core.register_subsystem("straylight-bus",      SubsystemPriority::Critical);
    core.register_subsystem("straylight-registry", SubsystemPriority::Critical);

    // No health reports yet → not ready
    EXPECT_FALSE(core.is_ready());

    core.on_health_update("straylight-bus",      HealthStatus::Healthy);
    EXPECT_FALSE(core.is_ready());  // still waiting for registry

    core.on_health_update("straylight-registry", HealthStatus::Healthy);
    EXPECT_TRUE(core.is_ready());
}
```

Run: `cmake --build build --target test_core && ctest --test-dir build -R test_core` → expect 5 failures.

---

### Task 2: Implement pipeline, doctor, core_daemon

- [ ] **Step 1: `pipeline.h`**

```cpp
enum class SubsystemPriority { Critical, Normal, Low };

struct SubsystemEntry {
    std::string name;
    SubsystemPriority priority;
    HealthStatus last_health = HealthStatus::Unknown;
};

class Pipeline {
public:
    void register_subsystem(const std::string& name, SubsystemPriority prio);
    size_t subsystem_count() const;
    size_t critical_count() const;
    std::vector<SubsystemEntry>& subsystems();

private:
    std::vector<SubsystemEntry> subsystems_;
};
```

- [ ] **Step 2: `doctor.cpp`** (unique logic: consecutive failure counting, restart decision)

```cpp
// Health records: name → consecutive failure count
void Doctor::record_health(const std::string& name, HealthStatus status) {
    std::lock_guard lock(mutex_);
    health_[name] = status;
    if (status == HealthStatus::Failed || status == HealthStatus::Degraded)
        ++fail_streak_[name];
    else
        fail_streak_[name] = 0;
}

bool Doctor::needs_restart(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = fail_streak_.find(name);
    return it != fail_streak_.end() && it->second >= kRestartThreshold;
}

bool Doctor::all_healthy() const {
    std::lock_guard lock(mutex_);
    return std::all_of(health_.begin(), health_.end(),
        [](const auto& kv) { return kv.second == HealthStatus::Healthy; });
}
```

- [ ] **Step 3: `core_daemon.cpp`** (unique logic: health polling loop, sd_notify, restart via systemctl)

```cpp
Result<void, SLError> CoreDaemon::init(const Config& cfg) {
    poll_interval_s_ = cfg.get_int("core.health_poll_interval_s", 10);
    restart_max_ = cfg.get_int("core.restart_max", 5);

    // Connect to D-Bus to poll subsystem health
    dbus_conn_ = sdbus::createSystemBusConnection("org.straylight.Core1");

    // Register known subsystems
    register_subsystem("straylight-entropy",  SubsystemPriority::Critical);
    register_subsystem("straylight-bus",      SubsystemPriority::Critical);
    register_subsystem("straylight-registry", SubsystemPriority::Critical);
    register_subsystem("straylight-scheduler",SubsystemPriority::Normal);

    SL_INFO("core: initialized, monitoring {} subsystems",
            pipeline_.subsystem_count());
    return {};
}

Result<void, SLError> CoreDaemon::tick() {
    // Poll each subsystem's Health() D-Bus method
    for (auto& entry : pipeline_.subsystems()) {
        auto proxy = sdbus::createProxy(*dbus_conn_,
                                        "org.straylight." + entry.name,
                                        "/org/straylight/Health");
        try {
            std::string status;
            proxy->callMethod("Health").onInterface("org.straylight.Health1")
                 .storeResultsTo(status);
            auto hs = parse_health_status(status);
            doctor_.record_health(entry.name, hs);
            on_health_update(entry.name, hs);
        } catch (const sdbus::Error& e) {
            SL_WARN("core: health poll failed for {}: {}", entry.name, e.getMessage());
            doctor_.record_health(entry.name, HealthStatus::Failed);
        }

        if (doctor_.needs_restart(entry.name)) {
            auto& count = restart_counts_[entry.name];
            if (count < restart_max_) {
                SL_WARN("core: restarting {} (attempt {}/{})",
                        entry.name, count + 1, restart_max_);
                restart_subsystem(entry.name);
                ++count;
            } else {
                SL_ERROR("core: {} exceeded max restarts, marking degraded", entry.name);
            }
        }
    }

    // Notify systemd we are still alive (watchdog keepalive)
    sd_notify(0, "WATCHDOG=1");

    std::this_thread::sleep_for(std::chrono::seconds(poll_interval_s_));
    return {};
}

void CoreDaemon::restart_subsystem(const std::string& name) {
    // Use systemd D-Bus API to restart the unit
    auto systemd = sdbus::createProxy(*dbus_conn_,
                                      "org.freedesktop.systemd1",
                                      "/org/freedesktop/systemd1");
    systemd->callMethod("RestartUnit")
            .onInterface("org.freedesktop.systemd1.Manager")
            .withArguments(name + ".service", std::string("replace"));
}
```

`is_ready()` returns true when all Critical subsystems have reported `Healthy` at least once.

- [ ] **Step 4: `bin/core/CMakeLists.txt`**

```cmake
add_executable(straylight-core main.cpp core_daemon.cpp pipeline.cpp doctor.cpp)
target_link_libraries(straylight-core PRIVATE straylight-common sdbus-c++ systemd)
```

---

### Task 3: systemd service file

**File:** `etc/systemd/system/straylight-core.service`

```ini
[Unit]
Description=StrayLight Core Orchestrator
Documentation=https://straylight.dev/docs/core
After=straylight-bus.service straylight-registry.service straylight-scheduler.service
Requires=straylight-bus.service straylight-registry.service

[Service]
Type=notify
NotifyAccess=main
ExecStart=/usr/bin/straylight-core
Restart=on-failure
RestartSec=3s
# Core monitors and restarts other daemons — needs elevated privilege
AmbientCapabilities=CAP_KILL CAP_SYS_ADMIN
WatchdogSec=60s

[Install]
WantedBy=multi-user.target
```

---

### Task 4: Tests pass + commit

- [ ] Run: `ctest --test-dir build -R test_core` → all 5 pass
- [ ] `git add bin/core/ tests/unit/subsystems/test_core.cpp etc/systemd/system/straylight-core.service`
- [ ] `git commit -m "feat(core): implement straylight-core health monitoring and subsystem lifecycle"`

---

## Chunk 6: Integration + Root CMake wiring

### Task 1: Wire all daemons into root CMakeLists.txt

- [ ] **Step 1: Add daemon subdirectories to root `CMakeLists.txt`**

```cmake
# Core daemons (Plan 2)
if(BUILD_SUBSYSTEMS)
    add_subdirectory(bin/bus)
    add_subdirectory(bin/registry)
    add_subdirectory(bin/entropy)
    add_subdirectory(bin/scheduler)
    add_subdirectory(bin/core)
endif()
```

- [ ] **Step 2: Add `DaemonBase` to libstraylight-common**

```cmake
# In lib/common/CMakeLists.txt — add daemon.cpp to sources
target_sources(straylight-common PRIVATE src/daemon.cpp)
```

- [ ] **Step 3: Full build check**

```bash
cmake --preset dev
cmake --build build --parallel $(nproc)
```

Expect: 5 daemon binaries built, all 18 unit tests passing.

---

### Task 2: Integration test — daemon startup ordering

**File:** `tests/integration/test_daemon_startup.cpp`

- [ ] **Step 1: Write integration test**

```cpp
// Verifies that the startup dependency graph is consistent:
// entropy must be ready before registry, bus before core.
TEST(DaemonStartup, DependencyOrderConsistent) {
    // Parse all .service files and build a dependency graph
    ServiceGraph graph;
    graph.load_service_dir("etc/systemd/system/");

    // entropy has no straylight deps
    EXPECT_TRUE(graph.dependencies("straylight-entropy.service").empty() ||
                !graph.has_dep("straylight-entropy.service", "straylight-bus.service"));

    // bus depends on dbus but not on other straylight services
    EXPECT_FALSE(graph.has_dep("straylight-bus.service",
                               "straylight-core.service"));

    // core depends on bus and registry
    EXPECT_TRUE(graph.has_dep("straylight-core.service",
                              "straylight-bus.service"));
    EXPECT_TRUE(graph.has_dep("straylight-core.service",
                              "straylight-registry.service"));

    // No cycles
    EXPECT_FALSE(graph.has_cycle());
}
```

- [ ] **Step 2: Add to `tests/integration/CMakeLists.txt`**

```cmake
add_executable(test_daemon_startup test_daemon_startup.cpp service_graph.cpp)
target_link_libraries(test_daemon_startup PRIVATE straylight-common GTest::gtest_main)
gtest_discover_tests(test_daemon_startup)
```

---

### Task 3: Final commit

- [ ] Run full test suite: `ctest --test-dir build --parallel $(nproc)`
- [ ] All tests pass (unit + integration)
- [ ] `git add lib/common/include/straylight/daemon.h lib/common/src/daemon.cpp`
- [ ] `git add tests/integration/test_daemon_startup.cpp`
- [ ] `git add CMakeLists.txt lib/common/CMakeLists.txt`
- [ ] `git commit -m "feat(plan2): wire all 5 core daemons into build + integration test"`

---

## Summary

| Daemon | Binary | Service | Priority | D-Bus name |
|--------|--------|---------|----------|------------|
| straylight-bus | `bin/bus/` | `straylight-bus.service` | High | `org.straylight.Bus1` |
| straylight-registry | `bin/registry/` | `straylight-registry.service` | Normal | `org.straylight.Registry1` |
| straylight-entropy | `bin/entropy/` | `straylight-entropy.service` | Critical (early) | — |
| straylight-scheduler | `bin/scheduler/` | `straylight-scheduler.service` | Normal | `org.straylight.Scheduler1` |
| straylight-core | `bin/core/` | `straylight-core.service` | Critical | `org.straylight.Core1` |

**Startup order:** entropy → bus → registry → scheduler → core

**Plan 3 depends on:** straylight-bus and straylight-registry running (ML agent daemon uses both for task distribution and config lookup).
