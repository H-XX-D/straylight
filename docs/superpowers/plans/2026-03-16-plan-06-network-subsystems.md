# Plan 6: Network Subsystems — xdp, dpdk, rdma-bus

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 3 network subsystem binaries that provide high-performance packet processing and zero-copy tensor transport. All are on-demand tools (no DaemonBase), invoked by `straylight-core` or CLI.

**Architecture:** Three binaries under `bin/` — each is a standalone CLI tool that initializes its subsystem, performs work, and exits. No systemd services.

**Tech Stack:** C++20, libbpf 1.3+, DPDK 23.11+, libibverbs + librdmacm, spdlog 1.13+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plan 1 (libstraylight-common, libstraylight-net, libstraylight-ml)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Requires kernel 5.15+ for AF_XDP, DPDK hugepages, RDMA-capable NIC or rxe (soft-RoCE).

---

## Common Tool Pattern

These are on-demand tools, not daemons. Each binary parses CLI args, runs its operation, and exits.

### main.cpp template (all tools follow this)

```cpp
int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/<name>.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-<name>", cfg.log_level());
    // Parse subcommand, dispatch, return exit code
}
```

---

## Chunk 1: straylight-xdp — BPF Loader & Maps

`bin/xdp/` — Loads eBPF programs via libbpf, manages BPF maps, creates AF_XDP sockets.

### File Structure

```
bin/xdp/
├── CMakeLists.txt
├── main.cpp
├── loader.h / loader.cpp
├── maps.h / maps.cpp
└── af_xdp.h / af_xdp.cpp
tests/unit/subsystems/
└── test_xdp.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_xdp.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "loader.h"
#include "maps.h"
#include "af_xdp.h"

TEST(XdpLoader, LoadInvalidPathFails) {
    xdp::Loader loader;
    auto r = loader.load("/nonexistent.bpf.o", "xdp_prog");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), SLErrorCode::IoError);
}

TEST(XdpLoader, AttachRequiresLoad) {
    xdp::Loader loader;
    auto r = loader.attach("eth0");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), SLErrorCode::InvalidState);
}

TEST(BpfMap, CreateAndLookup) {
    xdp::BpfMapManager mgr;
    auto r = mgr.create_hash_map("test_map", sizeof(uint32_t), sizeof(uint64_t), 64);
    ASSERT_TRUE(r.has_value());
    uint32_t key = 42;
    uint64_t val = 100;
    EXPECT_TRUE(mgr.update("test_map", &key, &val).has_value());
    auto lookup = mgr.lookup("test_map", &key);
    ASSERT_TRUE(lookup.has_value());
    EXPECT_EQ(*reinterpret_cast<uint64_t*>(lookup->data()), 100u);
}

TEST(AfXdp, SocketRequiresInterface) {
    xdp::AfXdpSocket sock;
    auto r = sock.create("nonexistent_if_999", 0, 2048);
    EXPECT_FALSE(r.has_value());
}
```

- [ ] **Step 2: Add to `tests/unit/subsystems/CMakeLists.txt`**

```cmake
add_executable(test_xdp test_xdp.cpp)
target_link_libraries(test_xdp PRIVATE straylight-common straylight-net bpf GTest::gtest_main)
gtest_discover_tests(test_xdp)
```

Run: `cmake --build build --target test_xdp && ctest --test-dir build -R test_xdp` — expect 4 failures.

---

### Task 2: Implement loader

**Files:** `bin/xdp/loader.h`, `bin/xdp/loader.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <bpf/libbpf.h>
#include <string>

namespace xdp {

class Loader {
public:
    Loader() = default;
    ~Loader();

    // Load BPF object file; find program by name
    Result<void, SLError> load(const std::string& obj_path, const std::string& prog_name);

    // Attach loaded program to interface via XDP
    Result<void, SLError> attach(const std::string& ifname, uint32_t flags = 0);

    // Detach and close
    void detach();

    int prog_fd() const { return prog_fd_; }

private:
    struct bpf_object* obj_ = nullptr;
    struct bpf_program* prog_ = nullptr;
    int prog_fd_ = -1;
    int ifindex_ = 0;
};

} // namespace xdp
```

- [ ] **Step 2: Implementation**

```cpp
#include "loader.h"
#include <net/if.h>
#include <bpf/bpf.h>

namespace xdp {

Loader::~Loader() { detach(); }

Result<void, SLError> Loader::load(const std::string& obj_path,
                                    const std::string& prog_name) {
    obj_ = bpf_object__open(obj_path.c_str());
    if (!obj_)
        return SLError{SLErrorCode::IoError, "bpf_object__open failed: " + obj_path};

    if (bpf_object__load(obj_) != 0) {
        bpf_object__close(obj_); obj_ = nullptr;
        return SLError{SLErrorCode::IoError, "bpf_object__load failed"};
    }

    prog_ = bpf_object__find_program_by_name(obj_, prog_name.c_str());
    if (!prog_)
        return SLError{SLErrorCode::NotFound, "program not found: " + prog_name};

    prog_fd_ = bpf_program__fd(prog_);
    SL_INFO("loaded BPF prog '{}' fd={}", prog_name, prog_fd_);
    return Result<void, SLError>::ok();
}

Result<void, SLError> Loader::attach(const std::string& ifname, uint32_t flags) {
    if (prog_fd_ < 0)
        return SLError{SLErrorCode::InvalidState, "no program loaded"};

    ifindex_ = if_nametoindex(ifname.c_str());
    if (ifindex_ == 0)
        return SLError{SLErrorCode::IoError, "unknown interface: " + ifname};

    if (bpf_xdp_attach(ifindex_, prog_fd_, flags, nullptr) != 0)
        return SLError{SLErrorCode::IoError, "bpf_xdp_attach failed"};

    SL_INFO("attached XDP to {} (ifindex={})", ifname, ifindex_);
    return Result<void, SLError>::ok();
}

void Loader::detach() {
    if (ifindex_ > 0) {
        bpf_xdp_detach(ifindex_, 0, nullptr);
        ifindex_ = 0;
    }
    if (obj_) { bpf_object__close(obj_); obj_ = nullptr; }
    prog_ = nullptr;
    prog_fd_ = -1;
}

} // namespace xdp
```

---

### Task 3: Implement maps

**Files:** `bin/xdp/maps.h`, `bin/xdp/maps.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <bpf/bpf.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace xdp {

class BpfMapManager {
public:
    Result<void, SLError> create_hash_map(const std::string& name,
                                           uint32_t key_size, uint32_t val_size,
                                           uint32_t max_entries);

    Result<void, SLError> update(const std::string& name, const void* key, const void* val);
    Result<std::vector<uint8_t>, SLError> lookup(const std::string& name, const void* key);
    Result<void, SLError> delete_key(const std::string& name, const void* key);

    ~BpfMapManager();

private:
    struct MapInfo { int fd; uint32_t key_size; uint32_t val_size; };
    std::unordered_map<std::string, MapInfo> maps_;
};

} // namespace xdp
```

