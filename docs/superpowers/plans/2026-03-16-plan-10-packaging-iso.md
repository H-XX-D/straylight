# Plan 10: Packaging, Live ISO & Calamares Installer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the complete distribution packaging pipeline — 7 `.deb` packages + 1 metapackage, live ISO generation via `live-build`, Calamares installer with custom StrayLight modules, systemd units for all daemons, D-Bus policy configs, udev rules, and CMakePresets.json.

**Architecture:** Debian packaging under `packaging/`, ISO generation under `iso/live-build/`, Calamares configuration under `iso/calamares/`, service configs under `services/`. The metapackage `straylight-os` pulls in all component packages. The live ISO boots into a Calamares-driven installer that writes `straylight-os` to disk.

**Tech Stack:** dpkg-buildpackage, debhelper-compat 13, dh-cmake, live-build 1:20230502, Calamares 3.3+, systemd 254+, CMake 3.25+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Depends on:** Plans 1–9 (all binaries, libraries, kernel modules, compositor, shell, apps)

**Development environment:** Linux x86_64 required (Debian Bookworm/Trixie). Package builds require `dpkg-buildpackage`, `debhelper`, `dh-cmake`. ISO builds require `live-build`, `debootstrap`. Calamares module testing requires a running Calamares instance or QEMU VM.

---

## Chunk 1: Debian Packaging — straylight-common + straylight-core

### File Structure

```
packaging/straylight-common/debian/
├── control
├── rules
├── changelog
├── compat
├── install
├── libstraylight-common1.install
├── libstraylight-common-dev.install
└── copyright

packaging/straylight-core/debian/
├── control
├── rules
├── changelog
├── compat
├── install
└── copyright
```

### Task 1: straylight-common debian/control

**File:** `packaging/straylight-common/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-common
Section: libs
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               nlohmann-json3-dev (>= 3.11),
               libspdlog-dev (>= 1.13),
               libsdbus-c++-dev (>= 2.0),
               libeigen3-dev (>= 3.4),
               libpmem2-dev (>= 1.12),
               libsgx-enclave-common-dev
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: libstraylight-common1
Architecture: amd64
Depends: ${shlibs:Depends}, ${misc:Depends}
Description: StrayLight OS shared libraries
 Core shared libraries for StrayLight OS subsystems.
 Includes libstraylight-common, libstraylight-ml,
 libstraylight-net, and libstraylight-hw.

Package: libstraylight-common-dev
Architecture: amd64
Section: libdevel
Depends: libstraylight-common1 (= ${binary:Version}),
         ${misc:Depends},
         nlohmann-json3-dev (>= 3.11),
         libspdlog-dev (>= 1.13),
         libsdbus-c++-dev (>= 2.0)
Description: StrayLight OS shared libraries - development files
 Headers and CMake config files for building against
 libstraylight-common, libstraylight-ml, libstraylight-net,
 and libstraylight-hw.
```

---

### Task 2: straylight-common debian/rules

**File:** `packaging/straylight-common/debian/rules`

- [ ] **Step 1: Create rules file**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all
export DEB_CXXFLAGS_MAINT_APPEND = -std=c++20

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu \
		-DBUILD_SHARED_LIBS=ON \
		-DSTRAYLIGHT_BUILD_LIBS_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-common straylight-ml straylight-net straylight-hw

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L lib"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp

override_dh_shlibdeps:
	dh_shlibdeps -l/usr/lib/x86_64-linux-gnu
