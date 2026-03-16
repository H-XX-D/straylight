// compositor/input.h
#pragma once
#include <memory>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>
}

namespace straylight::compositor {

class Server;
class View;

// Per-keyboard state
struct Keyboard {
    Server*      server   = nullptr;
    wlr_keyboard* wlr     = nullptr;
    wl_listener  on_modifiers{};
    wl_listener  on_key{};
    wl_listener  on_destroy{};
};

class InputManager {
public:
    InputManager(Server& server,
                 wlr_cursor* cursor,
                 wlr_xcursor_manager* cursor_mgr,
                 wlr_seat* seat);
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Called from Server::init — registers new_input listener
    void connect();

private:
    Server&               server_;
    wlr_cursor*           cursor_;
    wlr_xcursor_manager*  cursor_mgr_;
    wlr_seat*             seat_;

    wl_listener on_new_input_{};
    wl_listener on_cursor_motion_{};
    wl_listener on_cursor_motion_absolute_{};
    wl_listener on_cursor_button_{};
    wl_listener on_cursor_axis_{};
    wl_listener on_cursor_frame_{};
    wl_listener on_request_cursor_{};
    wl_listener on_request_set_selection_{};

    std::vector<std::unique_ptr<Keyboard>> keyboards_;

    // Keyboard
    void add_keyboard(wlr_input_device* device);
    static void kb_handle_modifiers(wl_listener* l, void*);
    static void kb_handle_key(wl_listener* l, void*);
    static void kb_handle_destroy(wl_listener* l, void*);
    bool handle_compositor_keybind(xkb_keysym_t sym, uint32_t modifiers);

    // Pointer
    void add_pointer(wlr_input_device* device);

    // Cursor
    void process_cursor_motion(uint32_t time_msec);
    View* view_at(double lx, double ly,
                  wlr_surface** surface, double* sx, double* sy) const;

    static void handle_new_input(wl_listener* l, void*);
    static void handle_cursor_motion(wl_listener* l, void*);
    static void handle_cursor_motion_absolute(wl_listener* l, void*);
    static void handle_cursor_button(wl_listener* l, void*);
    static void handle_cursor_axis(wl_listener* l, void*);
    static void handle_cursor_frame(wl_listener* l, void*);
    static void handle_request_cursor(wl_listener* l, void*);
    static void handle_request_set_selection(wl_listener* l, void*);
};

} // namespace straylight::compositor
