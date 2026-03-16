// shell/widgets/notification.cpp
// Notification toast manager implementation
#include "notification.h"

#include <straylight/log.h>

#include <imgui.h>

#include <algorithm>

namespace straylight::shell {

NotificationManager::NotificationManager() = default;
NotificationManager::~NotificationManager() = default;

uint32_t NotificationManager::notify(const std::string& app_name,
                                     const std::string& summary,
                                     const std::string& body,
                                     int expire_ms) {
    Notification notif;
    notif.id         = next_id_++;
    notif.app_name   = app_name;
    notif.summary    = summary;
    notif.body       = body;
    notif.expire_ms  = expire_ms;
    notif.created_at = ImGui::GetTime();

    // FIFO eviction if queue is full
    if (static_cast<int>(queue_.size()) >= kMaxToasts) {
        SL_DEBUG("Notification queue full, evicting oldest (id={})",
                 queue_.front().id);
        queue_.pop_front();
    }

    uint32_t id = notif.id;
    queue_.push_back(std::move(notif));
    SL_DEBUG("Notification added: id={}, summary='{}'", id, summary);

    return id;
}

void NotificationManager::close(uint32_t id) {
    auto it = std::find_if(queue_.begin(), queue_.end(),
                           [id](const Notification& n) {
                               return n.id == id;
                           });
    if (it != queue_.end()) {
        SL_DEBUG("Notification closed: id={}", id);
        queue_.erase(it);
    }
}

void NotificationManager::expire_old() {
    double now = ImGui::GetTime();
    queue_.erase(
        std::remove_if(queue_.begin(), queue_.end(),
                       [now](const Notification& n) {
                           double elapsed_ms =
                               (now - n.created_at) * 1000.0;
                           return elapsed_ms >= n.expire_ms;
                       }),
        queue_.end());
}

void NotificationManager::render() {
    expire_old();

    if (queue_.empty()) return;

    const ImGuiIO& io = ImGui::GetIO();
    constexpr float kToastWidth  = 320.0f;
    constexpr float kToastHeight = 80.0f;
    constexpr float kPadding     = 12.0f;

    float x = io.DisplaySize.x - kToastWidth - kPadding;
    float y = io.DisplaySize.y - kPadding;

    int index = 0;
    for (auto it = queue_.rbegin(); it != queue_.rend(); ++it, ++index) {
        y -= kToastHeight + kPadding;

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##Toast_%u", it->id);

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(kToastWidth, kToastHeight));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::Begin(win_id, nullptr, flags);

        // Summary (bold-like via color)
        ImGui::TextUnformatted(it->summary.c_str());
        ImGui::TextWrapped("%s", it->body.c_str());

        // Close button
        ImGui::SameLine(kToastWidth - 30.0f);
        char close_id[64];
        snprintf(close_id, sizeof(close_id), "X##close_%u", it->id);
        if (ImGui::SmallButton(close_id)) {
            close(it->id);
        }

        ImGui::End();
    }
}

int NotificationManager::count() const {
    return static_cast<int>(queue_.size());
}

const std::deque<Notification>& NotificationManager::queue() const {
    return queue_;
}

} // namespace straylight::shell
