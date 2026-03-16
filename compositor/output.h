// compositor/output.h
#pragma once
#include <memory>
#include <vector>
#include <string>

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
}

namespace straylight::compositor {

class Server;

// Output wraps a single physical monitor (wlr_output).
// Owns the scene_output, frame listener, and request-state listener.
class Output {
public:
    // Called from Server::handle_new_output
    static void setup(Server& server, wlr_output* wlr_output);

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    ~Output();

    wlr_output*        wlr()    const { return output_; }
    wlr_scene_output*  scene()  const { return scene_output_; }
    const std::string& name()   const { return name_; }

private:
    Output(Server& server, wlr_output* output);

    Server&           server_;
    wlr_output*       output_       = nullptr;
    wlr_scene_output* scene_output_ = nullptr;
    std::string       name_;

    wl_listener on_frame_{};
    wl_listener on_request_state_{};
    wl_listener on_destroy_{};

    static void handle_frame(wl_listener* l, void* data);
    static void handle_request_state(wl_listener* l, void* data);
    static void handle_destroy(wl_listener* l, void* data);
};

} // namespace straylight::compositor
