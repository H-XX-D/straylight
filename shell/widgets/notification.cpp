// shell/widgets/notification.cpp
// Notification toast manager — urgency, actions, history, DND, slide-in animation
#include "notification.h"

#include <straylight/log.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace straylight::shell {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

NotificationManager::NotificationManager() = default;
NotificationManager::~NotificationManager() = default;

// ---------------------------------------------------------------------------
// notify()
// ---------------------------------------------------------------------------

uint32_t NotificationManager::notify(Notification notif) {
    notif.id         = next_id_++;
    notif.created_at = ImGui::GetTime();

    // Critical notifications are persistent by default
    if (notif.urgency == Urgency::Critical && notif.expire_ms == 5000) {
        notif.expire_ms = -1;
    }
    // Low urgency default shorter display
    if (notif.urgency == Urgency::Low && notif.expire_ms == 5000) {
        notif.expire_ms = 3000;
    }

    uint32_t id = notif.id;

    if (!dnd_) {
        // FIFO eviction — never evict Critical toasts
        while (static_cast<int>(queue_.size()) >= kMaxToasts) {
            // Find first non-critical to evict
            auto evict = std::find_if(queue_.begin(), queue_.end(),
                [](const Notification& n) { return n.urgency != Urgency::Critical; });
            if (evict != queue_.end()) {
                push_to_history(*evict);
                queue_.erase(evict);
            } else {
                // All are Critical — drop the oldest
                push_to_history(queue_.front());
                queue_.pop_front();
            }
        }
        queue_.push_back(std::move(notif));
    } else {
        // DND: queue for history only
        push_to_history(notif);
    }

    SL_DEBUG("Notification {}: id={}, urgency={}, summary='{}'",
             dnd_ ? "queued (DND)" : "added", id,
             static_cast<int>(notif.urgency), notif.summary);
    return id;
}

uint32_t NotificationManager::notify(const std::string& app,
                                     const std::string& summary,
                                     const std::string& body,
                                     Urgency urgency,
                                     int expire_ms) {
    Notification n;
    n.app_name  = app;
    n.summary   = summary;
    n.body      = body;
    n.urgency   = urgency;
    n.expire_ms = expire_ms;
    return notify(std::move(n));
}

// ---------------------------------------------------------------------------
// close / close_all
// ---------------------------------------------------------------------------

void NotificationManager::close(uint32_t id) {
    auto it = std::find_if(queue_.begin(), queue_.end(),
                            [id](const Notification& n) { return n.id == id; });
    if (it != queue_.end()) {
        push_to_history(*it);
        queue_.erase(it);
        SL_DEBUG("Notification closed: id={}", id);
    }
}

void NotificationManager::close_all() {
    for (auto& n : queue_) push_to_history(n);
    queue_.clear();
}

void NotificationManager::set_action_callback(ActionCallback cb) {
    action_cb_ = std::move(cb);
}

void NotificationManager::set_dnd(bool enabled) {
    dnd_ = enabled;
    SL_INFO("Do Not Disturb: {}", enabled ? "on" : "off");
}

bool NotificationManager::dnd() const { return dnd_; }

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void NotificationManager::expire_old() {
    double now = ImGui::GetTime();
    std::vector<uint32_t> expired;
    for (const auto& n : queue_) {
        if (n.urgency == Urgency::Critical) continue;  // never auto-expire
        if (n.expire_ms < 0) continue;
        double elapsed_ms = (now - n.created_at) * 1000.0;
        if (elapsed_ms >= static_cast<double>(n.expire_ms)) {
            expired.push_back(n.id);
        }
    }
    for (uint32_t id : expired) {
        close(id);
    }
}

void NotificationManager::push_to_history(const Notification& n) {
    history_.push_back(n);
    if (static_cast<int>(history_.size()) > kMaxHistory) {
        history_.erase(history_.begin());
    }
}

