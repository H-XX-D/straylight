// shell/panels/left_dock.h
// Left dock panel (stub)
#pragma once

namespace straylight::shell {

/// Left-side dock panel for pinned application launchers.
/// TODO: Implement drag-and-drop reorder, running indicator dots,
///       and right-click context menus.
class LeftDock {
public:
    LeftDock() = default;
    ~LeftDock() = default;

    /// Render the left dock. Call once per frame.
    void render();

    /// Set dock visibility.
    void set_visible(bool visible) { visible_ = visible; }

    /// Returns true if the dock is visible.
    [[nodiscard]] bool is_visible() const { return visible_; }

private:
    bool visible_ = true;
};

} // namespace straylight::shell
