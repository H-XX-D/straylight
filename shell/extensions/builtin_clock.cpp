// shell/extensions/builtin_clock.cpp
// Built-in shell extension: analog + digital clock widget with timezone support.

#include "extension_api.h"

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

namespace {

struct ClockState {
    SlExtensionContext* ctx = nullptr;
    std::string timezone_name;  // e.g. "America/New_York" or empty for local
    int timezone_offset_hours = 0;
    bool use_24h = true;
    bool show_analog = true;
    bool show_digital = true;
    bool show_date = true;
    bool show_seconds = true;
    float widget_size = 200.0f;
};

ClockState g_clock;

// Get the current time, optionally applying a timezone offset
struct TimeInfo {
    int hour, minute, second;
    int year, month, day;
    int day_of_week; // 0=Sun
    float fractional_second;
};

TimeInfo get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;

    struct tm tm_buf{};
    ::localtime_r(&time_t_now, &tm_buf);

    // Apply timezone offset
    int total_hour = tm_buf.tm_hour + g_clock.timezone_offset_hours;
    int day_adjust = 0;
    while (total_hour < 0) { total_hour += 24; day_adjust--; }
    while (total_hour >= 24) { total_hour -= 24; day_adjust++; }

    TimeInfo info{};
    info.hour = total_hour;
    info.minute = tm_buf.tm_min;
    info.second = tm_buf.tm_sec;
    info.fractional_second = static_cast<float>(ms) / 1000.0f;
    info.year = tm_buf.tm_year + 1900;
    info.month = tm_buf.tm_mon + 1;
    info.day = tm_buf.tm_mday + day_adjust;
    info.day_of_week = (tm_buf.tm_wday + day_adjust + 7) % 7;
    return info;
}

void draw_analog_clock(const TimeInfo& t, ImVec2 center, float radius) {
    auto* draw = ImGui::GetWindowDrawList();

    // Clock face
    draw->AddCircle(center, radius, IM_COL32(0, 200, 130, 200), 64, 2.0f);

    // Hour markers
    for (int i = 0; i < 12; ++i) {
        float angle = static_cast<float>(i) * (2.0f * 3.14159265f / 12.0f) - 3.14159265f / 2.0f;
        float inner = (i % 3 == 0) ? radius * 0.80f : radius * 0.85f;
        float outer = radius * 0.92f;
        float thick = (i % 3 == 0) ? 2.5f : 1.5f;

        ImVec2 p1(center.x + std::cos(angle) * inner, center.y + std::sin(angle) * inner);
        ImVec2 p2(center.x + std::cos(angle) * outer, center.y + std::sin(angle) * outer);
        draw->AddLine(p1, p2, IM_COL32(200, 200, 200, 220), thick);
    }

    // Second hand
    if (g_clock.show_seconds) {
        float sec_frac = static_cast<float>(t.second) + t.fractional_second;
        float sec_angle = sec_frac * (2.0f * 3.14159265f / 60.0f) - 3.14159265f / 2.0f;
        ImVec2 sec_end(center.x + std::cos(sec_angle) * radius * 0.88f,
                       center.y + std::sin(sec_angle) * radius * 0.88f);
        draw->AddLine(center, sec_end, IM_COL32(255, 80, 80, 200), 1.0f);
    }

    // Minute hand
    float min_frac = static_cast<float>(t.minute) + static_cast<float>(t.second) / 60.0f;
    float min_angle = min_frac * (2.0f * 3.14159265f / 60.0f) - 3.14159265f / 2.0f;
    ImVec2 min_end(center.x + std::cos(min_angle) * radius * 0.75f,
                   center.y + std::sin(min_angle) * radius * 0.75f);
    draw->AddLine(center, min_end, IM_COL32(220, 220, 220, 240), 2.5f);

    // Hour hand
    float hr_frac = static_cast<float>(t.hour % 12) + static_cast<float>(t.minute) / 60.0f;
    float hr_angle = hr_frac * (2.0f * 3.14159265f / 12.0f) - 3.14159265f / 2.0f;
    ImVec2 hr_end(center.x + std::cos(hr_angle) * radius * 0.55f,
                  center.y + std::sin(hr_angle) * radius * 0.55f);
    draw->AddLine(center, hr_end, IM_COL32(0, 230, 150, 255), 3.5f);

    // Center dot
    draw->AddCircleFilled(center, 4.0f, IM_COL32(0, 230, 150, 255));
}

const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* month_names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

} // anonymous namespace

