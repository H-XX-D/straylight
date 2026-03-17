# StrayLight OS Tool Reference

## Overview

StrayLight OS ships with 55 integrated command-line tools that collectively manage every layer of the operating system, from hardware topology and kernel tuning through AI-driven automation and desktop experience. Each tool follows a uniform invocation pattern (`straylight-<name> [OPTIONS] COMMAND [ARGS...]`) and communicates with its peers over the StrayLight IPC bus.

This index organizes every tool by functional category. Click any tool name to read its full man-page-style reference.

---

## System Core

Tools that manage the kernel, boot process, system health, and foundational OS services.

| Tool | Description |
|------|-------------|
| [straylight-autotune](straylight-autotune.md) | Kernel tuning daemon that continuously optimizes sysctl, scheduler, and memory parameters |
| [straylight-health](straylight-health.md) | Unified health monitor aggregating metrics from all subsystems |
| [straylight-thermal](straylight-thermal.md) | Thermal zone management and fan-curve control |
| [straylight-power](straylight-power.md) | Power management, governor selection, and battery policy |
| [straylight-boot](straylight-boot.md) | Boot manager for kernel selection, initramfs, and boot-environment rollback |
| [straylight-update](straylight-update.md) | Atomic system updater with A/B partition support |

## AI & Prediction

Tools that bring machine-learning capabilities directly into OS operations.

| Tool | Description |
|------|-------------|
| [straylight-alice](straylight-alice.md) | AI system monitor that detects anomalies, forecasts failures, and suggests remediations |
| [straylight-intent](straylight-intent.md) | Natural language command interface backed by a local LLM |
| [straylight-predict](straylight-predict.md) | Predictive preloading engine that anticipates resource needs before they arise |

## Hardware & Devices

Tools that discover, configure, and optimize physical hardware.

| Tool | Description |
|------|-------------|
| [straylight-fabric](straylight-fabric.md) | Device topology graph builder for PCIe, USB, and NVLink trees |
| [straylight-nerve](straylight-nerve.md) | IRQ affinity optimizer that pins interrupts to optimal CPU cores |
| [straylight-display](straylight-display.md) | Display configuration for resolution, refresh rate, HDR, and multi-monitor layout |
| [straylight-input](straylight-input.md) | Input device configuration for keyboards, mice, tablets, and gamepads |
| [straylight-audio](straylight-audio.md) | Audio routing engine with per-application sink/source control |
| [straylight-disk](straylight-disk.md) | Disk management covering partitioning, RAID, encryption, and SMART monitoring |

## Memory & Allocation

Tools that govern how processes share memory, CPU, and I/O bandwidth.

| Tool | Description |
|------|-------------|
| [straylight-fuse](straylight-fuse.md) | Process fusion engine that merges cooperating processes into a shared address space |
| [straylight-splice](straylight-splice.md) | Zero-copy pipeline builder using kernel splice and io_uring |
| [straylight-quota](straylight-quota.md) | Resource budget manager with per-user and per-application cgroup limits |
| [straylight-capsule](straylight-capsule.md) | Resource-contract packages that declare and enforce CPU, memory, and I/O guarantees |

## Security & Isolation

Tools that harden the system and protect secrets.

| Tool | Description |
|------|-------------|
| [straylight-sandbox](straylight-sandbox.md) | Container isolation with namespace, seccomp, and landlock enforcement |
| [straylight-shield](straylight-shield.md) | Security audit framework scanning for CVEs, misconfigurations, and policy violations |
| [straylight-vault](straylight-vault.md) | Secret storage backed by the TPM and hardware security modules |
| [straylight-whisper](straylight-whisper.md) | Encrypted IPC channel for inter-process secret exchange |

## Networking

Tools that manage connectivity from the local NIC through multi-node clusters.

