# Plan 4: Shell, Greeter, OOBE, and Wizard

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the four UI-facing components of StrayLight OS: the desktop shell (ImGui layer-shell client), the login greeter (ext-session-lock-v1 + PAM), the first-boot OOBE wizard, and the post-login personalization wizard. Deliver the complete three-layer boot state machine.

**Architecture:** straylight-shell is a Wayland layer-shell client that renders via wl_egl_window + EGL (NOT GLFW). straylight-greeter uses ext-session-lock-v1 to lock the display and PAM to authenticate. straylight-oobe and straylight-wizard are fullscreen Wayland windows that track boot state in `/var/lib/straylight/state`. All four components link libstraylight-common and use spdlog + Result<T,E> throughout.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, wl_egl_window + EGL + OpenGL ES 3.0, wayland-client 1.22+, wayland-protocols 1.34+ (layer-shell, ext-session-lock-v1, xdg-shell), PAM, spdlog 1.13+, nlohmann/json 3.11+, GTest 1.14+

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

**Development environment:** Linux x86_64 required. Debian Bookworm or Trixie. macOS cannot build or test this code — it uses Linux-only Wayland APIs, EGL, PAM, and ext-session-lock-v1.

**Deferred to later plans:** PipeWire audio widgets (Plan 5), full NetworkManager settings panel (Plan 5), SGX-backed session tokens (Plan 7).

---

## Chunk 1: Shell Skeleton — ImGui + Wayland Layer-Shell (EGL)

**Goal:** Bring up a minimal straylight-shell process that connects to the Wayland compositor as a layer-shell client, creates an EGL surface via wl_egl_window, initialises an ImGui context, and renders a single frame.

### Step 1.1 — CMakeLists for shell/

- [ ] Create `shell/CMakeLists.txt`
  - Target: `straylight-shell` (executable)
  - Sources: `main.cpp`, `renderer.cpp`, `panels/top_bar.cpp`, `panels/app_launcher.cpp`, `panels/left_dock.cpp`, `panels/bottom_dock.cpp`, `widgets/notification.cpp`, `widgets/volume_osd.cpp`, `widgets/screenshot.cpp`, `themes/theme_engine.cpp`, `settings/display.cpp`, `settings/input.cpp`, `settings/appearance.cpp`, `settings/network.cpp`
  - Find packages: `Wayland`, `EGL`, `OpenGLES`, `PkgConfig` → `wayland-egl`, `wlr-layer-shell-unstable-v1` protocol, `xdg-output-unstable-v1`
  - Generate Wayland protocol bindings with `wayland-scanner` for `wlr-layer-shell-unstable-v1.xml`
  - Link: `straylight-common`, `wayland-client`, `wayland-egl`, `EGL`, `GLESv2`, `imgui`
  - Install: `${CMAKE_INSTALL_BINDIR}`, service file in `${CMAKE_INSTALL_SYSTEMDUSERUNITDIR}`

- [ ] Write test `tests/unit/shell/test_shell_cmake.cpp` — verifies target links correctly (compile-only smoke test via `try_compile`)

### Step 1.2 — Wayland + EGL connection

- [ ] Create `shell/renderer.h`
  ```cpp
  namespace straylight::shell {
  struct EGLContext;

  class Renderer {
  public:
      static Result<Renderer, Error> create(wl_display* display,
                                            wl_surface* surface,
                                            int width, int height);
      void begin_frame();
      void end_frame();   // eglSwapBuffers
      ~Renderer();
  private:
      struct Impl;
      std::unique_ptr<Impl> impl_;
  };
  } // namespace straylight::shell
  ```

- [ ] Implement `shell/renderer.cpp`
  - `eglGetDisplay(EGL_DEFAULT_DISPLAY)` → bind to Wayland display via `eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, ...)`
  - `eglInitialize`, `eglChooseConfig` (RGB888 + depth24 + stencil8)
  - `wl_egl_window_create(surface, width, height)` → `eglCreateWindowSurface`
  - `eglCreateContext(EGL_OPENGL_ES3_BIT)` → `eglMakeCurrent`
  - ImGui: `ImGui::CreateContext()`, `ImGui_ImplOpenGL3_Init("#version 300 es")`, custom Wayland input backend (keyboard/pointer via wl_keyboard + wl_pointer)
  - `begin_frame`: `ImGui_ImplOpenGL3_NewFrame()`, `ImGui::NewFrame()`
  - `end_frame`: `ImGui::Render()`, `ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData())`, `eglSwapBuffers`

