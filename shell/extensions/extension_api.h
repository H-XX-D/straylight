// shell/extensions/extension_api.h
// Public C API that shell extensions must implement.
// Extensions are shared libraries (.so) loaded at runtime by the shell.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// API version — extensions must match this to be loaded.
#define SL_EXTENSION_API_VERSION 1

/// Maximum string lengths for extension metadata.
#define SL_EXT_NAME_MAX 64
#define SL_EXT_VERSION_MAX 32
#define SL_EXT_AUTHOR_MAX 128
#define SL_EXT_DESCRIPTION_MAX 256

/// Extension metadata returned by sl_extension_info().
typedef struct {
    uint32_t api_version;
    char name[SL_EXT_NAME_MAX];
    char version[SL_EXT_VERSION_MAX];
    char author[SL_EXT_AUTHOR_MAX];
    char description[SL_EXT_DESCRIPTION_MAX];
} SlExtensionInfo;

/// Context provided to extensions during init.
/// Gives access to shell services.
typedef struct {
    /// Root path for extension config files (~/.config/straylight/extensions/<name>/)
    const char* config_dir;

    /// Root path for extension data files (~/.local/share/straylight/extensions/<name>/)
    const char* data_dir;

    /// Screen dimensions at init time (may change via resize callback)
    float screen_width;
    float screen_height;

    /// Request the shell to show a notification toast.
    void (*show_notification)(const char* title, const char* body, float duration_seconds);

    /// Request the shell to launch an application by .desktop file or binary path.
    void (*launch_app)(const char* app_id_or_path);

    /// Log a message through the shell's logging system.
    void (*log_info)(const char* message);
    void (*log_warn)(const char* message);
    void (*log_error)(const char* message);
} SlExtensionContext;

/// Every extension .so must export these four functions:

/// Return extension metadata. Called before init to check API version and display info.
typedef SlExtensionInfo (*sl_extension_info_fn)();

/// Initialize the extension. Called once after loading.
/// Returns 0 on success, non-zero on failure.
typedef int (*sl_extension_init_fn)(SlExtensionContext* ctx);

/// Render one frame. Called every frame with the delta time in seconds.
/// The extension should use ImGui calls to draw its UI.
typedef void (*sl_extension_render_fn)(float dt);

/// Clean up resources. Called before unloading the extension.
typedef void (*sl_extension_shutdown_fn)();

#ifdef __cplusplus
}
#endif
