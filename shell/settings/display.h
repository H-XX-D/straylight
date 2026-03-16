// shell/settings/display.h
// Display settings panel (stub)
#pragma once

namespace straylight::shell {

/// Display settings panel — resolution, scaling, multi-monitor layout.
/// TODO(plan5): Implement wlr-output-management-unstable-v1 integration.
class DisplaySettings {
public:
    DisplaySettings() = default;
    ~DisplaySettings() = default;

    void render();
};

} // namespace straylight::shell