```

- [ ] **Step 2: `chmod +x packaging/straylight-common/debian/rules`**

---

### Task 3: straylight-common remaining debian files

**File:** `packaging/straylight-common/debian/changelog`

- [ ] **Step 1: Create changelog**

```
straylight-common (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * libstraylight-common.so.1: Result<T,E>, config, IPC, logging, types
  * libstraylight-ml.so.1: tensor, graph IR, framework bridge, KV cache
  * libstraylight-net.so.1: socket abstraction, transport, protocol
  * libstraylight-hw.so.1: GPU slab, entropy, pmem, SGX

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

**File:** `packaging/straylight-common/debian/compat`

- [ ] **Step 2: Create compat**

```
13
```

**File:** `packaging/straylight-common/debian/libstraylight-common1.install`

- [ ] **Step 3: Create runtime install list**

```
usr/lib/x86_64-linux-gnu/libstraylight-common.so.1*
usr/lib/x86_64-linux-gnu/libstraylight-ml.so.1*
usr/lib/x86_64-linux-gnu/libstraylight-net.so.1*
usr/lib/x86_64-linux-gnu/libstraylight-hw.so.1*
```

**File:** `packaging/straylight-common/debian/libstraylight-common-dev.install`

- [ ] **Step 4: Create dev install list**

```
usr/lib/x86_64-linux-gnu/libstraylight-common.so
usr/lib/x86_64-linux-gnu/libstraylight-ml.so
usr/lib/x86_64-linux-gnu/libstraylight-net.so
usr/lib/x86_64-linux-gnu/libstraylight-hw.so
usr/include/straylight/
usr/lib/x86_64-linux-gnu/cmake/straylight/
```

**File:** `packaging/straylight-common/debian/copyright`

- [ ] **Step 5: Create copyright**

```
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: straylight-common
Upstream-Contact: dev@straylight.dev
Source: https://github.com/straylight-os/straylight

Files: *
Copyright: 2026 StrayLight OS Team
License: GPL-3+
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 .
 On Debian systems, the complete text of the GNU General
 Public License version 3 can be found in "/usr/share/common-licenses/GPL-3".
```

---

### Task 4: straylight-core debian/control

**File:** `packaging/straylight-core/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-core
Section: admin
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               libstraylight-common-dev (>= 1.0.0),
               libsdbus-c++-dev (>= 2.0)
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-core
Architecture: amd64
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libstraylight-common1 (>= 1.0.0),
         dbus
Description: StrayLight OS core daemons
 Persistent daemon backbone for StrayLight OS.
 Includes straylight-core (orchestrator), straylight-bus (IPC broker),
 straylight-registry (KV config store), straylight-scheduler (CPU/GPU
 pinning), and straylight-entropy (HWRNG pool). Provides systemd units
 with proper dependency ordering for multi-user.target.
```

---

### Task 5: straylight-core debian/rules + install + changelog

**File:** `packaging/straylight-core/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DSTRAYLIGHT_BUILD_CORE_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-core straylight-bus straylight-registry \
		straylight-scheduler straylight-entropy

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L core"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp

override_dh_installsystemd:
	dh_installsystemd --name=straylight-bus
	dh_installsystemd --name=straylight-entropy
	dh_installsystemd --name=straylight-registry
	dh_installsystemd --name=straylight-scheduler
	dh_installsystemd --name=straylight-core
```

- [ ] **Step 2: `chmod +x packaging/straylight-core/debian/rules`**

**File:** `packaging/straylight-core/debian/install`

- [ ] **Step 3: Create install file**

```
usr/bin/straylight-core
usr/bin/straylight-bus
usr/bin/straylight-registry
usr/bin/straylight-scheduler
usr/bin/straylight-entropy
etc/straylight/core.conf
etc/straylight/bus.conf
etc/straylight/registry.conf
etc/straylight/scheduler.conf
etc/straylight/entropy.conf
```

**File:** `packaging/straylight-core/debian/straylight-core.straylight-bus.service`

- [ ] **Step 4: Create bus service for dh_installsystemd**

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
ProtectSystem=strict
ReadWritePaths=/run/straylight /var/lib/straylight
PrivateTmp=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-core/debian/straylight-core.straylight-entropy.service`

- [ ] **Step 5: Create entropy service**

```ini
[Unit]
Description=StrayLight Hardware Entropy Pool
Documentation=https://straylight.dev/docs/entropy
Before=straylight-bus.service straylight-registry.service
After=systemd-random-seed.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-entropy
Restart=on-failure
RestartSec=2s
User=root
AmbientCapabilities=CAP_SYS_ADMIN
LimitNOFILE=65536
WatchdogSec=30s
ProtectSystem=strict
ReadWritePaths=/run/straylight /dev/straylight-entropy
PrivateTmp=yes
NoNewPrivileges=no

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-core/debian/straylight-core.straylight-registry.service`

- [ ] **Step 6: Create registry service**

```ini
[Unit]
Description=StrayLight Configuration Registry
Documentation=https://straylight.dev/docs/registry
After=straylight-bus.service straylight-entropy.service
Requires=straylight-bus.service
Wants=straylight-entropy.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-registry
Restart=on-failure
RestartSec=2s
User=root
LimitNOFILE=65536
WatchdogSec=30s
ProtectSystem=strict
ReadWritePaths=/var/lib/straylight /run/straylight
PrivateTmp=yes
NoNewPrivileges=yes
StateDirectory=straylight

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-core/debian/straylight-core.straylight-scheduler.service`

- [ ] **Step 7: Create scheduler service**

```ini
[Unit]
Description=StrayLight CPU/GPU Scheduler
Documentation=https://straylight.dev/docs/scheduler
After=straylight-registry.service
Requires=straylight-registry.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-scheduler
Restart=on-failure
RestartSec=2s
User=root
AmbientCapabilities=CAP_SYS_NICE CAP_SYS_ADMIN
LimitNOFILE=65536
WatchdogSec=30s
ProtectSystem=strict
ReadWritePaths=/proc/straylight /sys/fs/cgroup /run/straylight
PrivateTmp=yes
NoNewPrivileges=no

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-core/debian/straylight-core.straylight-core.service`

- [ ] **Step 8: Create core orchestrator service**

```ini
[Unit]
Description=StrayLight Core Orchestrator
Documentation=https://straylight.dev/docs/core
After=straylight-bus.service straylight-registry.service straylight-scheduler.service
Requires=straylight-bus.service straylight-registry.service
Wants=straylight-scheduler.service straylight-entropy.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-core
Restart=on-failure
RestartSec=5s
User=root
LimitNOFILE=65536
WatchdogSec=60s
ProtectSystem=strict
ReadWritePaths=/var/lib/straylight /run/straylight
PrivateTmp=yes
NoNewPrivileges=yes
StateDirectory=straylight

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-core/debian/changelog`

- [ ] **Step 9: Create changelog**

```
straylight-core (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * straylight-core: pipeline orchestrator, diagnostics, inventory
  * straylight-bus: zero-copy tensor IPC, D-Bus session manager
  * straylight-registry: persistent KV store with watch notifications
  * straylight-scheduler: topology-aware CPU/GPU pinning
  * straylight-entropy: HWRNG pool with NIST DRBG

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

**File:** `packaging/straylight-core/debian/copyright`

- [ ] **Step 10: Create copyright** (same as straylight-common, change Upstream-Name to `straylight-core`)

---

### Task 6: Verify chunk 1

- [ ] `cd packaging/straylight-common && dpkg-buildpackage -us -uc -nc --check-builddeps 2>&1 | head -20` → verify deps parse
- [ ] `cd packaging/straylight-core && dpkg-buildpackage -us -uc -nc --check-builddeps 2>&1 | head -20` → verify deps parse
- [ ] `git add packaging/straylight-common/ packaging/straylight-core/`
- [ ] `git commit -m "feat(packaging): add debian packaging for straylight-common and straylight-core"`

---

## Chunk 2: Debian Packaging — straylight-desktop + straylight-ml

### File Structure

```
packaging/straylight-desktop/debian/
├── control
├── rules
├── changelog
├── install
└── copyright

packaging/straylight-ml/debian/
├── control
├── rules
├── changelog
├── install
└── copyright
```

### Task 1: straylight-desktop debian/control

**File:** `packaging/straylight-desktop/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-desktop
Section: x11
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               libstraylight-common-dev (>= 1.0.0),
               libwlroots-dev (>= 0.18),
               libwayland-dev (>= 1.22),
               wayland-protocols (>= 1.34),
               libinput-dev (>= 1.25),
               libpixman-1-dev (>= 0.42),
               libegl-dev,
               libgles-dev,
               libpam0g-dev,
               libpipewire-0.3-dev (>= 1.0),
               libsdbus-c++-dev (>= 2.0)
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-desktop
Architecture: amd64
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libstraylight-common1 (>= 1.0.0),
         straylight-core (>= 1.0.0),
         libwlroots12 (>= 0.18),
         libwayland-server0 (>= 1.22),
         libinput10 (>= 1.25),
         xwayland,
         fonts-noto,
         adwaita-icon-theme,
         pipewire (>= 1.0),
         wireplumber,
         network-manager
Recommends: calamares (>= 3.3)
Description: StrayLight OS desktop environment
 Wayland compositor (wlroots), ImGui desktop shell, built-in applications
 (terminal, file manager, system monitor, settings, wizard, OOBE, greeter),
 themes, icons, and Calamares installer modules.
```

---

### Task 2: straylight-desktop debian/rules + install

**File:** `packaging/straylight-desktop/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DSTRAYLIGHT_BUILD_DESKTOP_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-compositor straylight-shell \
		straylight-terminal straylight-files straylight-monitor \
		straylight-settings straylight-wizard straylight-oobe \
		straylight-greeter

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L desktop"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp

override_dh_installsystemd:
	dh_installsystemd --name=straylight-compositor
	dh_installsystemd --name=straylight-greeter
	dh_installsystemd --name=straylight-firstboot
	dh_installsystemd --name=straylight-oobe
```

**File:** `packaging/straylight-desktop/debian/install`

- [ ] **Step 2: Create install file**

```
usr/bin/straylight-compositor
usr/bin/straylight-shell
usr/bin/straylight-terminal
usr/bin/straylight-files
usr/bin/straylight-monitor
usr/bin/straylight-settings
usr/bin/straylight-wizard
usr/bin/straylight-oobe
usr/bin/straylight-greeter
usr/share/straylight/themes/
usr/share/icons/straylight/
usr/share/wayland-sessions/straylight.desktop
etc/straylight/compositor/straylight-compositor.conf
etc/straylight/shell/straylight-shell.conf
usr/lib/calamares/modules/straylight-hwscan/
usr/lib/calamares/modules/straylight-drivers/
usr/lib/calamares/modules/straylight-hwtest/
usr/lib/calamares/modules/straylight-postinstall/
etc/calamares/settings.conf
etc/calamares/modules/
```

**File:** `packaging/straylight-desktop/debian/straylight-desktop.straylight-compositor.service`

- [ ] **Step 3: Create compositor service**

```ini
[Unit]
Description=StrayLight Wayland Compositor
Documentation=https://straylight.dev/docs/compositor
After=systemd-logind.service dbus.service
Requires=dbus.service
ConditionPathExists=/dev/dri/card0

[Service]
Type=notify
ExecStart=/usr/bin/straylight-compositor
Restart=on-failure
RestartSec=2s
User=root
Environment=WLR_BACKENDS=drm
Environment=WLR_RENDERER=gles2
Environment=XDG_RUNTIME_DIR=/run/user/%U
LimitNOFILE=65536
WatchdogSec=30s

[Install]
WantedBy=graphical.target
```

**File:** `packaging/straylight-desktop/debian/straylight-desktop.straylight-greeter.service`

- [ ] **Step 4: Create greeter service**

```ini
[Unit]
Description=StrayLight Login Greeter
Documentation=https://straylight.dev/docs/greeter
After=straylight-compositor.service
Requires=straylight-compositor.service
Before=straylight-shell.service
ConditionPathExists=!/var/lib/straylight/state.oobe

[Service]
Type=simple
ExecStart=/usr/bin/straylight-greeter
Restart=on-failure
RestartSec=1s
User=root
Environment=WAYLAND_DISPLAY=wayland-0
Environment=XDG_RUNTIME_DIR=/run/user/0

[Install]
WantedBy=graphical.target
```

**File:** `packaging/straylight-desktop/debian/straylight-desktop.straylight-firstboot.service`

- [ ] **Step 5: Create firstboot service**

```ini
[Unit]
Description=StrayLight First Boot Configuration
Documentation=https://straylight.dev/docs/firstboot
Before=graphical.target
After=local-fs.target systemd-tmpfiles-setup.service
ConditionFirstBoot=yes
ConditionPathExists=!/var/lib/straylight/state

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/lib/straylight/firstboot.sh
ExecStartPost=/bin/sh -c 'echo oobe > /var/lib/straylight/state'
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-desktop/debian/straylight-desktop.straylight-oobe.service`

- [ ] **Step 6: Create OOBE service**

```ini
[Unit]
Description=StrayLight Out-of-Box Experience
Documentation=https://straylight.dev/docs/oobe
After=straylight-compositor.service graphical.target
Requires=straylight-compositor.service
ConditionPathExists=/var/lib/straylight/state
ConditionFileNotEmpty=/var/lib/straylight/state

[Service]
Type=simple
ExecCondition=/bin/sh -c '[ "$(cat /var/lib/straylight/state)" = "oobe" ]'
ExecStart=/usr/bin/straylight-oobe
User=root
Environment=WAYLAND_DISPLAY=wayland-0
Environment=XDG_RUNTIME_DIR=/run/user/0
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=graphical.target
```

**File:** `packaging/straylight-desktop/debian/changelog`

- [ ] **Step 7: Create changelog**

```
straylight-desktop (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * Wayland compositor based on wlroots 0.18
  * ImGui desktop shell with layer-shell panels
  * Built-in apps: terminal, file manager, system monitor, settings
  * Wizard (personalization), OOBE (first-login setup), greeter (login)
  * Themes: default, cyberpunk, minimal
  * Calamares installer modules for hardware detection

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

**File:** `packaging/straylight-desktop/debian/copyright`

- [ ] **Step 8: Create copyright** (same format, Upstream-Name: straylight-desktop)

---

### Task 3: straylight-ml debian/control

**File:** `packaging/straylight-ml/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-ml
Section: science
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               libstraylight-common-dev (>= 1.0.0),
               libsdbus-c++-dev (>= 2.0)
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-ml
Architecture: amd64
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libstraylight-common1 (>= 1.0.0),
         straylight-core (>= 1.0.0)
Recommends: nvidia-cuda-toolkit | rocm-dev
Suggests: python3-torch, python3-jax
Description: StrayLight OS machine learning subsystem
 ML tools for StrayLight OS. Includes straylight-compiler (graph
 optimization and codegen), straylight-agent (event-driven task
 distribution), straylight-morph (quantization, pruning, distillation),
 straylight-snn (spiking neural network simulator), and straylight-rhem
 (heterogeneous resource management).
```

---

### Task 4: straylight-ml debian/rules + install + changelog

**File:** `packaging/straylight-ml/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DSTRAYLIGHT_BUILD_ML_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-compiler straylight-agent straylight-morph \
		straylight-snn straylight-rhem

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L ml"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp

override_dh_installsystemd:
	dh_installsystemd --name=straylight-agent
```

**File:** `packaging/straylight-ml/debian/install`

- [ ] **Step 2: Create install file**

```
usr/bin/straylight-compiler
usr/bin/straylight-agent
usr/bin/straylight-morph
usr/bin/straylight-snn
usr/bin/straylight-rhem
etc/straylight/compiler.conf
etc/straylight/agent.conf
```

**File:** `packaging/straylight-ml/debian/straylight-ml.straylight-agent.service`

- [ ] **Step 3: Create agent service**

```ini
[Unit]
Description=StrayLight Task Distribution Agent
Documentation=https://straylight.dev/docs/agent
After=straylight-scheduler.service straylight-registry.service
Requires=straylight-registry.service
Wants=straylight-scheduler.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-agent
Restart=on-failure
RestartSec=2s
User=root
LimitNOFILE=65536
LimitMEMLOCK=infinity
WatchdogSec=30s
ProtectSystem=strict
ReadWritePaths=/run/straylight /var/lib/straylight /dev/shm
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-ml/debian/changelog`

- [ ] **Step 4: Create changelog**

```
straylight-ml (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * straylight-compiler: graph optimization + CUDA/ROCm/CPU codegen
  * straylight-agent: event-driven task distribution daemon
  * straylight-morph: quantization, pruning, distillation, adaptation
  * straylight-snn: spiking neural network simulator
  * straylight-rhem: heterogeneous device management + migration

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

---

### Task 5: Verify chunk 2

- [ ] Validate control file syntax: `grep-dctrl -s Package -n '' packaging/straylight-desktop/debian/control`
- [ ] Validate control file syntax: `grep-dctrl -s Package -n '' packaging/straylight-ml/debian/control`
- [ ] `git add packaging/straylight-desktop/ packaging/straylight-ml/`
- [ ] `git commit -m "feat(packaging): add debian packaging for straylight-desktop and straylight-ml"`

---

## Chunk 3: Debian Packaging — straylight-network + straylight-exotic + straylight-kernel

### File Structure

```
packaging/straylight-network/debian/
├── control
├── rules
├── changelog
├── install
└── copyright

packaging/straylight-exotic/debian/
├── control
├── rules
├── changelog
├── install
└── copyright

packaging/straylight-kernel/debian/
├── control
├── rules
├── changelog
├── install
├── dkms
└── copyright
```

### Task 1: straylight-network debian/control

**File:** `packaging/straylight-network/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-network
Section: net
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               libstraylight-common-dev (>= 1.0.0),
               libbpf-dev (>= 1.3),
               libdpdk-dev (>= 23.11),
               libibverbs-dev,
               librdmacm-dev,
               libelf-dev,
               clang,
               llvm
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-network
Architecture: amd64
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libstraylight-common1 (>= 1.0.0),
         straylight-core (>= 1.0.0),
         libbpf1 (>= 1.3)
Recommends: dpdk (>= 23.11),
            rdma-core
Description: StrayLight OS high-performance networking
 Network subsystem for StrayLight OS. Includes straylight-xdp
 (eBPF/AF_XDP loader), straylight-dpdk (packet processing pipeline),
 and straylight-rdma-bus (RDMA zero-copy tensor transport).
```

---

### Task 2: straylight-network debian/rules + install

**File:** `packaging/straylight-network/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DSTRAYLIGHT_BUILD_NETWORK_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-xdp straylight-dpdk straylight-rdma-bus

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L network"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp
```

**File:** `packaging/straylight-network/debian/install`

- [ ] **Step 2: Create install file**

```
usr/bin/straylight-xdp
usr/bin/straylight-dpdk
usr/bin/straylight-rdma-bus
usr/lib/straylight/bpf/xdp_filter.bpf.o
usr/lib/straylight/bpf/xdp_redirect.bpf.o
usr/lib/straylight/bpf/xdp_stats.bpf.o
etc/straylight/xdp.conf
etc/straylight/dpdk.conf
```

**File:** `packaging/straylight-network/debian/changelog`

- [ ] **Step 3: Create changelog**

```
straylight-network (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * straylight-xdp: eBPF/AF_XDP loader and manager
  * straylight-dpdk: DPDK packet processing pipeline
  * straylight-rdma-bus: RDMA zero-copy tensor transport

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

---

### Task 3: straylight-exotic debian/control

**File:** `packaging/straylight-exotic/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-exotic
Section: science
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               cmake (>= 3.25),
               pkg-config,
               libstraylight-common-dev (>= 1.0.0),
               libeigen3-dev (>= 3.4),
               libpmem2-dev (>= 1.12),
               libsgx-enclave-common-dev,
               libfuse3-dev (>= 3.14)
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-exotic
Architecture: amd64
Depends: ${shlibs:Depends},
         ${misc:Depends},
         libstraylight-common1 (>= 1.0.0),
         straylight-core (>= 1.0.0),
         fuse3 (>= 3.14)
Recommends: libpmem2-1 (>= 1.12),
            libsgx-enclave-common
Description: StrayLight OS exotic computing subsystems
 Exotic hardware subsystems for StrayLight OS. Includes
 straylight-quantum (gate simulator), straylight-photonics (optical
 computing), straylight-pmem (persistent memory), straylight-enclave
 (SGX secure inference), and straylight-fuse (tensor filesystem).
```

---

### Task 4: straylight-exotic debian/rules + install

**File:** `packaging/straylight-exotic/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

export DEB_BUILD_MAINT_OPTIONS = hardening=+all

%:
	dh $@

override_dh_auto_configure:
	dh_auto_configure -- \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DSTRAYLIGHT_BUILD_EXOTIC_ONLY=ON \
		-DSTRAYLIGHT_VERSION=$(DEB_VERSION_UPSTREAM)

override_dh_auto_build:
	dh_auto_build -- -j$(shell nproc) \
		straylight-quantum straylight-photonics straylight-pmem \
		straylight-enclave straylight-fuse

override_dh_auto_test:
	dh_auto_test -- ARGS="--output-on-failure -L exotic"

override_dh_auto_install:
	dh_auto_install --destdir=debian/tmp

override_dh_installsystemd:
	dh_installsystemd --name=straylight-fuse
```

**File:** `packaging/straylight-exotic/debian/install`

- [ ] **Step 2: Create install file**

```
usr/bin/straylight-quantum
usr/bin/straylight-photonics
usr/bin/straylight-pmem
usr/bin/straylight-enclave
usr/bin/straylight-fuse
etc/straylight/quantum.conf
etc/straylight/fuse.conf
```

**File:** `packaging/straylight-exotic/debian/straylight-exotic.straylight-fuse.service`

- [ ] **Step 3: Create fuse service**

```ini
[Unit]
Description=StrayLight Tensor Filesystem (FUSE)
Documentation=https://straylight.dev/docs/fuse
After=straylight-bus.service
Requires=straylight-bus.service

[Service]
Type=notify
ExecStart=/usr/bin/straylight-fuse --mountpoint=/mnt/straylight-tensors
ExecStop=/bin/fusermount3 -u /mnt/straylight-tensors
Restart=on-failure
RestartSec=2s
User=root
AmbientCapabilities=CAP_SYS_ADMIN
LimitNOFILE=65536
WatchdogSec=30s
ProtectSystem=no
PrivateTmp=no

[Install]
WantedBy=multi-user.target
```

**File:** `packaging/straylight-exotic/debian/changelog`

- [ ] **Step 4: Create changelog**

```
straylight-exotic (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * straylight-quantum: state vector gate simulator with noise models
  * straylight-photonics: MZI mesh simulation + optical device I/O
  * straylight-pmem: persistent memory DAX allocator + checkpointing
  * straylight-enclave: SGX secure inference + sealed storage
  * straylight-fuse: FUSE3 tensor compression filesystem

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

---

### Task 5: straylight-kernel debian/control

**File:** `packaging/straylight-kernel/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-kernel
Section: kernel
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13),
               dkms
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-kernel
Architecture: amd64
Depends: ${misc:Depends},
         dkms,
         linux-headers-amd64 | linux-headers-generic
Description: StrayLight OS kernel modules (DKMS)
 Loadable kernel modules for StrayLight OS, built via DKMS.
 Includes straylight-vpu (GPU memory management), straylight-hypervisor
 (KVM extensions), straylight-scheduler (ML-aware sched_class),
 straylight-xdp (eBPF programs), straylight-entropy (hardware entropy),
 and straylight-enclave (SGX extensions).
```

---

### Task 6: straylight-kernel debian/rules + dkms

**File:** `packaging/straylight-kernel/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

%:
	dh $@ --with dkms

override_dh_auto_configure:
override_dh_auto_build:
override_dh_auto_test:
override_dh_auto_install:
	# DKMS source trees are installed directly
	install -d debian/straylight-kernel/usr/src
	for mod in vpu hypervisor scheduler xdp entropy enclave; do \
		cp -a kernel/$$mod debian/straylight-kernel/usr/src/straylight-$$mod-1.0.0; \
		install -m 644 kernel/dkms/straylight-$$mod-dkms/dkms.conf \
			debian/straylight-kernel/usr/src/straylight-$$mod-1.0.0/dkms.conf; \
	done

override_dh_dkms:
	dh_dkms -V 1.0.0
```

**File:** `packaging/straylight-kernel/debian/install`

- [ ] **Step 2: Create install file**

```
usr/src/straylight-vpu-1.0.0/
usr/src/straylight-hypervisor-1.0.0/
usr/src/straylight-scheduler-1.0.0/
usr/src/straylight-xdp-1.0.0/
usr/src/straylight-entropy-1.0.0/
usr/src/straylight-enclave-1.0.0/
```

**File:** `packaging/straylight-kernel/debian/straylight-kernel.dkms`

- [ ] **Step 3: Create DKMS config**

```
# Each module is registered as a separate DKMS package
# Handled by the per-module dkms.conf files installed to /usr/src/
```

**File:** `packaging/straylight-kernel/debian/postinst`

- [ ] **Step 4: Create postinst script**

```bash
#!/bin/bash
set -e

MODULES="vpu hypervisor scheduler xdp entropy enclave"
VERSION="1.0.0"

for mod in $MODULES; do
    DKMS_NAME="straylight-${mod}"
    if dkms status "${DKMS_NAME}/${VERSION}" 2>/dev/null | grep -q "installed"; then
        echo "${DKMS_NAME}/${VERSION} already installed, skipping"
        continue
    fi
    echo "Adding and building ${DKMS_NAME}/${VERSION}..."
    dkms add -m "${DKMS_NAME}" -v "${VERSION}" 2>/dev/null || true
    dkms build -m "${DKMS_NAME}" -v "${VERSION}"
    dkms install -m "${DKMS_NAME}" -v "${VERSION}"
done

#DEBHELPER#

exit 0
```

**File:** `packaging/straylight-kernel/debian/prerm`

- [ ] **Step 5: Create prerm script**

```bash
#!/bin/bash
set -e

MODULES="vpu hypervisor scheduler xdp entropy enclave"
VERSION="1.0.0"

for mod in $MODULES; do
    DKMS_NAME="straylight-${mod}"
    echo "Removing ${DKMS_NAME}/${VERSION}..."
    dkms remove -m "${DKMS_NAME}" -v "${VERSION}" --all 2>/dev/null || true
done

#DEBHELPER#

exit 0
```

**File:** `packaging/straylight-kernel/debian/changelog`

- [ ] **Step 6: Create changelog**

```
straylight-kernel (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * straylight-vpu.ko: GPU memory management via DMA-BUF + ioctl
  * straylight-hypervisor.ko: KVM extensions with VMCS + EPT
  * straylight-scheduler.ko: ML-aware sched_class with NUMA topology
  * straylight-xdp: eBPF programs for packet filtering/redirect
  * straylight-entropy.ko: hwrng with RDRAND + jitter harvesting
  * straylight-enclave.ko: SGX EPC management + sealed storage

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

---

### Task 7: Verify chunk 3

- [ ] Validate all three control files parse correctly
- [ ] `git add packaging/straylight-network/ packaging/straylight-exotic/ packaging/straylight-kernel/`
- [ ] `git commit -m "feat(packaging): add debian packaging for network, exotic, and kernel packages"`

---

## Chunk 4: Debian Packaging — straylight-os metapackage

### File Structure

```
packaging/straylight-os/debian/
├── control
├── rules
├── changelog
└── copyright
```

### Task 1: straylight-os debian/control

**File:** `packaging/straylight-os/debian/control`

- [ ] **Step 1: Create control file**

```
Source: straylight-os
Section: metapackages
Priority: optional
Maintainer: StrayLight OS Team <dev@straylight.dev>
Build-Depends: debhelper-compat (= 13)
Standards-Version: 4.6.2
Homepage: https://straylight.dev
Rules-Requires-Root: no

Package: straylight-os
Architecture: amd64
Depends: ${misc:Depends},
         straylight-common (>= 1.0.0),
         straylight-core (>= 1.0.0),
         straylight-desktop (>= 1.0.0),
         straylight-ml (>= 1.0.0),
         straylight-network (>= 1.0.0),
         straylight-exotic (>= 1.0.0),
         straylight-kernel (>= 1.0.0)
Recommends: linux-image-amd64,
            grub-efi-amd64,
            network-manager,
            pipewire,
            wireplumber,
            bluez,
            cups,
            avahi-daemon,
            systemd-timesyncd
Suggests: nvidia-driver,
          firmware-amd-graphics,
          firmware-iwlwifi,
          firmware-realtek
Description: StrayLight OS - complete operating system
 Meta-package that installs the complete StrayLight OS distribution.
 StrayLight is a Debian-based Linux distribution designed for machine
 learning workloads, featuring a custom Wayland compositor, ML-aware
 scheduling, zero-copy tensor IPC, and exotic computing subsystems
 (quantum simulation, photonics, persistent memory, SGX enclaves).
 .
 Installing this package pulls in all StrayLight components:
  - straylight-common: shared libraries
  - straylight-core: daemon backbone (bus, registry, scheduler, entropy)
  - straylight-desktop: compositor, shell, apps, greeter
  - straylight-ml: compiler, agent, morph, snn, rhem
  - straylight-network: xdp, dpdk, rdma-bus
  - straylight-exotic: quantum, photonics, pmem, enclave, fuse
  - straylight-kernel: DKMS kernel modules
```

---

### Task 2: straylight-os debian/rules + changelog

**File:** `packaging/straylight-os/debian/rules`

- [ ] **Step 1: Create rules**

```makefile
#!/usr/bin/make -f

# Metapackage — no build steps needed

%:
	dh $@

override_dh_auto_configure:
override_dh_auto_build:
override_dh_auto_test:
override_dh_auto_install:
```

**File:** `packaging/straylight-os/debian/changelog`

- [ ] **Step 2: Create changelog**

```
straylight-os (1.0.0-1) unstable; urgency=medium

  * Initial release.
  * Metapackage pulling in all StrayLight OS components.

 -- StrayLight OS Team <dev@straylight.dev>  Sun, 16 Mar 2026 00:00:00 +0000
```

**File:** `packaging/straylight-os/debian/copyright`

- [ ] **Step 3: Create copyright** (same format, Upstream-Name: straylight-os)

---

### Task 3: Verify chunk 4

- [ ] `grep-dctrl -s Package,Depends -n '' packaging/straylight-os/debian/control` → verify all 7 deps listed
- [ ] `git add packaging/straylight-os/`
- [ ] `git commit -m "feat(packaging): add straylight-os metapackage"`

---

## Chunk 5: Live ISO — live-build Configuration + build.sh

### File Structure

```
iso/live-build/
├── auto/
│   ├── config
│   ├── build
│   └── clean
├── config/
│   ├── package-lists/
│   │   └── straylight.list.chroot
│   ├── hooks/
│   │   └── live/
│   │       └── 0100-straylight.hook.chroot
│   ├── includes.chroot/
│   │   ├── etc/
│   │   │   ├── skel/
│   │   │   │   └── .config/
│   │   │   │       └── straylight/
│   │   │   │           └── shell.conf
│   │   │   ├── sysctl.d/
│   │   │   │   └── 99-straylight.conf
│   │   │   └── calamares/
│   │   │       ├── settings.conf
│   │   │       └── modules/
│   │   └── usr/
│   │       └── share/
│   │           └── applications/
│   │               └── straylight-installer.desktop
│   └── bootloaders/
│       └── grub/
│           └── grub.cfg
└── build.sh
```

### Task 1: auto/config

**File:** `iso/live-build/auto/config`

- [ ] **Step 1: Create auto/config**

```bash
#!/bin/bash
set -e

lb config noauto \
    --architectures amd64 \
    --distribution bookworm \
    --archive-areas "main contrib non-free non-free-firmware" \
    --binary-images iso-hybrid \
    --binary-filesystem ext4 \
    --debian-installer none \
    --bootloaders grub-efi \
    --bootappend-live "boot=live components quiet splash" \
    --iso-application "StrayLight OS" \
    --iso-publisher "StrayLight OS Team <dev@straylight.dev>" \
    --iso-volume "StrayLight OS 1.0.0" \
    --image-name "straylight-os-1.0.0-amd64" \
    --debootstrap-options "--variant=minbase" \
    --apt-recommends true \
    --security true \
    --updates true \
    --backports false \
    --cache true \
    --cache-packages true \
    --firmware-binary true \
    --firmware-chroot true \
    --memtest none \
    --win32-loader false \
    --checksums sha256 \
    "${@}"
```

---

### Task 2: auto/build + auto/clean

**File:** `iso/live-build/auto/build`

- [ ] **Step 1: Create auto/build**

```bash
#!/bin/bash
set -e

lb build noauto "${@}" 2>&1 | tee build.log
```

**File:** `iso/live-build/auto/clean`

- [ ] **Step 2: Create auto/clean**

```bash
#!/bin/bash
set -e

lb clean noauto "${@}"
rm -f build.log
```

---

### Task 3: Package list

**File:** `iso/live-build/config/package-lists/straylight.list.chroot`

- [ ] **Step 1: Create package list**

```
# StrayLight OS packages
straylight-os

# Base system
linux-image-amd64
grub-efi-amd64
systemd
systemd-sysv
dbus
policykit-1

# Live environment
live-boot
live-config
live-config-systemd
user-setup
sudo

# Installer
calamares
calamares-settings-debian

# Firmware
firmware-linux-free
firmware-linux-nonfree
firmware-iwlwifi
firmware-realtek
firmware-atheros
firmware-misc-nonfree

# Hardware support
dkms
linux-headers-amd64
pciutils
usbutils
lshw
smartmontools
nvme-cli

# Networking
network-manager
wpasupplicant
isc-dhcp-client
wireless-tools

# Desktop dependencies
xwayland
mesa-utils
libgl1-mesa-dri
libglx-mesa0
libegl-mesa0
pipewire
wireplumber
pipewire-pulse
pipewire-alsa

# Fonts
fonts-noto
fonts-noto-color-emoji
fonts-liberation2
fonts-dejavu-core

# Utilities
bash-completion
less
nano
wget
curl
ca-certificates
gnupg
htop
```

---

### Task 4: Chroot hook

**File:** `iso/live-build/config/hooks/live/0100-straylight.hook.chroot`

- [ ] **Step 1: Create live hook**

```bash
#!/bin/bash
# StrayLight OS live environment customization hook
set -e

echo "StrayLight OS: Configuring live environment..."

# Create straylight state directory
mkdir -p /var/lib/straylight
echo "live" > /var/lib/straylight/state

# Create live user autostart for Calamares
mkdir -p /etc/skel/.config/autostart
cat > /etc/skel/.config/autostart/straylight-installer.desktop <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Install StrayLight OS
Comment=Install StrayLight OS to disk
Exec=pkexec calamares
Icon=calamares
Terminal=false
Categories=System;
DESKTOP

# Create desktop shortcut for installer
mkdir -p /usr/share/applications
cat > /usr/share/applications/straylight-installer.desktop <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Install StrayLight OS
Comment=Install StrayLight OS to your hard drive
Exec=pkexec calamares
Icon=calamares
Terminal=false
Categories=System;
StartupNotify=true
DESKTOP

# Configure auto-login for live session
mkdir -p /etc/systemd/system/straylight-greeter.service.d
cat > /etc/systemd/system/straylight-greeter.service.d/live-autologin.conf <<'CONF'
[Service]
ExecStart=
ExecStart=/usr/bin/straylight-greeter --autologin user
CONF

# Set live session default theme
mkdir -p /etc/skel/.config/straylight
cat > /etc/skel/.config/straylight/shell.conf <<'JSON'
{
    "theme": "cyberpunk",
    "panels": {
        "top_bar": true,
        "left_dock": true,
        "bottom_dock": false
    }
}
JSON

# Enable StrayLight services for live boot
systemctl enable straylight-bus.service || true
systemctl enable straylight-entropy.service || true
systemctl enable straylight-registry.service || true
systemctl enable straylight-scheduler.service || true
systemctl enable straylight-core.service || true
systemctl enable straylight-agent.service || true
systemctl enable straylight-fuse.service || true
systemctl enable straylight-compositor.service || true
systemctl enable straylight-greeter.service || true

# Kernel parameters for live environment
cat > /etc/sysctl.d/99-straylight.conf <<'SYSCTL'
# StrayLight OS kernel tuning
vm.swappiness=10
vm.dirty_ratio=20
vm.dirty_background_ratio=5
net.core.rmem_max=16777216
net.core.wmem_max=16777216
net.core.rmem_default=1048576
net.core.wmem_default=1048576
net.core.optmem_max=65536
net.core.somaxconn=8192
net.core.netdev_max_backlog=16384
kernel.sched_autogroup_enabled=0
kernel.numa_balancing=1
SYSCTL

# Disable unnecessary services in live env
systemctl disable apt-daily.timer || true
systemctl disable apt-daily-upgrade.timer || true
systemctl disable e2scrub_all.timer || true

echo "StrayLight OS: Live environment configuration complete."
```

---

### Task 5: GRUB config + skel files

**File:** `iso/live-build/config/bootloaders/grub/grub.cfg`

- [ ] **Step 1: Create GRUB config**

```
set default=0
set timeout=5

insmod efi_gop
insmod efi_uga
insmod all_video
insmod gfxterm
insmod png

set gfxmode=auto
terminal_output gfxterm

set theme=/boot/grub/themes/straylight/theme.txt

menuentry "StrayLight OS (Live)" --class straylight {
    linux /live/vmlinuz boot=live components quiet splash
    initrd /live/initrd.img
}

menuentry "StrayLight OS (Live - Safe Graphics)" --class straylight {
    linux /live/vmlinuz boot=live components quiet splash nomodeset
    initrd /live/initrd.img
}

menuentry "StrayLight OS (Live - Debug)" --class straylight {
    linux /live/vmlinuz boot=live components debug systemd.log_level=debug
    initrd /live/initrd.img
}

menuentry "Memory Test (memtest86+)" --class memtest {
    linux16 /live/memtest
}
```

**File:** `iso/live-build/config/includes.chroot/etc/sysctl.d/99-straylight.conf`

- [ ] **Step 2: Create sysctl defaults**

```
# StrayLight OS kernel tuning
vm.swappiness=10
vm.dirty_ratio=20
vm.dirty_background_ratio=5
net.core.rmem_max=16777216
net.core.wmem_max=16777216
kernel.sched_autogroup_enabled=0
kernel.numa_balancing=1
```

---

### Task 6: build.sh

**File:** `iso/live-build/build.sh`

- [ ] **Step 1: Create build script**

```bash
#!/bin/bash
# StrayLight OS ISO build script
# Requires: live-build, debootstrap, root privileges
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
VERSION="${STRAYLIGHT_VERSION:-1.0.0}"
TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUTPUT_NAME="straylight-os-${VERSION}-amd64"

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --clean         Clean previous build artifacts"
    echo "  --config-only   Run lb config only (for debugging)"
    echo "  --no-cache      Disable package cache"
    echo "  --repo URL      Custom APT repository for StrayLight packages"
    echo "  --help          Show this help"
    exit 0
}

log() { echo "[$(date -u +%H:%M:%S)] $*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# Parse arguments
CLEAN=false
CONFIG_ONLY=false
USE_CACHE=true
CUSTOM_REPO=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)       CLEAN=true; shift ;;
        --config-only) CONFIG_ONLY=true; shift ;;
        --no-cache)    USE_CACHE=false; shift ;;
        --repo)        CUSTOM_REPO="$2"; shift 2 ;;
        --help)        usage ;;
        *)             die "Unknown option: $1" ;;
    esac
