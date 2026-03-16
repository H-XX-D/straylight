# Plan 12: System Utilities — Encryption, Backup, Media Library, Clipboard, Audio Mixer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Five system utilities as standalone ImGui Wayland clients under `apps/`.

**Architecture:** All inherit `AppBase` (Plan 9A). C++20, `Result<T,E>::ok/error`, ImGui UI, Wayland+EGL rendering.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, wl_egl_window+EGL+GLES3, wayland-client 1.22+, xdg-shell, libsodium 1.0.18+, PipeWire 1.0+/PulseAudio, sqlite3, nlohmann/json, spdlog

**Depends on:** Plan 1 (libstraylight-common), Plan 3 (compositor), Plan 9A (AppBase)

---

## File Structure

```
apps/encryption/      — main.cpp, crypto.h/.cpp, keyring.h/.cpp, ui.h/.cpp, CMakeLists.txt
apps/backup/          — main.cpp, engine.h/.cpp, scheduler.h/.cpp, ui.h/.cpp, CMakeLists.txt
apps/media_library/   — main.cpp, scanner.h/.cpp, metadata.h/.cpp, ui.h/.cpp, CMakeLists.txt
apps/clipboard/       — main.cpp, wl_clipboard.h/.cpp, history.h/.cpp, ui.h/.cpp, CMakeLists.txt
apps/audio_mixer/     — main.cpp, pipewire_backend.h/.cpp, pulse_backend.h/.cpp, ui.h/.cpp, CMakeLists.txt
```

---

## Chunk 1: Encryption Manager

### Task 1.1: crypto.h/.cpp — libsodium encrypt/decrypt

- [ ] `Crypto::init()` — `sodium_init()`
- [ ] `Crypto::derive_key(passphrase, salt?)` — Argon2id via `crypto_pwhash`, returns `DerivedKey{key[32], salt[16]}`
- [ ] `Crypto::encrypt_file(in, out, key, progress)` — write salt+header, stream 64KB chunks via `crypto_secretstream_xchacha20poly1305_push`, TAG_FINAL on last
- [ ] `Crypto::decrypt_file(in, out, passphrase, progress)` — read salt, derive key, `init_pull`, loop `pull` chunks, verify TAG_FINAL

```cpp
// apps/encryption/crypto.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <functional>
#include <array>
namespace straylight::encryption {
namespace fs = std::filesystem;

struct DerivedKey {
    std::array<uint8_t, 32> key;  // KEYBYTES
    std::array<uint8_t, 16> salt; // SALTBYTES
};

class Crypto {
public:
    static Result<void, SLError> init();
    static Result<DerivedKey, SLError> derive_key(std::string_view passphrase, const uint8_t* salt = nullptr);
    static Result<void, SLError> encrypt_file(const fs::path& in, const fs::path& out,
        const DerivedKey& key, std::function<void(uint64_t, uint64_t)> progress = {});
    static Result<void, SLError> decrypt_file(const fs::path& in, const fs::path& out,
        std::string_view passphrase, std::function<void(uint64_t, uint64_t)> progress = {});
};
} // namespace
```

### Task 1.2: keyring.h/.cpp — key storage

- [ ] `Keyring::load(master_pass)` — read `~/.config/straylight/keyring.json`, derive master key, unseal entries via `crypto_secretbox_open`
- [ ] `Keyring::save()` — seal entries with master key, write JSON atomically (tmp+rename)
- [ ] `Keyring::add(name, desc, pass)` — derive key, seal with master, append
- [ ] `Keyring::remove(name)` — erase by name, save
- [ ] `Keyring::unlock(name)` — find entry, unseal, return `DerivedKey`
- [ ] `KeyEntry` struct: name, description, encrypted_key bytes, salt, created timestamp

```cpp
// apps/encryption/keyring.h
#pragma once
#include "crypto.h"
#include <vector>
#include <chrono>
namespace straylight::encryption {
struct KeyEntry {
    std::string name, description;
    std::vector<uint8_t> encrypted_key, salt;
    std::chrono::system_clock::time_point created;
};
class Keyring {
public:
    Result<void, SLError> load(std::string_view master_pass);
    Result<void, SLError> save() const;
    Result<void, SLError> add(const std::string& name, const std::string& desc, std::string_view pass);
    Result<void, SLError> remove(const std::string& name);
    Result<DerivedKey, SLError> unlock(const std::string& name) const;
    const std::vector<KeyEntry>& entries() const { return entries_; }
    bool is_unlocked() const { return unlocked_; }
private:
    std::vector<KeyEntry> entries_;
    DerivedKey master_key_;
    bool unlocked_ = false;
};
} // namespace
```

