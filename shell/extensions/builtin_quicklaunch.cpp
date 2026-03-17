// shell/extensions/builtin_quicklaunch.cpp
// Built-in shell extension: configurable app launcher bar with icons.

#include "extension_api.h"

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

struct LaunchItem {
    std::string name;
    std::string exec_cmd;      // Command to execute
    std::string icon_label;    // Short label shown on the button (1-3 chars)
    ImVec4 color;              // Button tint color
    std::string tooltip;
};

struct QuickLaunchState {
    SlExtensionContext* ctx = nullptr;
    std::vector<LaunchItem> items;
    float button_size = 48.0f;
    float spacing = 4.0f;
    bool horizontal = true;
    std::string config_path;
};

QuickLaunchState g_ql;

void load_config() {
    if (g_ql.config_path.empty()) return;

    std::ifstream f(g_ql.config_path);
    if (!f.is_open()) return;

    try {
        nlohmann::json j;
        f >> j;

        g_ql.items.clear();
        if (j.contains("items") && j["items"].is_array()) {
            for (const auto& item : j["items"]) {
                LaunchItem li;
                li.name = item.value("name", "App");
                li.exec_cmd = item.value("exec", "");
                li.icon_label = item.value("icon", li.name.substr(0, 2));
                li.tooltip = item.value("tooltip", li.name);

                // Parse color
                if (item.contains("color") && item["color"].is_array() && item["color"].size() >= 3) {
                    li.color = ImVec4(
                        item["color"][0].get<float>(),
                        item["color"][1].get<float>(),
                        item["color"][2].get<float>(),
                        item["color"].size() > 3 ? item["color"][3].get<float>() : 1.0f);
                } else {
                    li.color = ImVec4(0.0f, 0.8f, 0.53f, 1.0f);
                }

                g_ql.items.push_back(li);
            }
        }

        g_ql.button_size = j.value("button_size", 48.0f);
        g_ql.spacing = j.value("spacing", 4.0f);
        g_ql.horizontal = j.value("horizontal", true);

    } catch (const std::exception& e) {
        if (g_ql.ctx && g_ql.ctx->log_warn) {
            std::string msg = "quicklaunch: failed to parse config: ";
            msg += e.what();
            g_ql.ctx->log_warn(msg.c_str());
        }
    }
}

void save_config() {
    if (g_ql.config_path.empty()) return;

    nlohmann::json j;
    j["button_size"] = g_ql.button_size;
    j["spacing"] = g_ql.spacing;
    j["horizontal"] = g_ql.horizontal;

    nlohmann::json items_arr = nlohmann::json::array();
    for (const auto& item : g_ql.items) {
        nlohmann::json ij;
        ij["name"] = item.name;
        ij["exec"] = item.exec_cmd;
        ij["icon"] = item.icon_label;
        ij["tooltip"] = item.tooltip;
        ij["color"] = {item.color.x, item.color.y, item.color.z, item.color.w};
        items_arr.push_back(ij);
    }
    j["items"] = items_arr;

    std::ofstream f(g_ql.config_path);
    if (f.is_open()) {
        f << j.dump(4);
    }
}

void setup_default_items() {
    g_ql.items = {
        {"Terminal", "straylight-terminal", "T",
         ImVec4(0.2f, 0.8f, 0.5f, 1.0f), "Open Terminal"},
        {"Files", "straylight-files", "F",
         ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "File Manager"},
        {"Browser", "straylight-browser", "B",
         ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Web Browser"},
        {"Editor", "straylight-editor", "E",
         ImVec4(0.8f, 0.4f, 0.9f, 1.0f), "Text Editor"},
        {"Monitor", "straylight-sysmon", "M",
         ImVec4(0.0f, 0.9f, 0.6f, 1.0f), "System Monitor"},
        {"Settings", "straylight-settings", "S",
         ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "System Settings"},
    };
}

void launch_app(const std::string& cmd) {
    if (cmd.empty()) return;

    // Use the shell context's launch function if available
    if (g_ql.ctx && g_ql.ctx->launch_app) {
        g_ql.ctx->launch_app(cmd.c_str());
        return;
    }

    // Fallback: fork + exec in background
    std::string bg_cmd = cmd + " &";
    int ret = std::system(bg_cmd.c_str());
    (void)ret;
}

} // anonymous namespace

extern "C" {

SlExtensionInfo sl_extension_info() {
    SlExtensionInfo info{};
    info.api_version = SL_EXTENSION_API_VERSION;
    std::strncpy(info.name, "quicklaunch", SL_EXT_NAME_MAX - 1);
    std::strncpy(info.version, "1.0.0", SL_EXT_VERSION_MAX - 1);
    std::strncpy(info.author, "StrayLight OS", SL_EXT_AUTHOR_MAX - 1);
    std::strncpy(info.description,
                 "Configurable app launcher bar with icons",
                 SL_EXT_DESCRIPTION_MAX - 1);
    return info;
}

int sl_extension_init(SlExtensionContext* ctx) {
    g_ql = QuickLaunchState{};
    g_ql.ctx = ctx;

    // Build config path
    if (ctx && ctx->config_dir) {
        g_ql.config_path = std::string(ctx->config_dir) + "/quicklaunch.json";
    } else {
        const char* home = ::getenv("HOME");
        if (home) {
            g_ql.config_path = std::string(home) + "/.config/straylight/extensions/quicklaunch/quicklaunch.json";
        }
    }

    // Try to load saved config, fall back to defaults
    load_config();
    if (g_ql.items.empty()) {
        setup_default_items();
        save_config();
    }

    if (ctx && ctx->log_info) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "QuickLaunch initialized with %zu items",
                      g_ql.items.size());
        ctx->log_info(msg);
    }
    return 0;
}

