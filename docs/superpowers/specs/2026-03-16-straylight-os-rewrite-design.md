# StrayLight OS Clean-Room Rewrite Design

**Date:** 2026-03-16
**Status:** Approved
**Scope:** Full clean-room rewrite of StrayLight OS as a Debian-based Linux distribution

---

## Decisions

| Decision | Choice |
|----------|--------|
| Rewrite strategy | Clean-room rewrite |
| Target platform | Linux x86_64 only, Debian base |
| Subsystem count | All 18, fully implemented (no stubs) |
| Desktop shell | Wayland compositor (wlroots) + ImGui shell via layer-shell |
| C++ standard | C++20 |
| Build system | CMake 3.25+ with presets |
| IPC | Unix domain sockets + D-Bus |
| JSON | nlohmann/json |
| Logging | spdlog |
| Error handling | `std::expected<T,E>` (backported from C++23) |
| Packaging | Hybrid metapackages (7 `.deb` groups) |
| Architecture | Layered monorepo |

---

## Repository Structure

```
straylight/
├── CMakeLists.txt
├── cmake/
│   ├── presets/
│   │   └── CMakePresets.json
│   ├── FindWlroots.cmake
│   ├── FindSpdlog.cmake
│   └── StraylightCommon.cmake
│
├── lib/                              # Shared libraries
│   ├── common/                       # libstraylight-common.so
│   │   ├── include/straylight/
│   │   │   ├── common.h
│   │   │   ├── config.h
│   │   │   ├── ipc.h
│   │   │   ├── log.h
│   │   │   └── types.h
│   │   └── src/
│   │       ├── config.cpp
│   │       ├── ipc.cpp
│   │       └── log.cpp
│   │
│   ├── ml/                           # libstraylight-ml.so
│   │   ├── include/straylight/ml/
│   │   │   ├── tensor.h
│   │   │   ├── graph.h
│   │   │   ├── framework_bridge.h
│   │   │   └── kv_cache.h
│   │   └── src/
│   │
│   ├── net/                          # libstraylight-net.so
│   │   ├── include/straylight/net/
│   │   │   ├── socket.h
│   │   │   ├── transport.h
│   │   │   └── protocol.h
│   │   └── src/
│   │
│   └── hw/                           # libstraylight-hw.so
│       ├── include/straylight/hw/
│       │   ├── gpu.h
│       │   ├── entropy.h
│       │   ├── pmem.h
│       │   └── sgx.h
│       └── src/
│
├── kernel/                           # Loadable kernel modules (DKMS)
│   ├── vpu/                          # straylight-vpu.ko
│   │   ├── Kbuild
│   │   ├── vpu_main.c
│   │   ├── vpu_slab.c
│   │   ├── vpu_ioctl.c
│   │   ├── vpu_dma.c
│   │   ├── vpu_sysfs.c
│   │   └── vpu.h
│   │
│   ├── hypervisor/                   # straylight-hypervisor.ko
│   │   ├── Kbuild
│   │   ├── hv_main.c
│   │   ├── hv_vmcs.c
│   │   ├── hv_memory.c
│   │   ├── hv_intercept.c
│   │   ├── hv_profiler.c
│   │   └── hv.h
│   │
│   ├── scheduler/                    # straylight-scheduler.ko
│   │   ├── Kbuild
│   │   ├── sched_main.c
│   │   ├── sched_ml.c
│   │   ├── sched_topology.c
│   │   └── sched.h
│   │
│   ├── xdp/                          # eBPF programs
│   │   ├── Kbuild
│   │   ├── xdp_filter.bpf.c
│   │   ├── xdp_redirect.bpf.c
│   │   ├── xdp_stats.bpf.c
│   │   └── xdp_maps.h
│   │
│   ├── entropy/                      # straylight-entropy.ko
│   │   ├── Kbuild
│   │   ├── entropy_main.c
│   │   ├── entropy_jitter.c
│   │   ├── entropy_rdrand.c
│   │   └── entropy_health.c
│   │
│   ├── enclave/                      # straylight-enclave.ko
│   │   ├── Kbuild
│   │   ├── enclave_main.c
│   │   ├── enclave_epc.c
│   │   ├── enclave_sealed.c
│   │   └── enclave_attestation.c
│   │
│   └── dkms/
│       ├── straylight-vpu-dkms/dkms.conf
│       ├── straylight-hypervisor-dkms/dkms.conf
│       ├── straylight-scheduler-dkms/dkms.conf
│       ├── straylight-xdp-dkms/dkms.conf
│       ├── straylight-entropy-dkms/dkms.conf
│       └── straylight-enclave-dkms/dkms.conf
│
├── subsystems/                       # 18 userspace binaries (7 daemons, 11 on-demand tools)
│   ├── core/                         # straylight-core (orchestrator)
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp
│   │   ├── pipeline.h/cpp
│   │   ├── doctor.h/cpp
│   │   └── inventory.h/cpp
│   │
│   ├── bus/                          # straylight-bus (zero-copy tensor IPC)
│   │   ├── main.cpp
│   │   ├── shm_transport.h/cpp
│   │   ├── pub_sub.h/cpp
│   │   └── bus_daemon.h/cpp
│   │
│   ├── registry/                     # straylight-registry (distributed KV)
│   │   ├── main.cpp
│   │   ├── store.h/cpp
│   │   ├── replication.h/cpp
│   │   └── dbus_interface.h/cpp
│   │
│   ├── scheduler/                    # straylight-scheduler (CPU/GPU pinning)
│   │   ├── main.cpp
│   │   ├── topology.h/cpp
│   │   ├── classifier.h/cpp
│   │   ├── pinning.h/cpp
│   │   └── profiles.h/cpp
│   │
│   ├── entropy/                      # straylight-entropy (HWRNG)
│   │   ├── main.cpp
│   │   ├── sources.h/cpp
│   │   ├── pool.h/cpp
│   │   └── drbg.h/cpp
│   │
│   ├── agent/                        # straylight-agent (task distribution)
│   │   ├── main.cpp
│   │   ├── event_loop.h/cpp
│   │   ├── task_queue.h/cpp
│   │   ├── worker_pool.h/cpp
│   │   └── distribution.h/cpp
│   │
│   ├── compiler/                     # straylight-compiler (graph optimizer)
│   │   ├── main.cpp
│   │   ├── ir/
│   │   │   ├── graph.h/cpp
│   │   │   ├── passes.h/cpp
│   │   │   └── lowering.h/cpp
│   │   ├── backends/
│   │   │   ├── cuda.h/cpp
│   │   │   ├── rocm.h/cpp
│   │   │   └── cpu.h/cpp
│   │   └── cache.h/cpp
│   │
│   ├── morph/                        # straylight-morph (model transformation)
│   │   ├── main.cpp
│   │   ├── quantize.h/cpp
│   │   ├── prune.h/cpp
│   │   ├── distill.h/cpp
│   │   └── adapt.h/cpp
│   │
│   ├── snn/                          # straylight-snn (spiking neural nets)
│   │   ├── main.cpp
│   │   ├── neuron.h/cpp
│   │   ├── network.h/cpp
│   │   ├── plasticity.h/cpp
│   │   └── simulator.h/cpp
│   │
│   ├── rhem/                         # straylight-rhem (heterogeneous resources)
│   │   ├── main.cpp
│   │   ├── discovery.h/cpp
│   │   ├── allocator.h/cpp
│   │   ├── migration.h/cpp
│   │   └── policy.h/cpp
│   │
│   ├── xdp/                          # straylight-xdp (eBPF loader/manager)
│   │   ├── main.cpp
│   │   ├── loader.h/cpp
│   │   ├── maps.h/cpp
│   │   └── af_xdp.h/cpp
│   │
│   ├── dpdk/                         # straylight-dpdk (packet processing)
│   │   ├── main.cpp
│   │   ├── port.h/cpp
│   │   ├── pipeline.h/cpp
│   │   ├── flow.h/cpp
│   │   └── tensor_transport.h/cpp
│   │
│   ├── rdma_bus/                     # straylight-rdma-bus (RDMA transport)
│   │   ├── main.cpp
│   │   ├── verbs.h/cpp
│   │   ├── memory_region.h/cpp
│   │   ├── queue_pair.h/cpp
│   │   └── tensor_rdma.h/cpp
│   │
│   ├── quantum/                      # straylight-quantum (gate simulator)
│   │   ├── main.cpp
│   │   ├── state_vector.h/cpp
│   │   ├── gates.h/cpp
│   │   ├── circuit.h/cpp
│   │   ├── noise.h/cpp
│   │   └── measure.h/cpp
│   │
│   ├── photonics/                    # straylight-photonics (optical computing)
│   │   ├── main.cpp
│   │   ├── mzi.h/cpp
│   │   ├── mesh.h/cpp
│   │   ├── detector.h/cpp
│   │   └── device.h/cpp
│   │
│   ├── pmem/                         # straylight-pmem (persistent memory)
│   │   ├── main.cpp
│   │   ├── dax.h/cpp
│   │   ├── allocator.h/cpp
│   │   ├── log.h/cpp
│   │   └── checkpoint.h/cpp
│   │
│   ├── enclave/                      # straylight-enclave (SGX)
│   │   ├── main.cpp
│   │   ├── enclave_def/
│   │   │   ├── enclave.edl
│   │   │   └── enclave.cpp
│   │   ├── attestation.h/cpp
│   │   ├── sealed_storage.h/cpp
│   │   └── secure_inference.h/cpp
│   │
│   └── fuse/                         # straylight-fuse (tensor filesystem)
│       ├── main.cpp
│       ├── operations.h/cpp
│       ├── compression.h/cpp
│       ├── tensor_format.h/cpp
│       └── cache.h/cpp
│
├── compositor/                       # Wayland compositor (wlroots)
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── server.h/cpp
│   ├── output.h/cpp
│   ├── view.h/cpp
│   ├── input.h/cpp
│   ├── workspace.h/cpp
│   ├── tiling.h/cpp
│   ├── animations.h/cpp
│   ├── layer_shell.h/cpp
│   ├── decorations.h/cpp
│   └── ipc.h/cpp
│
├── shell/                            # ImGui desktop shell (layer-shell client)
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── renderer.h/cpp
│   ├── panels/
│   │   ├── top_bar.h/cpp
│   │   ├── app_launcher.h/cpp
│   │   ├── left_dock.h/cpp
│   │   └── bottom_dock.h/cpp
│   ├── widgets/
│   │   ├── notification.h/cpp
│   │   ├── volume_osd.h/cpp
│   │   ├── screenshot.h/cpp
│   │   └── lock_screen.h/cpp
│   ├── themes/
│   │   ├── theme_engine.h/cpp
│   │   ├── default.json
│   │   ├── cyberpunk.json
│   │   └── minimal.json
│   └── settings/
│       ├── display.h/cpp
│       ├── input.h/cpp
│       ├── appearance.h/cpp
│       └── network.h/cpp
│
├── apps/                             # Built-in applications
│   ├── terminal/                     # straylight-terminal
│   │   ├── main.cpp
│   │   ├── pty.h/cpp
│   │   ├── vte.h/cpp
│   │   ├── renderer.h/cpp
│   │   └── config.h/cpp
│   │
│   ├── file_manager/                 # straylight-files
│   │   ├── main.cpp
│   │   ├── browser.h/cpp
│   │   ├── operations.h/cpp
│   │   ├── preview.h/cpp
│   │   └── bookmarks.h/cpp
│   │
│   ├── system_monitor/               # straylight-monitor
│   │   ├── main.cpp
│   │   ├── cpu.h/cpp
│   │   ├── memory.h/cpp
│   │   ├── gpu.h/cpp
│   │   ├── network.h/cpp
│   │   └── process.h/cpp
│   │
│   ├── settings/                     # straylight-settings
│   │   ├── main.cpp
│   │   └── pages/
│   │
│   ├── wizard/                       # straylight-wizard (personalization, run anytime)
│   │   ├── main.cpp
│   │   ├── pages/
│   │   │   ├── welcome.h/cpp
│   │   │   ├── theme_picker.h/cpp
│   │   │   ├── layout_config.h/cpp
│   │   │   ├── ml_setup.h/cpp
│   │   │   └── summary.h/cpp
│   │   └── firstboot.h/cpp
│   │
│   ├── oobe/                         # straylight-oobe (first-login interactive setup)
│   │   ├── main.cpp
│   │   ├── pages/
│   │   │   ├── welcome.h/cpp         # "Welcome to StrayLight"
│   │   │   ├── account_setup.h/cpp   # Confirm admin account, add users
│   │   │   ├── package_profile.h/cpp # ML workstation / developer / server / minimal
│   │   │   ├── network_config.h/cpp  # WiFi/ethernet setup
│   │   │   └── summary.h/cpp         # Review + apply
│   │   └── oobe_state.h/cpp          # OOBE progress tracking
│   │
│   └── greeter/                      # straylight-greeter (login screen)
│       ├── main.cpp                  # Wayland client via ext-session-lock-v1
│       ├── auth.h/cpp                # PAM authentication
│       ├── session.h/cpp             # Session selection + launch
│       └── ui.h/cpp                  # ImGui login form rendering
│
├── services/                         # systemd + D-Bus + udev
│   ├── compositor/
│   │   └── straylight-compositor.service   # Type=notify, WantedBy=graphical.target
│   ├── shell/
│   │   └── straylight-shell.service        # After=compositor, user service
│   ├── greeter/
│   │   └── straylight-greeter.service      # After=compositor, Before=shell
│   ├── daemons/                            # Persistent daemons (7 total)
│   │   ├── straylight-core.service         # Type=notify, After=bus,registry
│   │   ├── straylight-bus.service          # Type=notify, WantedBy=multi-user
│   │   ├── straylight-registry.service     # After=bus
│   │   ├── straylight-scheduler.service    # After=registry
│   │   ├── straylight-entropy.service      # Before=registry
│   │   ├── straylight-agent.service        # After=scheduler,registry
│   │   └── straylight-fuse.service         # After=bus
│   ├── dbus/
│   │   ├── org.straylight.Core1.conf
│   │   ├── org.straylight.Bus1.conf
│   │   ├── org.straylight.Registry1.conf
│   │   ├── org.straylight.Scheduler1.conf
│   │   ├── org.straylight.Entropy1.conf
│   │   ├── org.straylight.Agent1.conf
│   │   ├── org.straylight.Compositor1.conf
│   │   └── org.straylight.Shell1.conf
│   ├── udev/
│   │   ├── 90-straylight-gpu.rules
│   │   ├── 90-straylight-sgx.rules
│   │   └── 90-straylight-pmem.rules
│   └── firstboot/
│       ├── straylight-firstboot.service    # Type=oneshot, Before=graphical.target
│       ├── straylight-oobe.service         # After=graphical.target, ConditionPathExists=
│       └── straylight-oobe.target
│
├── packaging/                        # Debian packages
│   ├── straylight-common/debian/
│   ├── straylight-core/debian/
│   ├── straylight-desktop/debian/
│   ├── straylight-ml/debian/
│   ├── straylight-network/debian/
│   ├── straylight-exotic/debian/
│   ├── straylight-kernel/debian/
│   └── straylight-os/debian/         # Metapackage
│
├── iso/                              # Live ISO generation
│   ├── live-build/
│   │   ├── auto/
│   │   │   ├── config
│   │   │   ├── build
│   │   │   └── clean
│   │   ├── config/
│   │   │   ├── package-lists/straylight.list.chroot
│   │   │   ├── hooks/live/0100-straylight.hook.chroot
│   │   │   ├── includes.chroot/
│   │   │   └── bootloaders/grub/
│   │   └── build.sh
│   └── calamares/
│       ├── settings.conf
│       └── modules/
│           ├── welcome.conf
│           ├── locale.conf
│           ├── partition.conf
│           ├── straylight-hwscan.conf
│           ├── straylight-drivers.conf
│           ├── straylight-hwtest.conf
│           ├── users.conf
│           ├── packages.conf
│           ├── straylight-postinstall.conf
│           └── finished.conf
│
├── tests/                            # Test infrastructure
│   ├── unit/
│   ├── integration/
│   ├── e2e/
│   └── CMakeLists.txt
│
└── config/                           # Runtime defaults
    ├── themes/
    ├── compositor/straylight-compositor.conf
    ├── shell/straylight-shell.conf
    └── sysctl.d/99-straylight.conf
```