done

# Preflight checks
[[ $EUID -eq 0 ]] || die "Must run as root (use sudo)"
command -v lb >/dev/null || die "live-build not installed: apt install live-build"
command -v debootstrap >/dev/null || die "debootstrap not installed: apt install debootstrap"

# Setup build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if $CLEAN; then
    log "Cleaning previous build..."
    lb clean --purge 2>/dev/null || true
    rm -rf .build/ cache/ chroot/ binary/ binary.* "${OUTPUT_NAME}"*
    log "Clean complete."
    exit 0
fi

# Copy configuration
log "Setting up live-build configuration..."
mkdir -p auto config

cp "${SCRIPT_DIR}/auto/config" auto/
cp "${SCRIPT_DIR}/auto/build"  auto/
cp "${SCRIPT_DIR}/auto/clean"  auto/
chmod +x auto/*

cp -a "${SCRIPT_DIR}/config/"* config/ 2>/dev/null || true

# Add custom repository if specified
if [[ -n "${CUSTOM_REPO}" ]]; then
    log "Adding custom repository: ${CUSTOM_REPO}"
    mkdir -p config/archives
    echo "deb ${CUSTOM_REPO} bookworm main" > config/archives/straylight.list.chroot
    # If a GPG key is needed, add it:
    # cp /path/to/straylight-archive-keyring.gpg config/archives/straylight.key.chroot
fi

# Disable cache if requested
if ! $USE_CACHE; then
    export LB_CACHE=false
    export LB_CACHE_PACKAGES=false
fi

# Run lb config
log "Running lb config..."
lb config

if $CONFIG_ONLY; then
    log "Config-only mode: stopping here."
    exit 0
fi

# Build the ISO
log "Building StrayLight OS ISO... (this will take 15-45 minutes)"
lb build 2>&1 | tee "${SCRIPT_DIR}/build-${TIMESTAMP}.log"

# Move output
if [[ -f live-image-amd64.hybrid.iso ]]; then
    mv live-image-amd64.hybrid.iso "${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "ISO created: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "Size: $(du -h "${SCRIPT_DIR}/${OUTPUT_NAME}.iso" | cut -f1)"

    # Generate checksums
    cd "${SCRIPT_DIR}"
    sha256sum "${OUTPUT_NAME}.iso" > "${OUTPUT_NAME}.iso.sha256"
    log "Checksum: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso.sha256"
elif [[ -f "${OUTPUT_NAME}.hybrid.iso" ]]; then
    mv "${OUTPUT_NAME}.hybrid.iso" "${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
    log "ISO created: ${SCRIPT_DIR}/${OUTPUT_NAME}.iso"
else
    die "Build completed but no ISO file found. Check build log."
fi

log "Build complete!"
```

- [ ] **Step 2: `chmod +x iso/live-build/build.sh iso/live-build/auto/*`**

---

### Task 7: Verify chunk 5

- [ ] `bash -n iso/live-build/build.sh` → syntax check passes
- [ ] `bash -n iso/live-build/auto/config` → syntax check passes
- [ ] `bash -n iso/live-build/config/hooks/live/0100-straylight.hook.chroot` → syntax check passes
- [ ] `git add iso/live-build/`
- [ ] `git commit -m "feat(iso): add live-build configuration and build script"`

---

## Chunk 6: Calamares — settings + standard modules

### File Structure

```
iso/calamares/
├── settings.conf
└── modules/
    ├── welcome.conf
    ├── locale.conf
    ├── partition.conf
    ├── users.conf
    ├── packages.conf
    └── finished.conf
```

### Task 1: settings.conf

**File:** `iso/calamares/settings.conf`

- [ ] **Step 1: Create Calamares settings**

```yaml
# StrayLight OS Calamares Installer Settings
# SPDX-License-Identifier: GPL-3.0-or-later

modules-search: [ local, /usr/lib/calamares/modules ]

sequence:
  - show:
      - welcome
      - locale
      - partition
      - straylight-hwscan
      - straylight-drivers
      - users
  - exec:
      - partition
      - mount
      - unpackfs
      - machineid
      - fstab
      - locale
      - keyboard
      - localecfg
      - users
      - networkcfg
      - hwclock
      - packages
      - straylight-hwtest
      - initramfscfg
      - initramfs
      - grubcfg
      - bootloader
      - straylight-postinstall
  - show:
      - finished

branding: straylight

prompt-install: true

dont-chroot: false

oem-setup: false

disable-cancel: false
disable-cancel-during-exec: true

hide-back-and-next-during-exec: true

quit-at-end: false
```

---

### Task 2: welcome.conf

**File:** `iso/calamares/modules/welcome.conf`

- [ ] **Step 1: Create welcome config**

```yaml
# Welcome module configuration
# Shows system requirements and checks

showSupportUrl: true
showKnownIssuesUrl: true
showReleaseNotesUrl: true

requirements:
    requiredStorage: 20.0
    requiredRam: 4.0
    internetCheckUrl: https://straylight.dev/ping
    check:
        - storage
        - ram
        - power
        - internet
        - root
        - screen
    required:
        - storage
        - ram
        - root

geoip:
    style: "none"

languages:
    - en_US.UTF-8
    - de_DE.UTF-8
    - fr_FR.UTF-8
    - ja_JP.UTF-8
    - zh_CN.UTF-8
    - ko_KR.UTF-8
    - pt_BR.UTF-8
    - es_ES.UTF-8
    - ru_RU.UTF-8
```

---

### Task 3: locale.conf

**File:** `iso/calamares/modules/locale.conf`

- [ ] **Step 1: Create locale config**

```yaml
# Locale module configuration

region: "NorthAmerica"
zone: "New_York"

localeGenPath: /etc/locale.gen

geoip:
    style: "json"
    url: "https://geoip.straylight.dev/json"
    selector: ""

adjustLiveTimezone: true

useSystemTimezone: true
```

---

### Task 4: partition.conf

**File:** `iso/calamares/modules/partition.conf`

- [ ] **Step 1: Create partition config**

```yaml
# Partition module configuration

efiSystemPartition: /boot/efi
efiSystemPartitionSize: 512M
efiSystemPartitionName: EFI

userSwapChoices:
    - none
    - small
    - suspend
    - file

drawNestedPartitions: false
alwaysShowPartitionLabels: true

allowManualPartitioning: true

initialPartitioningChoice: erase
initialSwapChoice: small

defaultPartitionTableType: gpt

requiredPartitionTableType:
    - gpt

defaultFileSystemType: ext4

availableFileSystemTypes:
    - ext4
    - btrfs
    - xfs

enableLuksAutomatedPartitioning: true

allowZfsEncryption: false

luksGeneration: luks2

preCheckEncryption: good
```

---

### Task 5: users.conf

**File:** `iso/calamares/modules/users.conf`

- [ ] **Step 1: Create users config**

```yaml
# Users module configuration

defaultGroups:
    - name: users
    - name: sudo
    - name: cdrom
    - name: floppy
    - name: audio
    - name: dip
    - name: video
    - name: plugdev
    - name: netdev
    - name: bluetooth
    - name: lpadmin
    - name: scanner
    - name: straylight
      must_exist: false
      system: true

autologinGroup: autologin

doAutologin: false

sudoersGroup: sudo

setRootPassword: true
doReusePassword: true

passwordRequirements:
    minLength: 8
    maxLength: -1
    libpwquality:
        - minlen=8
        - minclass=2

allowWeakPasswords: false
allowWeakPasswordsDefault: false

userShell: /bin/bash

hostname:
    location: EtcFile
    writeHostsFile: true
    template: "straylight-${cpu}"
    forbidden_names:
        - localhost
        - localhost.localdomain
```

---

### Task 6: packages.conf

**File:** `iso/calamares/modules/packages.conf`

- [ ] **Step 1: Create packages config**

```yaml
# Packages module configuration
# Runs during exec phase to install/remove packages on the target

backend: apt

update_db: true
update_system: false

operations:
    - install:
        - straylight-os
        - linux-image-amd64
        - linux-headers-amd64
        - grub-efi-amd64
        - network-manager
        - pipewire
        - wireplumber
        - bluez
        - cups
        - avahi-daemon
        - bash-completion
        - sudo
        - locales
    - remove:
        - live-boot
        - live-config
        - live-config-systemd
        - user-setup
        - calamares
        - calamares-settings-debian
```

---

### Task 7: finished.conf

**File:** `iso/calamares/modules/finished.conf`

- [ ] **Step 1: Create finished config**

```yaml
# Finished module configuration

restartNowEnabled: true
restartNowChecked: true

restartNowCommand: "systemctl reboot"

notifyOnFinished: true

restartNowMode: user-unchecked
```

---

### Task 8: Verify chunk 6

- [ ] Validate YAML syntax: `python3 -c "import yaml; yaml.safe_load(open('iso/calamares/settings.conf'))"` for each file
- [ ] Verify `sequence` in settings.conf references all module names correctly
- [ ] `git add iso/calamares/settings.conf iso/calamares/modules/{welcome,locale,partition,users,packages,finished}.conf`
- [ ] `git commit -m "feat(calamares): add settings and standard module configurations"`

---

## Chunk 7: Calamares — Custom StrayLight Modules

### File Structure

```
iso/calamares/modules/
├── straylight-hwscan.conf
├── straylight-drivers.conf
├── straylight-hwtest.conf
└── straylight-postinstall.conf
```

### Task 1: straylight-hwscan.conf

**File:** `iso/calamares/modules/straylight-hwscan.conf`

- [ ] **Step 1: Create hardware scan module config**

```yaml
# StrayLight Hardware Scan Module
# Runs during the 'show' phase — detects hardware and presents results to user
# This is a viewmodule (has both UI and exec logic)

type: viewmodule
name: straylight-hwscan
interface: qtplugin

# Hardware categories to scan
scan:
    gpu:
        enabled: true
        detect:
            - vendor    # nvidia, amd, intel
            - model     # e.g., RTX 4090, RX 7900 XTX
            - vram_mb   # video memory in MB
            - driver    # current driver in use
        pci_classes:
            - "0300"    # VGA compatible controller
            - "0302"    # 3D controller
            - "0380"    # Display controller

    network:
        enabled: true
        detect:
            - vendor
            - model
            - driver
            - link_state
            - speed_mbps
        pci_classes:
            - "0200"    # Ethernet controller
            - "0280"    # Network controller (WiFi)

    storage:
        enabled: true
        detect:
            - model
            - size_gb
            - transport  # nvme, sata, usb
            - smart_ok
            - rotational
        paths:
            - /sys/class/block/sd*
            - /sys/class/block/nvme*

    sgx:
        enabled: true
        detect:
            - supported
            - epc_size_mb
        cpuid_leaf: 0x12

    pmem:
        enabled: true
        detect:
            - namespace
            - size_gb
            - mode       # fsdax, devdax, raw
        paths:
            - /sys/bus/nd/devices/nmem*

    entropy:
        enabled: true
        detect:
            - rdrand_supported
            - rdseed_supported
            - hwrng_available
        test_command: "dd if=/dev/hwrng bs=32 count=1 2>/dev/null | wc -c"

# Global scan options
timeout_seconds: 30
parallel: true

# Store results in Calamares global storage for later modules
storage_key: "straylight_hardware"
```

---

### Task 2: straylight-drivers.conf

**File:** `iso/calamares/modules/straylight-drivers.conf`

- [ ] **Step 1: Create driver installation module config**

```yaml
# StrayLight Driver Installation Module
# Runs during the 'show' phase — presents driver choices based on hwscan results
# Reads from global storage key "straylight_hardware"

type: viewmodule
name: straylight-drivers
interface: qtplugin

# Driver mapping: hardware vendor → packages to install
gpu_drivers:
    nvidia:
        packages:
            - nvidia-driver
            - nvidia-kernel-dkms
            - nvidia-cuda-toolkit
            - nvidia-smi
            - libnvidia-ml1
        blacklist:
            - nouveau
        kernel_params:
            - "nvidia-drm.modeset=1"
        post_install:
            - "update-initramfs -u"

    amd:
        packages:
            - firmware-amd-graphics
            - libdrm-amdgpu1
            - mesa-vulkan-drivers
            - radeontop
        kernel_params:
            - "amdgpu.ppfeaturemask=0xffffffff"

    intel:
        packages:
            - intel-media-va-driver
            - mesa-vulkan-drivers
            - intel-gpu-tools

# Network firmware mapping
network_firmware:
    intel_wifi:
        match_vendor: "8086"
        match_classes: ["0280"]
        packages:
            - firmware-iwlwifi

    realtek:
        match_vendor: "10ec"
        packages:
            - firmware-realtek

    broadcom:
        match_vendor: "14e4"
        packages:
            - firmware-brcm80211
            - broadcom-sta-dkms

    atheros:
        match_vendor: "168c"
        packages:
            - firmware-atheros

# SGX support
sgx_packages:
    - libsgx-enclave-common
    - libsgx-urts
    - sgx-aesm-service

# DKMS modules — always install
dkms_packages:
    - straylight-kernel

# Persistent memory
pmem_packages:
    - ndctl
    - daxctl
    - libpmem2-1

# Options
allow_skip: true
storage_key_in: "straylight_hardware"
storage_key_out: "straylight_drivers"
```

---

### Task 3: straylight-hwtest.conf

**File:** `iso/calamares/modules/straylight-hwtest.conf`

- [ ] **Step 1: Create hardware test module config**

```yaml
# StrayLight Hardware Test Module
# Runs during 'exec' phase — validates hardware after driver install
# Tests produce warnings (non-blocking) or errors (blocking)

type: jobmodule
name: straylight-hwtest
interface: process

# Test definitions
tests:
    gpu_render:
        description: "GPU rendering test"
        command: "/usr/lib/straylight/hwtest/gpu-render-test"
        timeout_seconds: 15
        severity: warning
        warning_message: "GPU acceleration unavailable. Software rendering will be used."
        success_message: "GPU rendering OK"
        depends_on_driver: true

    nic_link:
        description: "Network interface test"
        command: "/usr/lib/straylight/hwtest/nic-link-test"
        timeout_seconds: 10
        severity: warning
        warning_message: "No network detected. Configure later in Settings."
        success_message: "Network link detected"

    entropy_source:
        description: "Entropy source test"
        command: "/usr/lib/straylight/hwtest/entropy-test"
        timeout_seconds: 5
        severity: warning
        warning_message: "Hardware RNG unavailable. Using software fallback."
        success_message: "Hardware entropy source OK"

    disk_smart:
        description: "Disk health check"
        command: "/usr/lib/straylight/hwtest/disk-smart-test"
        timeout_seconds: 20
        severity: error
        error_message: "Disk health critical! Select a different disk."
        success_message: "Disk SMART status OK"
        blocking: true

    dkms_build:
        description: "DKMS module build test"
        command: "/usr/lib/straylight/hwtest/dkms-build-test"
        timeout_seconds: 120
        severity: warning
        warning_message: "Some kernel modules failed to build. Userspace fallbacks will be used."
        success_message: "All DKMS modules built successfully"

# Global options
parallel_tests: false
continue_on_warning: true
abort_on_error: true
storage_key_in: "straylight_drivers"
storage_key_out: "straylight_hwtest_results"
```

---

### Task 4: straylight-postinstall.conf

**File:** `iso/calamares/modules/straylight-postinstall.conf`

- [ ] **Step 1: Create post-install module config**

```yaml
# StrayLight Post-Install Module
# Runs during 'exec' phase — final system configuration after packages installed

type: jobmodule
name: straylight-postinstall
interface: process

# Steps executed in order within the target chroot
steps:
    - name: "Enable StrayLight services"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              systemctl enable straylight-entropy.service
              systemctl enable straylight-bus.service
              systemctl enable straylight-registry.service
              systemctl enable straylight-scheduler.service
              systemctl enable straylight-core.service
              systemctl enable straylight-agent.service
              systemctl enable straylight-fuse.service
              systemctl enable straylight-compositor.service
              systemctl enable straylight-greeter.service
              systemctl enable straylight-firstboot.service

    - name: "Configure DKMS modules"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              MODULES="vpu hypervisor scheduler xdp entropy enclave"
              for mod in $MODULES; do
                  dkms autoinstall -m "straylight-${mod}" 2>/dev/null || true
              done

    - name: "Install kernel parameters"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              install -m 644 /dev/stdin /etc/sysctl.d/99-straylight.conf <<'SYSCTL'
              vm.swappiness=10
              vm.dirty_ratio=20
              vm.dirty_background_ratio=5
              net.core.rmem_max=16777216
              net.core.wmem_max=16777216
              net.core.rmem_default=1048576
              net.core.wmem_default=1048576
              kernel.sched_autogroup_enabled=0
              kernel.numa_balancing=1
              SYSCTL

    - name: "Configure GPU blacklists"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              # Read driver choice from Calamares global storage
              if [ -f /tmp/straylight-gpu-driver ] && grep -q nvidia /tmp/straylight-gpu-driver; then
                  echo "blacklist nouveau" > /etc/modprobe.d/straylight-nvidia.conf
                  echo "options nvidia-drm modeset=1" >> /etc/modprobe.d/straylight-nvidia.conf
                  update-initramfs -u -k all
              fi

    - name: "Create straylight group"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              groupadd -r straylight 2>/dev/null || true
              # Add the installed user to straylight group
              if [ -n "$(ls /home/)" ]; then
                  for user_dir in /home/*/; do
                      user=$(basename "$user_dir")
                      usermod -aG straylight "$user" 2>/dev/null || true
                  done
              fi

    - name: "Set initial state for first boot"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              mkdir -p /var/lib/straylight
              rm -f /var/lib/straylight/state
              # firstboot.service uses ConditionFirstBoot=yes

    - name: "Install D-Bus policy files"
      chroot: true
      command: "/bin/bash"
      args:
          - "-c"
          - |
              # D-Bus configs should already be in place from package install
              # Verify they exist
              for conf in Core1 Bus1 Registry1 Scheduler1 Entropy1 Agent1 Compositor1 Shell1; do
                  if [ ! -f "/etc/dbus-1/system.d/org.straylight.${conf}.conf" ]; then
                      echo "WARNING: Missing D-Bus config for org.straylight.${conf}"
                  fi
              done