### Task 1.3: ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] `EncryptionApp : AppBase` — tabs: Keys, Encrypt, Decrypt
- [ ] `render_unlock()` — masked passphrase InputText, Button → `keyring_.load()`
- [ ] `render_keys()` — table (Name|Desc|Created|[Remove]), add form (name+desc+pass)
- [ ] `render_encrypt()` — file path input, key dropdown, [Encrypt] → `std::async(encrypt_file)`
- [ ] `render_decrypt()` — file path input, passphrase, [Decrypt] → `std::async(decrypt_file)`
- [ ] ProgressBar for active_op_, status message display

```cpp
// apps/encryption/ui.h
#pragma once
#include "keyring.h"
#include <straylight/app_base.h>
#include <future>
namespace straylight::encryption {
class EncryptionApp : public AppBase {
public:
    const char* title() const override { return "Encryption Manager"; }
    Result<void, SLError> init() override;
    void update() override;
    void render() override;
    void shutdown() override {}
private:
    Keyring keyring_;
    char master_buf_[256]={}, pass_buf_[256]={}, name_buf_[128]={};
    std::string status_;
    float progress_ = 0.f;
    std::future<Result<void, SLError>> active_op_;
    enum class Tab { Keys, Encrypt, Decrypt } tab_ = Tab::Keys;
    void render_unlock(), render_keys(), render_encrypt(), render_decrypt();
};
} // namespace
// main.cpp: EncryptionApp app; return app.run(argc, argv);
```

```cmake
# apps/encryption/CMakeLists.txt
add_executable(straylight-encryption main.cpp crypto.cpp keyring.cpp ui.cpp)
target_link_libraries(straylight-encryption PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2 sodium nlohmann_json spdlog)
target_compile_features(straylight-encryption PRIVATE cxx_std_20)
install(TARGETS straylight-encryption RUNTIME DESTINATION bin)
```

---

## Chunk 2: Backup Utility

### Task 2.1: engine.h/.cpp — rsync wrapper

- [ ] `build_args(profile, link_dest)` — rsync argv: `-a --info=progress2 [-z] [--delete] [--link-dest=] [--exclude=]... src/ dest/YYYY-MM-DD_HHMMSS/`
- [ ] `exec_rsync(args, progress)` — fork, dup2 pipe, execvp, parse progress2 `N%` output, waitpid
- [ ] `run_backup(profile, progress)` — find latest snapshot for --link-dest, build_args, exec_rsync, write_manifest
- [ ] `history(profile)` — parse manifest.json; `restore()` — rsync snapshot→target

```cpp
// apps/backup/engine.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <vector>
#include <functional>
#include <chrono>
namespace straylight::backup {
namespace fs = std::filesystem;
struct BackupProfile {
    std::string name;
    fs::path source, destination;
    std::vector<std::string> excludes;
    bool compress = true, delete_removed = false;
};
struct BackupRun {
    std::chrono::system_clock::time_point timestamp;
    uint64_t files = 0, bytes = 0;
    bool success = false;
    std::string error_msg;
};
using ProgressFn = std::function<void(int pct, uint64_t bytes, const std::string& file)>;
class Engine {
public:
    Result<BackupRun, SLError> run_backup(const BackupProfile& p, ProgressFn prog = {});
    Result<std::vector<BackupRun>, SLError> history(const BackupProfile& p);
    Result<void, SLError> restore(const BackupProfile& p, const fs::path& to,
                                   std::chrono::system_clock::time_point snap);
private:
    std::vector<std::string> build_args(const BackupProfile& p, const fs::path& link);
    Result<BackupRun, SLError> exec_rsync(const std::vector<std::string>& args, ProgressFn prog);
    void write_manifest(const BackupProfile& p, const BackupRun& r);
};
} // namespace
```

### Task 2.2: scheduler.h/.cpp — timed backups

- [ ] `Schedule` struct: profile_name, interval (Hourly/Daily/Weekly/Custom), custom_seconds, hour, weekday, enabled, last_run
- [ ] `Scheduler::load()/save()` — `~/.config/straylight/backup-schedules.json`
- [ ] `Scheduler::start(engine)` — spawn thread, sleep 60s, check `is_due()` for each schedule, fire `engine.run_backup`
- [ ] `Scheduler::stop()` — set `running_=false`, join thread
- [ ] `Scheduler::overdue()` — return profile names where `is_due()` and machine was off