- [ ] **Step 2: Implementation**

```cpp
#include "maps.h"
namespace xdp {

BpfMapManager::~BpfMapManager() {
    for (auto& [_, info] : maps_) close(info.fd);
}

Result<void, SLError> BpfMapManager::create_hash_map(
    const std::string& name, uint32_t key_size, uint32_t val_size, uint32_t max_entries) {
    BPFATTR_INIT(attr);  // zero-init union bpf_attr
    int fd = bpf_map_create(BPF_MAP_TYPE_HASH, name.c_str(),
                             key_size, val_size, max_entries, nullptr);
    if (fd < 0)
        return SLError{SLErrorCode::IoError, "bpf_map_create failed: " + name};
    maps_[name] = {fd, key_size, val_size};
    return Result<void, SLError>::ok();
}

Result<void, SLError> BpfMapManager::update(const std::string& name,
                                             const void* key, const void* val) {
    auto it = maps_.find(name);
    if (it == maps_.end())
        return SLError{SLErrorCode::NotFound, "map not found: " + name};
    if (bpf_map_update_elem(it->second.fd, key, val, BPF_ANY) != 0)
        return SLError{SLErrorCode::IoError, "bpf_map_update_elem failed"};
    return Result<void, SLError>::ok();
}

Result<std::vector<uint8_t>, SLError> BpfMapManager::lookup(
    const std::string& name, const void* key) {
    auto it = maps_.find(name);
    if (it == maps_.end())
        return SLError{SLErrorCode::NotFound, "map not found"};
    std::vector<uint8_t> val(it->second.val_size);
    if (bpf_map_lookup_elem(it->second.fd, key, val.data()) != 0)
        return SLError{SLErrorCode::NotFound, "key not found"};
    return val;
}

// delete_key follows same pattern — bpf_map_delete_elem
// ... standard error handling

} // namespace xdp
```

---

### Task 4: Implement af_xdp

**Files:** `bin/xdp/af_xdp.h`, `bin/xdp/af_xdp.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <xdp/xsk.h>
#include <string>
#include <cstdint>

namespace xdp {

class AfXdpSocket {
public:
    ~AfXdpSocket();

    Result<void, SLError> create(const std::string& ifname, uint32_t queue_id,
                                  uint32_t frame_count);

    Result<uint32_t, SLError> recv_batch(void** bufs, uint32_t max_batch);
    Result<void, SLError>     send_batch(const void** bufs, const uint32_t* lens,
                                          uint32_t count);
    void release(uint32_t idx);

    int fd() const;

private:
    struct xsk_socket*    xsk_ = nullptr;
    struct xsk_umem*      umem_ = nullptr;
    void*                 umem_area_ = nullptr;
    size_t                umem_size_ = 0;
    struct xsk_ring_prod  fill_{};
    struct xsk_ring_cons  comp_{};
    struct xsk_ring_prod  tx_{};
    struct xsk_ring_cons  rx_{};
};

} // namespace xdp
```

- [ ] **Step 2: Implementation**

```cpp
#include "af_xdp.h"
#include <sys/mman.h>
#include <net/if.h>

namespace xdp {

AfXdpSocket::~AfXdpSocket() {
    if (xsk_) xsk_socket__delete(xsk_);
    if (umem_) xsk_umem__delete(umem_);
    if (umem_area_) munmap(umem_area_, umem_size_);
}

Result<void, SLError> AfXdpSocket::create(const std::string& ifname,
                                            uint32_t queue_id,
                                            uint32_t frame_count) {
    static constexpr size_t FRAME_SIZE = 4096;
    umem_size_ = frame_count * FRAME_SIZE;

    umem_area_ = mmap(nullptr, umem_size_, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (umem_area_ == MAP_FAILED) {
        // Fallback without hugepages
        umem_area_ = mmap(nullptr, umem_size_, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (umem_area_ == MAP_FAILED)
            return SLError{SLErrorCode::IoError, "mmap failed for UMEM"};
    }

    struct xsk_umem_config ucfg = {
        .fill_size = frame_count, .comp_size = frame_count,
        .frame_size = FRAME_SIZE, .frame_headroom = 0, .flags = 0
    };
    if (xsk_umem__create(&umem_, umem_area_, umem_size_, &fill_, &comp_, &ucfg) != 0)
        return SLError{SLErrorCode::IoError, "xsk_umem__create failed"};

    struct xsk_socket_config scfg = {
        .rx_size = frame_count, .tx_size = frame_count,
        .libxdp_flags = 0, .xdp_flags = 0, .bind_flags = XDP_USE_NEED_WAKEUP
    };
    if (xsk_socket__create(&xsk_, ifname.c_str(), queue_id, umem_,
                            &rx_, &tx_, &scfg) != 0)
        return SLError{SLErrorCode::IoError, "xsk_socket__create failed on " + ifname};

    // Populate fill ring with initial frames
    uint32_t idx = 0;
    if (xsk_ring_prod__reserve(&fill_, frame_count, &idx) != frame_count)
        return SLError{SLErrorCode::IoError, "fill ring reserve failed"};
    for (uint32_t i = 0; i < frame_count; i++)
        *xsk_ring_prod__fill_addr(&fill_, idx + i) = i * FRAME_SIZE;
    xsk_ring_prod__submit(&fill_, frame_count);

    SL_INFO("AF_XDP socket on {}:{} frames={}", ifname, queue_id, frame_count);
    return Result<void, SLError>::ok();
}

Result<uint32_t, SLError> AfXdpSocket::recv_batch(void** bufs, uint32_t max_batch) {
    uint32_t idx = 0;
    uint32_t rcvd = xsk_ring_cons__peek(&rx_, max_batch, &idx);
    for (uint32_t i = 0; i < rcvd; i++) {
        auto* desc = xsk_ring_cons__rx_desc(&rx_, idx + i);
        bufs[i] = static_cast<uint8_t*>(umem_area_) + desc->addr;
    }
    if (rcvd > 0) xsk_ring_cons__release(&rx_, rcvd);
    return rcvd;
}

// send_batch: reserve tx ring, fill descriptors, submit, kick with sendto()
// release: return frame to fill ring
// ... standard error handling

int AfXdpSocket::fd() const { return xsk_ ? xsk_socket__fd(xsk_) : -1; }

} // namespace xdp
```

---

### Task 5: main.cpp and CMakeLists.txt

- [ ] **Step 1: main.cpp**