# Global options
timeout_seconds: 300
abort_on_error: false
```

---

### Task 5: Verify chunk 7

- [ ] Validate YAML syntax for all four custom module configs
- [ ] Verify module names in configs match the names in `settings.conf` sequence
- [ ] `git add iso/calamares/modules/{straylight-hwscan,straylight-drivers,straylight-hwtest,straylight-postinstall}.conf`
- [ ] `git commit -m "feat(calamares): add custom StrayLight hardware scan, driver, test, and postinstall modules"`

---

## Chunk 8: systemd Units + D-Bus Configs + udev Rules + CMakePresets.json

### File Structure

```
services/
├── dbus/
│   ├── org.straylight.Core1.conf
│   ├── org.straylight.Bus1.conf
│   ├── org.straylight.Registry1.conf
│   ├── org.straylight.Scheduler1.conf
│   ├── org.straylight.Entropy1.conf
│   ├── org.straylight.Agent1.conf
│   ├── org.straylight.Compositor1.conf
│   └── org.straylight.Shell1.conf
├── udev/
│   ├── 90-straylight-gpu.rules
│   ├── 90-straylight-sgx.rules
│   └── 90-straylight-pmem.rules
└── daemons/
    └── (systemd units already created in chunks 1-3)

CMakePresets.json
```

### Task 1: D-Bus policy — Core1

**File:** `services/dbus/org.straylight.Core1.conf`

- [ ] **Step 1: Create Core1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <!-- Only root can own the Core1 bus name -->
  <policy user="root">
    <allow own="org.straylight.Core1"/>
    <allow send_destination="org.straylight.Core1"/>
    <allow send_interface="org.straylight.Core1.Pipeline"/>
    <allow send_interface="org.straylight.Core1.Doctor"/>
    <allow send_interface="org.straylight.Core1.Inventory"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <!-- Members of straylight group can call Core1 methods -->
  <policy group="straylight">
    <allow send_destination="org.straylight.Core1"/>
    <allow send_interface="org.straylight.Core1.Pipeline"/>
    <allow send_interface="org.straylight.Core1.Doctor"/>
    <allow send_interface="org.straylight.Core1.Inventory"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <!-- Default deny for everyone else -->
  <policy context="default">
    <deny send_destination="org.straylight.Core1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

---

### Task 2: D-Bus policy — Bus1

**File:** `services/dbus/org.straylight.Bus1.conf`

- [ ] **Step 1: Create Bus1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Bus1"/>
    <allow send_destination="org.straylight.Bus1"/>
    <allow send_interface="org.straylight.Bus1.ServiceRegistry"/>
    <allow send_interface="org.straylight.Bus1.SignalRouter"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Bus1"/>
    <allow send_interface="org.straylight.Bus1.ServiceRegistry"/>
    <allow send_interface="org.straylight.Bus1.SignalRouter"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Bus1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

---

### Task 3: D-Bus policy — Registry1

**File:** `services/dbus/org.straylight.Registry1.conf`

- [ ] **Step 1: Create Registry1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Registry1"/>
    <allow send_destination="org.straylight.Registry1"/>
    <allow send_interface="org.straylight.Registry1.Store"/>
    <allow send_interface="org.straylight.Registry1.Watch"/>
    <allow send_interface="org.straylight.Registry1.Replication"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Registry1"/>
    <allow send_interface="org.straylight.Registry1.Store"/>
    <allow send_interface="org.straylight.Registry1.Watch"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Registry1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

---

### Task 4: D-Bus policy — Scheduler1, Entropy1, Agent1

**File:** `services/dbus/org.straylight.Scheduler1.conf`

- [ ] **Step 1: Create Scheduler1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Scheduler1"/>
    <allow send_destination="org.straylight.Scheduler1"/>
    <allow send_interface="org.straylight.Scheduler1.Topology"/>
    <allow send_interface="org.straylight.Scheduler1.Pinning"/>
    <allow send_interface="org.straylight.Scheduler1.Profiles"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Scheduler1"/>
    <allow send_interface="org.straylight.Scheduler1.Topology"/>
    <allow send_interface="org.straylight.Scheduler1.Profiles"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Scheduler1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

**File:** `services/dbus/org.straylight.Entropy1.conf`

- [ ] **Step 2: Create Entropy1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Entropy1"/>
    <allow send_destination="org.straylight.Entropy1"/>
    <allow send_interface="org.straylight.Entropy1.Pool"/>
    <allow send_interface="org.straylight.Entropy1.Sources"/>
    <allow send_interface="org.straylight.Entropy1.DRBG"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Entropy1"/>
    <allow send_interface="org.straylight.Entropy1.Pool"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Entropy1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

**File:** `services/dbus/org.straylight.Agent1.conf`

- [ ] **Step 3: Create Agent1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Agent1"/>
    <allow send_destination="org.straylight.Agent1"/>
    <allow send_interface="org.straylight.Agent1.TaskQueue"/>
    <allow send_interface="org.straylight.Agent1.WorkerPool"/>
    <allow send_interface="org.straylight.Agent1.Distribution"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Agent1"/>
    <allow send_interface="org.straylight.Agent1.TaskQueue"/>
    <allow send_interface="org.straylight.Agent1.WorkerPool"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Agent1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

---

### Task 5: D-Bus policy — Compositor1, Shell1

**File:** `services/dbus/org.straylight.Compositor1.conf`

- [ ] **Step 1: Create Compositor1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Compositor1"/>
    <allow send_destination="org.straylight.Compositor1"/>
    <allow send_interface="org.straylight.Compositor1.Output"/>
    <allow send_interface="org.straylight.Compositor1.Workspace"/>
    <allow send_interface="org.straylight.Compositor1.Input"/>
    <allow send_interface="org.straylight.Compositor1.LayerShell"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Compositor1"/>
    <allow send_interface="org.straylight.Compositor1.Output"/>
    <allow send_interface="org.straylight.Compositor1.Workspace"/>
    <allow send_interface="org.straylight.Compositor1.Input"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Compositor1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

**File:** `services/dbus/org.straylight.Shell1.conf`

- [ ] **Step 2: Create Shell1 D-Bus policy**

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <policy user="root">
    <allow own="org.straylight.Shell1"/>
    <allow send_destination="org.straylight.Shell1"/>
    <allow send_interface="org.straylight.Shell1.Panels"/>
    <allow send_interface="org.straylight.Shell1.Notifications"/>
    <allow send_interface="org.straylight.Shell1.Theme"/>
    <allow send_interface="org.straylight.Shell1.AppLauncher"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <policy group="straylight">
    <allow send_destination="org.straylight.Shell1"/>
    <allow send_interface="org.straylight.Shell1.Panels"/>
    <allow send_interface="org.straylight.Shell1.Notifications"/>
    <allow send_interface="org.straylight.Shell1.Theme"/>
    <allow send_interface="org.straylight.Shell1.AppLauncher"/>
    <allow send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>

  <!-- Allow any logged-in user to interact with the shell -->
  <policy at_console="true">
    <allow send_destination="org.straylight.Shell1"/>
    <allow send_interface="org.straylight.Shell1.Panels"/>
    <allow send_interface="org.straylight.Shell1.Notifications"/>
    <allow send_interface="org.straylight.Shell1.Theme"/>
    <allow send_interface="org.straylight.Shell1.AppLauncher"/>
  </policy>

  <policy context="default">
    <deny send_destination="org.straylight.Shell1"/>
    <allow send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

---

### Task 6: udev rules — GPU

**File:** `services/udev/90-straylight-gpu.rules`

- [ ] **Step 1: Create GPU udev rules**

```udev
# StrayLight OS GPU device rules
# Provides /dev/straylight-vpu and sets permissions for GPU access