```cpp
// apps/backup/scheduler.h
#pragma once
#include "engine.h"
#include <thread>
#include <atomic>
#include <mutex>
namespace straylight::backup {
enum class Interval { Hourly, Daily, Weekly, Custom };
struct Schedule {
    std::string profile_name;
    Interval interval = Interval::Daily;
    std::chrono::seconds custom_interval{0};
    int hour = 2, weekday = 0;
    bool enabled = true;
    std::chrono::system_clock::time_point last_run;
};
class Scheduler {
public:
    Result<void, SLError> load();
    Result<void, SLError> save() const;
    void add(Schedule s);
    void remove(const std::string& name);
    void start(Engine& engine);
    void stop();
    const std::vector<Schedule>& schedules() const { return scheds_; }
    std::vector<std::string> overdue() const;
private:
    std::vector<Schedule> scheds_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mtx_;
    bool is_due(const Schedule& s) const;
};
} // namespace
```

### Task 2.3: ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] `BackupApp : AppBase` — two columns: profile list | detail tabs
- [ ] Profile panel: Selectable list, Add/Remove buttons, edit fields (source, dest, excludes)
- [ ] Schedule tab: Interval combo, hour/weekday inputs, enable toggle
- [ ] History tab: table of BackupRuns (timestamp|files|bytes|status) with [Restore] button
- [ ] "Backup Now" button → `std::async(engine_.run_backup)` with ProgressBar

```cpp
// apps/backup/ui.h
#pragma once
#include "engine.h"
#include "scheduler.h"
#include <straylight/app_base.h>
#include <future>
namespace straylight::backup {
class BackupApp : public AppBase {
public:
    const char* title() const override { return "Backup"; }
    Result<void, SLError> init() override;
    void update() override;
    void render() override;
    void shutdown() override;
private:
    Engine engine_;
    Scheduler scheduler_;
    std::vector<BackupProfile> profiles_;
    int sel_ = -1;
    float progress_ = 0.f;
    std::future<Result<BackupRun, SLError>> active_;
    void render_profiles(), render_schedule(), render_history();
};
} // namespace
// main.cpp: BackupApp app; return app.run(argc, argv);
```

```cmake
# apps/backup/CMakeLists.txt
add_executable(straylight-backup main.cpp engine.cpp scheduler.cpp ui.cpp)
target_link_libraries(straylight-backup PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2 nlohmann_json spdlog pthread)
target_compile_features(straylight-backup PRIVATE cxx_std_20)
install(TARGETS straylight-backup RUNTIME DESTINATION bin)
```

---

## Chunk 3: Media Library Organizer

### Task 3.1: scanner.h/.cpp — folder scan + SQLite catalog

- [ ] `classify(path)` — ext map: mp3/flac/ogg/wav/opus/aac→Audio, mp4/mkv/avi/webm→Video, png/jpg/bmp/webp/gif→Image
- [ ] `open_db(path)` — sqlite3_open, CREATE TABLE media(id PK, path UNIQUE, type, size, mtime, title, artist, album, duration, width, height)
- [ ] `start_scan(progress)` — thread: recursive_directory_iterator, classify, INSERT OR REPLACE
- [ ] `query(filter, search)` — SELECT with optional WHERE type= and LIKE on title/artist/path

```cpp
// apps/media_library/scanner.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <filesystem>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
namespace straylight::media {
namespace fs = std::filesystem;
enum class MediaType { Audio, Video, Image, Unknown };
struct MediaFile {
    int64_t id = 0; fs::path path; MediaType type; uint64_t size;
    std::string title, artist, album;
    int duration_secs = 0, width = 0, height = 0;
};
class Scanner {
public:
    void add_root(const fs::path& r);
    void remove_root(const fs::path& r);
    void start_scan(std::function<void(uint32_t, const std::string&)> prog = {});
    void cancel_scan();
    bool is_scanning() const { return scanning_.load(); }
    Result<std::vector<MediaFile>, SLError> query(MediaType filter = MediaType::Unknown, const std::string& search = "");
    Result<void, SLError> open_db(const fs::path& db);
    static MediaType classify(const fs::path& p);
private:
    std::vector<fs::path> roots_;
    std::atomic<bool> scanning_{false};
    std::thread thread_;
    void* db_ = nullptr; // sqlite3*
};
} // namespace
```

### Task 3.2: metadata.h/.cpp — tag extraction