---

## Boot Flow (Three-Layer Architecture)

### Phase 1: Live USB Installer (Calamares)

1. Boot from USB into live Debian environment
2. Calamares launches with StrayLight branding
3. **Disk selection** — user picks target disk and partition scheme
4. **Hardware scan** — detect GPU (NVIDIA/AMD/Intel), NIC chipset, SGX capability, PMEM namespaces
5. **Driver installation** — install nvidia-driver / firmware-amd-graphics / firmware packages from Debian repos, build StrayLight DKMS modules
6. **Hardware test** — quick validation (failures are warnings, not blockers):
   - GPU renders test frame (warning: "GPU acceleration unavailable, software rendering will be used")
   - NIC has link (warning: "No network detected, configure later in settings")
   - Entropy source produces output (warning: "Hardware RNG unavailable, using software fallback")
   - Disk SMART OK (blocking: "Disk health critical, select a different disk")
7. **Create admin user** — username, password, hostname
8. **Install to disk** — debootstrap + `apt install straylight-os`
9. **Reboot** — pull USB, boot from disk

### Phase 2: First Boot — `straylight-firstboot` (automatic, no UI)

Runs once as a systemd service before graphical.target. No user interaction.

1. `straylight-firstboot.service` executes (Type=oneshot, Before=graphical.target)
2. Generates machine-id, SSH host keys
3. Rebuilds DKMS modules for installed kernel
4. Detects hardware changes since installer (hot-plugged devices)
5. Configures kernel parameters in `/etc/sysctl.d/99-straylight.conf`
6. Sets firstboot flag: `/var/lib/straylight/state` → `oobe`
7. Service completes, systemd proceeds to graphical.target