# NVIDIA GPUs — set group and permissions
SUBSYSTEM=="drm", KERNEL=="card[0-9]*", DRIVERS=="nvidia", \
    GROUP="straylight", MODE="0660", \
    TAG+="straylight-gpu", ENV{STRAYLIGHT_GPU_VENDOR}="nvidia"

# NVIDIA render nodes
SUBSYSTEM=="drm", KERNEL=="renderD[0-9]*", DRIVERS=="nvidia", \
    GROUP="straylight", MODE="0666"

# AMD GPUs
SUBSYSTEM=="drm", KERNEL=="card[0-9]*", DRIVERS=="amdgpu", \
    GROUP="straylight", MODE="0660", \
    TAG+="straylight-gpu", ENV{STRAYLIGHT_GPU_VENDOR}="amd"

# AMD render nodes
SUBSYSTEM=="drm", KERNEL=="renderD[0-9]*", DRIVERS=="amdgpu", \
    GROUP="straylight", MODE="0666"

# Intel GPUs
SUBSYSTEM=="drm", KERNEL=="card[0-9]*", DRIVERS=="i915", \
    GROUP="straylight", MODE="0660", \
    TAG+="straylight-gpu", ENV{STRAYLIGHT_GPU_VENDOR}="intel"

# Intel render nodes
SUBSYSTEM=="drm", KERNEL=="renderD[0-9]*", DRIVERS=="i915", \
    GROUP="straylight", MODE="0666"

