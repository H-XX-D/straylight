// shell/themes/theme_engine.cpp
// Theme engine implementation — loads JSON themes, CSS-variable system,
// applies to ImGui, hot-reload via inotify directory watch, live-preview panel
#include "theme_engine.h"

#include <straylight/log.h>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#endif

namespace straylight::shell {

namespace fs = std::filesystem;
using json   = nlohmann::json;

static constexpr const char* kDefaultThemesDirSys = "/etc/straylight/themes";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string user_themes_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        return std::string(xdg) + "/straylight/themes";
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "/root") + "/.config/straylight/themes";
}

// ---------------------------------------------------------------------------
// ThemeEngine lifecycle
// ---------------------------------------------------------------------------

ThemeEngine::ThemeEngine() = default;

ThemeEngine::~ThemeEngine() {
#ifdef __linux__
    if (inotify_fd_ >= 0) {
        if (watch_fd_sys_ >= 0) inotify_rm_watch(inotify_fd_, watch_fd_sys_);
        if (watch_fd_usr_ >= 0) inotify_rm_watch(inotify_fd_, watch_fd_usr_);
        close(inotify_fd_);
    }
#endif
}

// ---------------------------------------------------------------------------
// Color utilities
// ---------------------------------------------------------------------------

uint32_t ThemeEngine::parse_color(const std::string& hex) {
    if (hex.empty() || hex[0] != '#') {
        return 0xFF000000;
    }

    uint32_t r = 0, g = 0, b = 0, a = 255;
    try {
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
    } catch (...) {
        return 0xFF000000;
    }
    // ImGui uses ABGR format for ImU32
    return (a << 24) | (b << 16) | (g << 8) | r;
}