```cpp
#include <straylight/common.h>
#include "loader.h"
#include "maps.h"
#include "af_xdp.h"
#include <iostream>

static void usage() {
    std::cerr << "Usage: straylight-xdp <command> [args...]\n"
              << "  load <obj> <prog> <iface>   Load BPF program and attach\n"
              << "  socket <iface> <queue>       Create AF_XDP socket and dump stats\n"
              << "  stats <map_name>             Dump BPF map contents\n";
}

int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/xdp.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-xdp", cfg.log_level());

    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "load" && argc >= 5) {
        xdp::Loader loader;
        auto r = loader.load(argv[2], argv[3]);
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        r = loader.attach(argv[4]);
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        SL_INFO("attached — press Ctrl-C to detach");
        pause();  // Wait for signal
    } else if (cmd == "socket" && argc >= 4) {
        xdp::AfXdpSocket sock;
        auto r = sock.create(argv[2], std::stoul(argv[3]), 4096);
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        SL_INFO("AF_XDP socket ready fd={}", sock.fd());
        // ... poll loop for benchmarking
    } else {
        usage(); return 1;
    }
    return 0;
}
```

- [ ] **Step 2: CMakeLists.txt**

```cmake
add_executable(straylight-xdp
    main.cpp loader.cpp maps.cpp af_xdp.cpp)
target_link_libraries(straylight-xdp PRIVATE
    straylight-common straylight-net bpf xdp)
target_include_directories(straylight-xdp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-xdp DESTINATION bin)
```

Run: `cmake --build build --target test_xdp && ctest --test-dir build -R test_xdp` — expect all tests pass.

---

## Chunk 2: straylight-dpdk — Port Manager

`bin/dpdk/` — DPDK EAL initialization and port management.

### File Structure

```
bin/dpdk/
├── CMakeLists.txt
├── main.cpp
├── port.h / port.cpp
├── pipeline.h / pipeline.cpp
├── flow.h / flow.cpp
└── tensor_transport.h / tensor_transport.cpp
tests/unit/subsystems/
└── test_dpdk.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_dpdk.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "port.h"
#include "pipeline.h"
#include "flow.h"
#include "tensor_transport.h"

TEST(DpdkPort, InitWithoutEalFails) {
    dpdk::PortManager mgr;
    auto r = mgr.probe_ports();
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), SLErrorCode::InvalidState);
}

TEST(DpdkPipeline, EmptyPipelineReturnsZero) {
    dpdk::Pipeline pipe;
    EXPECT_EQ(pipe.stage_count(), 0u);
}

TEST(DpdkFlow, RuleValidation) {
    dpdk::FlowRule rule{.src_ip = 0, .dst_ip = 0, .dst_port = 0, .action = dpdk::Action::Drop};
    EXPECT_FALSE(dpdk::FlowTable::validate(rule).has_value());
}

TEST(TensorTransport, SerializeRoundTrip) {
    dpdk::TensorHeader hdr{.shape = {2, 3}, .dtype = DType::Float32, .byte_size = 24};
    auto buf = dpdk::TensorTransport::serialize_header(hdr);
    auto parsed = dpdk::TensorTransport::parse_header(buf.data(), buf.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->shape, hdr.shape);
    EXPECT_EQ(parsed->dtype, hdr.dtype);
}
```

- [ ] **Step 2: CMakeLists.txt entry**

```cmake
add_executable(test_dpdk test_dpdk.cpp)
target_link_libraries(test_dpdk PRIVATE straylight-common straylight-net straylight-ml GTest::gtest_main)
gtest_discover_tests(test_dpdk)
```

---

### Task 2: Implement port

**Files:** `bin/dpdk/port.h`, `bin/dpdk/port.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <rte_ethdev.h>
#include <vector>
#include <string>

namespace dpdk {

struct PortInfo {
    uint16_t port_id;
    std::string name;
    struct rte_ether_addr mac;
    uint16_t rx_queues;
    uint16_t tx_queues;
};

class PortManager {
public:
    // Must call after rte_eal_init()
    Result<std::vector<PortInfo>, SLError> probe_ports();
    Result<void, SLError> configure(uint16_t port_id, uint16_t rx_q, uint16_t tx_q,
                                     uint16_t rx_desc = 1024, uint16_t tx_desc = 1024);
    Result<void, SLError> start(uint16_t port_id);
    void stop(uint16_t port_id);

    bool eal_initialized() const { return eal_init_; }
    void set_eal_initialized() { eal_init_ = true; }

private:
    bool eal_init_ = false;
    struct rte_mempool* mbuf_pool_ = nullptr;
    Result<void, SLError> ensure_mempool();
};

} // namespace dpdk
```

- [ ] **Step 2: Implementation**

```cpp
#include "port.h"
#include <rte_mbuf.h>

namespace dpdk {

Result<void, SLError> PortManager::ensure_mempool() {
    if (mbuf_pool_) return Result<void, SLError>::ok();
    mbuf_pool_ = rte_pktmbuf_pool_create("SL_MBUF", 8192, 256, 0,
                                          RTE_MBUF_DEFAULT_BUF_SIZE,
                                          rte_socket_id());
    if (!mbuf_pool_)
        return SLError{SLErrorCode::IoError, "rte_pktmbuf_pool_create failed"};
    return Result<void, SLError>::ok();
}

Result<std::vector<PortInfo>, SLError> PortManager::probe_ports() {
    if (!eal_init_)
        return SLError{SLErrorCode::InvalidState, "EAL not initialized"};

    std::vector<PortInfo> ports;
    uint16_t nb = rte_eth_dev_count_avail();
    for (uint16_t p = 0; p < nb; p++) {
        struct rte_eth_dev_info info;
        rte_eth_dev_info_get(p, &info);
        PortInfo pi{.port_id = p, .name = info.device->name,
                     .rx_queues = info.max_rx_queues, .tx_queues = info.max_tx_queues};
        rte_eth_macaddr_get(p, &pi.mac);
        ports.push_back(pi);
    }
    return ports;
}

Result<void, SLError> PortManager::configure(uint16_t port_id, uint16_t rx_q,
                                              uint16_t tx_q, uint16_t rx_desc,
                                              uint16_t tx_desc) {
    auto r = ensure_mempool();
    if (!r) return r;

    struct rte_eth_conf port_conf{};
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    port_conf.rx_adv_conf.rss_conf.rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP;

    if (rte_eth_dev_configure(port_id, rx_q, tx_q, &port_conf) != 0)
        return SLError{SLErrorCode::IoError, "rte_eth_dev_configure failed"};

    for (uint16_t q = 0; q < rx_q; q++)
        if (rte_eth_rx_queue_setup(port_id, q, rx_desc, rte_eth_dev_socket_id(port_id),
                                    nullptr, mbuf_pool_) < 0)
            return SLError{SLErrorCode::IoError, "rx_queue_setup failed q=" + std::to_string(q)};

    for (uint16_t q = 0; q < tx_q; q++)
        if (rte_eth_tx_queue_setup(port_id, q, tx_desc, rte_eth_dev_socket_id(port_id),
                                    nullptr) < 0)
            return SLError{SLErrorCode::IoError, "tx_queue_setup failed q=" + std::to_string(q)};

    SL_INFO("port {} configured rx={} tx={}", port_id, rx_q, tx_q);
    return Result<void, SLError>::ok();
}

Result<void, SLError> PortManager::start(uint16_t port_id) {
    if (rte_eth_dev_start(port_id) != 0)
        return SLError{SLErrorCode::IoError, "rte_eth_dev_start failed"};
    rte_eth_promiscuous_enable(port_id);
    return Result<void, SLError>::ok();
}

void PortManager::stop(uint16_t port_id) {
    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);
}

} // namespace dpdk
```

