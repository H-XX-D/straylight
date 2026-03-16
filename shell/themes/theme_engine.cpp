// shell/themes/theme_engine.cpp
// Theme engine implementation — loads JSON themes, applies to ImGui
#include "theme_engine.h"

#include <straylight/log.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <cstdint>

// inotify is Linux-only; guard for cross-platform compilation
#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace straylight::shell {

namespace fs = std::filesystem;
using json = nlohmann::json;

static constexpr const char* kDefaultThemesDir = "/etc/straylight/themes";

ThemeEngine::ThemeEngine() = default;

ThemeEngine::~ThemeEngine() {
#ifdef __linux__
    if (watch_fd_ >= 0 && inotify_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, watch_fd_);
    }
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
    }
#endif
}

uint32_t ThemeEngine::parse_color(const std::string& hex) {
    // Expected format: "#RRGGBB" or "#RRGGBBAA"
    if (hex.empty() || hex[0] != '#') {
        return 0xFF000000;  // opaque black fallback
    }

    uint32_t r = 0, g = 0, b = 0, a = 255;
    if (hex.size() == 7) {
        r = std::stoul(hex.substr(1, 2), nullptr, 16);
        g = std::stoul(hex.substr(3, 2), nullptr, 16);
        b = std::stoul(hex.substr(5, 2), nullptr, 16);
    } else if (hex.size() == 9) {
        r = std::stoul(hex.substr(1, 2), nullptr, 16);
        g = std::stoul(hex.substr(3, 2), nullptr, 16);
        b = std::stoul(hex.substr(5, 2), nullptr, 16);
        a = std::stoul(hex.substr(7, 2), nullptr, 16);
    }
    // ImGui uses ABGR format for ImU32
    return (a << 24) | (b << 16) | (g << 8) | r;
}

Result<Theme, SLError> ThemeEngine::load(std::string_view path) {
    std::string path_str(path);

    if (!fs::exists(path_str)) {
        return Result<Theme, SLError>::error(
            SLError{SLErrorCode::NotFound,
                    "Theme file not found: " + path_str});
    }

    std::ifstream file(path_str);
    if (!file.is_open()) {
        return Result<Theme, SLError>::error(
            SLError{SLErrorCode::PermissionDenied,
                    "Cannot open theme file: " + path_str});
    }

    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        return Result<Theme, SLError>::error(
            SLError{SLErrorCode::ParseError,
                    std::string("Theme JSON parse error: ") + e.what()});
    }

    Theme theme;
    theme.name = j.value("name", "default");

    if (j.contains("colors")) {
        const auto& colors = j["colors"];
        if (colors.contains("bg"))     theme.bg     = parse_color(colors["bg"].get<std::string>());
        if (colors.contains("fg"))     theme.fg     = parse_color(colors["fg"].get<std::string>());
        if (colors.contains("accent")) theme.accent = parse_color(colors["accent"].get<std::string>());
        if (colors.contains("panel"))  theme.panel  = parse_color(colors["panel"].get<std::string>());
    }

    theme.font_size     = j.value("font_size", 16.0f);
    theme.corner_radius = j.value("corner_radius", 4.0f);
    theme.icon_theme    = j.value("icon_theme", "straylight-icons");

    current_theme_ = theme;
    watched_path_  = path_str;

    SL_INFO("Loaded theme '{}' from {}", theme.name, path_str);
    return Result<Theme, SLError>::ok(std::move(theme));
}

Result<Theme, SLError> ThemeEngine::load_by_name(std::string_view theme_name) {
    std::string path = std::string(kDefaultThemesDir) + "/" +
                       std::string(theme_name) + ".json";
    return load(path);
}

void ThemeEngine::apply(ImGuiStyle& style) const {
    // Convert ABGR uint32 to ImVec4
    auto to_vec4 = [](uint32_t abgr) -> ImVec4 {
        float r = static_cast<float>((abgr >> 0)  & 0xFF) / 255.0f;
        float g = static_cast<float>((abgr >> 8)  & 0xFF) / 255.0f;
        float b = static_cast<float>((abgr >> 16) & 0xFF) / 255.0f;
        float a = static_cast<float>((abgr >> 24) & 0xFF) / 255.0f;
        return ImVec4(r, g, b, a);
    };

    const auto& t = current_theme_;

    style.WindowRounding   = t.corner_radius;
    style.FrameRounding    = t.corner_radius * 0.5f;
    style.GrabRounding     = t.corner_radius * 0.5f;
    style.PopupRounding    = t.corner_radius;
    style.ScrollbarRounding = t.corner_radius;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]         = to_vec4(t.bg);
    colors[ImGuiCol_PopupBg]          = to_vec4(t.panel);
    colors[ImGuiCol_Text]             = to_vec4(t.fg);
    colors[ImGuiCol_TextDisabled]     = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_Border]           = to_vec4(t.panel);
    colors[ImGuiCol_FrameBg]          = to_vec4(t.panel);
    colors[ImGuiCol_FrameBgHovered]   = to_vec4(t.accent);
    colors[ImGuiCol_FrameBgActive]    = to_vec4(t.accent);
    colors[ImGuiCol_TitleBg]          = to_vec4(t.panel);
    colors[ImGuiCol_TitleBgActive]    = to_vec4(t.accent);
    colors[ImGuiCol_MenuBarBg]        = to_vec4(t.panel);
    colors[ImGuiCol_Button]           = to_vec4(t.panel);
    colors[ImGuiCol_ButtonHovered]    = to_vec4(t.accent);
    colors[ImGuiCol_ButtonActive]     = to_vec4(t.accent);
    colors[ImGuiCol_Header]           = to_vec4(t.panel);
    colors[ImGuiCol_HeaderHovered]    = to_vec4(t.accent);
    colors[ImGuiCol_HeaderActive]     = to_vec4(t.accent);
    colors[ImGuiCol_ScrollbarBg]      = to_vec4(t.bg);
    colors[ImGuiCol_ScrollbarGrab]    = to_vec4(t.panel);

    SL_DEBUG("Applied theme '{}': corner_radius={}, font_size={}",
             t.name, t.corner_radius, t.font_size);
}

const Theme& ThemeEngine::current() const {
    return current_theme_;
}

void ThemeEngine::watch_for_changes() {
#ifdef __linux__
    if (watched_path_.empty()) return;

    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        SL_WARN("Failed to init inotify for theme hot-reload");
        return;
    }

    watch_fd_ = inotify_add_watch(inotify_fd_, watched_path_.c_str(),
                                   IN_MODIFY | IN_CLOSE_WRITE);
    if (watch_fd_ < 0) {
        SL_WARN("Failed to watch theme file: {}", watched_path_);
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
#endif
}

void ThemeEngine::poll_changes() {
#ifdef __linux__
    if (inotify_fd_ < 0) return;

    struct pollfd pfd = {inotify_fd_, POLLIN, 0};
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        // Drain inotify events
        char buf[4096];
        while (read(inotify_fd_, buf, sizeof(buf)) > 0) {}

        SL_INFO("Theme file changed, reloading: {}", watched_path_);
        auto result = load(watched_path_);
        if (result.has_value()) {
            ImGuiStyle& style = ImGui::GetStyle();
            apply(style);
        }
    }
#endif
}

} // namespace straylight::shell