### Phase 3: First Boot — `straylight-oobe` (interactive, one-time)

Runs as a Wayland window after the desktop loads. Triggered when state = `oobe`.

1. Compositor starts, greeter skipped (auto-login for OOBE only)
2. `straylight-oobe` launches as a fullscreen Wayland window:
   - **Welcome** — "Welcome to StrayLight"
   - **Account confirmation** — verify admin account, create additional users
   - **Package profile** — ML workstation / developer / server / minimal (installs/removes packages)
   - **Network configuration** — WiFi/ethernet setup via NetworkManager D-Bus
   - **Summary + apply**
3. OOBE updates state: `/var/lib/straylight/state` → `wizard`
4. System reboots or re-logs to apply profile changes

### Phase 4: First Boot — `straylight-wizard` (personalization)

Runs as a normal Wayland window after login. Triggered when state = `wizard`.

1. Greeter (login screen) renders, user logs in
2. Desktop loads, `straylight-wizard` launches:
   - **Theme selection** — live preview (cyberpunk / default / minimal)
   - **Dock/panel layout** — top bar, left dock, bottom dock preferences
   - **ML environment setup** — detect installed frameworks, configure GPU scheduling profile
   - **Summary + apply**
3. Wizard updates state: `/var/lib/straylight/state` → `complete`
4. Wizard can be re-run anytime from Settings → Personalization