---

## Chunk 3: straylight-dpdk — Pipeline, Flow, Tensor Transport

### Task 1: Implement pipeline

**Files:** `bin/dpdk/pipeline.h`, `bin/dpdk/pipeline.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <rte_mbuf.h>
#include <functional>
#include <vector>
#include <string>

namespace dpdk {

using StageFn = std::function<bool(struct rte_mbuf* pkt)>;

class Pipeline {
public:
    void add_stage(std::string name, StageFn fn);
    uint32_t stage_count() const { return stages_.size(); }

    // Process burst; returns count of packets that passed all stages
    uint32_t process(struct rte_mbuf** pkts, uint32_t count);

    struct Stats { uint64_t processed; uint64_t dropped; };
    Stats stats() const { return stats_; }

private:
    struct Stage { std::string name; StageFn fn; };
    std::vector<Stage> stages_;
    Stats stats_{};
};

} // namespace dpdk
```

- [ ] **Step 2: Implementation**

```cpp
#include "pipeline.h"
namespace dpdk {

void Pipeline::add_stage(std::string name, StageFn fn) {
    stages_.push_back({std::move(name), std::move(fn)});
}

uint32_t Pipeline::process(struct rte_mbuf** pkts, uint32_t count) {
    uint32_t out = 0;
    for (uint32_t i = 0; i < count; i++) {
        bool pass = true;
        for (auto& stage : stages_) {
            if (!stage.fn(pkts[i])) { pass = false; break; }
        }
        if (pass) pkts[out++] = pkts[i];
        else { rte_pktmbuf_free(pkts[i]); stats_.dropped++; }
    }
    stats_.processed += count;
    return out;
}

} // namespace dpdk
```

---

### Task 2: Implement flow

**Files:** `bin/dpdk/flow.h`, `bin/dpdk/flow.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <rte_flow.h>
#include <vector>

namespace dpdk {

enum class Action { Drop, Forward, Mark, Count };

struct FlowRule {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t dst_port;
    Action action;
    uint16_t forward_queue = 0;  // for Action::Forward
};

class FlowTable {
public:
    static Result<void, SLError> validate(const FlowRule& rule);

    Result<uint32_t, SLError> install(uint16_t port_id, const FlowRule& rule);
    Result<void, SLError> remove(uint16_t port_id, uint32_t rule_id);
    void flush(uint16_t port_id);

private:
    struct Installed { uint32_t id; struct rte_flow* handle; };
    std::vector<Installed> rules_;
    uint32_t next_id_ = 1;
};

} // namespace dpdk
```

- [ ] **Step 2: Implementation**

```cpp
#include "flow.h"
namespace dpdk {

Result<void, SLError> FlowTable::validate(const FlowRule& rule) {
    if (rule.src_ip == 0 && rule.dst_ip == 0 && rule.dst_port == 0)
        return SLError{SLErrorCode::InvalidArgument, "at least one match field required"};
    return Result<void, SLError>::ok();
}

Result<uint32_t, SLError> FlowTable::install(uint16_t port_id, const FlowRule& rule) {
    auto v = validate(rule);
    if (!v) return v.error();

    struct rte_flow_attr attr{.group = 0, .priority = 0, .ingress = 1};
    struct rte_flow_item_ipv4 ip_spec{}, ip_mask{};

    if (rule.dst_ip) {
        ip_spec.hdr.dst_addr = rte_cpu_to_be_32(rule.dst_ip);
        ip_mask.hdr.dst_addr = 0xFFFFFFFF;
    }
    if (rule.src_ip) {
        ip_spec.hdr.src_addr = rte_cpu_to_be_32(rule.src_ip);
        ip_mask.hdr.src_addr = 0xFFFFFFFF;
    }

    struct rte_flow_item pattern[] = {
        {.type = RTE_FLOW_ITEM_TYPE_ETH},
        {.type = RTE_FLOW_ITEM_TYPE_IPV4, .spec = &ip_spec, .mask = &ip_mask},
        {.type = RTE_FLOW_ITEM_TYPE_END}
    };

    // Build action list based on rule.action
    struct rte_flow_action_drop drop_action{};
    struct rte_flow_action_queue queue_action{.index = rule.forward_queue};
    struct rte_flow_action actions[2]{};

    switch (rule.action) {
        case Action::Drop:    actions[0] = {.type = RTE_FLOW_ACTION_TYPE_DROP, .conf = &drop_action}; break;
        case Action::Forward: actions[0] = {.type = RTE_FLOW_ACTION_TYPE_QUEUE, .conf = &queue_action}; break;
        // ... Mark, Count follow same pattern
        default: break;
    }
    actions[1] = {.type = RTE_FLOW_ACTION_TYPE_END};

    struct rte_flow_error err{};
    auto* flow = rte_flow_create(port_id, &attr, pattern, actions, &err);
    if (!flow)
        return SLError{SLErrorCode::IoError, std::string("rte_flow_create: ") + (err.message ? err.message : "unknown")};

    uint32_t id = next_id_++;
    rules_.push_back({id, flow});
    return id;
}

// remove, flush: iterate rules_, call rte_flow_destroy
// ... standard error handling

} // namespace dpdk
```

---

### Task 3: Implement tensor_transport

**Files:** `bin/dpdk/tensor_transport.h`, `bin/dpdk/tensor_transport.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <straylight/ml/tensor.h>
#include <rte_mbuf.h>
#include <vector>
#include <cstdint>

namespace dpdk {

struct TensorHeader {
    std::vector<int64_t> shape;
    DType dtype;
    uint64_t byte_size;
};

class TensorTransport {
public:
    static std::vector<uint8_t> serialize_header(const TensorHeader& hdr);
    static Result<TensorHeader, SLError> parse_header(const uint8_t* data, size_t len);

    // Fragment tensor into DPDK mbufs for wire transmission
    Result<std::vector<struct rte_mbuf*>, SLError>
        fragment(const void* tensor_data, const TensorHeader& hdr,
                 struct rte_mempool* pool);

    // Reassemble mbufs into contiguous tensor buffer
    Result<std::vector<uint8_t>, SLError>
        reassemble(struct rte_mbuf** frags, uint32_t count);
};

} // namespace dpdk
```

- [ ] **Step 2: Implementation**