extern "C" {

SlExtensionInfo sl_extension_info() {
    SlExtensionInfo info{};
    info.api_version = SL_EXTENSION_API_VERSION;
    std::strncpy(info.name, "clock", SL_EXT_NAME_MAX - 1);
    std::strncpy(info.version, "1.0.0", SL_EXT_VERSION_MAX - 1);
    std::strncpy(info.author, "StrayLight OS", SL_EXT_AUTHOR_MAX - 1);
    std::strncpy(info.description,
                 "Analog + digital clock widget with timezone support",
                 SL_EXT_DESCRIPTION_MAX - 1);
    return info;
}

int sl_extension_init(SlExtensionContext* ctx) {
    g_clock.ctx = ctx;
    g_clock.use_24h = true;
    g_clock.show_analog = true;
    g_clock.show_digital = true;
    g_clock.show_date = true;
    g_clock.show_seconds = true;
    g_clock.widget_size = 200.0f;
    g_clock.timezone_offset_hours = 0;
    g_clock.timezone_name = "Local";

    if (ctx && ctx->log_info) {
        ctx->log_info("Clock extension initialized");
    }
    return 0;
}

void sl_extension_render(float /*dt*/) {
    TimeInfo t = get_current_time();

    ImGui::SetNextWindowSize(ImVec2(g_clock.widget_size + 20.0f,
                                     g_clock.widget_size + 100.0f),
                              ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("Clock", nullptr, flags)) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float clock_size = std::min(avail.x, avail.y - 60.0f);
        if (clock_size < 50.0f) clock_size = 50.0f;
        float radius = clock_size * 0.45f;

        if (g_clock.show_analog) {
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImVec2 center(cursor.x + avail.x * 0.5f, cursor.y + radius + 5.0f);
            draw_analog_clock(t, center, radius);
            ImGui::Dummy(ImVec2(avail.x, clock_size + 10.0f));
        }

        if (g_clock.show_digital) {
            char time_buf[32];
            int display_hour = t.hour;
            const char* ampm = "";

            if (!g_clock.use_24h) {
                ampm = (t.hour >= 12) ? " PM" : " AM";
                display_hour = t.hour % 12;
                if (display_hour == 0) display_hour = 12;
            }

            if (g_clock.show_seconds) {
                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d%s",
                              display_hour, t.minute, t.second, ampm);
            } else {
                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d%s",
                              display_hour, t.minute, ampm);
            }

            // Center the digital time
            float text_width = ImGui::CalcTextSize(time_buf).x;
            ImGui::SetCursorPosX((avail.x - text_width) * 0.5f);
            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.6f, 1.0f), "%s", time_buf);
        }

        if (g_clock.show_date) {
            char date_buf[64];
            int month_idx = (t.month >= 1 && t.month <= 12) ? t.month - 1 : 0;
            int dow = (t.day_of_week >= 0 && t.day_of_week <= 6) ? t.day_of_week : 0;
            std::snprintf(date_buf, sizeof(date_buf), "%s, %s %d, %d",
                          day_names[dow], month_names[month_idx], t.day, t.year);

            float date_width = ImGui::CalcTextSize(date_buf).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - date_width) * 0.5f + ImGui::GetCursorPosX() * 0.0f);
            float text_avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((text_avail - date_width) * 0.5f);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", date_buf);
        }

        if (!g_clock.timezone_name.empty()) {
            float tz_width = ImGui::CalcTextSize(g_clock.timezone_name.c_str()).x;
            float tz_avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((tz_avail - tz_width) * 0.5f);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s",
                              g_clock.timezone_name.c_str());
        }

        // Settings toggle
        if (ImGui::CollapsingHeader("Settings")) {
            ImGui::Checkbox("24-hour format", &g_clock.use_24h);
            ImGui::Checkbox("Show analog", &g_clock.show_analog);
            ImGui::Checkbox("Show digital", &g_clock.show_digital);
            ImGui::Checkbox("Show date", &g_clock.show_date);
            ImGui::Checkbox("Show seconds", &g_clock.show_seconds);
            ImGui::SliderInt("TZ offset (hours)", &g_clock.timezone_offset_hours, -12, 12);
        }
    }
    ImGui::End();
}

void sl_extension_shutdown() {
    if (g_clock.ctx && g_clock.ctx->log_info) {
        g_clock.ctx->log_info("Clock extension shut down");
    }
    g_clock = ClockState{};
}

} // extern "C"