### Phase 5: Normal Boot (every subsequent boot)

1. GRUB loads kernel + initramfs
2. systemd reaches multi-user.target:
   - `straylight-entropy.service` (entropy pool)
   - `straylight-bus.service` (IPC broker)
   - `straylight-core.service` (orchestrator)
   - `straylight-registry.service` (config store)
   - `straylight-scheduler.service` (CPU pinning)
   - `straylight-agent.service` (task distribution)
   - `straylight-fuse.service` (tensor filesystem)
3. systemd reaches graphical.target:
   - `straylight-compositor.service` (Wayland server, Type=notify)
   - `straylight-greeter.service` (login screen via ext-session-lock-v1)
4. User authenticates via PAM
5. Greeter launches user session:
   - `straylight-shell.service` (desktop panels)
   - User's autostart applications

---

## Shared Libraries

### libstraylight-common.so

Every binary links this. Provides:
- `Result<T,E>` type (backported `std::expected`)
- Error code taxonomy
- JSON config loader (nlohmann/json)
- Unix socket client/server + D-Bus helpers
- spdlog wrapper with structured logging
- Common types: tensor descriptors, device enums

### libstraylight-ml.so

ML domain library. Provides:
- Tensor type with shape, dtype, device
- Computation graph IR (DAG of operations)
- Framework bridge API (PyTorch/JAX/TF/ONNX interception)
- KV cache with LRU eviction and compression