std::string ThemeEngine::color_to_hex(uint32_t abgr) {
    uint32_t r = (abgr >>  0) & 0xFF;
    uint32_t g = (abgr >>  8) & 0xFF;
    uint32_t b = (abgr >> 16) & 0xFF;
    uint32_t a = (abgr >> 24) & 0xFF;
    char buf[10];
    if (a == 255) {
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    } else {
        snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// load() / load_by_name()
// ---------------------------------------------------------------------------

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

    theme.font_size     = j.value("font_size",     16.0f);
    theme.corner_radius = j.value("corner_radius",  4.0f);
    theme.icon_theme    = j.value("icon_theme", "straylight-icons");

    if (j.contains("vars") && j["vars"].is_object()) {
        for (auto& [key, val] : j["vars"].items()) {
            if (val.is_string()) {
                theme.vars[key] = val.get<std::string>();
            } else if (val.is_number()) {
                theme.vars[key] = std::to_string(val.get<double>());
            }
        }
    }

    current_theme_ = theme;
    watched_path_  = path_str;

    SL_INFO("Loaded theme '{}' from {}", theme.name, path_str);
    return Result<Theme, SLError>::ok(std::move(theme));
}

Result<Theme, SLError> ThemeEngine::load_by_name(std::string_view theme_name) {
    // Search user dir first, then system dir
    for (const std::string& dir : { user_themes_dir(),
                                     std::string(kDefaultThemesDirSys) }) {
        std::string path = dir + "/" + std::string(theme_name) + ".json";
        if (fs::exists(path)) {
            return load(path);
        }
    }
    return Result<Theme, SLError>::error(
        SLError{SLErrorCode::NotFound,
                "Theme not found: " + std::string(theme_name)});
}

// ---------------------------------------------------------------------------
// apply()
// ---------------------------------------------------------------------------

void ThemeEngine::apply(ImGuiStyle& style) const {
    auto to_vec4 = [](uint32_t abgr) -> ImVec4 {
        float r = static_cast<float>((abgr >>  0) & 0xFF) / 255.0f;
        float g = static_cast<float>((abgr >>  8) & 0xFF) / 255.0f;
        float b = static_cast<float>((abgr >> 16) & 0xFF) / 255.0f;
        float a = static_cast<float>((abgr >> 24) & 0xFF) / 255.0f;
        return ImVec4(r, g, b, a);
    };

    const auto& t = current_theme_;

    // Rounding
    style.WindowRounding    = t.corner_radius;
    style.FrameRounding     = t.corner_radius * 0.5f;
    style.GrabRounding      = t.corner_radius * 0.5f;
    style.PopupRounding     = t.corner_radius;
    style.ScrollbarRounding = t.corner_radius;
    style.TabRounding       = t.corner_radius * 0.5f;
    style.ChildRounding     = t.corner_radius * 0.5f;

    // Spacing from vars (fall back to style defaults if absent)
    float sm = float_var("spacing.sm", 4.0f);
    float md = float_var("spacing.md", 8.0f);
    style.ItemSpacing        = ImVec2(md, sm);
    style.FramePadding       = ImVec2(md, sm);
    style.WindowPadding      = ImVec2(md, md);

    ImVec4* c = style.Colors;

    // Background and surface colors
    c[ImGuiCol_WindowBg]         = to_vec4(t.bg);
    c[ImGuiCol_PopupBg]          = to_vec4(t.panel);
    c[ImGuiCol_ChildBg]          = to_vec4(t.bg);
    c[ImGuiCol_MenuBarBg]        = to_vec4(t.panel);

    // Text
    c[ImGuiCol_Text]             = to_vec4(t.fg);
    c[ImGuiCol_TextDisabled]     = to_vec4(color_var("text.dim",
                                            0xFFA6ADC8));

    // Borders
    c[ImGuiCol_Border]           = to_vec4(color_var("border.color",
                                            0xFF6C7086));
    c[ImGuiCol_BorderShadow]     = ImVec4(0, 0, 0, 0);

    // Frames
    c[ImGuiCol_FrameBg]          = to_vec4(t.panel);
    c[ImGuiCol_FrameBgHovered]   = to_vec4(color_var("surface.hover",  t.panel));
    c[ImGuiCol_FrameBgActive]    = to_vec4(color_var("surface.active", t.accent));

    // Title bars
    c[ImGuiCol_TitleBg]          = to_vec4(t.panel);
    c[ImGuiCol_TitleBgActive]    = to_vec4(t.accent);
    c[ImGuiCol_TitleBgCollapsed] = to_vec4(t.panel);

    // Buttons
    c[ImGuiCol_Button]           = to_vec4(t.panel);
    c[ImGuiCol_ButtonHovered]    = to_vec4(color_var("surface.hover",  t.panel));
    c[ImGuiCol_ButtonActive]     = to_vec4(color_var("surface.active", t.accent));

    // Headers (CollapsingHeader, TreeNode, Selectable, MenuItem)
    c[ImGuiCol_Header]           = to_vec4(t.panel);
    c[ImGuiCol_HeaderHovered]    = to_vec4(color_var("surface.hover",  t.panel));
    c[ImGuiCol_HeaderActive]     = to_vec4(color_var("surface.active", t.accent));

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]      = to_vec4(t.bg);
    c[ImGuiCol_ScrollbarGrab]    = to_vec4(t.panel);
    c[ImGuiCol_ScrollbarGrabHovered] = to_vec4(color_var("surface.hover", t.panel));
    c[ImGuiCol_ScrollbarGrabActive]  = to_vec4(t.accent);

    // Tabs
    c[ImGuiCol_Tab]              = to_vec4(t.panel);
    c[ImGuiCol_TabHovered]       = to_vec4(t.accent);
    c[ImGuiCol_TabActive]        = to_vec4(t.accent);
    c[ImGuiCol_TabUnfocused]     = to_vec4(t.panel);
    c[ImGuiCol_TabUnfocusedActive] = to_vec4(t.panel);

    // Slider / check
    c[ImGuiCol_SliderGrab]       = to_vec4(t.accent);
    c[ImGuiCol_SliderGrabActive] = to_vec4(t.accent);
    c[ImGuiCol_CheckMark]        = to_vec4(t.accent);

    SL_DEBUG("Applied theme '{}': corner_radius={}, font_size={}",
             t.name, t.corner_radius, t.font_size);
}

const Theme& ThemeEngine::current() const {
    return current_theme_;
}

// ---------------------------------------------------------------------------
// Variable resolution
// ---------------------------------------------------------------------------

std::string ThemeEngine::var(std::string_view key, std::string_view fallback) const {
    auto it = current_theme_.vars.find(std::string(key));
    if (it != current_theme_.vars.end()) {
        return it->second;
    }
    return std::string(fallback);
}

uint32_t ThemeEngine::color_var(std::string_view key, uint32_t fallback) const {
    std::string v = var(key);
    if (v.empty()) return fallback;
    return parse_color(v);
}

