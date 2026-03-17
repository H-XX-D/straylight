// apps/clipboard/history.cpp
// Ring-buffer clipboard history implementation with JSON persistence
#include "history.h"

#include <straylight/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace straylight::clipboard {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

inline SLError make_err(SLErrorCode code, const std::string& msg) {
    return SLError{code, msg};
}

int64_t tp_to_epoch(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(
        tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point epoch_to_tp(int64_t s) {
    return std::chrono::system_clock::time_point{std::chrono::seconds{s}};
}

// ---------------------------------------------------------------------------
// Minimal base64 encode/decode (RFC 4648)
// ---------------------------------------------------------------------------

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t(data[i]) << 16);
        if (i + 1 < len) b |= uint32_t(data[i + 1]) << 8;
        if (i + 2 < len) b |= uint32_t(data[i + 2]);
        out += kB64Chars[(b >> 18) & 0x3F];
        out += kB64Chars[(b >> 12) & 0x3F];
        out += (i + 1 < len) ? kB64Chars[(b >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kB64Chars[b & 0x3F] : '=';
    }
    return out;
}

std::vector<uint8_t> base64_decode(const std::string& in) {
    auto decode_char = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::vector<uint8_t> out;
    out.reserve((in.size() / 4) * 3);
    for (size_t i = 0; i + 3 < in.size(); i += 4) {
        int b0 = decode_char(in[i]);
        int b1 = decode_char(in[i + 1]);
        int b2 = decode_char(in[i + 2]);
        int b3 = decode_char(in[i + 3]);
        if (b0 < 0 || b1 < 0) break;
        out.push_back(uint8_t((b0 << 2) | (b1 >> 4)));
        if (b2 >= 0) out.push_back(uint8_t(((b1 & 0xF) << 4) | (b2 >> 2)));
        if (b3 >= 0) out.push_back(uint8_t(((b2 & 0x3) << 6) | b3));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// ClipEntry::preview
// ---------------------------------------------------------------------------

std::string ClipEntry::preview(size_t max_chars) const {
    if (kind == EntryKind::Image) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "[Image %zu bytes]", image_data.size());
        return buf;
    }
    if (text.size() <= max_chars) return text;
    return text.substr(0, max_chars - 3) + "...";
}

// ---------------------------------------------------------------------------
// Storage path
// ---------------------------------------------------------------------------

fs::path ClipHistory::storage_path() {
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) / ".local" / "share" / "straylight"
                         : fs::path("/tmp");
    std::error_code ec;
    fs::create_directories(base, ec);
    return base / "clipboard-history.json";
}

// ---------------------------------------------------------------------------
// push_text
// ---------------------------------------------------------------------------

void ClipHistory::push_text(std::string text, const std::string& mime) {
    if (text.empty()) return;

    std::lock_guard lock(mtx_);

    // Deduplicate against most recent entry
    if (!entries_.empty() &&
        entries_.front().kind == EntryKind::Text &&
        entries_.front().text == text) {
        // Update timestamp but don't duplicate
        entries_.front().timestamp = std::chrono::system_clock::now();
        return;
    }

    ClipEntry e;
    e.kind      = EntryKind::Text;
    e.text      = std::move(text);
    e.mime      = mime;
    e.timestamp = std::chrono::system_clock::now();
    entries_.push_front(std::move(e));

    // Evict oldest non-pinned if over capacity
    while (entries_.size() > kMaxEntries) {
        // find last non-pinned entry from the back
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (!it->pinned) {
                entries_.erase(std::next(it).base());
                break;
            }
        }
        // Safety: if all are pinned just drop the oldest
        if (entries_.size() > kMaxEntries) {
            entries_.pop_back();
        }
    }
}

// ---------------------------------------------------------------------------
// push_image
// ---------------------------------------------------------------------------

