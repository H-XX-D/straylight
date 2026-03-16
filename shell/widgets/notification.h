// shell/widgets/notification.h
// Desktop notification manager — urgency levels, actions, history, DND
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace straylight::shell {

/// Notification urgency per freedesktop.org spec.
enum class Urgency : uint8_t { Low = 0, Normal = 1, Critical = 2 };

/// A single action button attached to a notification.
struct NotifAction {
    std::string key;    // e.g. "reply", "dismiss"
    std::string label;  // e.g. "Reply", "Dismiss"
};

/// A desktop notification entry.
struct Notification {
    uint32_t    id          = 0;
    std::string app_name;
    std::string summary;
    std::string body;
    std::string icon;
    Urgency     urgency     = Urgency::Normal;
    int         expire_ms   = 5000;   // -1 = persistent (Critical default)
    double      created_at  = 0.0;    // ImGui::GetTime() at creation
    std::vector<NotifAction> actions;
    bool        resident    = false;  // stay in history after dismiss
};

/// Manages a queue of desktop notifications rendered as toasts.
/// Supports urgency-driven styling, action buttons, DND mode,
/// notification history, and animated slide-in.
///
/// Max kMaxToasts concurrent toasts, FIFO eviction when full.
/// History capped at kMaxHistory entries.
class NotificationManager {
public:
    static constexpr int kMaxToasts  = 5;
    static constexpr int kMaxHistory = 100;

    using ActionCallback = std::function<void(uint32_t id, std::string_view action_key)>;

    NotificationManager();
    ~NotificationManager();

    /// Send a fully specified notification. Returns assigned ID.
    uint32_t notify(Notification notif);

    /// Convenience: simple text notification.
    uint32_t notify(const std::string& app,
                    const std::string& summary,
                    const std::string& body,
                    Urgency urgency  = Urgency::Normal,
                    int expire_ms    = 5000);

    /// Close a notification by ID.
    void close(uint32_t id);

    /// Close all active toasts.
    void close_all();

    /// Register callback for action button clicks.
    void set_action_callback(ActionCallback cb);

    /// Render active toasts. Call once per frame.
    void render();

    /// Render the notification history panel.
    void render_history(bool* p_open);

    /// Toggle Do Not Disturb (suppresses visual toasts, still queues).
    void set_dnd(bool enabled);
    [[nodiscard]] bool dnd() const;

    [[nodiscard]] int count() const;
    [[nodiscard]] const std::deque<Notification>& queue() const;
    [[nodiscard]] const std::vector<Notification>& history() const;

private:
    std::deque<Notification>  queue_;
    std::vector<Notification> history_;
    uint32_t     next_id_ = 1;
    bool         dnd_     = false;
    ActionCallback action_cb_;

    void expire_old();
    void push_to_history(const Notification& n);

    struct ImVec4;
    static struct ImVec4 urgency_accent(Urgency u);
};

} // namespace straylight::shell
