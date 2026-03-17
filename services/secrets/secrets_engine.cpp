// services/secrets/secrets_engine.cpp
#include "secrets_engine.h"

#include <straylight/log.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace straylight {

namespace fs = std::filesystem;

namespace {

std::string timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string time_to_iso(std::chrono::system_clock::time_point tp) {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::chrono::system_clock::time_point parse_iso(const std::string& s) {
    std::tm tm{};
    std::istringstream ss(s);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

} // namespace

// Simple XOR-based encryption with master key (in production, use libsodium/openssl).
// This is a symmetric cipher suitable for local secret storage.
std::string SecretsEngine::encrypt(const std::string& plaintext) const {
    std::string result;
    result.reserve(plaintext.size() * 2);
    for (size_t i = 0; i < plaintext.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(plaintext[i]) ^
                          static_cast<unsigned char>(master_key_[i % master_key_.size()]);
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", c);
        result += hex;
    }
    return result;
}

std::string SecretsEngine::decrypt(const std::string& ciphertext) const {
    std::string result;
    result.reserve(ciphertext.size() / 2);
    for (size_t i = 0; i + 1 < ciphertext.size(); i += 2) {
        unsigned char c = static_cast<unsigned char>(
            std::stoi(ciphertext.substr(i, 2), nullptr, 16));
        c ^= static_cast<unsigned char>(master_key_[(i / 2) % master_key_.size()]);
        result += static_cast<char>(c);
    }
    return result;
}

std::string SecretsEngine::generate_random_value(int length) const {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(charset) - 2));

    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        result += charset[dist(gen)];
    }
    return result;
}

bool SecretsEngine::check_access(const SecretEntry& entry, uint32_t uid) const {
    // Root always has access
    if (uid == 0) return true;

    // Check UID list
    if (entry.acl.allowed_uids.count(uid)) return true;

    // Empty ACL means owner-only (uid 0)
    return false;
}

void SecretsEngine::audit(const std::string& action, const std::string& key,
                           uint32_t uid, bool allowed, const std::string& detail) {
    AuditEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.action = action;
    entry.key = key;
    entry.uid = uid;
    entry.allowed = allowed;
    entry.detail = detail;
    audit_log_.push_back(std::move(entry));

    // Keep last 10000 entries
    if (audit_log_.size() > 10000) {
        audit_log_.erase(audit_log_.begin(), audit_log_.begin() + 1000);
    }

    if (!allowed) {
        SL_WARN("secrets: ACCESS DENIED uid={} action={} key={}", uid, action, key);
    }
}

Result<void, SLError> SecretsEngine::init(const fs::path& store_path,
                                            const std::string& master_key_path) {
    store_path_ = store_path;

    // Load master key
    std::ifstream kf(master_key_path);
    if (!kf) {
        // Generate a new master key if none exists
        master_key_ = generate_random_value(64);
        std::error_code ec;
        fs::create_directories(fs::path(master_key_path).parent_path(), ec);
        std::ofstream of(master_key_path);
        if (of) {
            of << master_key_;
            // Set restrictive permissions
            fs::permissions(master_key_path,
                           fs::perms::owner_read | fs::perms::owner_write, ec);
        }
        SL_INFO("secrets: generated new master key at {}", master_key_path);
    } else {
        std::getline(kf, master_key_);
    }

    if (master_key_.empty()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal, "Master key is empty"});
    }

    // Create store directory
    std::error_code ec;
    fs::create_directories(store_path_.parent_path(), ec);

    load();

    SL_INFO("secrets: initialized with {} secrets", secrets_.size());
    return Result<void, SLError>::ok();
}

Result<void, SLError> SecretsEngine::set(const std::string& key, const std::string& value,
                                          uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it != secrets_.end()) {
        // Update — check access
        if (!check_access(it->second, caller_uid)) {
            audit("set", key, caller_uid, false, "permission denied on update");
            return Result<void, SLError>::error(
                SLError{SLErrorCode::PermissionDenied, "Access denied to key: " + key});
        }
        it->second.encrypted_value = encrypt(value);
        it->second.version++;
        audit("set", key, caller_uid, true, "updated v" + std::to_string(it->second.version));
    } else {
        // Create new
        SecretEntry entry;
        entry.key = key;
        entry.encrypted_value = encrypt(value);
        entry.created = std::chrono::system_clock::now();
        entry.last_rotated = entry.created;
        entry.acl.allowed_uids.insert(caller_uid);
        secrets_[key] = std::move(entry);
        audit("set", key, caller_uid, true, "created");
    }

    save();
    return Result<void, SLError>::ok();
}