void sl_extension_render(float /*dt*/) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                              ImGuiWindowFlags_NoScrollbar;

    float total_size;
    if (g_ql.horizontal) {
        total_size = static_cast<float>(g_ql.items.size()) * (g_ql.button_size + g_ql.spacing) + 20.0f;
        ImGui::SetNextWindowSize(ImVec2(total_size, g_ql.button_size + 60.0f),
                                  ImGuiCond_FirstUseEver);
    } else {
        total_size = static_cast<float>(g_ql.items.size()) * (g_ql.button_size + g_ql.spacing) + 20.0f;
        ImGui::SetNextWindowSize(ImVec2(g_ql.button_size + 60.0f, total_size),
                                  ImGuiCond_FirstUseEver);
    }

    if (ImGui::Begin("Quick Launch", nullptr, flags)) {
        for (size_t i = 0; i < g_ql.items.size(); ++i) {
            const auto& item = g_ql.items[i];

            if (i > 0 && g_ql.horizontal) {
                ImGui::SameLine(0.0f, g_ql.spacing);
            }

            // Colored button
            ImGui::PushStyleColor(ImGuiCol_Button,
                                   ImVec4(item.color.x * 0.3f, item.color.y * 0.3f,
                                          item.color.z * 0.3f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                   ImVec4(item.color.x * 0.5f, item.color.y * 0.5f,
                                          item.color.z * 0.5f, 0.9f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, item.color);
            ImGui::PushStyleColor(ImGuiCol_Text, item.color);

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Button(item.icon_label.c_str(),
                              ImVec2(g_ql.button_size, g_ql.button_size))) {
                launch_app(item.exec_cmd);

                if (g_ql.ctx && g_ql.ctx->show_notification) {
                    std::string msg = "Launching " + item.name;
                    g_ql.ctx->show_notification("Quick Launch", msg.c_str(), 2.0f);
                }
            }
            ImGui::PopID();

            ImGui::PopStyleColor(4);

            // Tooltip on hover
            if (ImGui::IsItemHovered() && !item.tooltip.empty()) {
                ImGui::SetTooltip("%s\n%s", item.tooltip.c_str(), item.exec_cmd.c_str());
            }
        }

        // Settings section
        if (ImGui::CollapsingHeader("Configure")) {
            ImGui::SliderFloat("Button Size", &g_ql.button_size, 24.0f, 96.0f);
            ImGui::SliderFloat("Spacing", &g_ql.spacing, 0.0f, 16.0f);
            ImGui::Checkbox("Horizontal", &g_ql.horizontal);

            ImGui::Separator();
            ImGui::Text("Items:");
            for (size_t i = 0; i < g_ql.items.size(); ++i) {
                ImGui::PushID(static_cast<int>(i + 1000));
                auto& item = g_ql.items[i];

                char name_buf[64];
                std::strncpy(name_buf, item.name.c_str(), sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';

                char exec_buf[256];
                std::strncpy(exec_buf, item.exec_cmd.c_str(), sizeof(exec_buf) - 1);
                exec_buf[sizeof(exec_buf) - 1] = '\0';

                char icon_buf[8];
                std::strncpy(icon_buf, item.icon_label.c_str(), sizeof(icon_buf) - 1);
                icon_buf[sizeof(icon_buf) - 1] = '\0';

                ImGui::InputText("Name", name_buf, sizeof(name_buf));
                item.name = name_buf;

                ImGui::InputText("Exec", exec_buf, sizeof(exec_buf));
                item.exec_cmd = exec_buf;

                ImGui::InputText("Icon", icon_buf, sizeof(icon_buf));
                item.icon_label = icon_buf;

                ImGui::ColorEdit4("Color", &item.color.x,
                                   ImGuiColorEditFlags_NoInputs);

                if (ImGui::Button("Remove")) {
                    g_ql.items.erase(g_ql.items.begin() + static_cast<long>(i));
                    ImGui::PopID();
                    break;
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add Item")) {
                LaunchItem new_item;
                new_item.name = "New App";
                new_item.exec_cmd = "";
                new_item.icon_label = "?";
                new_item.color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                new_item.tooltip = "New Application";
                g_ql.items.push_back(new_item);
            }

            if (ImGui::Button("Save Config")) {
                save_config();
                if (g_ql.ctx && g_ql.ctx->show_notification) {
                    g_ql.ctx->show_notification("Quick Launch", "Configuration saved", 2.0f);
                }
            }
        }
    }
    ImGui::End();
}

void sl_extension_shutdown() {
    save_config();
    if (g_ql.ctx && g_ql.ctx->log_info) {
        g_ql.ctx->log_info("QuickLaunch extension shut down");
    }
    g_ql = QuickLaunchState{};
}

} // extern "C"