# StrayLight VPU kernel module device node
KERNEL=="straylight-vpu", SUBSYSTEM=="misc", \
    GROUP="straylight", MODE="0660", \
    SYMLINK+="straylight/vpu"

# NVIDIA UVM (Unified Virtual Memory)
KERNEL=="nvidia-uvm", \
    GROUP="straylight", MODE="0660"

# NVIDIA control device
KERNEL=="nvidiactl", \
    GROUP="straylight", MODE="0660"

# NVIDIA per-GPU devices
KERNEL=="nvidia[0-9]*", \
    GROUP="straylight", MODE="0660"

# Notify straylight-core on GPU hotplug
SUBSYSTEM=="drm", ACTION=="add", TAG=="straylight-gpu", \
    RUN+="/usr/bin/straylight-core --notify gpu-added %k"
SUBSYSTEM=="drm", ACTION=="remove", TAG=="straylight-gpu", \
    RUN+="/usr/bin/straylight-core --notify gpu-removed %k"
```

---

### Task 7: udev rules — SGX

**File:** `services/udev/90-straylight-sgx.rules`

- [ ] **Step 1: Create SGX udev rules**

```udev
# StrayLight OS SGX device rules
# Provides /dev/straylight-sgx and sets permissions for enclave access

# Intel SGX enclave device (in-kernel driver)
SUBSYSTEM=="sgx", KERNEL=="sgx_enclave", \
    GROUP="straylight", MODE="0660", \
    SYMLINK+="straylight/sgx-enclave"

