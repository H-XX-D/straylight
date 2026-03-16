// compositor/workspace.cpp
#include "workspace.h"
#include "server.h"
#include "view.h"
#include "tiling.h"
#include <straylight/log.h>
#include <algorithm>

extern "C" {
#include <wlr/types/wlr_output_layout.h>
}

namespace straylight::compositor {

Workspace::Workspace(Server& server)
    : server_(server)
    , tiler_(std::make_unique<Tiling>())
{}

Workspace::~Workspace() = default;

void Workspace::on_view_map(View* v) {
    views_.insert(views_.begin(), v);
    if (layout_ == LayoutMode::Tiling) retile();
}

void Workspace::on_view_unmap(View* v) {
    remove_view(v);
    if (layout_ == LayoutMode::Tiling) retile();
    // Re-focus the next view
    if (!views_.empty()) focus_view(views_.front());
}

void Workspace::on_view_commit(View* /*v*/) {
    // No-op for now; geometry changes handled in retile()
}

void Workspace::on_view_request_move(View* v) {
    // Move is only meaningful in floating mode; switch if needed
    if (layout_ == LayoutMode::Tiling) {
        set_layout(LayoutMode::Floating);
    }
    // Actual pointer-driven move handled in InputManager
}

void Workspace::on_view_request_resize(View* v, uint32_t /*edges*/) {
    if (layout_ == LayoutMode::Tiling) set_layout(LayoutMode::Floating);
}

void Workspace::on_view_request_maximize(View* v) {
    wlr_box area = usable_area();
    v->set_position(area.x, area.y);
    v->set_size(area.width, area.height);
    wlr_xdg_toplevel_set_maximized(v->toplevel(), true);
}

void Workspace::on_view_request_fullscreen(View* v) {
    // Get primary output dimensions
    wlr_output* output = wlr_output_layout_get_center_output(server_.output_layout());
    if (!output) return;
    v->set_position(0, 0);
    v->set_size(output->width, output->height);
    wlr_xdg_toplevel_set_fullscreen(v->toplevel(), true);
}

void Workspace::focus_view(View* v) {
    if (!v) return;
    // Move to front of views list
    remove_view(v);
    views_.insert(views_.begin(), v);
    v->focus();
}

void Workspace::focus_next() {
    if (views_.size() < 2) return;
    focus_view(views_[1]);
}

void Workspace::focus_prev() {
    if (views_.empty()) return;
    focus_view(views_.back());
}

View* Workspace::focused() const {
    return views_.empty() ? nullptr : views_.front();
}

void Workspace::set_layout(LayoutMode mode) {
    layout_ = mode;
    retile();
}

void Workspace::retile() {
    if (views_.empty()) return;
    wlr_box area = usable_area();
    tiler_->arrange(views_, area, layout_);
}

void Workspace::remove_view(View* v) {
    views_.erase(std::remove(views_.begin(), views_.end(), v), views_.end());
    float_states_.erase(
        std::remove_if(float_states_.begin(), float_states_.end(),
            [v](const FloatState& s){ return s.view == v; }),
        float_states_.end());
}

wlr_box Workspace::usable_area() const {
    wlr_output* output = wlr_output_layout_get_center_output(server_.output_layout());
    if (!output) return {0, 0, 1920, 1080};
    wlr_box box{};
    wlr_output_layout_get_box(server_.output_layout(), output, &box);
    // TODO: subtract layer-shell reserved areas (Plan 4 shell panels)
    return box;
}

} // namespace straylight::compositor