- [ ] `Metadata::extract(path)` — dispatch to extract_audio/image/video by classify()
- [ ] `extract_image()` — PNG: read IHDR at offset 16 (width[4]+height[4] big-endian); JPEG: scan for SOF0 (0xFFC0), read height+width at +5
- [ ] `extract_audio()` — ID3v2: read 10B header, iterate frames (TIT2→title, TPE1→artist, TALB→album); OGG: find vorbis comment block, parse key=value
- [ ] `extract_video()` — libavformat `avformat_open_input` if linked, else size-only fallback

```cpp
// apps/media_library/metadata.h
#pragma once
#include "scanner.h"
namespace straylight::media {
class Metadata {
public:
    static Result<MediaFile, SLError> extract(const std::filesystem::path& p);
private:
    static Result<MediaFile, SLError> extract_audio(const std::filesystem::path& p);
    static Result<MediaFile, SLError> extract_image(const std::filesystem::path& p);
    static Result<MediaFile, SLError> extract_video(const std::filesystem::path& p);
};
} // namespace
```

### Task 3.3: ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] `MediaLibraryApp : AppBase` — toolbar + content area + optional detail pane
- [ ] Toolbar: type filter buttons (All/Audio/Video/Images), search InputText, Grid/List toggle, Scan/Cancel button
- [ ] Grid view: ImGui columns with media name + type indicator
- [ ] List view: ImGui table (Name|Type|Artist|Duration|Size)
- [ ] Detail pane: `Metadata::extract()` for selected file, show all fields
- [ ] `refresh()` — `results_ = scanner_.query(filter_, search_)`

```cpp
// apps/media_library/ui.h
#pragma once
#include "scanner.h"
#include "metadata.h"
#include <straylight/app_base.h>
namespace straylight::media {
class MediaLibraryApp : public AppBase {
public:
    const char* title() const override { return "Media Library"; }
    Result<void, SLError> init() override;
    void update() override;
    void render() override;
    void shutdown() override {}
private:
    Scanner scanner_;
    std::vector<MediaFile> results_;
    MediaType filter_ = MediaType::Unknown;
    char search_[256] = {};
    int sel_ = -1;
    bool grid_ = true;
    void render_toolbar(), render_grid(), render_list(), render_detail(), refresh();
};
} // namespace
// main.cpp: MediaLibraryApp app; return app.run(argc, argv);
```

```cmake
# apps/media_library/CMakeLists.txt
add_executable(straylight-media-library main.cpp scanner.cpp metadata.cpp ui.cpp)
target_link_libraries(straylight-media-library PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2 sqlite3 nlohmann_json spdlog)
target_compile_features(straylight-media-library PRIVATE cxx_std_20)
install(TARGETS straylight-media-library RUNTIME DESTINATION bin)
```

---

## Chunk 4: Clipboard Manager

### Task 4.1: wl_clipboard.h/.cpp — Wayland data device

- [ ] `WlClipboard::init(display, seat)` — bind `wl_data_device_manager`, `get_data_device`, add listener
- [ ] `on_selection` — `wl_data_offer_receive` preferred MIME, read via pipe, invoke callback with `ClipboardEntry`
- [ ] `read_offer(offer, mime)` — pipe + wl_data_offer_receive + roundtrip + read loop
- [ ] `set_clipboard(mime, data)` — create `wl_data_source`, send listener, `set_selection`

```cpp
// apps/clipboard/wl_clipboard.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <wayland-client.h>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
namespace straylight::clipboard {
struct ClipboardEntry {
    uint64_t id;
    std::string mime_type;
    std::vector<uint8_t> data;
    std::chrono::system_clock::time_point timestamp;
    std::string preview;
};
class WlClipboard {
public:
    Result<void, SLError> init(wl_display* dpy, wl_seat* seat);
    void on_new_content(std::function<void(ClipboardEntry)> cb) { cb_ = cb; }
    Result<void, SLError> set_clipboard(const std::string& mime, const std::vector<uint8_t>& data);
    void dispatch();
private:
    wl_display* dpy_ = nullptr;
    wl_data_device_manager* ddm_ = nullptr;
    wl_data_device* dev_ = nullptr;
    std::function<void(ClipboardEntry)> cb_;
    static void on_selection(void*, wl_data_device*, wl_data_offer*);
    Result<std::vector<uint8_t>, SLError> read_offer(wl_data_offer* o, const std::string& mime);
};
} // namespace
```

### Task 4.2: history.h/.cpp — clipboard ring buffer

