// compositor/tiling.h
#pragma once
#include <vector>
#include <cstdint>

extern "C" { struct wlr_box; }

namespace straylight::compositor {

class View;
enum class LayoutMode;

// Tiling implements the master-stack layout algorithm.
// Pure geometry — no wlroots state, fully unit-testable.
class Tiling {
public:
    // Arrange views within the given usable area.
    // LayoutMode::Floating: no-op (views keep their current positions).
    // LayoutMode::Monocle:  all views overlap, only focused is visible.
    // LayoutMode::Tiling:   master on left, stack on right.
    void arrange(const std::vector<View*>& views,
                 const wlr_box& area,
                 LayoutMode mode);

    // Master split ratio (0.0-1.0). Default: 0.55
    float master_ratio() const { return master_ratio_; }
    void  set_master_ratio(float r);

    // Gap between windows in pixels. Default: 4
    int gap() const { return gap_px_; }
    void set_gap(int px) { gap_px_ = px; }

private:
    float master_ratio_ = 0.55f;
    int   gap_px_       = 4;

    void arrange_tiling(const std::vector<View*>& views, const wlr_box& area);
    void arrange_monocle(const std::vector<View*>& views, const wlr_box& area);
};

} // namespace straylight::compositor
