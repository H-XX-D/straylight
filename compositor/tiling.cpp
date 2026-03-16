// compositor/tiling.cpp
#include "tiling.h"
#include "view.h"
#include "workspace.h"  // LayoutMode

extern "C" {
#include <wlr/types/wlr_output_layout.h>
}

#include <algorithm>

namespace straylight::compositor {

void Tiling::arrange(const std::vector<View*>& views,
                     const wlr_box& area,
                     LayoutMode mode)
{
    if (views.empty()) return;
    switch (mode) {
        case LayoutMode::Tiling:   arrange_tiling(views, area);  break;
        case LayoutMode::Monocle:  arrange_monocle(views, area); break;
        case LayoutMode::Floating: break; // no-op
    }
}

void Tiling::arrange_tiling(const std::vector<View*>& views, const wlr_box& area) {
    const int g = gap_px_;
    const int n = static_cast<int>(views.size());

    if (n == 1) {
        views[0]->set_position(area.x + g, area.y + g);
        views[0]->set_size(area.width - 2*g, area.height - 2*g);
        return;
    }

    // Master: left column
    int master_w = static_cast<int>((area.width - 3*g) * master_ratio_);
    int stack_w  = area.width - master_w - 3*g;
    int master_x = area.x + g;
    int stack_x  = master_x + master_w + g;

    views[0]->set_position(master_x, area.y + g);
    views[0]->set_size(master_w, area.height - 2*g);

    // Stack: right column, evenly divided
    int stack_count = n - 1;
    int slot_h = (area.height - 2*g - (stack_count - 1)*g) / stack_count;
    for (int i = 0; i < stack_count; ++i) {
        int y = area.y + g + i * (slot_h + g);
        views[i+1]->set_position(stack_x, y);
        views[i+1]->set_size(stack_w, slot_h);
    }
}

void Tiling::arrange_monocle(const std::vector<View*>& views, const wlr_box& area) {
    const int g = gap_px_;
    for (auto* v : views) {
        v->set_position(area.x + g, area.y + g);
        v->set_size(area.width - 2*g, area.height - 2*g);
    }
}

void Tiling::set_master_ratio(float r) {
    master_ratio_ = std::clamp(r, 0.1f, 0.9f);
}

} // namespace straylight::compositor