### libstraylight-net.so

Networking domain library. Provides:
- Socket abstraction (AF_XDP, RDMA, UDP)
- Zero-copy tensor transport protocol
- Wire protocol definitions (header format, tensor serialization)

### libstraylight-hw.so

Hardware abstraction library. Provides:
- VPU slab allocator (CUDA/ROCm/oneAPI backends)
- Hardware entropy (RDRAND/RDSEED + /dev/urandom)
- Persistent memory (libpmem2 DAX wrapper)
- Intel SGX enclave management

---

## Kernel Modules (6 total, all via DKMS)

| Module | Purpose | Key APIs |
|--------|---------|----------|
| straylight-vpu.ko | GPU memory management | `/dev/straylight-vpu` ioctl, DMA-BUF, sysfs params |
| straylight-hypervisor.ko | KVM extensions | VT-x VMCS, EPT, VM-exit profiling |
| straylight-scheduler.ko | ML-aware sched_class | Custom task placement, NUMA + P/E-core topology |
| straylight-xdp (eBPF) | Packet processing | BPF programs loaded via libbpf, AF_XDP rings |
| straylight-entropy.ko | Hardware entropy source | hwrng registration, RDRAND/jitter harvesting |
| straylight-enclave.ko | SGX kernel extensions | `/dev/straylight-sgx`, EPC management, sealed storage |