```cpp
#include "tensor_transport.h"
#include <cstring>
namespace dpdk {

static constexpr uint32_t MAGIC = 0x534C5448; // "SLTH"
static constexpr size_t MAX_PAYLOAD = 1500 - 42; // MTU minus headers

std::vector<uint8_t> TensorTransport::serialize_header(const TensorHeader& hdr) {
    std::vector<uint8_t> buf;
    auto push = [&](const void* p, size_t n) {
        buf.insert(buf.end(), (uint8_t*)p, (uint8_t*)p + n);
    };
    push(&MAGIC, 4);
    uint32_t ndim = hdr.shape.size();
    push(&ndim, 4);
    for (auto d : hdr.shape) push(&d, 8);
    uint32_t dt = static_cast<uint32_t>(hdr.dtype);
    push(&dt, 4);
    push(&hdr.byte_size, 8);
    return buf;
}

Result<TensorHeader, SLError> TensorTransport::parse_header(const uint8_t* data, size_t len) {
    if (len < 16) return SLError{SLErrorCode::InvalidArgument, "header too short"};
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    if (magic != MAGIC) return SLError{SLErrorCode::InvalidArgument, "bad magic"};

    uint32_t ndim;
    std::memcpy(&ndim, data + 4, 4);
    if (len < 16 + ndim * 8)
        return SLError{SLErrorCode::InvalidArgument, "truncated header"};

    TensorHeader hdr;
    hdr.shape.resize(ndim);
    for (uint32_t i = 0; i < ndim; i++)
        std::memcpy(&hdr.shape[i], data + 8 + i * 8, 8);

    size_t off = 8 + ndim * 8;
    uint32_t dt;
    std::memcpy(&dt, data + off, 4);
    hdr.dtype = static_cast<DType>(dt);
    std::memcpy(&hdr.byte_size, data + off + 4, 8);
    return hdr;
}

Result<std::vector<struct rte_mbuf*>, SLError>
TensorTransport::fragment(const void* tensor_data, const TensorHeader& hdr,
                           struct rte_mempool* pool) {
    auto hdr_buf = serialize_header(hdr);
    size_t total = hdr_buf.size() + hdr.byte_size;
    size_t nfrags = (total + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

    std::vector<struct rte_mbuf*> mbufs(nfrags);
    size_t offset = 0;
    const uint8_t* src = static_cast<const uint8_t*>(tensor_data);

    for (size_t i = 0; i < nfrags; i++) {
        mbufs[i] = rte_pktmbuf_alloc(pool);
        if (!mbufs[i]) {
            for (size_t j = 0; j < i; j++) rte_pktmbuf_free(mbufs[j]);
            return SLError{SLErrorCode::IoError, "mbuf alloc failed"};
        }
        uint8_t* dst = rte_pktmbuf_mtod(mbufs[i], uint8_t*);
        size_t chunk = std::min(MAX_PAYLOAD, total - offset);
        // First fragment includes header prefix
        if (i == 0) {
            std::memcpy(dst, hdr_buf.data(), hdr_buf.size());
            size_t data_chunk = chunk - hdr_buf.size();
            std::memcpy(dst + hdr_buf.size(), src, data_chunk);
            offset = chunk;
        } else {
            size_t data_off = offset - hdr_buf.size();
            std::memcpy(dst, src + data_off, chunk);
            offset += chunk;
        }
        mbufs[i]->data_len = chunk;
        mbufs[i]->pkt_len = chunk;
    }
    return mbufs;
}

// reassemble: concatenate mbuf payloads, parse_header from first, return data portion
// ... standard error handling

} // namespace dpdk
```

---

### Task 4: main.cpp and CMakeLists.txt

- [ ] **Step 1: main.cpp**

```cpp
#include <straylight/common.h>
#include "port.h"
#include "pipeline.h"
#include "flow.h"
#include "tensor_transport.h"
#include <rte_eal.h>
#include <iostream>

static void usage() {
    std::cerr << "Usage: straylight-dpdk <command> [args...]\n"
              << "  probe                        List available ports\n"
              << "  forward <port> <rx_q> <tx_q> Start forwarding pipeline\n"
              << "  flow <port> <src> <dst> <action>  Install flow rule\n";
}

int main(int argc, char* argv[]) {
    // EAL consumes its args first
    int eal_ret = rte_eal_init(argc, argv);
    if (eal_ret < 0) {
        std::cerr << "EAL init failed\n";
        return 1;
    }
    argc -= eal_ret; argv += eal_ret;

    auto cfg = Config::from_file("/etc/straylight/dpdk.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-dpdk", cfg.log_level());

    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    dpdk::PortManager ports;
    ports.set_eal_initialized();

    if (cmd == "probe") {
        auto r = ports.probe_ports();
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        for (auto& p : *r)
            SL_INFO("port {} name={} mac={:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                     p.port_id, p.name,
                     p.mac.addr_bytes[0], p.mac.addr_bytes[1], p.mac.addr_bytes[2],
                     p.mac.addr_bytes[3], p.mac.addr_bytes[4], p.mac.addr_bytes[5]);
    } else if (cmd == "forward" && argc >= 5) {
        uint16_t port_id = std::stoi(argv[2]);
        auto r = ports.configure(port_id, std::stoi(argv[3]), std::stoi(argv[4]));
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        ports.start(port_id);
        // ... run pipeline poll loop until signal
    } else {
        usage(); return 1;
    }

    rte_eal_cleanup();
    return 0;
}
```

- [ ] **Step 2: CMakeLists.txt**

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

add_executable(straylight-dpdk
    main.cpp port.cpp pipeline.cpp flow.cpp tensor_transport.cpp)
target_link_libraries(straylight-dpdk PRIVATE
    straylight-common straylight-net straylight-ml ${DPDK_LIBRARIES})
target_include_directories(straylight-dpdk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${DPDK_INCLUDE_DIRS})
target_compile_options(straylight-dpdk PRIVATE ${DPDK_CFLAGS_OTHER})
install(TARGETS straylight-dpdk DESTINATION bin)
```

---

## Chunk 4: straylight-rdma-bus — Verbs & Memory Regions

`bin/rdma_bus/` — RDMA transport using libibverbs for zero-copy tensor movement.

### File Structure

```
bin/rdma_bus/
├── CMakeLists.txt
├── main.cpp
├── verbs.h / verbs.cpp
├── memory_region.h / memory_region.cpp
├── queue_pair.h / queue_pair.cpp
└── tensor_rdma.h / tensor_rdma.cpp
tests/unit/subsystems/
└── test_rdma_bus.cpp
```

### Task 1: Failing tests

**File:** `tests/unit/subsystems/test_rdma_bus.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
#include <gtest/gtest.h>
#include "verbs.h"
#include "memory_region.h"
#include "queue_pair.h"
#include "tensor_rdma.h"

TEST(RdmaVerbs, OpenNonexistentDeviceFails) {
    rdma::VerbsContext ctx;
    auto r = ctx.open("nonexistent_rdma_dev_999");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), SLErrorCode::NotFound);
}