- [ ] `History(max=500)` — deque + pinned set + auto-incrementing IDs
- [ ] `push(entry)` — deduplicate vs front, assign ID, push_front, evict oldest unpinned if over max
- [ ] `search(query)` — linear scan, `preview.find(query)`
- [ ] `pin/unpin(id)` — insert/erase from pinned_ set
- [ ] `load()/save()` — `~/.config/straylight/clipboard-history.json`, base64 for binary data

```cpp
// apps/clipboard/history.h
#pragma once
#include "wl_clipboard.h"
#include <deque>
#include <set>
namespace straylight::clipboard {
class History {
public:
    explicit History(size_t max = 500) : max_(max) {}
    void push(ClipboardEntry e);
    void remove(uint64_t id);
    void pin(uint64_t id);
    void unpin(uint64_t id);
    void clear();
    std::vector<const ClipboardEntry*> search(const std::string& q) const;
    const std::deque<ClipboardEntry>& entries() const { return entries_; }
    bool is_pinned(uint64_t id) const { return pinned_.count(id); }
    Result<void, SLError> load();
    Result<void, SLError> save() const;
private:
    std::deque<ClipboardEntry> entries_;
    std::set<uint64_t> pinned_;
    size_t max_;
    uint64_t next_id_ = 1;
};
} // namespace
```

### Task 4.3: ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] `ClipboardApp : AppBase` — search bar + scrollable history
- [ ] `init()` — `history_.load()`, wire `wl_.on_new_content → history_.push`
- [ ] Each entry: pin indicator, Selectable preview (click → re-copy via `wl_.set_clipboard`)
- [ ] Right-click context: Pin/Unpin, Delete
- [ ] Settings popup: max history slider, Clear All
- [ ] `shutdown()` — `history_.save()`

```cpp
// apps/clipboard/ui.h
#pragma once
#include "wl_clipboard.h"
#include "history.h"
#include <straylight/app_base.h>
namespace straylight::clipboard {
class ClipboardApp : public AppBase {
public:
    const char* title() const override { return "Clipboard Manager"; }
    Result<void, SLError> init() override;
    void update() override;
    void render() override;
    void shutdown() override;
private:
    WlClipboard wl_; History hist_; char search_[256]={};
};
} // namespace
// main.cpp: ClipboardApp app; return app.run(argc, argv);
```

```cmake
# apps/clipboard/CMakeLists.txt
add_executable(straylight-clipboard main.cpp wl_clipboard.cpp history.cpp ui.cpp)
target_link_libraries(straylight-clipboard PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2 nlohmann_json spdlog)
target_compile_features(straylight-clipboard PRIVATE cxx_std_20)
install(TARGETS straylight-clipboard RUNTIME DESTINATION bin)
```

---

## Chunk 5: Audio Mixer (PipeWire/PulseAudio)

### Task 5.1: pipewire_backend.h/.cpp

- [ ] `PipewireBackend::init()` — `pw_init`, main_loop, context, connect, get_registry, add listener, roundtrip
- [ ] `on_global()` — classify by `media.class` → Sink/Source/Stream vectors
- [ ] `set_volume(id, vol)` — SPA pod `SPA_PROP_channelVolumes` → `pw_node_set_param`
- [ ] `set_mute/set_default` — `SPA_PROP_mute`, `pw_metadata` default.audio.sink/source
- [ ] `poll()` — `pw_loop_iterate(loop, 0)`

```cpp
// apps/audio_mixer/pipewire_backend.h
#pragma once
#include <straylight/result.h>
#include <straylight/error.h>
#include <string>
#include <vector>
namespace straylight::audio {
enum class NodeType { Sink, Source, Stream };
struct AudioNode {
    uint32_t id; NodeType type;
    std::string name, description;
    float volume = 1.f; bool muted = false;
    uint32_t channels = 2; bool is_default = false;
};
class PipewireBackend {
public:
    Result<void, SLError> init();
    void shutdown();
    const std::vector<AudioNode>& sinks() const { return sinks_; }
    const std::vector<AudioNode>& sources() const { return sources_; }
    const std::vector<AudioNode>& streams() const { return streams_; }
    Result<void, SLError> set_volume(uint32_t id, float v);
    Result<void, SLError> set_mute(uint32_t id, bool m);
    Result<void, SLError> set_default(uint32_t id, NodeType t);
    void poll();
private:
    struct pw_main_loop* loop_ = nullptr;
    struct pw_context* ctx_ = nullptr;
    struct pw_core* core_ = nullptr;
    struct pw_registry* reg_ = nullptr;
    std::vector<AudioNode> sinks_, sources_, streams_;
    static void on_global(void*, uint32_t, uint32_t, const char*, uint32_t, const struct spa_dict*);
    static void on_global_remove(void*, uint32_t);
};
} // namespace
```