void ClipHistory::push_image(std::vector<uint8_t> png_data, const std::string& mime) {
    if (png_data.empty()) return;

    std::lock_guard lock(mtx_);

    ClipEntry e;
    e.kind       = EntryKind::Image;
    e.image_data = std::move(png_data);
    e.mime       = mime;
    e.timestamp  = std::chrono::system_clock::now();
    entries_.push_front(std::move(e));

    while (entries_.size() > kMaxEntries) {
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (!it->pinned) {
                entries_.erase(std::next(it).base());
                break;
            }
        }
        if (entries_.size() > kMaxEntries) entries_.pop_back();
    }
}

// ---------------------------------------------------------------------------
// Read-only accessors
// ---------------------------------------------------------------------------

std::vector<ClipEntry> ClipHistory::entries() const {
    std::lock_guard lock(mtx_);
    return {entries_.begin(), entries_.end()};
}

size_t ClipHistory::size() const {
    std::lock_guard lock(mtx_);
    return entries_.size();
}

void ClipHistory::toggle_pin(size_t index) {
    std::lock_guard lock(mtx_);
    if (index < entries_.size()) entries_[index].pinned = !entries_[index].pinned;
}

void ClipHistory::remove(size_t index) {
    std::lock_guard lock(mtx_);
    if (index < entries_.size()) entries_.erase(entries_.begin() + index);
}

void ClipHistory::clear_unpinned() {
    std::lock_guard lock(mtx_);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [](const ClipEntry& e){ return !e.pinned; }),
        entries_.end());
}

void ClipHistory::clear_all() {
    std::lock_guard lock(mtx_);
    entries_.clear();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

Result<void, SLError> ClipHistory::save() const {
    std::lock_guard lock(mtx_);

    json jarr = json::array();
    for (const auto& e : entries_) {
        json je;
        je["kind"]      = (e.kind == EntryKind::Image) ? "image" : "text";
        je["mime"]      = e.mime;
        je["pinned"]    = e.pinned;
        je["timestamp"] = tp_to_epoch(e.timestamp);

        if (e.kind == EntryKind::Text) {
            je["text"] = e.text;
        } else {
            // Store image as base64 (only persist pinned images to avoid huge files)
            if (e.pinned) {
                je["image_b64"] = base64_encode(e.image_data.data(),
                                                 e.image_data.size());
            }
        }
        jarr.push_back(std::move(je));
    }

    json root;
    root["version"] = 1;
    root["entries"] = std::move(jarr);

    std::ofstream f(storage_path());
    if (!f) {
        return Result<void, SLError>::error(
            make_err(SLErrorCode::IOError,
                     "Cannot write clipboard history: " + storage_path().string()));
    }
    f << root.dump(2);
    return Result<void, SLError>::ok();
}

Result<void, SLError> ClipHistory::load() {
    const fs::path p = storage_path();
    if (!fs::exists(p)) return Result<void, SLError>::ok();

    std::ifstream f(p);
    if (!f) {
        return Result<void, SLError>::error(
            make_err(SLErrorCode::IOError, "Cannot read clipboard history: " + p.string()));
    }

    json root;
    try { f >> root; }
    catch (const std::exception& ex) {
        return Result<void, SLError>::error(
            make_err(SLErrorCode::ParseError,
                     std::string("JSON parse error: ") + ex.what()));
    }

    std::lock_guard lock(mtx_);
    entries_.clear();

    for (const auto& je : root.value("entries", json::array())) {
        ClipEntry e;
        std::string kind_s = je.value("kind", "text");
        e.kind      = (kind_s == "image") ? EntryKind::Image : EntryKind::Text;
        e.mime      = je.value("mime", "text/plain");
        e.pinned    = je.value("pinned", false);
        e.timestamp = epoch_to_tp(je.value("timestamp", int64_t{0}));

        if (e.kind == EntryKind::Text) {
            e.text = je.value("text", "");
        } else {
            std::string b64 = je.value("image_b64", "");
            if (!b64.empty()) e.image_data = base64_decode(b64);
        }
        entries_.push_back(std::move(e));
    }

    SL_INFO("Loaded {} clipboard history entries", entries_.size());
    return Result<void, SLError>::ok();
}

} // namespace straylight::clipboard
