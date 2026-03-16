// compositor/view.h
#pragma once
#include <memory>
#include <string>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

class Server;
class Workspace;

// View represents a single xdg_toplevel application window.
// Owns the scene tree node for the window and all its listeners.
class View {
public:
    static void create(Server& server, wlr_xdg_toplevel* toplevel);

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    ~View();

    wlr_xdg_toplevel*  toplevel()    const { return toplevel_; }
    wlr_scene_tree*    scene_tree()  const { return scene_tree_; }
    bool               mapped()      const { return mapped_; }
    bool               focused()     const { return focused_; }
    std::string        title()       const;
    std::string        app_id()      const;

    void focus(wlr_surface* surface = nullptr);
    void set_position(int x, int y);
    void set_size(int w, int h);
    void close();

private:
    View(Server& server, wlr_xdg_toplevel* toplevel);

    Server&           server_;
    wlr_xdg_toplevel* toplevel_   = nullptr;
    wlr_scene_tree*   scene_tree_ = nullptr;
    bool              mapped_     = false;
    bool              focused_    = false;

    wl_listener on_map_{};
    wl_listener on_unmap_{};
    wl_listener on_commit_{};
    wl_listener on_destroy_{};
    wl_listener on_request_move_{};
    wl_listener on_request_resize_{};
    wl_listener on_request_maximize_{};
    wl_listener on_request_fullscreen_{};
    wl_listener on_set_title_{};

    static void handle_map(wl_listener* l, void*);
    static void handle_unmap(wl_listener* l, void*);
    static void handle_commit(wl_listener* l, void*);
    static void handle_destroy(wl_listener* l, void*);
    static void handle_request_move(wl_listener* l, void*);
    static void handle_request_resize(wl_listener* l, void*);
    static void handle_request_maximize(wl_listener* l, void*);
    static void handle_request_fullscreen(wl_listener* l, void*);
    static void handle_set_title(wl_listener* l, void*);
};

} // namespace straylight::compositor