Each kernel module has a userspace fallback in the shared libraries. If the module is not loaded, the subsystem degrades gracefully to the userspace implementation.

---

## 18 Subsystem Binaries

Each binary links against the shared libraries. Classified as either a **persistent daemon** (runs continuously, has a systemd service) or an **on-demand tool** (invoked by core or CLI, exits when done).

| # | Binary | Package | Type | Purpose | Links |
|---|--------|---------|------|---------|-------|
| 1 | straylight-core | core | daemon | Pipeline orchestrator, diagnostics, inventory | common, ml, net, hw |
| 2 | straylight-bus | core | daemon | Zero-copy tensor IPC via /dev/shm | common, ml |
| 3 | straylight-registry | core | daemon | Persistent KV store (single-node default, Raft for cluster) | common, net |
| 4 | straylight-scheduler | core | daemon | CPU/GPU pinning with topology awareness | common |
| 5 | straylight-entropy | core | daemon | HWRNG pool with NIST DRBG | common, hw |
| 6 | straylight-agent | ml | daemon | Event-driven task distribution | common, ml, net |
| 7 | straylight-compiler | ml | tool | Graph optimization and codegen | common, ml |
| 8 | straylight-morph | ml | tool | Quantization, pruning, distillation | common, ml, hw |
| 9 | straylight-snn | ml | tool | Spiking neural network simulator | common, ml |
| 10 | straylight-rhem | ml | tool | Heterogeneous device management | common, ml, hw |
| 11 | straylight-xdp | network | tool | eBPF/AF_XDP loader and manager | common, net + libbpf |
| 12 | straylight-dpdk | network | tool | DPDK packet processing pipeline | common, net, ml + libdpdk |
| 13 | straylight-rdma-bus | network | tool | RDMA zero-copy tensor transport | common, net, ml + libibverbs |
| 14 | straylight-quantum | exotic | tool | Quantum gate simulator (state vector) | common + Eigen |
| 15 | straylight-photonics | exotic | tool | Photonic mesh simulation + device I/O | common, hw |
| 16 | straylight-pmem | exotic | tool | Persistent memory allocator + checkpoints | common, hw + libpmem2 |
| 17 | straylight-enclave | exotic | tool | SGX secure inference | common, ml + SGX SDK |
| 18 | straylight-fuse | exotic | daemon | Transparent tensor compression filesystem | common, ml + libfuse3 |