| Tool | Description |
|------|-------------|
| [straylight-mesh](straylight-mesh.md) | Distributed GPU pool for transparent multi-node compute |
| [straylight-bridge](straylight-bridge.md) | Cross-machine shared memory over RDMA or TCP fallback |
| [straylight-probe](straylight-probe.md) | Network scanner and diagnostic toolkit |
| [straylight-network](straylight-network.md) | Network management for interfaces, routing, DNS, and VPN tunnels |
| [straylight-remote](straylight-remote.md) | Remote system control with an SSH-free authenticated channel |
| [straylight-swarm](straylight-swarm.md) | Multi-node orchestration for workload placement and cluster health |

## Development

Tools aimed at developers who build, debug, and benchmark on StrayLight.

| Tool | Description |
|------|-------------|
| [straylight-trace](straylight-trace.md) | Application tracer using eBPF and dynamic probes |
| [straylight-lens](straylight-lens.md) | Full-stack tracing that correlates user-space, kernel, and network events |
| [straylight-bench](straylight-bench.md) | Hardware and software benchmark suite |
| [straylight-garden](straylight-garden.md) | Environment manager for reproducible dev/build environments |
| [straylight-rewind](straylight-rewind.md) | Process checkpointing and time-travel debugging |
| [straylight-replay](straylight-replay.md) | Flight recorder that captures system events for post-mortem analysis |

## Data & Workflows

Tools that move, transform, schedule, and observe data flows.

| Tool | Description |
|------|-------------|
| [straylight-flux](straylight-flux.md) | Realtime stream processor for structured and unstructured event data |
| [straylight-pipe](straylight-pipe.md) | Visual dataflow editor with a TUI and YAML backend |
| [straylight-weave](straylight-weave.md) | Service composition engine that wires microservices into declarative graphs |
| [straylight-cron](straylight-cron.md) | Smart task scheduler with dependency awareness and resource gating |
| [straylight-timeline](straylight-timeline.md) | Activity tracker that records system-wide events to a queryable log |
| [straylight-log](straylight-log.md) | Structured log viewer with filtering, correlation, and live tail |

## Migration & Recovery

Tools that protect against data loss and enable seamless system transitions.

| Tool | Description |
|------|-------------|
| [straylight-snapshot](straylight-snapshot.md) | Filesystem snapshots using Btrfs/ZFS with automated retention policies |
| [straylight-mirror](straylight-mirror.md) | Live system cloning to a secondary disk or remote host |
| [straylight-migrate](straylight-migrate.md) | Full system migration between machines preserving state, secrets, and identity |
| [straylight-echo](straylight-echo.md) | System-wide undo that reverts configuration and filesystem changes |
| [straylight-hotpatch](straylight-hotpatch.md) | Live patching of running kernels and services without restart |
| [straylight-ghost](straylight-ghost.md) | Process migration that relocates running processes between nodes |

## Desktop

Tools that shape the graphical environment and user-facing experience.

| Tool | Description |
|------|-------------|
| [straylight-hub](straylight-hub.md) | Central dashboard aggregating status from every StrayLight subsystem |
| [straylight-dash](straylight-dash.md) | TUI system dashboard for terminal-first operators |
| [straylight-notify](straylight-notify.md) | Notification daemon with priority routing, grouping, and do-not-disturb |
| [straylight-color](straylight-color.md) | Color management with ICC profile support and night-shift scheduling |
| [straylight-fonts](straylight-fonts.md) | Font management including discovery, installation, and rendering hints |
| [straylight-users](straylight-users.md) | User and group management with role-based access control |
| [straylight-link](straylight-link.md) | Symlink manager that tracks, validates, and repairs symbolic links |
| [straylight-policy](straylight-policy.md) | System role and policy engine for declarative machine configuration |

---

## Conventions

All StrayLight tools share the following conventions:

- **Configuration** lives under `/etc/straylight/<tool>.toml` with user overrides in `~/.config/straylight/<tool>.toml`.
- **Logs** are emitted as structured JSON to the StrayLight journal, queryable with `straylight-log`.
- **Exit codes** follow POSIX conventions: 0 for success, 1 for general errors, 2 for usage errors.
- **IPC** uses the StrayLight bus at `/run/straylight/bus.sock` for inter-tool communication.
- **Output formats** default to human-readable tables; pass `--json` for machine-parseable output or `--yaml` for configuration-friendly output.
