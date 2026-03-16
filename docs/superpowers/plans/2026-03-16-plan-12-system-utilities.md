# Plan 12: System Utilities — Encryption, Backup, Media Library, Clipboard, Audio Mixer

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Five system utilities as standalone ImGui Wayland clients under `apps/`.

**Architecture:** All inherit `AppBase` (Plan 9A). C++20, `Result<T,E>::ok/error`, ImGui UI, Wayland+EGL rendering.

**Tech Stack:** C++20, CMake 3.25+, ImGui 1.90+, wl_egl_window+EGL+GLES3, wayland-client 1.22+, xdg-shell, libsodium 1.0.18+, PipeWire 1.0+/PulseAudio, sqlite3, nlohmann/json, spdlog

**Spec:** `docs/superpowers/specs/2026-03-16-straylight-os-rewrite-design.md`

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

### Task 1.1: apps/encryption/crypto.h/.cpp

- [ ] libsodium init, XChaCha20-Poly1305 streaming encrypt/decrypt
- [ ] Argon2id key derivation via `crypto_pwhash`
- [ ] File encrypt: read 64KB chunks, `crypto_secretstream_push`, write salt+header+ciphertext
- [ ] File decrypt: read salt, derive key, `crypto_secretstream_pull` chunks

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
    std::array<uint8_t, 32> key;   // crypto_secretstream_xchacha20poly1305_KEYBYTES
    std::array<uint8_t, 16> salt;  // crypto_pwhash_SALTBYTES
};

class Crypto {
public:
    static Result<void, SLError> init();
    static Result<DerivedKey, SLError> derive_key(std::string_view passphrase,
                                                    const uint8_t* salt = nullptr);
    static Result<void, SLError> encrypt_file(const fs::path& in, const fs::path& out,
        const DerivedKey& key, std::function<void(uint64_t, uint64_t)> progress = {});
    static Result<void, SLError> decrypt_file(const fs::path& in, const fs::path& out,
        std::string_view passphrase, std::function<void(uint64_t, uint64_t)> progress = {});
};

} // namespace straylight::encryption
```

```cpp
// apps/encryption/crypto.cpp
#include "crypto.h"
#include <sodium.h>
#include <fstream>
namespace straylight::encryption {
static constexpr size_t CHUNK = 65536;

Result<void, SLError> Crypto::init() {
    if (sodium_init() < 0) return SLError("sodium init failed");
    return {};
}

Result<DerivedKey, SLError> Crypto::derive_key(std::string_view pass, const uint8_t* salt) {
    DerivedKey dk;
    if (salt) std::memcpy(dk.salt.data(), salt, 16);
    else randombytes_buf(dk.salt.data(), 16);
    if (crypto_pwhash(dk.key.data(), 32, pass.data(), pass.size(), dk.salt.data(),
                      crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0)
        return SLError("key derivation OOM");
    return dk;
}

Result<void, SLError> Crypto::encrypt_file(const fs::path& in, const fs::path& out,
    const DerivedKey& key, std::function<void(uint64_t,uint64_t)> progress)
{
    std::ifstream fi(in, std::ios::binary); if (!fi) return SLError("open: " + in.string());
    std::ofstream fo(out, std::ios::binary); if (!fo) return SLError("create: " + out.string());
    fo.write((const char*)key.salt.data(), 16);
    crypto_secretstream_xchacha20poly1305_state st;
    uint8_t hdr[24]; // HEADERBYTES
    crypto_secretstream_xchacha20poly1305_init_push(&st, hdr, key.key.data());
    fo.write((char*)hdr, 24);
    uint64_t total = fs::file_size(in), done = 0;
    std::vector<uint8_t> buf(CHUNK), ct(CHUNK + 17); // +ABYTES
    while (fi) {
        fi.read((char*)buf.data(), CHUNK);
        auto n = (size_t)fi.gcount(); if (!n) break;
        uint8_t tag = fi.peek() == EOF ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : 0;
        unsigned long long clen;
        crypto_secretstream_xchacha20poly1305_push(&st, ct.data(), &clen, buf.data(), n, nullptr, 0, tag);
        fo.write((char*)ct.data(), clen);
        done += n; if (progress) progress(done, total);
    }
    return {};
}
// decrypt_file: read salt(16)+header(24), derive_key(pass,salt), init_pull, loop pull, verify TAG_FINAL
} // namespace
```

### Task 1.2: apps/encryption/keyring.h/.cpp

- [ ] Named key storage in `~/.config/straylight/keyring.json`
- [ ] Entries: name, salt, description, created timestamp
- [ ] Master passphrase unlocks keyring (sealed with `crypto_secretbox`)
- [ ] CRUD: load, save, add, remove, unlock by name

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
// .cpp: load/save JSON with crypto_secretbox seal/unseal per entry
} // namespace
```

### Task 1.3: apps/encryption/ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] AppBase subclass: tabs for Keys, Encrypt, Decrypt
- [ ] Unlock dialog with masked passphrase input
- [ ] Async encrypt/decrypt with progress bar
- [ ] Key list table with add/remove

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
```

```cpp
// apps/encryption/main.cpp
#include "ui.h"
int main(int argc, char* argv[]) {
    straylight::encryption::EncryptionApp app;
    return app.run(argc, argv);
}
```

```cpp
// apps/encryption/ui.cpp — render dispatch
void EncryptionApp::render() {
    if (!keyring_.is_unlocked()) { render_unlock(); return; }
    ImGui::BeginTabBar("T");
    if (ImGui::BeginTabItem("Keys"))    { render_keys();    ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Encrypt")) { render_encrypt(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Decrypt")) { render_decrypt(); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
    if (active_op_.valid()) {
        ImGui::ProgressBar(progress_);
        if (active_op_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            auto r = active_op_.get();
            status_ = r ? "Done" : r.error().message();
        }
    }
    if (!status_.empty()) ImGui::TextColored({1,1,0,1}, "%s", status_.c_str());
}
// render_unlock(): InputText(password), Button("Unlock") → keyring_.load()
// render_keys(): table of entries, add form (name+desc+pass), remove button
// render_encrypt(): file path input, key dropdown, [Encrypt] → async encrypt_file
// render_decrypt(): file path input, passphrase, [Decrypt] → async decrypt_file
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

### Task 2.1: apps/backup/engine.h/.cpp

- [ ] Rsync wrapper: build args, fork/exec, parse `--info=progress2` output
- [ ] Incremental via `--link-dest` pointing to previous snapshot
- [ ] Snapshot dirs: `destination/YYYY-MM-DD_HHMMSS/`
- [ ] Manifest: append each BackupRun to `manifest.json`

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
                                   std::chrono::system_clock::time_point snapshot);