float ThemeEngine::float_var(std::string_view key, float fallback) const {
    std::string v = var(key);
    if (v.empty()) return fallback;
    try {
        return std::stof(v);
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> ThemeEngine::var_keys() const {
    std::vector<std::string> keys;
    keys.reserve(current_theme_.vars.size());
    for (const auto& [k, _] : current_theme_.vars) {
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void ThemeEngine::set_var(std::string_view key, std::string_view value) {
    current_theme_.vars[std::string(key)] = std::string(value);
}

// ---------------------------------------------------------------------------
// Hot-reload: inotify directory watch
// ---------------------------------------------------------------------------

void ThemeEngine::watch_for_changes() {
#ifdef __linux__
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ < 0) {
        SL_WARN("Failed to init inotify for theme hot-reload");
        return;
    }

    constexpr uint32_t kMask = IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE;

    // System themes dir
    if (fs::exists(kDefaultThemesDirSys)) {
        watch_fd_sys_ = inotify_add_watch(inotify_fd_, kDefaultThemesDirSys, kMask);
        if (watch_fd_sys_ < 0) {
            SL_WARN("Cannot watch system themes dir: {}", kDefaultThemesDirSys);
        }
    }

    // User themes dir (create if absent)
    std::string udir = user_themes_dir();
    std::error_code ec;
    fs::create_directories(udir, ec);
    if (fs::exists(udir)) {
        watch_fd_usr_ = inotify_add_watch(inotify_fd_, udir.c_str(), kMask);
        if (watch_fd_usr_ < 0) {
            SL_WARN("Cannot watch user themes dir: {}", udir);
        }
    }
#endif
}

void ThemeEngine::poll_changes() {
#ifdef __linux__
    if (inotify_fd_ < 0) return;

    struct pollfd pfd{inotify_fd_, POLLIN, 0};
    if (poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) return;

    auto now = std::chrono::steady_clock::now();
    if (now - last_reload_time_ < kReloadDebounce) {
        // Drain without acting
        char buf[4096];
        while (read(inotify_fd_, buf, sizeof(buf)) > 0) {}
        return;
    }

    // Read events to find the modified file name
    alignas(struct inotify_event) char buf[4096];
    std::string modified_name;
    ssize_t len;
    while ((len = read(inotify_fd_, buf, sizeof(buf))) > 0) {
        for (char* ptr = buf; ptr < buf + len;) {
            auto* ev = reinterpret_cast<struct inotify_event*>(ptr);
            if (ev->len > 0 && ev->name[0] != '\0') {
                modified_name = ev->name;
            }
            ptr += sizeof(struct inotify_event) + ev->len;
        }
    }

    last_reload_time_ = now;

    // If we have the currently watched file, reload it.
    // Otherwise reload the first matching file in either dir.
    std::string target;
    if (!watched_path_.empty() && fs::exists(watched_path_)) {
        target = watched_path_;
    } else if (!modified_name.empty()) {
        for (const std::string& dir : { user_themes_dir(),
                                         std::string(kDefaultThemesDirSys) }) {
            std::string p = dir + "/" + modified_name;
            if (fs::exists(p)) { target = p; break; }
        }
    }

    if (target.empty()) return;

    SL_INFO("Theme file changed, reloading: {}", target);
    auto result = load(target);
    if (result.has_value()) {
        apply(ImGui::GetStyle());
    }
#endif
}

// ---------------------------------------------------------------------------
// Live-preview ImGui panel
// ---------------------------------------------------------------------------

void ThemeEngine::render_live_preview(bool* p_open) {
    if (!ImGui::Begin("Theme Editor", p_open,
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // ----- Section 1: Core palette -----
    if (ImGui::CollapsingHeader("Core Palette", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto edit_core_color = [&](const char* label, uint32_t& abgr_ref) {
            float col[4];
            col[0] = ((abgr_ref >>  0) & 0xFF) / 255.0f;
            col[1] = ((abgr_ref >>  8) & 0xFF) / 255.0f;
            col[2] = ((abgr_ref >> 16) & 0xFF) / 255.0f;
            col[3] = ((abgr_ref >> 24) & 0xFF) / 255.0f;
            if (ImGui::ColorEdit4(label, col,
                                   ImGuiColorEditFlags_AlphaBar |
                                   ImGuiColorEditFlags_DisplayHex)) {
                uint32_t r = static_cast<uint32_t>(col[0] * 255.0f + 0.5f);
                uint32_t g = static_cast<uint32_t>(col[1] * 255.0f + 0.5f);
                uint32_t b = static_cast<uint32_t>(col[2] * 255.0f + 0.5f);
                uint32_t a = static_cast<uint32_t>(col[3] * 255.0f + 0.5f);
                abgr_ref = (a << 24) | (b << 16) | (g << 8) | r;
                changed = true;
            }
        };

        edit_core_color("Background",   current_theme_.bg);
        edit_core_color("Foreground",   current_theme_.fg);
        edit_core_color("Accent",       current_theme_.accent);
        edit_core_color("Panel",        current_theme_.panel);

        if (ImGui::DragFloat("Font Size",     &current_theme_.font_size,     0.5f, 8.0f, 48.0f)) changed = true;
        if (ImGui::DragFloat("Corner Radius", &current_theme_.corner_radius, 0.25f, 0.0f, 24.0f)) changed = true;
    }

    // ----- Section 2: Extended variables -----
    if (ImGui::CollapsingHeader("Theme Variables")) {
        auto keys = var_keys();
        for (const auto& key : keys) {
            std::string& val = current_theme_.vars[key];

            // Heuristic: color vars start with '#' or their key contains
            // "color", "bg", "fg", "border", "surface", "success",
            // "warning", "error", "toast", "osd"
            bool is_color = (!val.empty() && val[0] == '#');
            if (!is_color) {
                // Check key segments
                static const char* kColorKeywords[] = {
                    "color", ".bg", ".fg", "border", "surface",
                    "success", "warning", "error", "toast", "osd"
                };
                for (const char* kw : kColorKeywords) {
                    if (key.find(kw) != std::string::npos) {
                        is_color = true; break;
                    }
                }
            }

            ImGui::PushID(key.c_str());
            if (is_color) {
                // Try to parse as color
                uint32_t abgr = parse_color(val);
                float col[4];
                col[0] = ((abgr >>  0) & 0xFF) / 255.0f;
                col[1] = ((abgr >>  8) & 0xFF) / 255.0f;
                col[2] = ((abgr >> 16) & 0xFF) / 255.0f;
                col[3] = ((abgr >> 24) & 0xFF) / 255.0f;
                if (ImGui::ColorEdit4(key.c_str(), col,
                                       ImGuiColorEditFlags_AlphaBar |
                                       ImGuiColorEditFlags_DisplayHex)) {
                    uint32_t r = static_cast<uint32_t>(col[0] * 255.0f + 0.5f);
                    uint32_t g = static_cast<uint32_t>(col[1] * 255.0f + 0.5f);
                    uint32_t b = static_cast<uint32_t>(col[2] * 255.0f + 0.5f);
                    uint32_t a = static_cast<uint32_t>(col[3] * 255.0f + 0.5f);
                    uint32_t new_abgr = (a << 24) | (b << 16) | (g << 8) | r;
                    val = color_to_hex(new_abgr);
                    changed = true;
                }
            } else {
                // Numeric var
                try {
                    float fval = std::stof(val);
                    if (ImGui::DragFloat(key.c_str(), &fval, 0.5f)) {
                        val = std::to_string(fval);
                        changed = true;
                    }
                } catch (...) {
                    // Generic text input fallback
                    char buf[256];
                    std::strncpy(buf, val.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputText(key.c_str(), buf, sizeof(buf))) {
                        val = buf;
                        changed = true;
                    }
                }
            }
            ImGui::PopID();
        }
    }

    // Apply live as edits happen
    if (changed) {
        apply(ImGui::GetStyle());
    }

    ImGui::Separator();

    // ----- Section 3: Actions -----
    if (ImGui::Button("Apply")) {
        apply(ImGui::GetStyle());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && !watched_path_.empty()) {
        auto r = save_current(watched_path_);
        if (!r.has_value()) {
            SL_ERROR("Failed to save theme: {}", r.error().message());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert") && !watched_path_.empty()) {
        auto r = load(watched_path_);
        if (r.has_value()) {
            apply(ImGui::GetStyle());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// save_current() — atomic write
// ---------------------------------------------------------------------------

Result<void, SLError> ThemeEngine::save_current(std::string_view path) {
    json j;
    const auto& t = current_theme_;

    j["name"]          = t.name;
    j["font_size"]     = t.font_size;
    j["corner_radius"] = t.corner_radius;
    j["icon_theme"]    = t.icon_theme;

    j["colors"]["bg"]     = color_to_hex(t.bg);
    j["colors"]["fg"]     = color_to_hex(t.fg);
    j["colors"]["accent"] = color_to_hex(t.accent);
    j["colors"]["panel"]  = color_to_hex(t.panel);

    json vars_obj = json::object();
    for (const auto& [key, val] : t.vars) {
        vars_obj[key] = val;
    }
    j["vars"] = vars_obj;

    std::string tmp_path = std::string(path) + ".tmp";

    {
        std::ofstream out(tmp_path);
        if (!out.is_open()) {
            return Result<void, SLError>::error(
                SLError{SLErrorCode::IOError,
                        "Cannot write temp theme file: " + tmp_path});
        }
        out << j.dump(2);
        if (!out.good()) {
            return Result<void, SLError>::error(
                SLError{SLErrorCode::IOError,
                        "Write error for temp theme file: " + tmp_path});
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, std::string(path), ec);
    if (ec) {
        return Result<void, SLError>::error(
            SLError{SLErrorCode::IOError,
                    "Rename failed: " + ec.message()});
    }

    SL_INFO("Theme '{}' saved to {}", t.name, path);
    return Result<void, SLError>::ok();
}

} // namespace straylight::shell
