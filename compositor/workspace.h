// compositor/workspace.h
#pragma once
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>

extern "C" {
#include <wlr/util/box.h>
}

namespace straylight::compositor {

class Server;
class View;
class Tiling;

enum class LayoutMode { Tiling, Floating, Monocle };

class Workspace {
public:
    explicit Workspace(Server& server);
    ~Workspace();

    // Called by View
    void on_view_map(View* v);
    void on_view_unmap(View* v);
    void on_view_commit(View* v);
    void on_view_request_move(View* v);
    void on_view_request_resize(View* v, uint32_t edges);
    void on_view_request_maximize(View* v);
    void on_view_request_fullscreen(View* v);

    // Focus management
    void focus_view(View* v);
    void focus_next();
    void focus_prev();
    View* focused() const;

    // Layout
    void set_layout(LayoutMode mode);
    LayoutMode layout() const { return layout_; }
    void retile(); // Re-run tiling algorithm

    // Window list
    const std::vector<View*>& views() const { return views_; }

private:
    Server&              server_;
    std::vector<View*>   views_;     // mapped views, front = most recently focused
    LayoutMode           layout_     = LayoutMode::Tiling;
    std::unique_ptr<Tiling> tiler_;

    // Floating state for a view
    struct FloatState {
        View* view;
        int x, y, w, h;
    };
    std::vector<FloatState> float_states_;

    void remove_view(View* v);
    wlr_box usable_area() const;  // Output area minus layer-shell reservations
};

} // namespace straylight::compositor