Result<std::string, SLError> SecretsEngine::get(const std::string& key,
                                                  uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it == secrets_.end()) {
        audit("get", key, caller_uid, false, "not found");
        return Result<std::string, SLError>::error(
            SLError{SLErrorCode::NotFound, "Secret not found: " + key});
    }

    if (!check_access(it->second, caller_uid)) {
        audit("get", key, caller_uid, false, "permission denied");
        return Result<std::string, SLError>::error(
            SLError{SLErrorCode::PermissionDenied, "Access denied to key: " + key});
    }

    it->second.last_accessed = std::chrono::system_clock::now();
    std::string value = decrypt(it->second.encrypted_value);
    audit("get", key, caller_uid, true);
    return Result<std::string, SLError>::ok(value);
}

Result<void, SLError> SecretsEngine::remove(const std::string& key, uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it == secrets_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Secret not found: " + key});
    }

    if (!check_access(it->second, caller_uid)) {
        audit("delete", key, caller_uid, false, "permission denied");
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied, "Access denied to key: " + key});
    }

    secrets_.erase(it);
    audit("delete", key, caller_uid, true);
    save();
    return Result<void, SLError>::ok();
}

std::vector<std::string> SecretsEngine::list(uint32_t caller_uid) const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> keys;
    for (const auto& [key, entry] : secrets_) {
        if (check_access(entry, caller_uid)) {
            keys.push_back(key);
        }
    }
    return keys;
}

Result<void, SLError> SecretsEngine::rotate(const std::string& key, uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it == secrets_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Secret not found: " + key});
    }

    if (!check_access(it->second, caller_uid)) {
        audit("rotate", key, caller_uid, false, "permission denied");
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied, "Access denied to key: " + key});
    }

    std::string new_value = generate_random_value(32);
    it->second.encrypted_value = encrypt(new_value);
    it->second.last_rotated = std::chrono::system_clock::now();
    it->second.version++;
    audit("rotate", key, caller_uid, true,
          "rotated to v" + std::to_string(it->second.version));
    save();

    return Result<void, SLError>::ok();
}

Result<void, SLError> SecretsEngine::acl_add(const std::string& key, uint32_t target_uid,
                                               uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it == secrets_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Secret not found: " + key});
    }

    if (!check_access(it->second, caller_uid)) {
        audit("acl_change", key, caller_uid, false, "permission denied");
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied, "Access denied"});
    }

    it->second.acl.allowed_uids.insert(target_uid);
    audit("acl_change", key, caller_uid, true,
          "added uid " + std::to_string(target_uid));
    save();
    return Result<void, SLError>::ok();
}

Result<void, SLError> SecretsEngine::acl_remove(const std::string& key,
                                                  uint32_t target_uid,
                                                  uint32_t caller_uid) {
    std::lock_guard lock(mutex_);

    auto it = secrets_.find(key);
    if (it == secrets_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Secret not found: " + key});
    }

    if (!check_access(it->second, caller_uid)) {
        audit("acl_change", key, caller_uid, false, "permission denied");
        return Result<void, SLError>::error(
            SLError{SLErrorCode::PermissionDenied, "Access denied"});
    }

    it->second.acl.allowed_uids.erase(target_uid);
    audit("acl_change", key, caller_uid, true,
          "removed uid " + std::to_string(target_uid));
    save();
    return Result<void, SLError>::ok();
}