private:
    std::vector<std::string> build_args(const BackupProfile& p, const fs::path& link);
    Result<BackupRun, SLError> exec_rsync(const std::vector<std::string>& args, ProgressFn prog);
    void write_manifest(const BackupProfile& p, const BackupRun& r);
};
} // namespace
```

```cpp
// apps/backup/engine.cpp — key implementation
#include "engine.h"
#include <sys/wait.h>
#include <unistd.h>
namespace straylight::backup {

std::vector<std::string> Engine::build_args(const BackupProfile& p, const fs::path& link) {
    std::vector<std::string> a = {"rsync", "-a", "--info=progress2"};
    if (p.compress) a.push_back("-z");
    if (p.delete_removed) a.push_back("--delete");
    if (!link.empty()) a.push_back("--link-dest=" + link.string());
    for (auto& ex : p.excludes) a.push_back("--exclude=" + ex);
    // snapshot dir = destination / timestamp
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char ts[32]; std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H%M%S", std::localtime(&t));
    a.push_back(p.source.string() + "/");
    a.push_back((p.destination / ts).string() + "/");
    return a;
}

Result<BackupRun, SLError> Engine::exec_rsync(const std::vector<std::string>& args, ProgressFn prog) {
    int pfd[2]; if (pipe(pfd) < 0) return SLError::from_errno("pipe");
    pid_t pid = fork();
    if (pid < 0) return SLError::from_errno("fork");
    if (pid == 0) {
        close(pfd[0]); dup2(pfd[1], 1); dup2(pfd[1], 2); close(pfd[1]);
        std::vector<const char*> av;
        for (auto& a : args) av.push_back(a.c_str()); av.push_back(nullptr);
        execvp(av[0], const_cast<char**>(av.data())); _exit(127);
    }
    close(pfd[1]);
    BackupRun run; run.timestamp = std::chrono::system_clock::now();
    // Read pfd[0], parse "N% bytes" lines, invoke prog callback
    char buf[4096]; while (read(pfd[0], buf, sizeof(buf)) > 0) { /* parse progress */ }
    int st; waitpid(pid, &st, 0); close(pfd[0]);
    run.success = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    return run;
}

Result<BackupRun, SLError> Engine::run_backup(const BackupProfile& p, ProgressFn prog) {
    fs::path link; // find latest snapshot dir for --link-dest
    auto hist = history(p);
    if (hist && !hist->empty()) { /* link = latest snapshot path */ }
    auto args = build_args(p, link);
    auto run = exec_rsync(args, prog);
    if (run) write_manifest(p, *run);
    return run;
}
// history(): parse manifest.json; restore(): rsync snapshot→target; write_manifest(): append JSON
} // namespace
```

### Task 2.2: apps/backup/scheduler.h/.cpp

- [ ] Cron-like intervals: Hourly, Daily, Weekly, Custom
- [ ] Persist to `~/.config/straylight/backup-schedules.json`
- [ ] Background timer thread checks every 60s
- [ ] Missed-run detection on startup

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
// .cpp: start() spawns thread sleeping 60s, iterates schedules, calls engine.run_backup if due
} // namespace
```

