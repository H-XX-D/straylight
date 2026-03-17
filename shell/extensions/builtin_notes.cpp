// shell/extensions/builtin_notes.cpp
// Built-in shell extension: sticky notes on desktop, persisted to JSON.

#include "extension_api.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct StickyNote {
    int id = 0;
    char title[64] = "Note";
    char content[4096] = "";
    ImVec2 position{100.0f, 100.0f};
    ImVec2 size{200.0f, 200.0f};
    ImVec4 color{0.15f, 0.15f, 0.25f, 0.95f};
    bool open = true;
    bool pinned = false;
    std::string created_at;
    std::string modified_at;
};

struct NotesState {
    SlExtensionContext* ctx = nullptr;
    std::vector<StickyNote> notes;
    int next_id = 1;
    std::string data_path;
    bool dirty = false;
    float auto_save_timer = 0.0f;
    float auto_save_interval = 5.0f; // seconds

    // For the note list window
    bool show_list = true;
};

NotesState g_notes;

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf{};
    ::localtime_r(&time_t_now, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

void load_notes() {
    if (g_notes.data_path.empty()) return;

    std::ifstream f(g_notes.data_path);
    if (!f.is_open()) return;

    try {
        nlohmann::json j;
        f >> j;

        g_notes.notes.clear();
        g_notes.next_id = j.value("next_id", 1);

        if (j.contains("notes") && j["notes"].is_array()) {
            for (const auto& nj : j["notes"]) {
                StickyNote note;
                note.id = nj.value("id", 0);

                std::string title = nj.value("title", "Note");
                std::strncpy(note.title, title.c_str(), sizeof(note.title) - 1);
                note.title[sizeof(note.title) - 1] = '\0';

                std::string content = nj.value("content", "");
                std::strncpy(note.content, content.c_str(), sizeof(note.content) - 1);
                note.content[sizeof(note.content) - 1] = '\0';

                note.position.x = nj.value("x", 100.0f);
                note.position.y = nj.value("y", 100.0f);
                note.size.x = nj.value("w", 200.0f);
                note.size.y = nj.value("h", 200.0f);

                if (nj.contains("color") && nj["color"].is_array() && nj["color"].size() >= 4) {
                    note.color.x = nj["color"][0].get<float>();
                    note.color.y = nj["color"][1].get<float>();
                    note.color.z = nj["color"][2].get<float>();
                    note.color.w = nj["color"][3].get<float>();
                }

                note.open = nj.value("open", true);
                note.pinned = nj.value("pinned", false);
                note.created_at = nj.value("created_at", "");
                note.modified_at = nj.value("modified_at", "");

                g_notes.notes.push_back(note);
            }
        }

    } catch (const std::exception& e) {
        if (g_notes.ctx && g_notes.ctx->log_warn) {
            std::string msg = "notes: failed to load: ";
            msg += e.what();
            g_notes.ctx->log_warn(msg.c_str());
        }
    }
}

void save_notes() {
    if (g_notes.data_path.empty()) return;

    nlohmann::json j;
    j["next_id"] = g_notes.next_id;

    nlohmann::json notes_arr = nlohmann::json::array();
    for (const auto& note : g_notes.notes) {
        nlohmann::json nj;
        nj["id"] = note.id;
        nj["title"] = note.title;
        nj["content"] = note.content;
        nj["x"] = note.position.x;
        nj["y"] = note.position.y;
        nj["w"] = note.size.x;
        nj["h"] = note.size.y;
        nj["color"] = {note.color.x, note.color.y, note.color.z, note.color.w};
        nj["open"] = note.open;
        nj["pinned"] = note.pinned;
        nj["created_at"] = note.created_at;
        nj["modified_at"] = note.modified_at;
        notes_arr.push_back(nj);
    }
    j["notes"] = notes_arr;

    std::ofstream f(g_notes.data_path);
    if (f.is_open()) {
        f << j.dump(4);
    }

    g_notes.dirty = false;
}

void create_note() {
    StickyNote note;
    note.id = g_notes.next_id++;
    std::snprintf(note.title, sizeof(note.title), "Note %d", note.id);
    note.content[0] = '\0';

    // Offset each new note slightly so they don't stack exactly
    float offset = static_cast<float>(g_notes.notes.size() % 10) * 20.0f;
    note.position = ImVec2(120.0f + offset, 120.0f + offset);
    note.size = ImVec2(220.0f, 180.0f);
    note.open = true;
    note.created_at = current_timestamp();
    note.modified_at = note.created_at;

    // Cycle through pastel-ish colors
    static const ImVec4 palette[] = {
        {0.15f, 0.15f, 0.28f, 0.95f}, // Dark blue
        {0.20f, 0.12f, 0.20f, 0.95f}, // Dark purple
        {0.12f, 0.20f, 0.18f, 0.95f}, // Dark teal
        {0.22f, 0.18f, 0.10f, 0.95f}, // Dark amber
        {0.20f, 0.10f, 0.10f, 0.95f}, // Dark red
    };
    note.color = palette[note.id % 5];

    g_notes.notes.push_back(note);
    g_notes.dirty = true;
}

void delete_note(int id) {
    g_notes.notes.erase(
        std::remove_if(g_notes.notes.begin(), g_notes.notes.end(),
                        [id](const StickyNote& n) { return n.id == id; }),
        g_notes.notes.end());
    g_notes.dirty = true;
}

} // anonymous namespace

