// shell/settings/appearance.h
// Appearance settings panel + D-Bus interface for theme/layout apply
#pragma once

#include <string>

namespace straylight::shell {

class ThemeEngine;

/// Appearance settings panel and D-Bus service endpoint.
/// Exposes org.straylight.Shell1 interface with ApplyTheme and ApplyLayout
/// methods callable by straylight-wizard and straylight-settings.
class AppearanceSettings {
public:
    AppearanceSettings() = default;
    ~AppearanceSettings() = default;

    /// Render the appearance settings UI.
    void render();

    /// D-Bus method: Apply a theme by name.
    /// Called by straylight-wizard via org.straylight.Shell1.ApplyTheme.
    void apply_theme(const std::string& theme_name, ThemeEngine* engine);

    /// D-Bus method: Apply a layout configuration (JSON string).
    /// Called by straylight-wizard via org.straylight.Shell1.ApplyLayout.
    void apply_layout(const std::string& layout_json);

    /// Register on D-Bus (org.straylight.Shell1, /org/straylight/Shell1).
    void register_dbus();

private:
    // TODO: sdbus-c++ connection for D-Bus service
};

} // namespace straylight::shell
