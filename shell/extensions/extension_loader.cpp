// shell/extensions/extension_loader.cpp
#include "extension_loader.h"
#include <straylight/log.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

namespace straylight {

ExtensionLoader::ExtensionLoader() {
    // Default scan directories
    scan_dirs_.push_back("/usr/lib/straylight/extensions/");

    // User extensions directory
    const char* home = ::getenv("HOME");
    if (home) {
        scan_dirs_.push_back(std::string(home) + "/.config/straylight/extensions/");
    }
}

ExtensionLoader::~ExtensionLoader() {
    shutdown_all();
}

void ExtensionLoader::add_scan_directory(const std::string& path) {
    scan_dirs_.push_back(path);
}

std::vector<std::string> ExtensionLoader::scan_directory(const std::string& dir_path) const {
    std::vector<std::string> results;

    DIR* dir = ::opendir(dir_path.c_str());
    if (!dir) return results;

    struct dirent* entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() < 4) continue;

        // Check for .so extension
        if (name.substr(name.size() - 3) != ".so") continue;

        std::string full_path = dir_path;
        if (!full_path.empty() && full_path.back() != '/') {
            full_path += '/';
        }
        full_path += name;

        // Verify it's a regular file
        struct stat st{};
        if (::stat(full_path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            results.push_back(full_path);
        }
    }

    ::closedir(dir);
    std::sort(results.begin(), results.end());
    return results;
}

Result<int, SLError> ExtensionLoader::scan_and_load() {
    int loaded = 0;

    for (const auto& dir : scan_dirs_) {
        auto files = scan_directory(dir);
        for (const auto& path : files) {
            auto r = load_so(path);
            if (r.has_value()) {
                ++loaded;
            } else {
                SL_WARN("extensions: skipping {}: {}", path, r.error().message());
            }
        }
    }

    SL_INFO("extensions: loaded {} extension(s) from {} director(ies)",
            loaded, scan_dirs_.size());
    return Result<int, SLError>::ok(loaded);
}

Result<void, SLError> ExtensionLoader::load(const std::string& path) {
    return load_so(path);
}

Result<void, SLError> ExtensionLoader::load_so(const std::string& path) {
    // Open the shared library
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError,
                    "dlopen failed for " + path + ": " + std::string(::dlerror() ? ::dlerror() : "unknown")});
    }

    // Resolve the four required symbols
    auto fn_info = reinterpret_cast<sl_extension_info_fn>(::dlsym(handle, "sl_extension_info"));
    auto fn_init = reinterpret_cast<sl_extension_init_fn>(::dlsym(handle, "sl_extension_init"));
    auto fn_render = reinterpret_cast<sl_extension_render_fn>(::dlsym(handle, "sl_extension_render"));
    auto fn_shutdown = reinterpret_cast<sl_extension_shutdown_fn>(::dlsym(handle, "sl_extension_shutdown"));

    if (!fn_info || !fn_init || !fn_render || !fn_shutdown) {
        ::dlclose(handle);
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound,
                    "Extension " + path + " is missing required exports (sl_extension_info/init/render/shutdown)"});
    }

    // Get info and validate API version
    SlExtensionInfo info = fn_info();
    if (info.api_version != SL_EXTENSION_API_VERSION) {
        ::dlclose(handle);
        return Result<void, SLError>::error(
            SLError{SLErrorCode::InvalidArgument,
                    "Extension " + path + " has API version " + std::to_string(info.api_version) +
                    ", expected " + std::to_string(SL_EXTENSION_API_VERSION)});
    }

    std::string name(info.name);
    if (name.empty()) {
        ::dlclose(handle);
        return Result<void, SLError>::error(
            SLError{SLErrorCode::InvalidArgument, "Extension " + path + " has empty name"});
    }

    // Check for duplicate
    if (extensions_.count(name)) {
        ::dlclose(handle);
        return Result<void, SLError>::error(
            SLError{SLErrorCode::AlreadyExists,
                    "Extension '" + name + "' already loaded from " + extensions_[name]->path});
    }

    // Initialize the extension
    int init_result = fn_init(context_);
    if (init_result != 0) {
        ::dlclose(handle);
        return Result<void, SLError>::error(
            SLError{SLErrorCode::Internal,
                    "Extension '" + name + "' init() returned " + std::to_string(init_result)});
    }

    // Register
    auto ext = std::make_unique<LoadedExtension>();
    ext->path = path;
    ext->name = name;
    ext->version = info.version;
    ext->author = info.author;
    ext->description = info.description;
    ext->enabled = true;
    ext->dl_handle = handle;
    ext->fn_info = fn_info;
    ext->fn_init = fn_init;
    ext->fn_render = fn_render;
    ext->fn_shutdown = fn_shutdown;

    SL_INFO("extensions: loaded '{}' v{} by {} ({})",
            ext->name, ext->version, ext->author, path);

    load_order_.push_back(name);
    extensions_[name] = std::move(ext);

    return Result<void, SLError>::ok();
}

Result<void, SLError> ExtensionLoader::unload(const std::string& name) {
    auto it = extensions_.find(name);
    if (it == extensions_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Extension '" + name + "' not loaded"});
    }

    auto& ext = it->second;

    // Call shutdown
    if (ext->fn_shutdown) {
        ext->fn_shutdown();
    }

    // Close the library
    if (ext->dl_handle) {
        ::dlclose(ext->dl_handle);
    }

    SL_INFO("extensions: unloaded '{}'", name);

    // Remove from load order
    load_order_.erase(
        std::remove(load_order_.begin(), load_order_.end(), name),
        load_order_.end());

    extensions_.erase(it);
    return Result<void, SLError>::ok();
}

Result<void, SLError> ExtensionLoader::set_enabled(const std::string& name, bool enabled) {
    auto it = extensions_.find(name);
    if (it == extensions_.end()) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::NotFound, "Extension '" + name + "' not loaded"});
    }

    it->second->enabled = enabled;
    SL_INFO("extensions: '{}' {}", name, enabled ? "enabled" : "disabled");
    return Result<void, SLError>::ok();
}

void ExtensionLoader::render_all(float dt) {
    for (const auto& name : load_order_) {
        auto it = extensions_.find(name);
        if (it == extensions_.end()) continue;

        auto& ext = it->second;
        if (!ext->enabled || !ext->fn_render) continue;

        ext->fn_render(dt);
    }
}

void ExtensionLoader::shutdown_all() {
    // Shutdown in reverse load order
    for (auto it = load_order_.rbegin(); it != load_order_.rend(); ++it) {
        auto eit = extensions_.find(*it);
        if (eit == extensions_.end()) continue;

        auto& ext = eit->second;
        if (ext->fn_shutdown) {
            ext->fn_shutdown();
        }
        if (ext->dl_handle) {
            ::dlclose(ext->dl_handle);
            ext->dl_handle = nullptr;
        }
        SL_INFO("extensions: shut down '{}'", ext->name);
    }

    load_order_.clear();
    extensions_.clear();
}

std::vector<const LoadedExtension*> ExtensionLoader::list() const {
    std::vector<const LoadedExtension*> result;
    result.reserve(load_order_.size());
    for (const auto& name : load_order_) {
        auto it = extensions_.find(name);
        if (it != extensions_.end()) {
            result.push_back(it->second.get());
        }
    }
    return result;
}

bool ExtensionLoader::is_loaded(const std::string& name) const {
    return extensions_.count(name) > 0;
}

} // namespace straylight
