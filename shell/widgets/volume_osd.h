// shell/widgets/volume_osd.h
// Volume on-screen display overlay (stub)
#pragma once

namespace straylight::shell {

/// Volume OSD overlay — displays a transient volume bar when the user
/// adjusts system volume via PipeWire.
/// TODO(plan5): Implement PipeWire volume monitoring and OSD rendering.
class VolumeOsd {
public:
    VolumeOsd() = default;
    ~VolumeOsd() = default;

    /// Show the OSD with the given volume level (0.0–1.0).
    void show(float level);

    /// Render the OSD if visible. Call once per frame.
    void render();

    /// Returns true if the OSD is currently displayed.
    [[nodiscard]] bool is_visible() const { return false; }

private:
    float level_   = 0.0f;
    bool  visible_ = false;
};

} // namespace straylight::shell
