// compositor/input.cpp
#include "input.h"
#include "server.h"
#include "view.h"
#include "workspace.h"
#include <straylight/log.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/edges.h>
}

#include <cstring>

#define input_from_listener(ptr, field) \
    reinterpret_cast<InputManager*>(reinterpret_cast<char*>(ptr) - offsetof(InputManager, field))
#define kb_from_listener(ptr, field) \
    reinterpret_cast<Keyboard*>(reinterpret_cast<char*>(ptr) - offsetof(Keyboard, field))

namespace straylight::compositor {

InputManager::InputManager(Server& server,
                           wlr_cursor* cursor,
                           wlr_xcursor_manager* cursor_mgr,
                           wlr_seat* seat)
    : server_(server), cursor_(cursor), cursor_mgr_(cursor_mgr), seat_(seat)
{
    connect();
}

InputManager::~InputManager() {
    wl_list_remove(&on_new_input_.link);
    wl_list_remove(&on_cursor_motion_.link);
    wl_list_remove(&on_cursor_motion_absolute_.link);
    wl_list_remove(&on_cursor_button_.link);
    wl_list_remove(&on_cursor_axis_.link);
    wl_list_remove(&on_cursor_frame_.link);
    wl_list_remove(&on_request_cursor_.link);
    wl_list_remove(&on_request_set_selection_.link);
}

void InputManager::connect() {
    on_new_input_.notify = handle_new_input;
    wl_signal_add(&server_.backend()->events.new_input, &on_new_input_);

    on_cursor_motion_.notify = handle_cursor_motion;
    wl_signal_add(&cursor_->events.motion, &on_cursor_motion_);

    on_cursor_motion_absolute_.notify = handle_cursor_motion_absolute;
    wl_signal_add(&cursor_->events.motion_absolute, &on_cursor_motion_absolute_);

    on_cursor_button_.notify = handle_cursor_button;
    wl_signal_add(&cursor_->events.button, &on_cursor_button_);

    on_cursor_axis_.notify = handle_cursor_axis;
    wl_signal_add(&cursor_->events.axis, &on_cursor_axis_);

    on_cursor_frame_.notify = handle_cursor_frame;
    wl_signal_add(&cursor_->events.frame, &on_cursor_frame_);

    on_request_cursor_.notify = handle_request_cursor;
    wl_signal_add(&seat_->events.request_set_cursor, &on_request_cursor_);

    on_request_set_selection_.notify = handle_request_set_selection;
    wl_signal_add(&seat_->events.request_set_selection, &on_request_set_selection_);
}

// --- Keyboard ---

void InputManager::add_keyboard(wlr_input_device* device) {
    auto kb = std::make_unique<Keyboard>();
    kb->server = &server_;
    kb->wlr    = wlr_keyboard_from_input_device(device);

    // Apply xkb keymap from environment (LANG, XKB_DEFAULT_*)
    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_keymap*  map = xkb_keymap_new_from_names(ctx, nullptr,
                           XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(kb->wlr, map);
    xkb_keymap_unref(map);
    xkb_context_unref(ctx);
    wlr_keyboard_set_repeat_info(kb->wlr, 25, 600);

    kb->on_modifiers.notify = kb_handle_modifiers;
    wl_signal_add(&kb->wlr->events.modifiers, &kb->on_modifiers);

    kb->on_key.notify = kb_handle_key;
    wl_signal_add(&kb->wlr->events.key, &kb->on_key);

    kb->on_destroy.notify = kb_handle_destroy;
    wl_signal_add(&device->events.destroy, &kb->on_destroy);

    wlr_seat_set_keyboard(seat_, kb->wlr);
    keyboards_.push_back(std::move(kb));
}

void InputManager::add_pointer(wlr_input_device* device) {
    wlr_cursor_attach_input_device(cursor_, device);
}

bool InputManager::handle_compositor_keybind(xkb_keysym_t sym, uint32_t mods) {
    // Super+Q = close focused window
    // Super+Tab = focus next
    // Super+Shift+Tab = focus prev
    // Super+T = toggle layout
    const uint32_t SUPER = WLR_MODIFIER_LOGO;
    const uint32_t SHIFT = WLR_MODIFIER_SHIFT;

    if (mods == SUPER) {
        switch (sym) {
            case XKB_KEY_q:
                if (auto* v = server_.workspace().focused()) v->close();
                return true;
            case XKB_KEY_Tab:
                server_.workspace().focus_next();
                return true;
            case XKB_KEY_t:
                server_.workspace().set_layout(
                    server_.workspace().layout() == LayoutMode::Tiling
                        ? LayoutMode::Floating : LayoutMode::Tiling);
                return true;
        }
    }
    if (mods == (SUPER | SHIFT)) {
        if (sym == XKB_KEY_Tab) { server_.workspace().focus_prev(); return true; }
        if (sym == XKB_KEY_q)   {
            wl_display_terminate(server_.display());
            return true;
        }
    }
    return false;
}

void InputManager::kb_handle_modifiers(wl_listener* l, void* /*data*/) {
    auto* kb = kb_from_listener(l, on_modifiers);
    wlr_seat_set_keyboard(kb->server->seat(), kb->wlr);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat(), &kb->wlr->modifiers);
}

void InputManager::kb_handle_key(wl_listener* l, void* data) {
    auto* kb    = kb_from_listener(l, on_key);
    auto* event = static_cast<wlr_keyboard_key_event*>(data);

    uint32_t keycode  = event->keycode + 8;
    const xkb_keysym_t* syms;
    int nsyms = xkb_state_key_get_syms(kb->wlr->xkb_state, keycode, &syms);
    bool handled = false;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr);
        for (int i = 0; i < nsyms; ++i) {
            if (kb->server->input().handle_compositor_keybind(syms[i], mods)) {
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(kb->server->seat(), kb->wlr);
        wlr_seat_keyboard_notify_key(kb->server->seat(),
            event->time_msec, event->keycode, event->state);
    }
}

void InputManager::kb_handle_destroy(wl_listener* l, void* /*data*/) {
    auto* kb = kb_from_listener(l, on_destroy);
    auto& keyboards = kb->server->input().keyboards_;
    keyboards.erase(
        std::remove_if(keyboards.begin(), keyboards.end(),
            [kb](const auto& up){ return up.get() == kb; }),
        keyboards.end());
}

// --- Cursor / Pointer ---

void InputManager::process_cursor_motion(uint32_t time_msec) {
    wlr_surface* surface = nullptr;
    double sx, sy;
    View* v = view_at(cursor_->x, cursor_->y, &surface, &sx, &sy);

    if (!v) {
        wlr_xcursor_manager_set_cursor_image(cursor_mgr_, "left_ptr", cursor_);
        wlr_seat_pointer_clear_focus(seat_);
    } else {
        wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
    }
}

View* InputManager::view_at(double lx, double ly,
                             wlr_surface** surface,
                             double* sx, double* sy) const {
    wlr_scene_node* node = wlr_scene_node_at(
        &server_.scene()->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;

    auto* scene_buf = wlr_scene_buffer_from_node(node);
    auto* scene_surface = wlr_scene_surface_try_from_buffer(scene_buf);
    if (!scene_surface) return nullptr;

    *surface = scene_surface->surface;
    wlr_scene_tree* tree = node->parent;
    while (tree && !tree->node.data) tree = tree->node.parent;
    if (!tree) return nullptr;

    return static_cast<View*>(tree->node.data);
}

void InputManager::handle_new_input(wl_listener* l, void* data) {
    auto* self   = input_from_listener(l, on_new_input_);
    auto* device = static_cast<wlr_input_device*>(data);

    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD: self->add_keyboard(device); break;
        case WLR_INPUT_DEVICE_POINTER:  self->add_pointer(device);  break;
        default: break;
    }

    // Update seat capabilities
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!self->keyboards_.empty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(self->seat_, caps);
}

void InputManager::handle_cursor_motion(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_motion_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    wlr_cursor_move(self->cursor_, &event->pointer->base,
                    event->delta_x, event->delta_y);
    self->process_cursor_motion(event->time_msec);
}

void InputManager::handle_cursor_motion_absolute(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_motion_absolute_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    wlr_cursor_warp_absolute(self->cursor_, &event->pointer->base,
                             event->x, event->y);
    self->process_cursor_motion(event->time_msec);
}

void InputManager::handle_cursor_button(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_button_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);