std::map<std::string, std::string> SecretsEngine::get_env_for(uint32_t uid) const {
    std::lock_guard lock(mutex_);
    std::map<std::string, std::string> env;
    for (const auto& [key, entry] : secrets_) {
        if (check_access(entry, uid)) {
            // Convert key to env var name: dots/dashes become underscores, uppercase
            std::string env_name = "SL_SECRET_";
            for (char c : key) {
                if (c == '.' || c == '-' || c == '/') env_name += '_';
                else env_name += static_cast<char>(std::toupper(c));
            }
            env[env_name] = decrypt(entry.encrypted_value);
        }
    }
    return env;
}

Result<void, SLError> SecretsEngine::check_rotations() {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::system_clock::now();

    for (auto& [key, entry] : secrets_) {
        if (entry.rotation_interval_hours <= 0) continue;

        auto elapsed = std::chrono::duration_cast<std::chrono::hours>(
            now - entry.last_rotated);
        if (elapsed.count() >= entry.rotation_interval_hours) {
            std::string new_value = generate_random_value(32);
            entry.encrypted_value = encrypt(new_value);
            entry.last_rotated = now;
            entry.version++;
            audit("rotate", key, 0, true, "auto-rotated to v" +
                  std::to_string(entry.version));
            SL_INFO("secrets: auto-rotated key '{}'", key);
        }
    }

    save();
    return Result<void, SLError>::ok();
}

std::vector<AuditEntry> SecretsEngine::get_audit_log(int last_n) const {
    std::lock_guard lock(mutex_);
    if (static_cast<int>(audit_log_.size()) <= last_n) {
        return audit_log_;
    }
    return std::vector<AuditEntry>(
        audit_log_.end() - last_n, audit_log_.end());
}

void SecretsEngine::save() const {
    nlohmann::json j;
    j["secrets"] = nlohmann::json::object();

    for (const auto& [key, entry] : secrets_) {
        nlohmann::json ej;
        ej["encrypted_value"] = entry.encrypted_value;
        ej["version"] = entry.version;
        ej["rotation_interval_hours"] = entry.rotation_interval_hours;
        ej["created"] = time_to_iso(entry.created);
        ej["last_rotated"] = time_to_iso(entry.last_rotated);

        nlohmann::json uids = nlohmann::json::array();
        for (uint32_t uid : entry.acl.allowed_uids) {
            uids.push_back(uid);
        }
        ej["allowed_uids"] = uids;

        nlohmann::json gids = nlohmann::json::array();
        for (uint32_t gid : entry.acl.allowed_gids) {
            gids.push_back(gid);
        }
        ej["allowed_gids"] = gids;

        j["secrets"][key] = ej;
    }

    std::ofstream ofs(store_path_);
    if (ofs) {
        ofs << j.dump(2) << "\n";
        // Set restrictive permissions
        std::error_code ec;
        fs::permissions(store_path_,
                       fs::perms::owner_read | fs::perms::owner_write, ec);
    }
}

void SecretsEngine::load() {
    if (!fs::exists(store_path_)) return;

    std::ifstream ifs(store_path_);
    if (!ifs) return;

    try {
        nlohmann::json j;
        ifs >> j;

        if (j.contains("secrets") && j["secrets"].is_object()) {
            for (auto& [key, ej] : j["secrets"].items()) {
                SecretEntry entry;
                entry.key = key;
                entry.encrypted_value = ej.value("encrypted_value", "");
                entry.version = ej.value("version", 1);
                entry.rotation_interval_hours = ej.value("rotation_interval_hours", 0);
                entry.created = parse_iso(ej.value("created", "2000-01-01T00:00:00"));
                entry.last_rotated = parse_iso(ej.value("last_rotated", "2000-01-01T00:00:00"));

                if (ej.contains("allowed_uids") && ej["allowed_uids"].is_array()) {
                    for (const auto& uid : ej["allowed_uids"]) {
                        entry.acl.allowed_uids.insert(uid.get<uint32_t>());
                    }
                }
                if (ej.contains("allowed_gids") && ej["allowed_gids"].is_array()) {
                    for (const auto& gid : ej["allowed_gids"]) {
                        entry.acl.allowed_gids.insert(gid.get<uint32_t>());
                    }
                }

                secrets_[key] = std::move(entry);
            }
        }
    } catch (const std::exception& e) {
        SL_WARN("secrets: failed to load store: {}", e.what());
    }
}

} // namespace straylight
