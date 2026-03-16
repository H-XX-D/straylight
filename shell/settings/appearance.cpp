// shell/settings/appearance.cpp
// Appearance settings + D-Bus interface stubs
#include "appearance.h"
#include "../themes/theme_engine.h"

#include <straylight/log.h>

#include <imgui.h>

namespace straylight::shell {

void AppearanceSettings::render() {
    // TODO: Theme selector, wallpaper picker, font size slider, accent color
}

void AppearanceSettings::apply_theme(const std::string& theme_name,
                                     ThemeEngine* engine) {
    if (!engine) {
        SL_ERROR("ApplyTheme: ThemeEngine is null");
        return;
    }

    auto result = engine->load_by_name(theme_name);
    if (result.has_value()) {
        ImGuiStyle& style = ImGui::GetStyle();
        engine->apply(style);
        SL_INFO("D-Bus ApplyTheme: applied '{}'", theme_name);
    } else {
        SL_ERROR("D-Bus ApplyTheme: failed to load '{}': {}",
                 theme_name, result.error().message());
    }
}

void AppearanceSettings::apply_layout(const std::string& layout_json) {
    SL_INFO("D-Bus ApplyLayout: {}", layout_json);
    // TODO: Parse layout_json, update dock visibility, recreate layer surfaces
}

void AppearanceSettings::register_dbus() {
    // TODO: Register org.straylight.Shell1 on session bus via sdbus-c++
    //       Methods: ApplyTheme(s), ApplyLayout(s)
    //       Path: /org/straylight/Shell1
    SL_INFO("D-Bus service registration stubbed (needs sdbus-c++)");
}

} // namespace straylight::shell
