// compositor/output.cpp
#include "output.h"
#include "server.h"
#include <straylight/log.h>

extern "C" {
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_management_v1.h>
}

#define output_from_listener(ptr, field) \
    reinterpret_cast<Output*>(reinterpret_cast<char*>(ptr) - offsetof(Output, field))

namespace straylight::compositor {

void Output::setup(Server& server, wlr_output* wlr_out) {
    wlr_output_init_render(wlr_out, server.allocator(), server.renderer());

    // Pick preferred mode
    if (!wl_list_empty(&wlr_out->modes)) {
        wlr_output_state state{};
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        wlr_output_mode* mode = wlr_output_preferred_mode(wlr_out);
        if (mode) wlr_output_state_set_mode(&state, mode);
        wlr_output_commit_state(wlr_out, &state);
        wlr_output_state_finish(&state);
    }

    // Transfer ownership to scene — heap-allocated, self-destructs on wlr_output destroy
    new Output(server, wlr_out);
}

Output::Output(Server& server, wlr_output* output)
    : server_(server), output_(output), name_(output->name)
{
    // Add to output layout at (0,0) — auto-arrangement
    wlr_output_layout_add_auto(server_.output_layout(), output);

    scene_output_ = wlr_scene_output_create(server_.scene(), output);

    on_frame_.notify = handle_frame;
    wl_signal_add(&output->events.frame, &on_frame_);

    on_request_state_.notify = handle_request_state;
    wl_signal_add(&output->events.request_state, &on_request_state_);

    on_destroy_.notify = handle_destroy;
    wl_signal_add(&output->events.destroy, &on_destroy_);

    SL_INFO("Output added: {} ({}x{})",
        name_,
        output->width, output->height);
}

Output::~Output() {
    wl_list_remove(&on_frame_.link);
    wl_list_remove(&on_request_state_.link);
    wl_list_remove(&on_destroy_.link);
}

void Output::handle_frame(wl_listener* l, void* /*data*/) {
    auto* self = output_from_listener(l, on_frame_);
    wlr_scene_output_commit(self->scene_output_, nullptr);

    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(self->scene_output_, &now);
}

void Output::handle_request_state(wl_listener* l, void* data) {
    auto* self  = output_from_listener(l, on_request_state_);
    auto* event = static_cast<wlr_output_event_request_state*>(data);
    wlr_output_commit_state(self->output_, event->state);
}

void Output::handle_destroy(wl_listener* l, void* /*data*/) {
    auto* self = output_from_listener(l, on_destroy_);
    SL_INFO("Output removed: {}", self->name_);
    delete self; // self-destruct
}

} // namespace straylight::compositor
