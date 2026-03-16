// shell/panels/top_bar.cpp
// Top taskbar panel implementation
#include "top_bar.h"

#include <straylight/log.h>

#include <imgui.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace straylight::shell {

// --- ClockWidget ---

std::string ClockWidget::formatted_time() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&time_t_now, &tm_buf);

    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm_buf.tm_hour
        << ":" << std::setfill('0') << std::setw(2) << tm_buf.tm_min;
    return oss.str();
}

void ClockWidget::render() {
    std::string time_str = formatted_time();
    ImGui::Text("%s", time_str.c_str());
}

// --- TopBar ---

TopBar::TopBar() = default;
TopBar::~TopBar() = default;

void TopBar::render(int screen_width) {
    constexpr float kBarHeight = 32.0f;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(screen_width),
                                     kBarHeight));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##TopBar", nullptr, flags);

    // Left section: workspace switcher
    render_workspace_switcher();

    // Center section: focused window title
    render_window_title(screen_width);

    // Right section: system tray + clock + notifications
    render_system_tray(screen_width);

    ImGui::End();
}

void TopBar::set_focused_title(const std::string& title) {
    focused_title_ = title;
}

void TopBar::set_workspace(int index) {
    workspace_index_ = index;
}

void TopBar::set_notification_count(int count) {
    notification_count_ = count;
}

void TopBar::render_workspace_switcher() {
    constexpr int kNumWorkspaces = 4;

    for (int i = 0; i < kNumWorkspaces; ++i) {
        if (i > 0) ImGui::SameLine();

        bool is_active = (i == workspace_index_);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }

        char label[16];
        snprintf(label, sizeof(label), " %d ", i + 1);

        if (ImGui::SmallButton(label)) {
            workspace_index_ = i;
            SL_DEBUG("Workspace switched to {}", i + 1);
            // TODO: Send D-Bus call to compositor to switch workspace
        }

        if (is_active) {
            ImGui::PopStyleColor();
        }
    }
}

void TopBar::render_window_title(int screen_width) {
    if (focused_title_.empty()) return;

    // Center the title text
    float title_width = ImGui::CalcTextSize(focused_title_.c_str()).x;
    float center_x = (static_cast<float>(screen_width) - title_width) * 0.5f;
    ImGui::SameLine(center_x);
    ImGui::Text("%s", focused_title_.c_str());
}

void TopBar::render_system_tray(int screen_width) {
    // Position system tray on the right edge
    float right_offset = static_cast<float>(screen_width) - 200.0f;
    ImGui::SameLine(right_offset);

    // Volume icon (placeholder)
    ImGui::Text("[VOL]");
    ImGui::SameLine();

    // Network icon (placeholder)
    ImGui::Text("[NET]");
    ImGui::SameLine();

    // Clock
    clock_.render();
    ImGui::SameLine();

    // Notification count badge
    if (notification_count_ > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::Text("[%d]", notification_count_);
        ImGui::PopStyleColor();
    }
}

} // namespace straylight::shell