ImVec4 NotificationManager::urgency_accent(Urgency u) {
    switch (u) {
        case Urgency::Low:      return ImVec4(0.40f, 0.40f, 0.45f, 1.0f);
        case Urgency::Normal:   return ImVec4(0.71f, 0.75f, 1.00f, 1.0f); // accent-ish
        case Urgency::Critical: return ImVec4(0.95f, 0.55f, 0.66f, 1.0f); // error red
        default:                return ImVec4(0.71f, 0.75f, 1.00f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// render()  — slide-in toasts with urgency styling and action buttons
// ---------------------------------------------------------------------------

void NotificationManager::render() {
    expire_old();

    if (queue_.empty() || dnd_) return;

    const ImGuiIO& io = ImGui::GetIO();
    constexpr float kToastWidth  = 340.0f;
    constexpr float kToastHeight = 90.0f;  // base; may grow with actions
    constexpr float kPadding     = 12.0f;
    constexpr float kSlideMs     = 200.0f;

    const float screen_right = io.DisplaySize.x;
    const float final_x      = screen_right - kToastWidth - kPadding;

    double now = ImGui::GetTime();

    float y_base = io.DisplaySize.y - kPadding;

    // Render in reverse order so newest is at the bottom
    for (auto it = queue_.rbegin(); it != queue_.rend(); ++it) {
        const auto& n = *it;

        // Compute height accounting for action buttons
        float height = kToastHeight;
        if (!n.actions.empty()) {
            height += 28.0f;  // extra row for buttons
        }
        y_base -= height + kPadding;

        // Slide-in animation: interpolate x from screen_right to final_x
        double age_ms     = (now - n.created_at) * 1000.0;
        float  t_slide    = static_cast<float>(
            std::min(age_ms / kSlideMs, 1.0));
        float  ease       = t_slide * (2.0f - t_slide);  // ease-out quad
        float  current_x  = screen_right + (final_x - screen_right) * ease;

        // Fade-out alpha for non-persistent toasts near their expiry
        float alpha = 1.0f;
        if (n.expire_ms > 0 && n.urgency != Urgency::Critical) {
            double total_s     = n.expire_ms / 1000.0;
            double age_s       = (now - n.created_at);
            double fade_start  = total_s - 0.5;
            if (age_s > fade_start) {
                alpha = static_cast<float>(
                    1.0 - (age_s - fade_start) / 0.5);
                alpha = std::max(0.0f, std::min(1.0f, alpha));
            }
        }

        // Critical: pulsing glow (alpha ping-pong)
        if (n.urgency == Urgency::Critical) {
            float pulse = 0.75f + 0.25f * static_cast<float>(
                std::sin(now * 4.0));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, pulse);
        } else {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        }

        // Border color by urgency
        ImVec4 border_col = urgency_accent(n.urgency);
        ImGui::PushStyleColor(ImGuiCol_Border, border_col);

        char win_id[64];
        snprintf(win_id, sizeof(win_id), "##Toast_%u", n.id);

        ImGui::SetNextWindowPos(ImVec2(current_x, y_base));
        ImGui::SetNextWindowSize(ImVec2(kToastWidth, height));

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar  |
            ImGuiWindowFlags_NoResize    |
            ImGuiWindowFlags_NoMove      |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse  |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin(win_id, nullptr, flags);

        // Header row: app name (dim) + close button
        ImGui::TextDisabled("%s", n.app_name.c_str());
        ImGui::SameLine(kToastWidth - 34.0f);

        char close_id[64];
        snprintf(close_id, sizeof(close_id), "x##close_%u", n.id);

        // We store the id to close after iteration
        uint32_t to_close = 0;
        if (ImGui::SmallButton(close_id)) {
            to_close = n.id;
        }

        // Summary
        ImGui::PushStyleColor(ImGuiCol_Text, urgency_accent(n.urgency));
        ImGui::TextUnformatted(n.summary.c_str());
        ImGui::PopStyleColor();

        // Body
        ImGui::TextWrapped("%s", n.body.c_str());

        // Action buttons
        if (!n.actions.empty()) {
            ImGui::Separator();
            uint32_t acted_id = 0;
            std::string acted_key;

            for (size_t i = 0; i < n.actions.size(); ++i) {
                if (i > 0) ImGui::SameLine();
                char btn_id[128];
                snprintf(btn_id, sizeof(btn_id), "%s##act_%u_%zu",
                         n.actions[i].label.c_str(), n.id, i);
                if (ImGui::Button(btn_id)) {
                    acted_id  = n.id;
                    acted_key = n.actions[i].key;
                    to_close  = n.id;
                }
            }

            if (acted_id && action_cb_) {
                action_cb_(acted_id, acted_key);
            }
        }

        ImGui::End();
        ImGui::PopStyleColor(); // Border
        ImGui::PopStyleVar();   // Alpha

        if (to_close) {
            close(to_close);
        }
    }
}

// ---------------------------------------------------------------------------
// render_history()
// ---------------------------------------------------------------------------

void NotificationManager::render_history(bool* p_open) {
    if (!ImGui::Begin("Notification History", p_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear History")) {
        history_.clear();
    }
    ImGui::Separator();

    if (history_.empty()) {
        ImGui::TextDisabled("No notifications yet.");
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##hist_scroll", ImVec2(0, 0), false);

    // Show newest first
    for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
        const auto& n = *it;

        // Urgency badge
        const char* badge = nullptr;
        ImVec4 badge_col;
        switch (n.urgency) {
            case Urgency::Low:      badge = "[LOW]";      badge_col = ImVec4(0.5f,0.5f,0.55f,1); break;
            case Urgency::Normal:   badge = "[NORMAL]";   badge_col = ImVec4(0.7f,0.75f,1,1);    break;
            case Urgency::Critical: badge = "[CRITICAL]"; badge_col = ImVec4(0.95f,0.55f,0.66f,1); break;
        }
        if (badge) {
            ImGui::PushStyleColor(ImGuiCol_Text, badge_col);
            ImGui::TextUnformatted(badge);
            ImGui::PopStyleColor();
            ImGui::SameLine();
        }

        // Timestamp (seconds-since-epoch approximation via created_at offset)
        ImGui::TextDisabled("[%.0fs ago]", ImGui::GetTime() - n.created_at);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", n.app_name.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(n.summary.c_str());

        if (!n.body.empty()) {
            ImGui::Indent(16.0f);
            ImGui::TextDisabled("%s", n.body.c_str());
            ImGui::Unindent(16.0f);
        }

        ImGui::Separator();
    }

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int NotificationManager::count() const {
    return static_cast<int>(queue_.size());
}

const std::deque<Notification>& NotificationManager::queue() const {
    return queue_;
}

const std::vector<Notification>& NotificationManager::history() const {
    return history_;
}

} // namespace straylight::shell
