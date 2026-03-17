// shell/extensions/extension_loader.h
// Runtime loader for shell extension shared libraries.
#pragma once

#include "extension_api.h"
#include <straylight/result.h>
#include <straylight/error.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace straylight {

/// A loaded extension instance with resolved function pointers.
struct LoadedExtension {
    std::string path;           // Full path to the .so file
    std::string name;           // From SlExtensionInfo::name
    std::string version;        // From SlExtensionInfo::version
    std::string author;         // From SlExtensionInfo::author
    std::string description;    // From SlExtensionInfo::description
    bool enabled = true;

    void* dl_handle = nullptr;

    sl_extension_info_fn     fn_info = nullptr;
    sl_extension_init_fn     fn_init = nullptr;
    sl_extension_render_fn   fn_render = nullptr;
    sl_extension_shutdown_fn fn_shutdown = nullptr;
};

/// Scans directories for extension .so files, validates them, and manages lifecycle.
class ExtensionLoader {
public:
    ExtensionLoader();
    ~ExtensionLoader();

    ExtensionLoader(const ExtensionLoader&) = delete;
    ExtensionLoader& operator=(const ExtensionLoader&) = delete;

    /// Set the shell context that will be passed to extensions on init.
    void set_context(SlExtensionContext* ctx) { context_ = ctx; }

    /// Scan default directories and load all valid extensions.
    /// System: /usr/lib/straylight/extensions/
    /// User:   ~/.config/straylight/extensions/
    Result<int, SLError> scan_and_load();

    /// Load a single extension from a .so file path.
    Result<void, SLError> load(const std::string& path);

    /// Unload an extension by name.
    Result<void, SLError> unload(const std::string& name);

    /// Enable/disable an extension (still loaded, just skips render).
    Result<void, SLError> set_enabled(const std::string& name, bool enabled);

    /// Render all enabled extensions. Call once per frame from the shell render loop.
    void render_all(float dt);

    /// Shutdown all extensions and unload their libraries.
    void shutdown_all();

    /// Get a list of all loaded extensions with metadata.
    std::vector<const LoadedExtension*> list() const;

    /// Check if an extension is loaded by name.
    bool is_loaded(const std::string& name) const;

    /// Add an additional scan directory.
    void add_scan_directory(const std::string& path);

private:
    /// Attempt to dlopen a .so, validate the API, and register it.
    Result<void, SLError> load_so(const std::string& path);

    /// Scan a single directory for .so files.
    std::vector<std::string> scan_directory(const std::string& dir_path) const;

    SlExtensionContext* context_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<LoadedExtension>> extensions_;
    std::vector<std::string> scan_dirs_;
    std::vector<std::string> load_order_; // Track insertion order for deterministic render
};

} // namespace straylight