TEST(RdmaVerbs, ListDevices) {
    rdma::VerbsContext ctx;
    auto devs = ctx.list_devices();
    ASSERT_TRUE(devs.has_value());
    // May be empty if no RDMA hardware, but should not error
}

TEST(MemoryRegion, RegisterRequiresContext) {
    rdma::MemoryRegionManager mrm;
    std::vector<uint8_t> buf(4096);
    auto r = mrm.register_mr(nullptr, buf.data(), buf.size(),
                              rdma::Access::LocalWrite | rdma::Access::RemoteRead);
    EXPECT_FALSE(r.has_value());
}

TEST(TensorRdma, HeaderRoundTrip) {
    rdma::TensorRdmaHeader hdr{.shape = {4, 256}, .dtype = DType::Float16,
                                .byte_size = 2048, .rkey = 0x1234, .remote_addr = 0xDEAD};
    auto buf = rdma::TensorRdma::serialize_header(hdr);
    auto parsed = rdma::TensorRdma::parse_header(buf.data(), buf.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->rkey, 0x1234u);
    EXPECT_EQ(parsed->remote_addr, 0xDEADu);
}
```

- [ ] **Step 2: CMakeLists.txt entry**

```cmake
add_executable(test_rdma_bus test_rdma_bus.cpp)
target_link_libraries(test_rdma_bus PRIVATE straylight-common straylight-net straylight-ml
    ibverbs rdmacm GTest::gtest_main)
gtest_discover_tests(test_rdma_bus)
```

---

### Task 2: Implement verbs

**Files:** `bin/rdma_bus/verbs.h`, `bin/rdma_bus/verbs.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <infiniband/verbs.h>
#include <string>
#include <vector>

namespace rdma {

struct DeviceInfo {
    std::string name;
    uint64_t guid;
    uint8_t port_count;
};

class VerbsContext {
public:
    ~VerbsContext();

    Result<std::vector<DeviceInfo>, SLError> list_devices();
    Result<void, SLError> open(const std::string& dev_name);

    struct ibv_context* ctx() const { return ctx_; }
    struct ibv_pd*      pd()  const { return pd_; }

private:
    struct ibv_context* ctx_ = nullptr;
    struct ibv_pd*      pd_  = nullptr;
};

} // namespace rdma
```

- [ ] **Step 2: Implementation**

```cpp
#include "verbs.h"
namespace rdma {

VerbsContext::~VerbsContext() {
    if (pd_)  ibv_dealloc_pd(pd_);
    if (ctx_) ibv_close_device(ctx_);
}

Result<std::vector<DeviceInfo>, SLError> VerbsContext::list_devices() {
    int num = 0;
    struct ibv_device** devs = ibv_get_device_list(&num);
    if (!devs)
        return SLError{SLErrorCode::IoError, "ibv_get_device_list failed"};

    std::vector<DeviceInfo> result;
    for (int i = 0; i < num; i++) {
        result.push_back({
            .name = ibv_get_device_name(devs[i]),
            .guid = ibv_get_device_guid(devs[i]),
            .port_count = 0  // filled after query
        });
    }
    ibv_free_device_list(devs);
    return result;
}

Result<void, SLError> VerbsContext::open(const std::string& dev_name) {
    int num = 0;
    struct ibv_device** devs = ibv_get_device_list(&num);
    if (!devs)
        return SLError{SLErrorCode::IoError, "ibv_get_device_list failed"};

    struct ibv_device* target = nullptr;
    for (int i = 0; i < num; i++) {
        if (ibv_get_device_name(devs[i]) == dev_name) { target = devs[i]; break; }
    }
    if (!target) {
        ibv_free_device_list(devs);
        return SLError{SLErrorCode::NotFound, "RDMA device not found: " + dev_name};
    }

    ctx_ = ibv_open_device(target);
    ibv_free_device_list(devs);
    if (!ctx_)
        return SLError{SLErrorCode::IoError, "ibv_open_device failed"};

    pd_ = ibv_alloc_pd(ctx_);
    if (!pd_)
        return SLError{SLErrorCode::IoError, "ibv_alloc_pd failed"};

    SL_INFO("opened RDMA device '{}' pd={}", dev_name, (void*)pd_);
    return Result<void, SLError>::ok();
}

} // namespace rdma
```

---

### Task 3: Implement memory_region

**Files:** `bin/rdma_bus/memory_region.h`, `bin/rdma_bus/memory_region.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <infiniband/verbs.h>
#include <unordered_map>
#include <cstdint>

namespace rdma {

enum class Access : int {
    LocalWrite  = IBV_ACCESS_LOCAL_WRITE,
    RemoteRead  = IBV_ACCESS_REMOTE_READ,
    RemoteWrite = IBV_ACCESS_REMOTE_WRITE,
};
inline Access operator|(Access a, Access b) {
    return static_cast<Access>(static_cast<int>(a) | static_cast<int>(b));
}

struct MRHandle {
    uint32_t lkey;
    uint32_t rkey;
    void* addr;
    size_t length;
};

class MemoryRegionManager {
public:
    ~MemoryRegionManager();

    Result<MRHandle, SLError> register_mr(struct ibv_pd* pd, void* addr,
                                           size_t length, Access access);
    void deregister(uint32_t lkey);
    MRHandle* find(uint32_t lkey);

private:
    struct Entry { struct ibv_mr* mr; MRHandle handle; };
    std::unordered_map<uint32_t, Entry> regions_;
};

} // namespace rdma
```

- [ ] **Step 2: Implementation**

```cpp
#include "memory_region.h"
namespace rdma {

MemoryRegionManager::~MemoryRegionManager() {
    for (auto& [_, e] : regions_) ibv_dereg_mr(e.mr);
}

Result<MRHandle, SLError> MemoryRegionManager::register_mr(
    struct ibv_pd* pd, void* addr, size_t length, Access access) {
    if (!pd)
        return SLError{SLErrorCode::InvalidArgument, "null protection domain"};

    auto* mr = ibv_reg_mr(pd, addr, length, static_cast<int>(access));
    if (!mr)
        return SLError{SLErrorCode::IoError, "ibv_reg_mr failed"};

    MRHandle h{.lkey = mr->lkey, .rkey = mr->rkey, .addr = addr, .length = length};
    regions_[mr->lkey] = {mr, h};
    SL_DEBUG("registered MR lkey={:#x} rkey={:#x} len={}", h.lkey, h.rkey, length);
    return h;
}

void MemoryRegionManager::deregister(uint32_t lkey) {
    if (auto it = regions_.find(lkey); it != regions_.end()) {
        ibv_dereg_mr(it->second.mr);
        regions_.erase(it);
    }
}

MRHandle* MemoryRegionManager::find(uint32_t lkey) {
    auto it = regions_.find(lkey);
    return it != regions_.end() ? &it->second.handle : nullptr;
}

} // namespace rdma
```

---

## Chunk 5: straylight-rdma-bus — Queue Pair & Tensor RDMA

### Task 1: Implement queue_pair

**Files:** `bin/rdma_bus/queue_pair.h`, `bin/rdma_bus/queue_pair.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <cstdint>