**Kernel/userspace scheduler interaction:** `straylight-scheduler.ko` (kernel module) provides the custom `sched_class` for ML-aware task placement. The userspace `straylight-scheduler` (daemon) communicates with it via `/proc/straylight/sched` and sysfs to configure policies, profiles, and topology hints. The kernel module handles real-time scheduling decisions; the userspace daemon handles policy configuration and monitoring.

---

## Packaging (7 `.deb` packages + 1 metapackage)

```
straylight-os (metapackage)
  Depends: straylight-common, straylight-core, straylight-desktop,
           straylight-ml, straylight-network, straylight-exotic,
           straylight-kernel

straylight-common
  Contents: libstraylight-{common,ml,net,hw}.so + headers

straylight-core
  Contents: core, bus, registry, scheduler, entropy binaries + systemd units
  Depends: straylight-common

straylight-desktop
  Contents: compositor, shell, apps (terminal, files, monitor, settings, wizard),
            themes, icons, Calamares modules
  Depends: straylight-common, straylight-core, wlroots, imgui

straylight-ml
  Contents: compiler, agent, morph, snn, rhem binaries
  Depends: straylight-common, straylight-core

straylight-network
  Contents: xdp, dpdk, rdma-bus binaries
  Depends: straylight-common, straylight-core

straylight-exotic
  Contents: quantum, photonics, pmem, enclave, fuse binaries
  Depends: straylight-common, straylight-core

straylight-kernel
  Contents: 6 DKMS module source trees
  Depends: dkms, linux-headers-amd64
```

---

## Key Architectural Differences from Old Codebase

| Old | New |
|-----|-----|
| C++17 | C++20 (concepts, ranges, `std::format`) |
| No shared libraries; 17 standalone monolithic `.cpp` files | 4 shared libraries; subsystems are thin binaries |
| `popen()` to call other binaries | Direct function calls via shared libs + Unix socket IPC |
| `#ifdef __APPLE__` everywhere | Linux x86_64 only, no portability shims |
| 3 competing GUI implementations | Single Wayland compositor + ImGui shell |
| Single GLFW window, can't manage other apps | Real Wayland WM, manages any Wayland client |
| No packaging, manual ISO build | 7 `.deb` packages + live-build ISO |
| No service management | systemd units with proper dependency ordering |
| Mix of cout/cerr/custom loggers | spdlog structured logging everywhere |
| Silent failures, mixed error handling | `Result<T,E>` throughout |
| Duplicated code across binaries | Shared library layer eliminates duplication |

---

## External Dependencies

