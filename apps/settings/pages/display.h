// apps/settings/pages/display.h
// Display settings — resolution, refresh rate, scaling
#pragma once

#include <straylight/result.h>

#include <string>
#include <vector>

namespace straylight::settings {

struct DisplayMode {
    int width = 0;
    int height = 0;
    float refresh_rate = 0.0f;
    bool current = false;
};

struct DisplayOutput {
    std::string name;
    std::string description;
    std::string make;
    std::string model;
    int physical_width_mm = 0;
    int physical_height_mm = 0;
    bool enabled = true;
    int x = 0;
    int y = 0;
    float scale = 1.0f;
    int transform = 0; // 0=normal, 1=90, 2=180, 3=270

    std::vector<DisplayMode> modes;
    int current_mode_index = 0;
};

/// Display settings page.
class DisplayPage {
public:
    DisplayPage();

    /// Detect connected displays.
    void detect();

    /// Apply display configuration via wlr-output-management protocol.
    Result<void, std::string> apply();

    /// Render the display settings page in ImGui.
    void render();

    /// Get the outputs.
    [[nodiscard]] const std::vector<DisplayOutput>& outputs() const {
        return outputs_;
    }

private:
    void read_drm_outputs();
    void read_xrandr_outputs();

    std::vector<DisplayOutput> outputs_;
    int selected_output_ = 0;
    bool dirty_ = false;
};

} // namespace straylight::settings