namespace rdma {

struct QPConfig {
    uint32_t max_send_wr = 128;
    uint32_t max_recv_wr = 128;
    uint32_t max_send_sge = 1;
    uint32_t max_recv_sge = 1;
};

struct QPEndpoint {
    uint16_t lid;
    uint32_t qp_num;
    uint32_t psn;
    union ibv_gid gid;
};

class QueuePair {
public:
    ~QueuePair();

    Result<void, SLError> create(struct ibv_pd* pd, struct ibv_cq* cq,
                                  const QPConfig& cfg = {});
    Result<void, SLError> transition_to_rtr(const QPEndpoint& remote, uint8_t port = 1);
    Result<void, SLError> transition_to_rts();

    // Post RDMA read/write
    Result<void, SLError> post_send(void* local_addr, uint32_t len, uint32_t lkey,
                                     uint64_t remote_addr, uint32_t rkey, bool is_write);
    Result<void, SLError> post_recv(void* addr, uint32_t len, uint32_t lkey);

    QPEndpoint local_endpoint(uint8_t port = 1) const;
    struct ibv_qp* qp() const { return qp_; }

private:
    struct ibv_qp* qp_ = nullptr;
    uint32_t psn_ = 0;
};

} // namespace rdma
```

- [ ] **Step 2: Implementation**

```cpp
#include "queue_pair.h"
#include <random>
namespace rdma {

QueuePair::~QueuePair() { if (qp_) ibv_destroy_qp(qp_); }

Result<void, SLError> QueuePair::create(struct ibv_pd* pd, struct ibv_cq* cq,
                                         const QPConfig& cfg) {
    struct ibv_qp_init_attr attr{};
    attr.send_cq = cq;
    attr.recv_cq = cq;
    attr.qp_type = IBV_QPT_RC;
    attr.cap.max_send_wr = cfg.max_send_wr;
    attr.cap.max_recv_wr = cfg.max_recv_wr;
    attr.cap.max_send_sge = cfg.max_send_sge;
    attr.cap.max_recv_sge = cfg.max_recv_sge;

    qp_ = ibv_create_qp(pd, &attr);
    if (!qp_) return SLError{SLErrorCode::IoError, "ibv_create_qp failed"};

    // Move to INIT
    struct ibv_qp_attr qp_attr{};
    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.port_num = 1;
    qp_attr.pkey_index = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE;
    if (ibv_modify_qp(qp_, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        return SLError{SLErrorCode::IoError, "QP transition to INIT failed"};

    std::random_device rd;
    psn_ = rd() & 0xFFFFFF;
    return Result<void, SLError>::ok();
}

Result<void, SLError> QueuePair::transition_to_rtr(const QPEndpoint& remote, uint8_t port) {
    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remote.qp_num;
    attr.rq_psn = remote.psn;
    attr.max_dest_rd_atomic = 4;
    attr.min_rnr_timer = 12;
    attr.ah_attr.dlid = remote.lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.port_num = port;
    attr.ah_attr.is_global = 1;
    attr.ah_attr.grh.dgid = remote.gid;
    attr.ah_attr.grh.hop_limit = 1;

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(qp_, &attr, flags))
        return SLError{SLErrorCode::IoError, "QP transition to RTR failed"};
    return Result<void, SLError>::ok();
}

Result<void, SLError> QueuePair::transition_to_rts() {
    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = psn_;
    attr.max_rd_atomic = 4;

    int flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(qp_, &attr, flags))
        return SLError{SLErrorCode::IoError, "QP transition to RTS failed"};
    return Result<void, SLError>::ok();
}

Result<void, SLError> QueuePair::post_send(void* local_addr, uint32_t len,
                                            uint32_t lkey, uint64_t remote_addr,
                                            uint32_t rkey, bool is_write) {
    struct ibv_sge sge{.addr = (uint64_t)local_addr, .length = len, .lkey = lkey};
    struct ibv_send_wr wr{};
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = is_write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey = rkey;

    struct ibv_send_wr* bad = nullptr;
    if (ibv_post_send(qp_, &wr, &bad))
        return SLError{SLErrorCode::IoError, "ibv_post_send failed"};
    return Result<void, SLError>::ok();
}

// post_recv, local_endpoint: similar pattern
// ... standard error handling

QPEndpoint QueuePair::local_endpoint(uint8_t port) const {
    QPEndpoint ep{};
    ep.qp_num = qp_->qp_num;
    ep.psn = psn_;
    struct ibv_port_attr pa;
    ibv_query_port(qp_->context, port, &pa);
    ep.lid = pa.lid;
    ibv_query_gid(qp_->context, port, 0, &ep.gid);
    return ep;
}

} // namespace rdma
```

---

### Task 2: Implement tensor_rdma

**Files:** `bin/rdma_bus/tensor_rdma.h`, `bin/rdma_bus/tensor_rdma.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <straylight/common.h>
#include <straylight/ml/tensor.h>
#include "verbs.h"
#include "memory_region.h"
#include "queue_pair.h"
#include <vector>
#include <cstdint>

namespace rdma {

struct TensorRdmaHeader {
    std::vector<int64_t> shape;
    DType dtype;
    uint64_t byte_size;
    uint32_t rkey;
    uint64_t remote_addr;
};

class TensorRdma {
public:
    static std::vector<uint8_t> serialize_header(const TensorRdmaHeader& hdr);
    static Result<TensorRdmaHeader, SLError> parse_header(const uint8_t* data, size_t len);

    // Register tensor buffer and return RDMA-readable handle
    Result<TensorRdmaHeader, SLError> expose(VerbsContext& ctx, MemoryRegionManager& mrm,
                                              void* data, const TensorRdmaHeader& meta);

    // RDMA-read remote tensor into local buffer
    Result<void, SLError> fetch(QueuePair& qp, void* local_buf, uint32_t lkey,
                                 const TensorRdmaHeader& remote);