    wlr_seat_pointer_notify_button(self->seat_,
        event->time_msec, event->button, event->state);

    if (event->state == WLR_BUTTON_PRESSED) {
        wlr_surface* surface = nullptr;
        double sx, sy;
        View* v = self->view_at(self->cursor_->x, self->cursor_->y,
                                &surface, &sx, &sy);
        if (v) self->server_.workspace().focus_view(v);
    }
}

void InputManager::handle_cursor_axis(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_cursor_axis_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    wlr_seat_pointer_notify_axis(self->seat_,
        event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source,
        event->relative_direction);
}

void InputManager::handle_cursor_frame(wl_listener* l, void* /*data*/) {
    auto* self = input_from_listener(l, on_cursor_frame_);
    wlr_seat_pointer_notify_frame(self->seat_);
}

void InputManager::handle_request_cursor(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_request_cursor_);
    auto* event = static_cast<wlr_seat_pointer_request_set_cursor_event*>(data);
    if (self->seat_->pointer_state.focused_client == event->seat_client) {
        wlr_cursor_set_surface(self->cursor_,
            event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void InputManager::handle_request_set_selection(wl_listener* l, void* data) {
    auto* self  = input_from_listener(l, on_request_set_selection_);
    auto* event = static_cast<wlr_seat_request_set_selection_event*>(data);
    wlr_seat_set_selection(self->seat_, event->source, event->serial);
}

} // namespace straylight::compositor