extern "C" {

SlExtensionInfo sl_extension_info() {
    SlExtensionInfo info{};
    info.api_version = SL_EXTENSION_API_VERSION;
    std::strncpy(info.name, "notes", SL_EXT_NAME_MAX - 1);
    std::strncpy(info.version, "1.0.0", SL_EXT_VERSION_MAX - 1);
    std::strncpy(info.author, "StrayLight OS", SL_EXT_AUTHOR_MAX - 1);
    std::strncpy(info.description,
                 "Sticky notes on desktop, persisted to JSON",
                 SL_EXT_DESCRIPTION_MAX - 1);
    return info;
}

int sl_extension_init(SlExtensionContext* ctx) {
    g_notes = NotesState{};
    g_notes.ctx = ctx;

    // Build data path
    if (ctx && ctx->data_dir) {
        g_notes.data_path = std::string(ctx->data_dir) + "/notes.json";
    } else {
        const char* home = ::getenv("HOME");
        if (home) {
            g_notes.data_path = std::string(home) +
                "/.local/share/straylight/extensions/notes/notes.json";
        }
    }

    load_notes();

    if (ctx && ctx->log_info) {
        char msg[128];
        std::snprintf(msg, sizeof(msg), "Notes extension initialized (%zu notes loaded)",
                      g_notes.notes.size());
        ctx->log_info(msg);
    }
    return 0;
}

void sl_extension_render(float dt) {
    // Auto-save timer
    if (g_notes.dirty) {
        g_notes.auto_save_timer += dt;
        if (g_notes.auto_save_timer >= g_notes.auto_save_interval) {
            g_notes.auto_save_timer = 0.0f;
            save_notes();
        }
    }

    // Note list / manager window
    if (g_notes.show_list) {
        ImGui::SetNextWindowSize(ImVec2(250.0f, 300.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Sticky Notes", &g_notes.show_list, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Button("+ New Note")) {
                create_note();
            }
            ImGui::SameLine();
            if (ImGui::Button("Save All")) {
                save_notes();
                if (g_notes.ctx && g_notes.ctx->show_notification) {
                    g_notes.ctx->show_notification("Notes", "All notes saved", 2.0f);
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%zu)",
                              g_notes.notes.size());

            ImGui::Separator();

            for (auto& note : g_notes.notes) {
                ImGui::PushID(note.id);

                // Color indicator
                ImGui::ColorButton("##color", note.color,
                                    ImGuiColorEditFlags_NoTooltip |
                                    ImGuiColorEditFlags_NoPicker,
                                    ImVec2(12, 12));
                ImGui::SameLine();

                // Toggle visibility
                if (ImGui::Selectable(note.title, note.open, 0, ImVec2(0, 0))) {
                    note.open = !note.open;
                }

                // Pin indicator
                if (note.pinned) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[pin]");
                }

                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    // Render each open note as its own window
    int delete_id = -1;
    for (auto& note : g_notes.notes) {
        if (!note.open) continue;

        ImGui::PushID(note.id);

        // Set window bg color to note color
        ImGui::PushStyleColor(ImGuiCol_WindowBg, note.color);
        ImGui::PushStyleColor(ImGuiCol_TitleBg,
                               ImVec4(note.color.x * 0.8f, note.color.y * 0.8f,
                                      note.color.z * 0.8f, note.color.w));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                               ImVec4(note.color.x * 1.2f, note.color.y * 1.2f,
                                      note.color.z * 1.2f, note.color.w));

        ImGui::SetNextWindowPos(note.position, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(note.size, ImGuiCond_FirstUseEver);

        char win_title[80];
        std::snprintf(win_title, sizeof(win_title), "%s###note_%d", note.title, note.id);

        ImGuiWindowFlags note_flags = ImGuiWindowFlags_NoCollapse;
        if (note.pinned) {
            note_flags |= ImGuiWindowFlags_NoMove;
        }

        if (ImGui::Begin(win_title, &note.open, note_flags)) {
            // Track position and size changes
            ImVec2 new_pos = ImGui::GetWindowPos();
            ImVec2 new_size = ImGui::GetWindowSize();
            if (new_pos.x != note.position.x || new_pos.y != note.position.y ||
                new_size.x != note.size.x || new_size.y != note.size.y) {
                note.position = new_pos;
                note.size = new_size;
                g_notes.dirty = true;
            }

            // Title edit
            if (ImGui::InputText("##title", note.title, sizeof(note.title))) {
                note.modified_at = current_timestamp();
                g_notes.dirty = true;
            }

            ImGui::SameLine();
            if (ImGui::Checkbox("Pin", &note.pinned)) {
                g_notes.dirty = true;
            }

            // Content text area
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float text_height = avail.y - 30.0f;
            if (text_height < 40.0f) text_height = 40.0f;

            if (ImGui::InputTextMultiline("##content", note.content, sizeof(note.content),
                                           ImVec2(-1.0f, text_height),
                                           ImGuiInputTextFlags_AllowTabInput)) {
                note.modified_at = current_timestamp();
                g_notes.dirty = true;
            }

            // Bottom bar: timestamp + delete
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s",
                              note.modified_at.c_str());
            ImGui::SameLine();

            // Color picker
            ImGui::ColorEdit4("##notecolor", &note.color.x,
                               ImGuiColorEditFlags_NoInputs |
                               ImGuiColorEditFlags_NoLabel);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.9f));
            if (ImGui::SmallButton("Delete")) {
                delete_id = note.id;
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::End();

        ImGui::PopStyleColor(3);
        ImGui::PopID();
    }

    if (delete_id >= 0) {
        delete_note(delete_id);
    }
}

void sl_extension_shutdown() {
    // Final save
    if (g_notes.dirty) {
        save_notes();
    }

    if (g_notes.ctx && g_notes.ctx->log_info) {
        g_notes.ctx->log_info("Notes extension shut down");
    }
    g_notes = NotesState{};
}

} // extern "C"