    // RDMA-write local tensor to remote buffer
    Result<void, SLError> push(QueuePair& qp, const void* local_buf, uint32_t lkey,
                                const TensorRdmaHeader& remote);
};

} // namespace rdma
```

- [ ] **Step 2: Implementation**

```cpp
#include "tensor_rdma.h"
#include <cstring>
namespace rdma {

static constexpr uint32_t MAGIC = 0x534C5452; // "SLTR"

std::vector<uint8_t> TensorRdma::serialize_header(const TensorRdmaHeader& hdr) {
    std::vector<uint8_t> buf;
    auto push = [&](const void* p, size_t n) {
        buf.insert(buf.end(), (uint8_t*)p, (uint8_t*)p + n);
    };
    push(&MAGIC, 4);
    uint32_t ndim = hdr.shape.size();
    push(&ndim, 4);
    for (auto d : hdr.shape) push(&d, 8);
    uint32_t dt = static_cast<uint32_t>(hdr.dtype);
    push(&dt, 4);
    push(&hdr.byte_size, 8);
    push(&hdr.rkey, 4);
    push(&hdr.remote_addr, 8);
    return buf;
}

Result<TensorRdmaHeader, SLError> TensorRdma::parse_header(const uint8_t* data, size_t len) {
    if (len < 20) return SLError{SLErrorCode::InvalidArgument, "header too short"};
    uint32_t magic;
    std::memcpy(&magic, data, 4);
    if (magic != MAGIC) return SLError{SLErrorCode::InvalidArgument, "bad RDMA magic"};

    uint32_t ndim;
    std::memcpy(&ndim, data + 4, 4);

    TensorRdmaHeader hdr;
    hdr.shape.resize(ndim);
    size_t off = 8;
    for (uint32_t i = 0; i < ndim; i++) { std::memcpy(&hdr.shape[i], data + off, 8); off += 8; }
    uint32_t dt; std::memcpy(&dt, data + off, 4); hdr.dtype = static_cast<DType>(dt); off += 4;
    std::memcpy(&hdr.byte_size, data + off, 8); off += 8;
    std::memcpy(&hdr.rkey, data + off, 4); off += 4;
    std::memcpy(&hdr.remote_addr, data + off, 8);
    return hdr;
}

Result<TensorRdmaHeader, SLError> TensorRdma::expose(
    VerbsContext& ctx, MemoryRegionManager& mrm, void* data, const TensorRdmaHeader& meta) {
    auto mr = mrm.register_mr(ctx.pd(), data, meta.byte_size,
                               Access::LocalWrite | Access::RemoteRead | Access::RemoteWrite);
    if (!mr) return mr.error();
    TensorRdmaHeader out = meta;
    out.rkey = mr->rkey;
    out.remote_addr = reinterpret_cast<uint64_t>(data);
    return out;
}

Result<void, SLError> TensorRdma::fetch(QueuePair& qp, void* local_buf,
                                          uint32_t lkey, const TensorRdmaHeader& remote) {
    return qp.post_send(local_buf, remote.byte_size, lkey,
                         remote.remote_addr, remote.rkey, /*is_write=*/false);
}

Result<void, SLError> TensorRdma::push(QueuePair& qp, const void* local_buf,
                                         uint32_t lkey, const TensorRdmaHeader& remote) {
    return qp.post_send(const_cast<void*>(local_buf), remote.byte_size, lkey,
                         remote.remote_addr, remote.rkey, /*is_write=*/true);
}

} // namespace rdma
```

---

## Chunk 6: straylight-rdma-bus — main.cpp, CMake, All Tests Pass

### Task 1: main.cpp

- [ ] **Step 1: Implement CLI**

```cpp
#include <straylight/common.h>
#include "verbs.h"
#include "memory_region.h"
#include "queue_pair.h"
#include "tensor_rdma.h"
#include <iostream>

static void usage() {
    std::cerr << "Usage: straylight-rdma-bus <command> [args...]\n"
              << "  list                         List RDMA devices\n"
              << "  serve <dev> <port>           Listen for tensor RDMA requests\n"
              << "  fetch <dev> <host> <port>    Fetch tensor from remote\n";
}

int main(int argc, char* argv[]) {
    auto cfg = Config::from_file("/etc/straylight/rdma-bus.conf")
                   .value_or(Config::defaults());
    SL_INIT("straylight-rdma-bus", cfg.log_level());

    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    rdma::VerbsContext ctx;

    if (cmd == "list") {
        auto r = ctx.list_devices();
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        for (auto& d : *r)
            SL_INFO("dev={} guid={:#018x}", d.name, d.guid);
    } else if (cmd == "serve" && argc >= 4) {
        auto r = ctx.open(argv[2]);
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }

        // Create CQ, QP, register memory, exchange endpoints via TCP on argv[3]
        struct ibv_cq* cq = ibv_create_cq(ctx.ctx(), 128, nullptr, nullptr, 0);
        if (!cq) { SL_ERROR("ibv_create_cq failed"); return 1; }

        rdma::QueuePair qp;
        auto qr = qp.create(ctx.pd(), cq);
        if (!qr) { SL_ERROR("{}", qr.error().message()); return 1; }

        auto ep = qp.local_endpoint();
        SL_INFO("serving on qp_num={} lid={} port={}", ep.qp_num, ep.lid, argv[3]);
        // ... TCP handshake to exchange endpoints, then wait for RDMA ops
        pause();

        ibv_destroy_cq(cq);
    } else if (cmd == "fetch" && argc >= 5) {
        auto r = ctx.open(argv[2]);
        if (!r) { SL_ERROR("{}", r.error().message()); return 1; }
        // ... TCP connect to argv[3]:argv[4], exchange endpoints, RDMA read
        SL_INFO("fetch complete");
    } else {
        usage(); return 1;
    }
    return 0;
}
```

---

### Task 2: CMakeLists.txt

- [ ] **Step 1: Write build file**

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(RDMA REQUIRED libibverbs librdmacm)

add_executable(straylight-rdma-bus
    main.cpp verbs.cpp memory_region.cpp queue_pair.cpp tensor_rdma.cpp)
target_link_libraries(straylight-rdma-bus PRIVATE
    straylight-common straylight-net straylight-ml ibverbs rdmacm)
target_include_directories(straylight-rdma-bus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS straylight-rdma-bus DESTINATION bin)
```

---

### Task 3: Wire into top-level CMake

- [ ] **Step 1: Add to `bin/CMakeLists.txt`**

```cmake
add_subdirectory(xdp)
add_subdirectory(dpdk)
add_subdirectory(rdma_bus)
```

---

### Task 4: Run all tests

- [ ] **Step 1: Build and run**

```bash
cmake --build build --target test_xdp test_dpdk test_rdma_bus
ctest --test-dir build -R "test_xdp|test_dpdk|test_rdma_bus" --output-on-failure
```

All 12 tests (4 xdp + 4 dpdk + 4 rdma_bus) must pass.

---

## Summary

| Chunk | Scope | Files | Tests |
|-------|-------|-------|-------|
| 1 | XDP: loader, maps, af_xdp | 7 (3 .h, 3 .cpp, CMake) | 4 |
| 2 | DPDK: port manager | 3 (.h, .cpp, tests) | 4 |
| 3 | DPDK: pipeline, flow, tensor_transport | 6 (.h/.cpp pairs) | — (covered by chunk 2 tests) |
| 4 | RDMA: verbs, memory_region | 5 (.h/.cpp, tests) | 4 |
| 5 | RDMA: queue_pair, tensor_rdma | 4 (.h/.cpp pairs) | — (covered by chunk 4 tests) |
| 6 | RDMA main.cpp, CMake, integration | 3 (main, CMake, top-level) | All 12 pass |

**Total new files:** ~25 source files across 3 binaries + 3 test files.