### Task 5.2: pulse_backend.h/.cpp — PulseAudio fallback

- [ ] Same AudioNode interface; `pa_mainloop_new`, `pa_context_new/connect`, wait `PA_CONTEXT_READY`
- [ ] `refresh()` — `get_sink_info_list`→sinks, `get_source_info_list`→sources, `get_sink_input_info_list`→streams; `pa_sw_volume_to_linear` conversion
- [ ] `set_volume` — `pa_cvolume_set` + `pa_context_set_sink_volume_by_index`; `set_mute` — `set_sink_mute_by_index`; `set_default` — `set_default_sink/source`

```cpp
// apps/audio_mixer/pulse_backend.h
#pragma once
#include "pipewire_backend.h" // reuse AudioNode, NodeType
namespace straylight::audio {
class PulseBackend {
public:
    Result<void, SLError> init();
    void shutdown();
    const std::vector<AudioNode>& sinks() const { return sinks_; }
    const std::vector<AudioNode>& sources() const { return sources_; }
    const std::vector<AudioNode>& streams() const { return streams_; }
    Result<void, SLError> set_volume(uint32_t id, float v);
    Result<void, SLError> set_mute(uint32_t id, bool m);
    Result<void, SLError> set_default(uint32_t id, NodeType t);
    void poll();
private:
    struct pa_context* ctx_ = nullptr;
    struct pa_mainloop* loop_ = nullptr;
    std::vector<AudioNode> sinks_, sources_, streams_;
};
} // namespace
```

### Task 5.3: ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] `AudioMixerApp : AppBase` — `std::variant<PipewireBackend,PulseBackend>` + `std::visit`
- [ ] `init()` — try PipeWire, fallback PulseAudio; Tabs: Output/Input/Apps
- [ ] `render_row` — default indicator, name, Mute checkbox, SliderFloat 0-150% + dB label

```cpp
// apps/audio_mixer/ui.h
#pragma once
#include "pipewire_backend.h"
#include "pulse_backend.h"
#include <straylight/app_base.h>
#include <variant>
namespace straylight::audio {
class AudioMixerApp : public AppBase {
public:
    const char* title() const override { return "Audio Mixer"; }
    Result<void, SLError> init() override;
    void update() override;
    void render() override;
    void shutdown() override;
private:
    std::variant<PipewireBackend, PulseBackend> be_;
    bool pw_ = true;
    // std::visit helpers for get_sinks/sources/streams, set_vol/mute/default
    void render_nodes(const std::vector<AudioNode>& nodes, NodeType t);
    void render_row(const AudioNode& n, NodeType t);
};
} // namespace
// main.cpp: AudioMixerApp app; return app.run(argc, argv);
```

```cmake
# apps/audio_mixer/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(libpulse REQUIRED IMPORTED_TARGET libpulse)
add_executable(straylight-audio-mixer main.cpp pipewire_backend.cpp pulse_backend.cpp ui.cpp)
target_link_libraries(straylight-audio-mixer PRIVATE
    straylight-common imgui wayland-client wayland-egl EGL GLESv2
    PkgConfig::pipewire PkgConfig::libpulse spdlog)
target_compile_features(straylight-audio-mixer PRIVATE cxx_std_20)
install(TARGETS straylight-audio-mixer RUNTIME DESTINATION bin)
```

---

## Summary

| Chunk | Utility | Key Files | Tasks |
|-------|---------|-----------|-------|
| 1 | Encryption Manager | `crypto.h/.cpp`, `keyring.h/.cpp`, `ui.h/.cpp` | 1.1-1.3 |
| 2 | Backup Utility | `engine.h/.cpp`, `scheduler.h/.cpp`, `ui.h/.cpp` | 2.1-2.3 |
| 3 | Media Library | `scanner.h/.cpp`, `metadata.h/.cpp`, `ui.h/.cpp` | 3.1-3.3 |
| 4 | Clipboard Manager | `wl_clipboard.h/.cpp`, `history.h/.cpp`, `ui.h/.cpp` | 4.1-4.3 |
| 5 | Audio Mixer | `pipewire_backend.h/.cpp`, `pulse_backend.h/.cpp`, `ui.h/.cpp` | 5.1-5.3 |