### Task 2.3: apps/backup/ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] Profile list panel (CRUD), schedule config, history table
- [ ] "Backup Now" with async progress bar
- [ ] Restore from snapshot picker

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
```

```cpp
// apps/backup/main.cpp
#include "ui.h"
int main(int argc, char* argv[]) { straylight::backup::BackupApp a; return a.run(argc, argv); }
```

```cpp
// apps/backup/ui.cpp
void BackupApp::render() {
    ImGui::Columns(2);
    render_profiles(); // Left: Selectable list, Add/Remove, edit fields
    ImGui::NextColumn();
    if (sel_ >= 0) {
        ImGui::BeginTabBar("BT");
        if (ImGui::BeginTabItem("Schedule")) { render_schedule(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("History"))  { render_history();  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        if (ImGui::Button("Backup Now") && !active_.valid()) {
            auto& p = profiles_[sel_];
            active_ = std::async(std::launch::async, [&]{
                return engine_.run_backup(p, [this](int pct, uint64_t, const std::string&){ progress_ = pct/100.f; });
            });
        }
        if (active_.valid()) ImGui::ProgressBar(progress_);
    }
    ImGui::Columns(1);
}
// render_profiles(): profile list + CRUD; render_schedule(): interval combo, hour, enable toggle
// render_history(): table of runs with [Restore] buttons
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

### Task 3.1: apps/media_library/scanner.h/.cpp

- [ ] Recursive folder scan, classify files by extension (audio/video/image)
- [ ] SQLite catalog: `CREATE TABLE media (id INTEGER PRIMARY KEY, path TEXT UNIQUE, type INT, size INT, mtime INT, title TEXT, artist TEXT, album TEXT, duration INT, width INT, height INT)`
- [ ] Async scan thread with progress callback + cancellation
- [ ] Query with optional type filter and text search

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
    int64_t id = 0;
    fs::path path;
    MediaType type;
    uint64_t size;
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
    Result<std::vector<MediaFile>, SLError> query(MediaType filter = MediaType::Unknown,
                                                    const std::string& search = "");
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

```cpp
// apps/media_library/scanner.cpp
#include "scanner.h"
#include <sqlite3.h>
#include <algorithm>
namespace straylight::media {

MediaType Scanner::classify(const fs::path& p) {
    static const std::unordered_map<std::string, MediaType> m = {
        {".mp3",MediaType::Audio},{".flac",MediaType::Audio},{".ogg",MediaType::Audio},
        {".wav",MediaType::Audio},{".opus",MediaType::Audio},{".aac",MediaType::Audio},
        {".mp4",MediaType::Video},{".mkv",MediaType::Video},{".avi",MediaType::Video},{".webm",MediaType::Video},
        {".png",MediaType::Image},{".jpg",MediaType::Image},{".jpeg",MediaType::Image},
        {".bmp",MediaType::Image},{".webp",MediaType::Image},{".gif",MediaType::Image},
    };
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = m.find(ext);
    return it != m.end() ? it->second : MediaType::Unknown;
}

void Scanner::start_scan(std::function<void(uint32_t, const std::string&)> prog) {
    scanning_ = true;
    thread_ = std::thread([this, prog] {
        uint32_t n = 0;
        for (auto& root : roots_) {
            if (!scanning_) break;
            std::error_code ec;
            for (auto& e : fs::recursive_directory_iterator(root, ec)) {
                if (!scanning_ || !e.is_regular_file()) continue;
                auto t = classify(e.path());
                if (t == MediaType::Unknown) continue;
                // INSERT OR REPLACE into media table via prepared statement
                n++; if (prog) prog(n, e.path().string());
            }
        }
        scanning_ = false;
    });
    thread_.detach();
}
// open_db(): sqlite3_open + CREATE TABLE IF NOT EXISTS
// query(): SELECT with optional WHERE type= and LIKE on title/artist/path
} // namespace
```

### Task 3.2: apps/media_library/metadata.h/.cpp

- [ ] Image dimensions: PNG IHDR (bytes 16-23), JPEG SOF0 marker
- [ ] Audio tags: ID3v2 frame parser (TIT2/TPE1/TALB), Vorbis comment parser
- [ ] Video: libavformat probe if available, else file-size-only fallback

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

```cpp
// apps/media_library/metadata.cpp
#include "metadata.h"
#include <fstream>
namespace straylight::media {

Result<MediaFile, SLError> Metadata::extract(const fs::path& p) {
    switch (Scanner::classify(p)) {
        case MediaType::Audio: return extract_audio(p);
        case MediaType::Image: return extract_image(p);
        case MediaType::Video: return extract_video(p);
        default: return SLError("unsupported");
    }
}

Result<MediaFile, SLError> Metadata::extract_image(const fs::path& p) {
    MediaFile mf{.path=p, .type=MediaType::Image};
    std::ifstream f(p, std::ios::binary); if (!f) return SLError("open: " + p.string());
    uint8_t sig[8]; f.read((char*)sig, 8);
    if (sig[1]=='P' && sig[2]=='N' && sig[3]=='G') { // PNG IHDR
        f.seekg(16); uint32_t w,h;
        f.read((char*)&w,4); f.read((char*)&h,4);
        mf.width = __builtin_bswap32(w); mf.height = __builtin_bswap32(h);
    }
    // JPEG: scan for SOF0 (0xFFC0), read height(2B)+width(2B) at offset+5
    return mf;
}
// extract_audio(): parse ID3v2 header (10B), iterate frames for TIT2/TPE1/TALB
//   OR OGG: find vorbis comment, parse key=value pairs
// extract_video(): avformat_open_input if available, else size-only
} // namespace
```

### Task 3.3: apps/media_library/ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] Toolbar: type filter tabs (All/Audio/Video/Images), search, grid/list toggle, scan button
- [ ] Grid or list view of media files
- [ ] Detail panel for selected file metadata
- [ ] Root folder management

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
```

```cpp
// apps/media_library/main.cpp
#include "ui.h"
int main(int argc, char* argv[]) { straylight::media::MediaLibraryApp a; return a.run(argc, argv); }
```

```cpp
// apps/media_library/ui.cpp
void MediaLibraryApp::render() {
    render_toolbar(); ImGui::Separator();
    float dw = sel_ >= 0 ? 280.f : 0.f;
    ImGui::BeginChild("C", {ImGui::GetContentRegionAvail().x - dw, 0});
    grid_ ? render_grid() : render_list();
    ImGui::EndChild();
    if (sel_ >= 0) { ImGui::SameLine(); ImGui::BeginChild("D",{dw,0},true); render_detail(); ImGui::EndChild(); }
}
void MediaLibraryApp::render_toolbar() {
    auto btn = [&](const char* l, MediaType t) { if (ImGui::Button(l)) { filter_=t; refresh(); } ImGui::SameLine(); };
    btn("All", MediaType::Unknown); btn("Audio", MediaType::Audio);
    btn("Video", MediaType::Video); btn("Images", MediaType::Image);
    if (ImGui::InputText("##s", search_, 256, ImGuiInputTextFlags_EnterReturnsTrue)) refresh();
    ImGui::SameLine();
    if (ImGui::Button(grid_ ? "List":"Grid")) grid_ = !grid_;
    ImGui::SameLine();
    if (ImGui::Button(scanner_.is_scanning() ? "Cancel":"Scan")) {
        scanner_.is_scanning() ? scanner_.cancel_scan() : scanner_.start_scan();
    }
}
// render_grid(): columns of name+type; render_list(): table Name|Type|Artist|Duration|Size
// render_detail(): Metadata::extract() for selected, show all fields
// refresh(): results_ = scanner_.query(filter_, search_)
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

### Task 4.1: apps/clipboard/wl_clipboard.h/.cpp

- [ ] Bind `wl_data_device_manager` from registry
- [ ] Listen `wl_data_device.selection` for incoming clipboard
- [ ] Read offer via pipe: `wl_data_offer_receive` → `read()` loop
- [ ] Write clipboard via `wl_data_source` with send handler
- [ ] MIME negotiation: text/plain, text/uri-list, image/png

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
    std::string preview; // first 200 chars or "[image NxN]"
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
    static void on_selection(void* d, wl_data_device*, wl_data_offer*);
    Result<std::vector<uint8_t>, SLError> read_offer(wl_data_offer* o, const std::string& mime);
};
} // namespace
```

```cpp
// apps/clipboard/wl_clipboard.cpp
#include "wl_clipboard.h"
#include <unistd.h>
namespace straylight::clipboard {

Result<void, SLError> WlClipboard::init(wl_display* dpy, wl_seat* seat) {
    dpy_ = dpy;
    auto* reg = wl_display_get_registry(dpy);
    // Bind wl_data_device_manager from registry globals
    wl_display_roundtrip(dpy);
    if (!ddm_) return SLError("no wl_data_device_manager");
    dev_ = wl_data_device_manager_get_data_device(ddm_, seat);
    // Add data_device_listener with .selection = on_selection
    return {};
}

void WlClipboard::on_selection(void* d, wl_data_device*, wl_data_offer* offer) {
    auto* self = static_cast<WlClipboard*>(d);
    auto content = self->read_offer(offer, "text/plain;charset=utf-8");
    if (!content) content = self->read_offer(offer, "text/plain");
    if (content && self->cb_) {
        ClipboardEntry e;
        e.data = std::move(*content); e.mime_type = "text/plain";
        e.timestamp = std::chrono::system_clock::now();
        e.preview = std::string(e.data.begin(), e.data.begin() + std::min<size_t>(e.data.size(), 200));
        self->cb_(std::move(e));
    }
    wl_data_offer_destroy(offer);
}

Result<std::vector<uint8_t>, SLError> WlClipboard::read_offer(wl_data_offer* o, const std::string& mime) {
    int fds[2]; if (pipe(fds) < 0) return SLError::from_errno("pipe");
    wl_data_offer_receive(o, mime.c_str(), fds[1]); close(fds[1]);
    wl_display_roundtrip(dpy_);
    std::vector<uint8_t> r; char buf[4096]; ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) r.insert(r.end(), buf, buf+n);
    close(fds[0]); return r;
}
// set_clipboard(): create wl_data_source, add send listener, wl_data_device_set_selection
} // namespace
```

### Task 4.2: apps/clipboard/history.h/.cpp

- [ ] Ring buffer (default 500 entries), dedup against most recent
- [ ] Pin entries to prevent eviction
- [ ] Text search over previews
- [ ] Persist to `~/.config/straylight/clipboard-history.json`

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

```cpp
// apps/clipboard/history.cpp
namespace straylight::clipboard {
void History::push(ClipboardEntry e) {
    if (!entries_.empty() && entries_.front().data == e.data) return; // dedup
    e.id = next_id_++;
    entries_.push_front(std::move(e));
    while (entries_.size() > max_ && !pinned_.count(entries_.back().id))
        entries_.pop_back();
}
std::vector<const ClipboardEntry*> History::search(const std::string& q) const {
    std::vector<const ClipboardEntry*> r;
    for (auto& e : entries_) if (e.preview.find(q) != std::string::npos) r.push_back(&e);
    return r;
}
// load/save: JSON with base64 for binary data; pin/unpin/remove/clear: standard container ops
} // namespace
```

### Task 4.3: apps/clipboard/ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] Search bar, scrollable history list with preview
- [ ] Click to re-copy, right-click context menu (Pin/Delete)
- [ ] Settings popup: max history, clear all

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
```

