// shell/panels/bottom_dock.h
// Bottom dock panel (stub)
#pragma once

namespace straylight::shell {

/// Bottom dock panel for a macOS-style application dock.
/// TODO: Implement icon magnification hover effect, window previews,
///       and drag-to-add from app launcher.
class BottomDock {
public:
    BottomDock() = default;
    ~BottomDock() = default;

    /// Render the bottom dock. Call once per frame.
    void render();

    /// Set dock visibility.
    void set_visible(bool visible) { visible_ = visible; }

    /// Returns true if the dock is visible.
    [[nodiscard]] bool is_visible() const { return visible_; }

private:
    bool visible_ = false;  // disabled by default
};

} // namespace straylight::shell
