# StrayLight OS

> A cognitive, AI-native Linux distribution built on Debian — Wayland-first desktop, GPU-aware kernel, and a unified subsystem fabric for ML, networking, and self-healing storage.

[![Status](https://img.shields.io/badge/status-alpha-orange)](#project-status)
[![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-informational)](#requirements)
[![Base](https://img.shields.io/badge/base-Debian%20%7C%20Ubuntu-A81D33)](https://www.debian.org)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C)](https://cmake.org)
[![Compositor](https://img.shields.io/badge/compositor-wlroots-3B9DD8)](https://gitlab.freedesktop.org/wlroots/wlroots)

---

## Table of Contents

- [Overview](#overview)
- [Project Status](#project-status)
- [Highlights](#highlights)
- [Architecture](#architecture)
- [Repository Layout](#repository-layout)
- [Requirements](#requirements)
- [Quick Start](#quick-start)
- [Building from Source](#building-from-source)
- [Building Debian Packages](#building-debian-packages)
- [Building the Live ISO](#building-the-live-iso)
- [Subsystems](#subsystems)
- [Boot Flow](#boot-flow)
- [Documentation](#documentation)
- [Development](#development)
- [Contributing](#contributing)
- [Security](#security)
- [License](#license)

---

## Overview

StrayLight OS is a clean-room operating system distribution for Linux x86_64 that treats AI workloads, GPU memory, and zero-copy data movement as first-class concerns of the OS — not as opt-in user-space libraries bolted on top.

It is delivered as a layered set of Debian packages on top of a stock Debian/Ubuntu base, plus a custom Wayland compositor, an ImGui-based desktop shell, and a fleet of subsystem daemons that share a unified IPC bus. Six DKMS kernel modules extend the running kernel with GPU memory virtualization, an ML-aware scheduler, hardware entropy, eBPF/XDP packet pipelines, and SGX enclave support.

The goal: a system where launching a model, migrating a process to another node, taking a filesystem snapshot, or hot-patching a running service are all ordinary, declarative operations exposed through a single command-line surface and a single GUI.

## Project Status

**Alpha — under active development.** The codebase is a clean-room rewrite of an earlier prototype. APIs, ABIs, package layouts, and command surfaces may change without notice until a `1.0` release is tagged. Do not deploy to production hosts. Expect rough edges in: ISO build reproducibility, Calamares branding, and OOBE flow polish.

Tracked design and plan documents live under [docs/superpowers/](docs/superpowers/).

## Highlights

- **AI-native runtime** — built-in tensor IPC bus, graph compiler, model morphing (quantize/prune/distill), spiking-NN simulator, and an LLM-backed natural-language command line.
- **GPU memory virtualization** — the `straylight-vpu` kernel module manages a slab allocator across CUDA / ROCm / oneAPI backends, exposed through `/dev/straylight-vpu` and DMA-BUF.
- **ML-aware scheduling** — a custom `sched_class` aware of NUMA, P/E-core topology, and GPU locality, with a userspace policy daemon for profiles and pinning.
- **Zero-copy fabric** — shared-memory tensor bus, AF_XDP/DPDK pipelines, and an RDMA transport for cross-machine tensor movement.
- **Self-healing storage** — Btrfs/ZFS snapshots, A/B atomic system updates, live kernel hot-patching, process checkpointing/rewind, and full system migration between hosts.
- **Wayland-first desktop** — wlroots compositor, ImGui shell rendered through `wl_egl_window` + EGL, no GLFW, no X dependency.
- **Unified CLI** — every subsystem follows the `straylight-<name> [OPTIONS] COMMAND [ARGS...]` convention, communicates over the bus at `/run/straylight/bus.sock`, logs structured JSON, and supports `--json` / `--yaml` output.
- **Debian-native packaging** — seven `.deb` groups plus a metapackage, live-build ISO, Calamares installer with hardware-scan and driver pre-install steps.

## Architecture

```
+----------------------------------------------------------------------+
|                        Desktop Experience                            |
|  Greeter | OOBE | Wizard | Shell (ImGui) | Apps (terminal,           |
|                          files, monitor, settings, GUIs...)          |
+----------------------------------------------------------------------+
|                      Wayland Compositor (wlroots)                    |
|       layer-shell . xdg-shell . session-lock . input . output        |
+----------------------------------------------------------------------+
|                          Subsystem Fabric                            |
|  core | bus | registry | scheduler | entropy | agent | compiler |    |
|  morph | snn | rhem | xdp | dpdk | rdma-bus | quantum | photonics |  |
|  pmem | enclave | fuse | + 30+ tools/services (alice, flux, ...)     |
+----------------------------------------------------------------------+
|     Shared Libraries: libstraylight-{common, ml, net, hw}.so         |
+----------------------------------------------------------------------+
|                  systemd  .  D-Bus  .  udev  .  PAM                  |
+----------------------------------------------------------------------+
|                      DKMS Kernel Modules                             |
|   vpu | hypervisor | scheduler | xdp (eBPF) | entropy | enclave      |
+----------------------------------------------------------------------+
|                Linux Kernel + Debian/Ubuntu Base                     |
+----------------------------------------------------------------------+
```

## Repository Layout

```
straylight/
  apps/             Built-in applications + per-subsystem GUIs
  assets/           Icons, themes, branding
  bin/              Subsystem daemon sources (core, bus, registry, ...)
  cmake/            Reusable CMake modules and Find* scripts
  compositor/       Wayland compositor (wlroots-based)
  config/           Runtime defaults (sysctl, themes, compositor, shell)
  docs/             Tool reference + design specs (docs/superpowers)
  etc/              On-disk configuration shipped with packages
  iso/              live-build configuration + Calamares modules
  kernel/           DKMS kernel module sources
  lib/              Shared libraries: common, ml, net, hw
  packaging/        Debian packaging (debian/ directories per .deb)
  scripts/          build-iso.sh, build-packages.sh
  services/         systemd units, D-Bus policies, udev rules,
                    long-running services (alice, flux, weave, ...)
  shell/            ImGui desktop shell + extensions
  tests/            Unit, integration, and end-to-end tests
  tools/            On-demand CLI tools (alice-cli, snapshot, vault, ...)
  CMakeLists.txt    Top-level build
  CMakePresets.json Build presets: dev, release, package, test
```

## Requirements

### Build host

- Debian Bookworm/Trixie or Ubuntu 24.04+ (amd64)
- CMake **3.25** or newer, Ninja
- A C++20 compiler (GCC 13+ or Clang 17+)
- `dpkg-dev`, `debhelper` (>= 13), `dh-cmake` for `.deb` builds
- `live-build`, `debootstrap`, `squashfs-tools`, `xorriso`,
  `grub-pc-bin`, `grub-efi-amd64-bin` for ISO builds

### Runtime libraries (development headers)

`wlroots 0.18+`, `wayland 1.22+`, `wayland-protocols 1.34+`, `libinput 1.25+`, `pixman 0.42+`, EGL/GLES, ImGui 1.90+, `nlohmann/json 3.11+`, `spdlog 1.13+`, `sdbus-c++ 2.0+`, PAM, NetworkManager, PipeWire, Eigen, libbpf, DPDK, libibverbs, libpmem2, Intel SGX SDK, libfuse3, GTest/GMock.

A consolidated `apt-get build-dep` list will be published with the first tagged release.

## Quick Start

The fastest path is to build the live ISO and boot it in a VM:

```bash
git clone https://github.com/H-XX-D/straylight.git
cd straylight

# Build all .deb packages
sudo ./scripts/build-packages.sh

# Build the live ISO (consumes the .debs above)
sudo ./scripts/build-iso.sh

# Boot it
qemu-system-x86_64 \
    -cdrom output/straylight-os-1.0.0-amd64.iso \
    -m 8G -smp 4 -enable-kvm
```

The ISO drops you into Calamares for an interactive install, then runs through the three-phase first-boot flow described in [Boot Flow](#boot-flow).

## Building from Source

For day-to-day development you will not want to rebuild the ISO every iteration. Use the in-tree CMake presets instead:

```bash
# Configure (Debug + ASan/UBSan + tests)
cmake --preset dev

# Build
cmake --build build/dev -j

# Run unit + integration tests
ctest --preset dev --output-on-failure
```

Available presets:

| Preset    | Build type      | Tests | Sanitizers | Notes                              |
|-----------|-----------------|-------|------------|------------------------------------|
| `dev`     | Debug           | yes   | ASan+UBSan | Day-to-day development             |
| `test`    | Debug           | yes   | gcov       | Coverage runs (CI)                 |
| `release` | Release         | no    | no         | LTO, `-march=x86-64-v3`            |
| `package` | RelWithDebInfo  | no    | no         | Used by `dpkg-buildpackage`        |

Top-level CMake options (default values shown):

```cmake
-DBUILD_TESTS=ON         # GTest unit + integration tests
-DBUILD_COMPOSITOR=ON    # Wayland compositor (requires wlroots)
-DBUILD_SHELL=ON         # ImGui desktop shell + extensions
-DBUILD_APPS=ON          # Built-in apps + GUIs
-DBUILD_SUBSYSTEMS=ON    # All subsystem binaries and tools
```

## Building Debian Packages

```bash
# All packages, in dependency order
sudo ./scripts/build-packages.sh

# Single package
sudo ./scripts/build-packages.sh straylight-core

# Verify build-deps without building
./scripts/build-packages.sh --check-deps

# Skip GPG signing (development)
sudo ./scripts/build-packages.sh --no-sign
```

`.deb` files land in `output/debs/` along with a `Packages` index that can be used as a local APT repository:

```bash
echo "deb [trusted=yes] file://$PWD/output/debs ./" | \
  sudo tee /etc/apt/sources.list.d/straylight-local.list
sudo apt update && sudo apt install straylight-os
```

The package set:

| Package                | Contents                                                  |
|------------------------|-----------------------------------------------------------|
| `straylight-common`    | `libstraylight-{common,ml,net,hw}.so` + headers           |
| `straylight-core`      | core, bus, registry, scheduler, entropy + systemd units   |
| `straylight-compositor`| Wayland compositor                                        |
| `straylight-desktop`   | shell, apps, themes, icons, Calamares modules             |
| `straylight-ml`        | compiler, agent, morph, snn, rhem                         |
| `straylight-network`   | xdp, dpdk, rdma-bus                                       |
| `straylight-exotic`    | quantum, photonics, pmem, enclave, fuse                   |
| `straylight-kernel`    | DKMS source trees for the six kernel modules              |
| `straylight-os`        | Metapackage that depends on all of the above              |

## Building the Live ISO

```bash
# Default build
sudo ./scripts/build-iso.sh

# From scratch
sudo ./scripts/build-iso.sh --clean

# Configure only (debug)
sudo ./scripts/build-iso.sh --config-only

# Use an external APT repository for StrayLight packages
sudo ./scripts/build-iso.sh --repo "https://apt.example.com/straylight"
```

The output is a hybrid ISO at `output/straylight-os-<version>-amd64.iso` plus a `.sha256` checksum. ISO builds take roughly **15 to 45 minutes** depending on your APT mirror and disk speed.

## Subsystems

StrayLight ships **18 core subsystem binaries** plus a wider set of services and tools that follow the same conventions. The core 18, grouped by package:

| Package        | Type    | Binaries                                                          |
|----------------|---------|-------------------------------------------------------------------|
| `core`         | daemon  | `straylight-core`, `bus`, `registry`, `scheduler`, `entropy`      |
| `ml`           | mixed   | `agent` (daemon), `compiler`, `morph`, `snn`, `rhem`              |
| `network`      | tool    | `xdp`, `dpdk`, `rdma-bus`                                         |
| `exotic`       | mixed   | `quantum`, `photonics`, `pmem`, `enclave`, `fuse` (daemon)        |

In addition, the system layer ships **55+ user-facing tools and services** for system administration, ML/HPC, security, and the desktop. The full reference is at [docs/index.md](docs/index.md).

### Conventions

- **Invocation** — `straylight-<tool> [OPTIONS] COMMAND [ARGS...]`
- **Configuration** — `/etc/straylight/<tool>.toml`, user overrides at `~/.config/straylight/<tool>.toml`
- **Logs** — structured JSON to the StrayLight journal, queryable via `straylight-log`
- **IPC** — Unix-domain bus at `/run/straylight/bus.sock` plus per-subsystem D-Bus interfaces under `org.straylight.*`
- **Output** — human tables by default; `--json` for machine-parseable, `--yaml` for config-friendly
- **Exit codes** — POSIX (0 success, 1 error, 2 usage)

## Boot Flow

The first boot is a three-phase flow; subsequent boots are a normal systemd start.

1. **Live ISO + Calamares** — disk selection, hardware scan (GPU / NIC / SGX / PMEM), driver install, admin user creation, `apt install straylight-os`, reboot.
2. **`straylight-firstboot.service`** — non-interactive oneshot. Generates `machine-id`, SSH host keys, rebuilds DKMS modules, applies sysctl tuning, sets state to `oobe`.
3. **`straylight-oobe`** — fullscreen Wayland window. Confirms admin account, picks a package profile (ML workstation / developer / server / minimal), configures network, applies and reboots.
4. **`straylight-wizard`** — first interactive desktop session. Theme selection, dock layout, ML environment setup. Re-runnable from Settings.
5. **Normal boot** — systemd starts `entropy`, `bus`, `core`, `registry`, `scheduler`, `agent`, `fuse` daemons, then the compositor and greeter.

Full sequence diagram and service ordering: [docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md](docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md).

## Documentation

- **Tool reference** — [docs/index.md](docs/index.md). Man-page-style reference for every CLI tool, organized by category.
- **Design spec** — [docs/superpowers/specs/](docs/superpowers/specs/). The clean-room rewrite design document and decision log.
- **Implementation plans** — [docs/superpowers/plans/](docs/superpowers/plans/). Phased delivery plans for each layer.

## Development

### Running tests

```bash
cmake --preset dev
cmake --build build/dev -j
ctest --preset dev --output-on-failure
```

Tests live under [tests/](tests/) and are split into:

- `tests/unit/` — per-library and per-subsystem
- `tests/integration/` — cross-subsystem flows (bus + registry, compositor + shell, boot ordering)
- `tests/e2e/` — full system smoke tests run inside QEMU

### Coding style

- C++20. No portability shims (Linux x86_64 only).
- Errors flow through `Result<T,E>` (a `std::expected` backport in `libstraylight-common`). No exceptions across module boundaries.
- Logging via `spdlog`, structured JSON only.
- Format with the in-repo `.clang-format`.
- Symbols are hidden by default; export with `STRAYLIGHT_EXPORT`.

### Shared library SO versioning

Each shared library is independently SO-versioned. Within a major SO version, ABI is stable; bumps require rebuilding dependents. The current release is `1.0.0` for all four libs.

## Contributing

This project does not yet have a `CONTRIBUTING.md`. Until one lands, please:

1. Open an issue describing the change before you start work on anything non-trivial.
2. Keep PRs scoped to a single subsystem when possible.
3. Add tests under `tests/unit/` or `tests/integration/` for new behavior.
4. Run `cmake --build build/dev -j && ctest --preset dev` locally before pushing.
5. Follow the existing commit-message style (`feat:`, `fix:`, `docs:`, `refactor:` prefixes — see `git log`).

## Security

There is no published security policy yet. If you believe you have found a vulnerability, please open a private security advisory through GitHub's [Security Advisories](https://github.com/H-XX-D/straylight/security/advisories) feature rather than a public issue.

The project ships several security-sensitive subsystems (`vault`, `enclave`, `shield`, `whisper`, `sandbox`) — these are pre-1.0 and should not be relied on for protecting production secrets.

## License

No license file is currently present in this repository. Until a license is added, default copyright applies and the source is **not** open source under any OSI-approved license. Track [the license discussion](https://github.com/H-XX-D/straylight/issues) for the licensing decision, or open an issue if one does not yet exist.

---

<sub>StrayLight OS is an independent project. Debian, Ubuntu, NVIDIA, CUDA, ROCm, oneAPI, Intel, SGX, Wayland, and other names referenced here are trademarks of their respective owners.</sub>