- [ ] Write `tests/unit/shell/test_renderer.cpp`
  - Mock wl_display and wl_surface via Wayland test compositor (wlr-headless backend)
  - TEST: `Renderer::create` succeeds with headless compositor
  - TEST: `begin_frame` / `end_frame` does not crash on empty draw list
  - TEST: `Renderer::create` returns `Error::kWaylandConnect` when display is null

### Step 1.3 — Layer-shell surface setup

- [ ] Create `shell/layer_surface.h` / `shell/layer_surface.cpp`
  - Wrap `zwlr_layer_shell_v1` global and `zwlr_layer_surface_v1`
  - `LayerSurface::create(wl_display*, Layer layer, ZwlrLayerShellV1Anchor anchor, int width, int height)`
  - Layer enum: `kBackground`, `kBottom`, `kTop`, `kOverlay`
  - Set exclusive zone for taskbar (reserves screen edge, compositor won't overlap it)
  - Handle `configure` event: ack with `zwlr_layer_surface_v1_ack_configure`
  - Expose `wl_surface*` for EGL window creation

- [ ] Write `tests/unit/shell/test_layer_surface.cpp`
  - TEST: `LayerSurface::create` with `kTop` anchor-top succeeds
  - TEST: configure callback is called and acked within one roundtrip
  - TEST: exclusive zone reported correctly for 32px height taskbar

### Step 1.4 — Main event loop

- [ ] Implement `shell/main.cpp`
  - Connect to `WAYLAND_DISPLAY`, bind globals: `wl_compositor`, `wl_seat`, `zwlr_layer_shell_v1`, `wl_output`
  - Create `LayerSurface` for top bar (anchor top, full width, 32px height, `kTop` layer)
  - Create `Renderer` from layer surface's `wl_surface*`
  - Event loop: `wl_display_dispatch_pending` + `eglSwapInterval(0)` (vsync via compositor)
  - Signal handling: `SIGTERM` / `SIGINT` → clean shutdown

- [ ] Run `ctest -R shell_smoke` — must pass before proceeding

---

## Chunk 2: Shell UI — Taskbar, Clock, App Launcher

**Goal:** Render a functional taskbar with clock, system tray area, and a slide-out app launcher panel. Load theme from JSON.

### Step 2.1 — Theme engine

- [ ] Implement `shell/themes/theme_engine.h` / `theme_engine.cpp`
  - Load `config/themes/*.json` via nlohmann/json
  - Schema: `{ "name", "colors": { "bg", "fg", "accent", "panel" }, "font_size", "corner_radius", "icon_theme" }`
  - `ThemeEngine::apply(ImGuiStyle&)` — push colors, sizes into ImGui style
  - Live reload via inotify watch on config file
  - Ship `config/themes/default.json`, `cyberpunk.json`, `minimal.json`

- [ ] Write `tests/unit/shell/test_theme_engine.cpp`
  - TEST: loads `default.json` without error
  - TEST: `apply()` sets `ImGuiCol_WindowBg` to spec color
  - TEST: missing key falls back to default value (no crash)
  - TEST: malformed JSON returns `Error::kConfigParse`

### Step 2.2 — Top bar panel

- [ ] Implement `shell/panels/top_bar.h` / `top_bar.cpp`
  - `TopBar::render(ImDrawList*, int screen_width)` — full-width 32px bar
  - Left section: workspace switcher (D-Bus call to compositor IPC)
  - Center: window title of focused surface (via `org.straylight.Compositor1`)
  - Right section: `ClockWidget`, volume icon, network icon, notification count badge
  - `ClockWidget::render()` — `std::chrono::system_clock` → `HH:MM` string, updates every second

- [ ] Write `tests/unit/shell/test_top_bar.cpp`
  - TEST: `TopBar::render` produces at least one draw command
  - TEST: `ClockWidget` formats time as `HH:MM` correctly for midnight, noon, 23:59
  - TEST: workspace switcher sends D-Bus signal on click (mock D-Bus)

### Step 2.3 — App launcher panel

- [ ] Implement `shell/panels/app_launcher.h` / `app_launcher.cpp`
  - Triggered by clicking launcher button in top bar or pressing Super key
  - Slide-in animation: translate-X from -300 to 0 over 150ms (ImGui `ImVec2` lerp)
  - Reads `.desktop` files from `/usr/share/applications/` and `~/.local/share/applications/`
  - Search box (ImGui `InputText`) — filters by name and keywords
  - Launches app via `fork` + `exec` with `WAYLAND_DISPLAY` inherited
  - Categories: All, Development, Internet, Settings, Accessories

- [ ] Write `tests/unit/shell/test_app_launcher.cpp`
  - TEST: parses a `Name=` and `Exec=` entry from `.desktop` file correctly
  - TEST: search filter with empty string returns all entries
  - TEST: search filter "ter" matches "terminal" but not "settings"
  - TEST: `Exec=` with `%U` placeholder is stripped before exec

### Step 2.4 — Notification area widget

- [ ] Implement `shell/widgets/notification.h` / `notification.cpp`
  - Listens on `org.freedesktop.Notifications` D-Bus interface (org.freedesktop.Notifications spec)
  - Renders toast overlay (bottom-right, `kOverlay` layer-shell surface)
  - Each toast: icon + summary + body, auto-dismiss after `expire_timeout` ms
  - Queue: max 5 concurrent toasts, FIFO eviction

- [ ] Write `tests/unit/shell/test_notification.cpp`
  - TEST: `Notify` D-Bus call queues one notification
  - TEST: after `expire_timeout` ms, notification is removed from queue
  - TEST: 6th notification evicts oldest when queue is full
  - TEST: `CloseNotification` by ID removes correct entry

---

## Chunk 3: Greeter — PAM Auth + Session Lock

**Goal:** Implement straylight-greeter as a Wayland client using ext-session-lock-v1 to present a login screen and authenticate via PAM.

### Step 3.1 — CMakeLists for apps/greeter/

- [ ] Create `apps/greeter/CMakeLists.txt`
  - Target: `straylight-greeter` (executable)
  - Sources: `main.cpp`, `auth.cpp`, `session.cpp`, `ui.cpp`
  - Link: `straylight-common`, `wayland-client`, `EGL`, `GLESv2`, `imgui`, `pam`
  - Generate bindings: `ext-session-lock-v1.xml`
  - Install: `${CMAKE_INSTALL_BINDIR}`, `services/greeter/straylight-greeter.service`

### Step 3.2 — PAM authentication

- [ ] Create `apps/greeter/auth.h` / `auth.cpp`
  ```cpp
  namespace straylight::greeter {
  class PamAuth {
  public:
      explicit PamAuth(std::string_view service = "straylight-greeter");
      Result<void, Error> authenticate(std::string_view username,
                                       std::string_view password);
      ~PamAuth();
  private:
      pam_handle_t* pamh_ = nullptr;
  };
  } // namespace straylight::greeter
  ```
  - PAM service file: `/etc/pam.d/straylight-greeter` (include `common-auth`)
  - `pam_start`, supply username + password via `pam_conv` callback
  - `pam_authenticate` → `pam_acct_mgmt` → return `Result`
  - Log failures at warn level; never log passwords
  - Rate-limit: after 3 failures, insert 3s delay (incremental backoff)

- [ ] Write `tests/unit/greeter/test_pam_auth.cpp`
  - TEST: `authenticate` with PAM service `straylight-greeter-test` (test PAM config) succeeds for correct credentials
  - TEST: wrong password returns `Error::kAuthFailed`
  - TEST: 3 failures → 4th attempt is delayed (mock `sleep` via injected clock)
  - TEST: username is passed unmodified to PAM (no trimming)

### Step 3.3 — Session lock surface

- [ ] Create `apps/greeter/session.h` / `session.cpp`
  - `SessionLock::acquire(wl_display*)` → `ext_session_lock_v1_lock`
  - Creates one `ext_session_lock_surface_v1` per output
  - Handles `configure` event on each lock surface (ack required before rendering)
  - `SessionLock::unlock_and_destroy()` → `ext_session_lock_v1_unlock_and_destroy`
  - Exposes `wl_surface*` for EGL rendering
  - If compositor denies lock (another locker active), returns `Error::kLockDenied`

- [ ] Write `tests/unit/greeter/test_session_lock.cpp`
  - TEST: `acquire` succeeds on headless compositor with no active lock
  - TEST: configure event is acked within one roundtrip
  - TEST: `unlock_and_destroy` releases surfaces cleanly
  - TEST: double `acquire` returns `Error::kLockDenied`

### Step 3.4 — Greeter UI

- [ ] Implement `apps/greeter/ui.h` / `ui.cpp`
  - Full-screen ImGui window (no title bar, no resize) rendered to EGL lock surface
  - Layout: centered card (480x320), username field, password field (masked), login button
  - Keyboard navigation: Tab cycles fields, Enter submits
  - Error state: red banner below card, message from `Error::message()`
  - Idle animation: subtle background gradient pulse every 4s (ImGui `sin` on `ImGui::GetTime()`)
  - Session selector: drop-down to pick `straylight` or `tty` session

- [ ] Write `tests/unit/greeter/test_greeter_ui.cpp`
  - TEST: rendering empty state produces no error text
  - TEST: after `Error::kAuthFailed`, error banner text contains "incorrect"
  - TEST: password field value is masked (draw list contains no literal password chars)
  - TEST: Tab key moves focus from username to password

### Step 3.5 — Greeter main + session launch

- [ ] Implement `apps/greeter/main.cpp`
  - Connect Wayland, acquire session lock, create EGL renderer
  - Event loop: dispatch Wayland events, render ImGui frame
  - On successful PAM auth:
    1. `SessionLock::unlock_and_destroy()`
    2. `exec` user session: set `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`, `HOME`, `USER`, `SHELL`
    3. Launch `straylight-session` (which starts `straylight-shell` and autostart apps)
  - `services/greeter/straylight-greeter.service`: `After=straylight-compositor.service`, `Before=straylight-shell.service`, `Type=simple`

- [ ] Run `ctest -R greeter` — all greeter tests must pass

---

## Chunk 4: OOBE Flow — First-Boot Interactive Setup

**Goal:** Implement straylight-oobe as a fullscreen Wayland XDG-shell window that guides the user through first-boot setup and writes state transitions.

### Step 4.1 — CMakeLists for apps/oobe/

- [ ] Create `apps/oobe/CMakeLists.txt`
  - Target: `straylight-oobe` (executable)
  - Sources: `main.cpp`, `oobe_state.cpp`, `pages/welcome.cpp`, `pages/account_setup.cpp`, `pages/package_profile.cpp`, `pages/network_config.cpp`, `pages/summary.cpp`
  - Link: `straylight-common`, `wayland-client`, `EGL`, `GLESv2`, `imgui`, sdbus-c++ (NetworkManager D-Bus)
  - Generate: `xdg-shell.xml` bindings

### Step 4.2 — OOBE state machine

- [ ] Create `apps/oobe/oobe_state.h` / `oobe_state.cpp`
  ```cpp
  namespace straylight::oobe {
  enum class OobeStep { kWelcome, kAccount, kPackageProfile, kNetwork, kSummary, kDone };

  class OobeState {
  public:
      static Result<OobeState, Error> load(std::string_view path =
          "/var/lib/straylight/oobe_progress.json");
      void advance(OobeStep next);
      OobeStep current() const;
      void save() const;  // atomic write via temp file + rename
  private:
      std::string path_;
      OobeStep step_ = OobeStep::kWelcome;
  };
  } // namespace straylight::oobe
  ```
  - State persisted to `/var/lib/straylight/oobe_progress.json`
  - If file is absent → start from `kWelcome`
  - Atomic save: write to `.tmp`, `fsync`, `rename`

- [ ] Write `tests/unit/oobe/test_oobe_state.cpp`
  - TEST: new state starts at `kWelcome`
  - TEST: `advance` → `save` → reload restores correct step
  - TEST: corrupt JSON file falls back to `kWelcome` without crash
  - TEST: save is atomic (temp file exists during write, final file appears only after rename)

### Step 4.3 — Welcome page

- [ ] Implement `apps/oobe/pages/welcome.h` / `welcome.cpp`
  - `WelcomePage::render()` → `bool` (true = advance)
  - Full-screen ImGui layout: StrayLight logo (SVG path rendered via ImDrawList), headline "Welcome to StrayLight OS", subtext, "Get Started" button
  - Keyboard: Enter key activates "Get Started"
  - Accessibility: font size scaled by `theme.font_scale` (default 1.0)

### Step 4.4 — Account setup page

- [ ] Implement `apps/oobe/pages/account_setup.h` / `account_setup.cpp`
  - Shows existing admin user (read from `/etc/passwd` via `getpwnam`)
  - Allows changing full name (writes via `chfn`)
  - Optional: add additional user (username + password, calls `useradd` + `chpasswd` via `fork`/`exec`)
  - Validates username: lowercase alphanumeric + underscore, 3–32 chars, not already in `/etc/passwd`
  - Password strength indicator (entropy estimate: zxcvbn-style, implemented inline — no external dep)

- [ ] Write `tests/unit/oobe/test_account_setup.cpp`
  - TEST: valid username "alice" passes validation
  - TEST: username "root" is rejected (reserved)
  - TEST: username with space is rejected
  - TEST: username 33 chars is rejected
  - TEST: empty password reports `score = 0`

### Step 4.5 — Package profile page

- [ ] Implement `apps/oobe/pages/package_profile.h` / `package_profile.cpp`
  - Four profiles: ML Workstation, Developer, Server, Minimal
  - Each profile: icon + name + description + list of extra packages to install / packages to remove
  - Profile selection triggers `apt-get install -y <pkgs>` / `apt-get remove -y <pkgs>` via `fork`/`exec` with live progress (read stdout line-by-line, update `ImGui::ProgressBar`)
  - "Skip" option leaves current package set unchanged

- [ ] Write `tests/unit/oobe/test_package_profile.cpp`
  - TEST: ML Workstation profile includes `straylight-ml` in install list
  - TEST: Minimal profile includes `straylight-ml` in remove list
  - TEST: "Skip" produces empty install and remove lists
  - TEST: progress parser reads `Get:1 ... [1.2 MB]` lines without crashing

### Step 4.6 — Network config page

- [ ] Implement `apps/oobe/pages/network_config.h` / `network_config.cpp`
  - List available connections via NetworkManager D-Bus (`org.freedesktop.NetworkManager`)
  - Ethernet: show interface, link state, auto-configure DHCP option
  - WiFi: scan for SSIDs, show RSSI bars, WPA2/3 passphrase entry
  - "Skip" option for headless servers
  - Connection state feedback: connecting → connected / failed

- [ ] Write `tests/unit/oobe/test_network_config.cpp`
  - TEST: `NetworkManager` D-Bus unavailable → returns empty list (no crash)
  - TEST: SSID list is deduplicated (same SSID on 2.4GHz + 5GHz shows once)
  - TEST: RSSI -30 dBm → 4 bars; RSSI -90 dBm → 1 bar

### Step 4.7 — Summary page + state transition

- [ ] Implement `apps/oobe/pages/summary.h` / `summary.cpp`
  - Shows review of all selections (user, profile, network)
  - "Apply and Continue" button:
    1. Applies any pending changes
    2. Writes `/var/lib/straylight/state` = `wizard`
    3. Exits with code 0 (systemd marks service complete)
  - "Back" button returns to previous page

- [ ] Implement `apps/oobe/main.cpp`
  - Check `/var/lib/straylight/state` — if not `oobe`, exit immediately (idempotent)
  - Create XDG-shell fullscreen Wayland window + EGL renderer
  - Page router: instantiate pages, call `render()`, advance on `true` return
  - Resume from `OobeState::load()` (supports crash recovery)

- [ ] Run `ctest -R oobe` — all oobe tests must pass

---

## Chunk 5: Wizard — Post-Login Personalization

**Goal:** Implement straylight-wizard as a normal Wayland window (non-fullscreen, XDG-shell) that runs after first login (state = `wizard`) and can be re-launched from Settings.

### Step 5.1 — CMakeLists for apps/wizard/

- [ ] Create `apps/wizard/CMakeLists.txt`
  - Target: `straylight-wizard` (executable)
  - Sources: `main.cpp`, `firstboot.cpp`, `pages/welcome.cpp`, `pages/theme_picker.cpp`, `pages/layout_config.cpp`, `pages/ml_setup.cpp`, `pages/summary.cpp`
  - Link: `straylight-common`, `wayland-client`, `EGL`, `GLESv2`, `imgui`, sdbus-c++

### Step 5.2 — Theme picker page

- [ ] Implement `apps/wizard/pages/theme_picker.h` / `theme_picker.cpp`
  - Shows three theme cards side-by-side: Default, Cyberpunk, Minimal
  - Live preview: applies theme to ImGui style in the preview card (push/pop style)
  - Selected theme written to `~/.config/straylight/theme.json`
  - Preview renders a miniaturised top bar + window mock using the theme's palette

- [ ] Write `tests/unit/wizard/test_theme_picker.cpp`
  - TEST: selecting "cyberpunk" writes `theme: "cyberpunk"` to config file
  - TEST: config file is created in `XDG_CONFIG_HOME/straylight/` (respects env var)
  - TEST: render with all three themes does not crash (headless EGL)

### Step 5.3 — Layout config page

- [ ] Implement `apps/wizard/pages/layout_config.h` / `layout_config.cpp`
  - Options: top bar (always on), left dock (optional, width 64px), bottom dock (optional, height 48px)
  - Toggle switches (ImGui checkbox + custom draw)
  - Live preview: diagram of screen showing enabled panels highlighted
  - Writes layout config to `~/.config/straylight/shell.json`

- [ ] Write `tests/unit/wizard/test_layout_config.cpp`
  - TEST: disabling left dock writes `left_dock: false`
  - TEST: enabling bottom dock writes `bottom_dock: true`
  - TEST: default state has top bar enabled, left dock enabled, bottom dock disabled

### Step 5.4 — ML setup page

- [ ] Implement `apps/wizard/pages/ml_setup.h` / `ml_setup.cpp`
  - Detects installed ML frameworks: check for `python3 -c "import torch"` / `import jax` / `import tensorflow` (each as `fork`/`exec` with 2s timeout)
  - Detects GPU: read `/proc/driver/nvidia/version` (NVIDIA) or `lspci | grep -i amd` (AMD)
  - GPU scheduling profile selector: Performance, Balanced, Power-save (writes to `~/.config/straylight/gpu_profile.json`)
  - "Skip" option for CPU-only setups

- [ ] Write `tests/unit/wizard/test_ml_setup.cpp`
  - TEST: framework detection with mock `fork`/`exec` returning exit code 0 → framework marked present
  - TEST: framework detection with exit code 1 → framework marked absent
  - TEST: 2s timeout: if `exec` hangs, child is killed and framework marked absent
  - TEST: "skip" leaves GPU profile file unchanged

### Step 5.5 — Summary page + state transition

- [ ] Implement `apps/wizard/pages/summary.h` / `summary.cpp`
  - Shows final summary: theme, layout, ML frameworks, GPU profile
  - "Finish" button:
    1. Applies theme to running shell via D-Bus (`org.straylight.Shell1.ApplyTheme`)
    2. Applies layout to shell via D-Bus (`org.straylight.Shell1.ApplyLayout`)
    3. Writes `/var/lib/straylight/state` = `complete`
    4. Window closes
  - Can be re-run: if state is already `complete`, wizard launches but skips straight to "Welcome back" page (no mandatory steps)

- [ ] Implement `apps/wizard/firstboot.h` / `firstboot.cpp`
  - `is_firstboot()` → reads `/var/lib/straylight/state`, returns `true` if `wizard`
  - `mark_complete()` → atomic write `complete` to state file

- [ ] Implement `apps/wizard/main.cpp`
  - Read state file; if not `wizard` AND not `--force` flag, exit 0 silently
  - Create XDG-shell normal window (800x600, not fullscreen)
  - Page router with back/forward navigation
  - On `--force` flag: re-run in personalization mode (for Settings → Personalization)

- [ ] Write `tests/unit/wizard/test_firstboot.cpp`
  - TEST: `is_firstboot()` returns `true` when state file contains `wizard`
  - TEST: `is_firstboot()` returns `false` when state file contains `complete`
  - TEST: `is_firstboot()` returns `false` when state file is absent (fresh non-firstboot scenario)
  - TEST: `mark_complete()` overwrites existing state atomically

- [ ] Run `ctest -R wizard` — all wizard tests must pass

---

## Chunk 6: Boot State Machine

**Goal:** Implement the complete three-layer boot state machine: firstboot service (no UI), OOBE trigger, wizard trigger, and normal boot path. Wire up systemd units and the `/var/lib/straylight/state` file.

### Step 6.1 — State file spec

- [ ] Create `services/firstboot/state_spec.md` (inline in this plan — not committed as a separate doc)

  State file: `/var/lib/straylight/state`

  | Value | Meaning |
  |-------|---------|
  | `firstboot` | Set by installer post-install script; triggers firstboot service |
  | `oobe` | Set by firstboot service; triggers OOBE |
  | `wizard` | Set by OOBE on completion; triggers wizard on next login |
  | `complete` | Set by wizard on completion; normal boot forever after |

  File format: plain text, single line, no trailing newline. Written atomically (temp + rename).

### Step 6.2 — straylight-firstboot service

- [ ] Create `services/firstboot/straylight-firstboot.service`
  ```ini
  [Unit]
  Description=StrayLight First Boot Initialization
  ConditionPathExists=/var/lib/straylight/state
  After=local-fs.target systemd-machine-id-commit.service
  Before=graphical.target

  [Service]
  Type=oneshot
  RemainAfterExit=yes
  ExecStart=/usr/lib/straylight/straylight-firstboot
  StandardOutput=journal
  StandardError=journal

  [Install]
  WantedBy=graphical.target
  ```

- [ ] Create `services/firstboot/straylight-firstboot` shell script (installed to `/usr/lib/straylight/`)
  - Read `/var/lib/straylight/state`
  - If state is not `firstboot` → exit 0 (idempotent)
  - Steps:
    1. `systemd-machine-id-setup --commit` (if needed)
    2. Generate SSH host keys: `ssh-keygen -A`
    3. `dkms autoinstall` (rebuild all DKMS modules for running kernel)
    4. `sysctl --system` (apply `/etc/sysctl.d/99-straylight.conf`)
    5. Atomic write: `/var/lib/straylight/state` = `oobe`
  - Log each step to journal via `echo "straylight-firstboot: <step>" | systemd-cat -t straylight-firstboot`

- [ ] Write `tests/integration/test_firstboot_script.sh`
  - Creates temp `state` file with `firstboot`
  - Runs script with `STATE_FILE` env override (script reads `${STATE_FILE:-/var/lib/straylight/state}`)
  - Asserts: state file contains `oobe` after run
  - Asserts: script exits 0 on second run (idempotent)
  - Asserts: script exits 0 if state is `oobe` (no double-transition)

### Step 6.3 — OOBE systemd unit

- [ ] Create `services/firstboot/straylight-oobe.service`
  ```ini
  [Unit]
  Description=StrayLight Out-of-Box Experience
  ConditionFileNotEmpty=/var/lib/straylight/state
  After=straylight-compositor.service graphical.target
  Before=straylight-shell.service
  PartOf=graphical-session.target

  [Service]
  Type=simple
  ExecStartPre=/usr/lib/straylight/straylight-oobe-check
  ExecStart=/usr/bin/straylight-oobe
  Restart=on-failure
  RestartSec=3

  [Install]
  WantedBy=graphical-session.target
  ```

- [ ] Create `services/firstboot/straylight-oobe-check` shell script
  - Reads state file; exits 0 if `oobe`, exits 1 otherwise (prevents OOBE from running on normal boot)
  - `ExecStartPre` with exit 1 causes systemd to skip `ExecStart`

### Step 6.4 — Wizard autostart

- [ ] Create `services/firstboot/straylight-wizard.service`
  ```ini
  [Unit]
  Description=StrayLight Personalization Wizard
  After=straylight-shell.service
  PartOf=graphical-session.target

  [Service]
  Type=simple
  ExecStartPre=/usr/lib/straylight/straylight-wizard-check
  ExecStart=/usr/bin/straylight-wizard
  Restart=no

  [Install]
  WantedBy=graphical-session.target
  ```

- [ ] Create `services/firstboot/straylight-wizard-check` shell script
  - Reads state file; exits 0 if `wizard`, exits 1 otherwise

### Step 6.5 — D-Bus interface for shell (theme + layout apply)

- [ ] Create `services/dbus/org.straylight.Shell1.conf`
  - Policy: allow `straylight-wizard` and `straylight-settings` to call `ApplyTheme` and `ApplyLayout`

- [ ] Add D-Bus method stubs to `shell/settings/appearance.cpp`
  - `ApplyTheme(theme_name: string)` — calls `ThemeEngine::load(theme_name)` + `ThemeEngine::apply(ImGuiStyle&)`
  - `ApplyLayout(layout_json: string)` — updates dock visibility, re-creates layer surfaces as needed
  - Register on `org.straylight.Shell1` at path `/org/straylight/Shell1`

- [ ] Write `tests/integration/test_shell_dbus.cpp`
  - TEST: `ApplyTheme("cyberpunk")` results in `ImGuiCol_WindowBg` matching cyberpunk palette (checked via exported getter)
  - TEST: `ApplyTheme("nonexistent")` returns D-Bus error, theme unchanged
  - TEST: `ApplyLayout` with `left_dock: false` causes left dock layer surface to be destroyed

### Step 6.6 — Full boot sequence integration test

- [ ] Write `tests/integration/test_boot_sequence.cpp`
  - Sets up temp state dir, runs through all transitions with mocked service runners:
    1. State = `firstboot` → firstboot script → assert state = `oobe`
    2. State = `oobe` → OOBE check → assert OOBE would launch
    3. OOBE completes → assert state = `wizard`
    4. State = `wizard` → wizard check → assert wizard would launch
    5. Wizard completes → assert state = `complete`
    6. State = `complete` → OOBE check exits 1 (skip), wizard check exits 1 (skip)
  - TEST: all six assertions pass in sequence
  - TEST: re-running firstboot with state = `oobe` does not regress state to `firstboot`

- [ ] Write `tests/e2e/test_firstboot.sh`
  - Requires QEMU with a Debian Trixie base image + straylight packages installed
  - Boots with state = `firstboot`, verifies all services reach expected states
  - Annotated with `# QEMU required` — skipped in CI unless `STRAYLIGHT_E2E=1`

- [ ] Run `ctest -R boot` — all boot state machine tests must pass

---

## Final Verification

- [ ] `cmake --preset dev && cmake --build --preset dev` — zero errors, zero warnings (`-Werror`)
- [ ] `ctest --preset dev` — all tests pass (unit + integration; e2e skipped unless `STRAYLIGHT_E2E=1`)
- [ ] Manual smoke on wlr-headless: `WAYLAND_DISPLAY=wayland-test straylight-shell` renders top bar and exits cleanly on SIGTERM
- [ ] Manual OOBE run: `straylight-oobe` with state = `oobe` shows welcome page and writes `wizard` on summary confirm
- [ ] Manual wizard run: `straylight-wizard` with state = `wizard` shows theme picker, applies theme via D-Bus, writes `complete`
- [ ] Manual greeter run: `straylight-greeter` acquires session lock on headless compositor, PAM auth with test credentials succeeds, unlocks
- [ ] `ldd /usr/bin/straylight-shell | grep -c 'not found'` = 0 (all deps satisfied)