# Intel SGX provision device
SUBSYSTEM=="sgx", KERNEL=="sgx_provision", \
    GROUP="sgx_prv", MODE="0660"

# Legacy SGX DCAP driver device nodes
SUBSYSTEM=="misc", KERNEL=="sgx_enclave", \
    GROUP="straylight", MODE="0660"

SUBSYSTEM=="misc", KERNEL=="sgx_provision", \
    GROUP="sgx_prv", MODE="0660"

# StrayLight enclave kernel module device node
KERNEL=="straylight-sgx", SUBSYSTEM=="misc", \
    GROUP="straylight", MODE="0660", \
    SYMLINK+="straylight/sgx"

# EPC memory sections (read-only info for monitoring)
SUBSYSTEM=="node", KERNEL=="memory[0-9]*", \
    ATTR{state}=="online", \
    TAG+="straylight-epc"

# Notify straylight-core when SGX becomes available
SUBSYSTEM=="sgx", ACTION=="add", \
    RUN+="/usr/bin/straylight-core --notify sgx-available"
```

---

### Task 8: udev rules — PMEM

**File:** `services/udev/90-straylight-pmem.rules`

- [ ] **Step 1: Create PMEM udev rules**

```udev
# StrayLight OS Persistent Memory device rules
# Sets permissions for DAX devices and PMEM namespaces