```cpp
// apps/clipboard/main.cpp
#include "ui.h"
int main(int argc, char* argv[]) { straylight::clipboard::ClipboardApp a; return a.run(argc, argv); }
```

```cpp
// apps/clipboard/ui.cpp
Result<void, SLError> ClipboardApp::init() {
    hist_.load();
    wl_.on_new_content([this](ClipboardEntry e) { hist_.push(std::move(e)); });
    return {};
}
void ClipboardApp::render() {
    ImGui::InputText("Search", search_, 256); ImGui::Separator();
    auto items = strlen(search_) ? hist_.search(search_)
        : [&]{ std::vector<const ClipboardEntry*> v;
               for (auto& e : hist_.entries()) v.push_back(&e); return v; }();
    ImGui::BeginChild("H");
    for (auto* e : items) {
        ImGui::PushID((int)e->id);
        ImGui::TextDisabled(hist_.is_pinned(e->id) ? "[P]" : "[ ]"); ImGui::SameLine();
        if (ImGui::Selectable(e->preview.c_str())) wl_.set_clipboard(e->mime_type, e->data);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Pin/Unpin"))
                hist_.is_pinned(e->id) ? hist_.unpin(e->id) : hist_.pin(e->id);
            if (ImGui::MenuItem("Delete")) hist_.remove(e->id);
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}
void ClipboardApp::update() { wl_.dispatch(); }
void ClipboardApp::shutdown() { hist_.save(); }
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

### Task 5.1: apps/audio_mixer/pipewire_backend.h/.cpp

- [ ] PipeWire context + main loop, registry listener
- [ ] Enumerate sinks/sources/streams via `pw_registry` global events
- [ ] Volume get/set via SPA_PROP_channelVolumes pod
- [ ] Mute toggle, default sink/source via pw_metadata

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
    Result<void, SLError> refresh();
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

```cpp
// apps/audio_mixer/pipewire_backend.cpp
#include "pipewire_backend.h"
#include <pipewire/pipewire.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
namespace straylight::audio {

Result<void, SLError> PipewireBackend::init() {
    pw_init(nullptr, nullptr);
    loop_ = pw_main_loop_new(nullptr);
    if (!loop_) return SLError("pw_main_loop_new failed");
    ctx_ = pw_context_new(pw_main_loop_get_loop(loop_), nullptr, 0);
    core_ = pw_context_connect(ctx_, nullptr, 0);
    if (!core_) return SLError("PipeWire connect failed");
    reg_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    // pw_registry_add_listener with on_global/on_global_remove
    // roundtrip to populate initial nodes
    return {};
}

void PipewireBackend::poll() {
    pw_loop_iterate(pw_main_loop_get_loop(loop_), 0);
}

Result<void, SLError> PipewireBackend::set_volume(uint32_t id, float v) {
    uint8_t buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    float vols[2] = {v, v};
    // Build SPA_TYPE_OBJECT_Props with SPA_PROP_channelVolumes array
    // pw_registry_... set param on node
    return {};
}
// on_global(): classify by media.class → Audio/Sink, Audio/Source, Stream/*
// set_mute(): SPA_PROP_mute; set_default(): pw_metadata "default.audio.sink"/"source"
// shutdown(): pw_core_disconnect, pw_context_destroy, pw_main_loop_destroy
} // namespace
```

### Task 5.2: apps/audio_mixer/pulse_backend.h/.cpp

- [ ] PulseAudio fallback: same AudioNode interface
- [ ] `pa_context` connect, enumerate sinks/sources/sink-inputs
- [ ] Volume via `pa_context_set_sink_volume_by_index`
- [ ] Mute via `pa_context_set_sink_mute_by_index`

```cpp
// apps/audio_mixer/pulse_backend.h
#pragma once
#include "pipewire_backend.h" // reuse AudioNode, NodeType
namespace straylight::audio {
class PulseBackend {
public:
    Result<void, SLError> init();
    void shutdown();
    Result<void, SLError> refresh();
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
// .cpp: pa_mainloop_new, pa_context_new/connect, wait READY
// refresh(): pa_context_get_sink_info_list → sinks_, get_source_info_list → sources_,
//   get_sink_input_info_list → streams_; convert pa_cvolume → float via pa_sw_volume_to_linear
// set_volume(): pa_cvolume_set + pa_context_set_sink_volume_by_index
// set_mute(): pa_context_set_sink_mute_by_index
// set_default(): pa_context_set_default_sink
// poll(): pa_mainloop_iterate(loop_, 0, nullptr)
} // namespace
```

### Task 5.3: apps/audio_mixer/ui.h/.cpp + main.cpp + CMakeLists.txt

- [ ] Auto-detect PipeWire vs PulseAudio at startup
- [ ] Tabs: Output (sinks), Input (sources), Applications (streams)
- [ ] Per-node row: default indicator, name, mute checkbox, volume slider + dB label
- [ ] `std::variant<PipewireBackend, PulseBackend>` with `std::visit` delegation

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
    enum class Tab { Out, In, Apps } tab_ = Tab::Out;
    const std::vector<AudioNode>& get_sinks() const;
    const std::vector<AudioNode>& get_sources() const;
    const std::vector<AudioNode>& get_streams() const;
    void set_vol(uint32_t id, float v);
    void set_mute(uint32_t id, bool m);
    void set_def(uint32_t id, NodeType t);
    void render_nodes(const std::vector<AudioNode>& nodes, NodeType t);
    void render_row(const AudioNode& n, NodeType t);
};
} // namespace
```

```cpp
// apps/audio_mixer/main.cpp
#include "ui.h"
int main(int argc, char* argv[]) { straylight::audio::AudioMixerApp a; return a.run(argc, argv); }
```

```cpp
// apps/audio_mixer/ui.cpp
Result<void, SLError> AudioMixerApp::init() {
    auto& pw = be_.emplace<PipewireBackend>();
    if (auto r = pw.init(); !r) { auto& pa = be_.emplace<PulseBackend>(); pw_ = false; return pa.init(); }
    return {};
}
void AudioMixerApp::update() { std::visit([](auto& b){ b.poll(); }, be_); }
void AudioMixerApp::render() {
    ImGui::Text("Backend: %s", pw_ ? "PipeWire" : "PulseAudio"); ImGui::Separator();
    ImGui::BeginTabBar("M");
    if (ImGui::BeginTabItem("Output")) { render_nodes(get_sinks(), NodeType::Sink);     ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Input"))  { render_nodes(get_sources(), NodeType::Source); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Apps"))   { render_nodes(get_streams(), NodeType::Stream); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
}
void AudioMixerApp::render_row(const AudioNode& n, NodeType t) {
    ImGui::PushID(n.id);
    if (t != NodeType::Stream) {
        if (n.is_default) ImGui::TextColored({0,1,0,1}, "*");
        else if (ImGui::SmallButton("Def")) set_def(n.id, t);
        ImGui::SameLine();
    }
    ImGui::Text("%s", n.description.empty() ? n.name.c_str() : n.description.c_str());
    ImGui::SameLine(300);
    bool m = n.muted; if (ImGui::Checkbox("Mute", &m)) set_mute(n.id, m);
    ImGui::SameLine(400); float v = n.volume; ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("##v", &v, 0.f, 1.5f)) set_vol(n.id, v);
    ImGui::SameLine();
    ImGui::Text("%.1f dB", v > .001f ? 20.f * log10f(v) : -60.f);
    ImGui::PopID(); ImGui::Separator();
}
// get_sinks/sources/streams, set_vol/mute/def: std::visit delegation to active backend
void AudioMixerApp::shutdown() { std::visit([](auto& b){ b.shutdown(); }, be_); }
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
