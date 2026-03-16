// compositor/ipc.cpp
#include "ipc.h"
#include "server.h"
#include "workspace.h"
#include "session_lock.h"
#include "tiling.h"
#include <straylight/log.h>
#include <nlohmann/json.hpp>

namespace straylight::compositor {

using json = nlohmann::json;

CompositorIpc::CompositorIpc(Server& server)
    : server_(server)
{
    connect_to_core();
}

CompositorIpc::~CompositorIpc() = default;

void CompositorIpc::connect_to_core() {
    const std::string socket_path = "/run/straylight/core.sock";

    auto client = std::make_unique<straylight::IpcClient>();
    auto result = client->connect(socket_path);
    if (!result.has_value()) {
        SL_WARN("CompositorIpc: cannot connect to core at {} — "
                "running without IPC", socket_path);
        return;
    }

    client_    = std::move(client);
    connected_ = true;

    // Register command handler on wl_event_loop fd
    int fd = client_->fd();
    wl_event_loop_add_fd(
        wl_display_get_event_loop(server_.display()),
        fd, WL_EVENT_READABLE,
        [](int /*fd*/, uint32_t /*mask*/, void* data) -> int {
            auto* self = static_cast<CompositorIpc*>(data);
            auto msg = self->client_->receive();
            if (!msg.has_value()) return 0;
            try {
                auto j = json::parse(msg.value());
                CompositorCommand cmd;
                std::string type_str = j.value("type", "");
                if      (type_str == "FocusApp")       cmd.type = CompositorCommand::Type::FocusApp;
                else if (type_str == "CloseApp")       cmd.type = CompositorCommand::Type::CloseApp;
                else if (type_str == "SetLayout")      cmd.type = CompositorCommand::Type::SetLayout;
                else if (type_str == "SetMasterRatio") cmd.type = CompositorCommand::Type::SetMasterRatio;
                else if (type_str == "LockSession")    cmd.type = CompositorCommand::Type::LockSession;
                else if (type_str == "UnlockSession")  cmd.type = CompositorCommand::Type::UnlockSession;
                else if (type_str == "Quit")           cmd.type = CompositorCommand::Type::Quit;
                else return 0;

                cmd.app_id = j.value("app_id", "");
                cmd.layout = j.value("layout", "");
                cmd.value  = j.value("value",  0.0f);
                self->dispatch_command(cmd);
            } catch (const json::exception& e) {
                SL_ERROR("CompositorIpc: JSON parse error: {}", e.what());
            }
            return 0;
        },
        this);

    SL_INFO("CompositorIpc: connected to core at {}", socket_path);
}

void CompositorIpc::send(const CompositorEvent& event) {
    if (!connected_) return;

    json j;
    switch (event.type) {
        case CompositorEvent::Type::WindowMapped:
            j = {{"type","WindowMapped"},{"app_id",event.app_id},{"title",event.title}};
            break;
        case CompositorEvent::Type::WindowUnmapped:
            j = {{"type","WindowUnmapped"},{"app_id",event.app_id}};
            break;
        case CompositorEvent::Type::OutputAdded:
            j = {{"type","OutputAdded"},{"name",event.output_name},
                 {"width",event.width},{"height",event.height}};
            break;
        case CompositorEvent::Type::OutputRemoved:
            j = {{"type","OutputRemoved"},{"name",event.output_name}};
            break;
        case CompositorEvent::Type::SessionLocked:
            j = {{"type","SessionLocked"}};
            break;
        case CompositorEvent::Type::SessionUnlocked:
            j = {{"type","SessionUnlocked"}};
            break;
    }

    auto r = client_->send(j.dump());
    if (!r.has_value()) {
        SL_WARN("CompositorIpc: send failed: {}", r.error());
    }
}

void CompositorIpc::dispatch_command(const CompositorCommand& cmd) {
    using T = CompositorCommand::Type;
    auto& ws = server_.workspace();

    switch (cmd.type) {
        case T::FocusApp: {
            for (auto* v : ws.views()) {
                if (v->app_id() == cmd.app_id) { ws.focus_view(v); break; }
            }
            break;
        }
        case T::CloseApp: {
            for (auto* v : ws.views()) {
                if (v->app_id() == cmd.app_id) { v->close(); break; }
            }
            break;
        }
        case T::SetLayout: {
            LayoutMode mode = LayoutMode::Tiling;
            if      (cmd.layout == "floating") mode = LayoutMode::Floating;
            else if (cmd.layout == "monocle")  mode = LayoutMode::Monocle;
            ws.set_layout(mode);
            break;
        }
        case T::SetMasterRatio:
            // Access tiler through workspace (add accessor if needed)
            break;
        case T::LockSession:
            server_.session_lock().lock();
            break;
        case T::UnlockSession:
            server_.session_lock().unlock();
            break;
        case T::Quit:
            SL_INFO("Compositor quit requested by core");
            wl_display_terminate(server_.display());
            break;
    }
}

} // namespace straylight::compositor
