// compositor/ipc.h
#pragma once
#include <memory>
#include <string>
#include <functional>

#include <straylight/ipc.h>  // IpcClient from libstraylight-common

namespace straylight::compositor {

class Server;

// IPC messages compositor sends to straylight-core
struct CompositorEvent {
    enum class Type {
        WindowMapped,       // app_id + title
        WindowUnmapped,     // app_id
        OutputAdded,        // name + resolution
        OutputRemoved,      // name
        SessionLocked,
        SessionUnlocked,
    };
    Type        type;
    std::string app_id;
    std::string title;
    std::string output_name;
    int         width  = 0;
    int         height = 0;
};

// Commands compositor receives from straylight-core
struct CompositorCommand {
    enum class Type {
        FocusApp,           // app_id
        CloseApp,           // app_id
        SetLayout,          // layout name: "tiling" | "floating" | "monocle"
        SetMasterRatio,     // value 0.0-1.0
        LockSession,
        UnlockSession,
        Quit,
    };
    Type        type;
    std::string app_id;
    std::string layout;
    float       value = 0.0f;
};

class CompositorIpc {
public:
    explicit CompositorIpc(Server& server);
    ~CompositorIpc();

    CompositorIpc(const CompositorIpc&) = delete;
    CompositorIpc& operator=(const CompositorIpc&) = delete;

    // Send event to core (non-blocking, queued)
    void send(const CompositorEvent& event);

private:
    Server& server_;
    std::unique_ptr<straylight::IpcClient> client_;
    bool connected_ = false;

    void connect_to_core();
    void dispatch_command(const CompositorCommand& cmd);
};

} // namespace straylight::compositor