# PMEM block devices
SUBSYSTEM=="block", KERNEL=="pmem[0-9]*", \
    GROUP="straylight", MODE="0660", \
    TAG+="straylight-pmem", \
    ENV{STRAYLIGHT_PMEM_DEVICE}="1"

# PMEM sector-mode block devices
SUBSYSTEM=="block", KERNEL=="pmem[0-9]*s", \
    GROUP="straylight", MODE="0660", \
    TAG+="straylight-pmem"

# DAX devices (fsdax mode)
SUBSYSTEM=="dax", KERNEL=="dax[0-9]*.[0-9]*", \
    GROUP="straylight", MODE="0660", \
    SYMLINK+="straylight/pmem-%k", \
    TAG+="straylight-dax"

# devdax character devices
KERNEL=="dax[0-9]*.[0-9]*", SUBSYSTEM=="misc", \
    GROUP="straylight", MODE="0660"

# NVDIMM bus devices (for monitoring)
SUBSYSTEM=="nd", KERNEL=="nmem[0-9]*", \
    TAG+="straylight-nvdimm"

# NVDIMM region devices
SUBSYSTEM=="nd", KERNEL=="region[0-9]*", \
    TAG+="straylight-nvdimm-region"

# Notify straylight-core on PMEM namespace changes
SUBSYSTEM=="block", ACTION=="add", TAG=="straylight-pmem", \
    RUN+="/usr/bin/straylight-core --notify pmem-added %k"
SUBSYSTEM=="block", ACTION=="remove", TAG=="straylight-pmem", \
    RUN+="/usr/bin/straylight-core --notify pmem-removed %k"
SUBSYSTEM=="dax", ACTION=="add", TAG=="straylight-dax", \
    RUN+="/usr/bin/straylight-core --notify dax-added %k"
```

---

### Task 9: CMakePresets.json

**File:** `CMakePresets.json`

- [ ] **Step 1: Create CMakePresets.json**

```json
{
    "version": 6,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 25,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "dev",
            "displayName": "Development",
            "description": "Debug build with sanitizers and tests enabled",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/dev",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_STANDARD": "20",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
                "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined",
                "CMAKE_SHARED_LINKER_FLAGS": "-fsanitize=address,undefined",
                "STRAYLIGHT_BUILD_TESTS": "ON",
                "STRAYLIGHT_BUILD_BENCHMARKS": "OFF",
                "STRAYLIGHT_ENABLE_SANITIZERS": "ON",
                "BUILD_SHARED_LIBS": "ON"
            },
            "environment": {
                "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"
            }
        },
        {
            "name": "release",
            "displayName": "Release",
            "description": "Optimized release build with LTO, no tests",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_CXX_STANDARD": "20",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
                "CMAKE_CXX_FLAGS": "-march=x86-64-v3",
                "STRAYLIGHT_BUILD_TESTS": "OFF",
                "STRAYLIGHT_BUILD_BENCHMARKS": "OFF",
                "BUILD_SHARED_LIBS": "ON",
                "CMAKE_INSTALL_PREFIX": "/usr"
            }
        },
        {
            "name": "package",
            "displayName": "Package",
            "description": "RelWithDebInfo build for Debian packaging",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/package",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "RelWithDebInfo",
                "CMAKE_CXX_STANDARD": "20",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
                "CMAKE_INSTALL_PREFIX": "/usr",
                "CMAKE_INSTALL_LIBDIR": "lib/x86_64-linux-gnu",
                "STRAYLIGHT_BUILD_TESTS": "OFF",
                "STRAYLIGHT_BUILD_BENCHMARKS": "OFF",
                "BUILD_SHARED_LIBS": "ON",
                "CPACK_GENERATOR": "DEB",
                "CPACK_DEBIAN_PACKAGE_MAINTAINER": "StrayLight OS Team <dev@straylight.dev>",
                "CPACK_DEBIAN_PACKAGE_HOMEPAGE": "https://straylight.dev",
                "CPACK_DEBIAN_PACKAGE_SECTION": "admin"
            }
        },
        {
            "name": "test",
            "displayName": "Test + Coverage",
            "description": "Debug build with code coverage (gcov) and tests enabled",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/test",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_STANDARD": "20",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "CMAKE_CXX_FLAGS": "--coverage -fprofile-arcs -ftest-coverage",
                "CMAKE_EXE_LINKER_FLAGS": "--coverage",
                "CMAKE_SHARED_LINKER_FLAGS": "--coverage",
                "STRAYLIGHT_BUILD_TESTS": "ON",
                "STRAYLIGHT_BUILD_BENCHMARKS": "OFF",
                "STRAYLIGHT_ENABLE_COVERAGE": "ON",
                "BUILD_SHARED_LIBS": "ON"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "dev",
            "configurePreset": "dev",
            "jobs": 0
        },
        {
            "name": "release",
            "configurePreset": "release",
            "jobs": 0
        },
        {
            "name": "package",
            "configurePreset": "package",
            "jobs": 0
        },
        {
            "name": "test",
            "configurePreset": "test",
            "jobs": 0
        }
    ],
    "testPresets": [
        {
            "name": "dev",
            "configurePreset": "dev",
            "output": {
                "outputOnFailure": true,
                "verbosity": "default"
            },
            "execution": {
                "noTestsAction": "error",
                "stopOnFailure": false,
                "jobs": 0
            }
        },
        {
            "name": "test",
            "configurePreset": "test",
            "output": {
                "outputOnFailure": true,
                "verbosity": "default"
            },
            "execution": {
                "noTestsAction": "error",
                "stopOnFailure": false,
                "jobs": 0
            }
        }
    ]
}
```

---

### Task 10: Verify chunk 8

- [ ] Validate all D-Bus XML configs: `xmllint --noout services/dbus/*.conf`
- [ ] Validate CMakePresets.json: `python3 -c "import json; json.load(open('CMakePresets.json'))"`
- [ ] Review udev rules syntax (no automated validator, manual review)
- [ ] `git add services/dbus/ services/udev/ CMakePresets.json`
- [ ] `git commit -m "feat(services): add D-Bus policies, udev rules, and CMakePresets.json"`

---

## Summary

| Chunk | Scope | Files |
|-------|-------|-------|
| 1 | straylight-common + straylight-core debian packaging | ~15 files |
| 2 | straylight-desktop + straylight-ml debian packaging | ~12 files |
| 3 | straylight-network + straylight-exotic + straylight-kernel debian packaging | ~15 files |
| 4 | straylight-os metapackage | ~4 files |
| 5 | live-build configuration + build.sh | ~8 files |
| 6 | Calamares settings + standard modules | ~7 files |
| 7 | Calamares custom StrayLight modules | ~4 files |
| 8 | D-Bus policies + udev rules + CMakePresets.json | ~12 files |

**Daemon startup order (enforced by systemd):**

```
entropy → bus → registry → scheduler → core → agent
                                              → fuse
compositor → greeter → shell
firstboot (oneshot, ConditionFirstBoot=yes)
oobe (after compositor, ConditionPathExists state=oobe)
```

**Package dependency tree:**

```
straylight-os
├── straylight-common (libstraylight-{common,ml,net,hw}.so)
├── straylight-core (core, bus, registry, scheduler, entropy)
│   └── depends: straylight-common
├── straylight-desktop (compositor, shell, apps, greeter, themes)
│   └── depends: straylight-common, straylight-core
├── straylight-ml (compiler, agent, morph, snn, rhem)
│   └── depends: straylight-common, straylight-core
├── straylight-network (xdp, dpdk, rdma-bus)
│   └── depends: straylight-common, straylight-core
├── straylight-exotic (quantum, photonics, pmem, enclave, fuse)
│   └── depends: straylight-common, straylight-core
└── straylight-kernel (6 DKMS modules)
    └── depends: dkms, linux-headers-amd64
```