| Dependency | Version | Used By |
|------------|---------|---------|
| wlroots | 0.18+ | compositor |
| wayland-server/client | 1.22+ | compositor, shell, apps, greeter |
| wayland-protocols | 1.34+ | layer-shell, session-lock, xdg-shell |
| libinput | 1.25+ | compositor |
| pixman | 0.42+ | compositor |
| EGL + OpenGL ES 3.0 | - | compositor, shell (via wl_egl_window) |
| ImGui | 1.90+ | shell, apps (rendered to EGL/Wayland surfaces) |
| nlohmann/json | 3.11+ | common |
| spdlog | 1.13+ | common |
| sdbus-c++ | 2.0+ | common (D-Bus bindings) |
| PAM | - | greeter (authentication) |
| NetworkManager | 1.44+ | network configuration, OOBE |
| PipeWire | 1.0+ | audio/video subsystem |
| Eigen | 3.4+ | quantum |
| libbpf | 1.3+ | xdp |
| libdpdk | 23.11+ | dpdk |
| libibverbs + librdmacm | - | rdma-bus |
| libpmem2 | 1.12+ | pmem |
| Intel SGX SDK | 2.22+ | enclave |
| libfuse3 | 3.14+ | fuse |
| Calamares | 3.3+ | installer |
| GTest + GMock | 1.14+ | tests |

Note: GLFW is NOT used. The shell and all apps are native Wayland clients using `wl_egl_window` + EGL for ImGui rendering. The compositor uses wlroots which provides its own backend abstraction.

---

## Shared Library Versioning

All shared libraries follow semantic versioning with SO version:

| Library | SO Version | Symbol Visibility |
|---------|-----------|-------------------|
| libstraylight-common.so.1 | 1.0.0 | Default hidden, explicit `STRAYLIGHT_EXPORT` |
| libstraylight-ml.so.1 | 1.0.0 | Default hidden, explicit `STRAYLIGHT_EXPORT` |
| libstraylight-net.so.1 | 1.0.0 | Default hidden, explicit `STRAYLIGHT_EXPORT` |
| libstraylight-hw.so.1 | 1.0.0 | Default hidden, explicit `STRAYLIGHT_EXPORT` |

ABI stability: within the same SO major version, ABI is stable. Major version bumps require package rebuilds of dependents.

---

## Testing Infrastructure

```
tests/
├── unit/                             # Per-library and per-subsystem unit tests
│   ├── common/                       # libstraylight-common tests
│   │   ├── test_config.cpp
│   │   ├── test_ipc.cpp
│   │   ├── test_log.cpp
│   │   └── test_result.cpp
│   ├── ml/                           # libstraylight-ml tests
│   ├── net/                          # libstraylight-net tests
│   ├── hw/                           # libstraylight-hw tests
│   └── subsystems/                   # Per-subsystem unit tests
│       ├── test_bus_pubsub.cpp
│       ├── test_registry_store.cpp
│       ├── test_scheduler_topology.cpp
│       ├── test_entropy_drbg.cpp
│       ├── test_compiler_passes.cpp
│       ├── test_quantum_gates.cpp
│       └── ...
├── integration/                      # Cross-subsystem integration tests
│   ├── test_bus_registry_flow.cpp    # Bus → Registry data flow
│   ├── test_core_pipeline.cpp        # Core orchestrating multiple subsystems
│   ├── test_compositor_shell.cpp     # Compositor ↔ shell IPC
│   └── test_boot_sequence.cpp        # Daemon startup ordering
├── e2e/                              # End-to-end system tests
│   ├── test_installer.sh             # Calamares ISO install in QEMU
│   ├── test_firstboot.sh             # Three-layer boot sequence
│   └── test_desktop_session.sh       # Login → desktop → app launch
└── CMakeLists.txt                    # Test targets, CTest integration
```

CMakePresets.json includes:
- `dev` preset: Debug, sanitizers (ASan + UBSan), tests ON
- `release` preset: Release, LTO, tests OFF
- `package` preset: RelWithDebInfo, tests OFF, packaging ON
- `test` preset: Debug, coverage (gcov), tests ON
