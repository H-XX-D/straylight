// shell/widgets/notification.h
// Desktop notification toast manager (freedesktop.org Notifications spec)
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace straylight::shell {

/// A single notification entry.
struct Notification {
    uint32_t    id           = 0;
    std::string app_name;
    std::string summary;
    std::string body;
    std::string icon;
    int         expire_ms    = 5000;  // auto-dismiss after this many ms
    double      created_at   = 0.0;   // ImGui::GetTime() at creation
};

/// Manages a queue of desktop notifications rendered as toasts.
/// Listens on org.freedesktop.Notifications D-Bus interface (stubbed
/// for now; real D-Bus integration deferred to Plan 5).
/// Max 5 concurrent toasts, FIFO eviction when full.
class NotificationManager {
public:
    static constexpr int kMaxToasts = 5;

    NotificationManager();
    ~NotificationManager();

    /// Add a new notification. Returns assigned ID.
    uint32_t notify(const std::string& app_name,
                    const std::string& summary,
                    const std::string& body,
                    int expire_ms = 5000);

    /// Close a notification by ID.
    void close(uint32_t id);

    /// Render all visible toasts. Call once per frame.
    void render();

    /// Get current notification count.
    [[nodiscard]] int count() const;

    /// Get the notification queue (for testing).
    [[nodiscard]] const std::deque<Notification>& queue() const;

private:
    std::deque<Notification> queue_;
    uint32_t next_id_ = 1;

    void expire_old();
};

} // namespace straylight::shell
