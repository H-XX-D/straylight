// apps/font-gui/font_panel.h
// StrayLight Font Manager panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::fontmgr {

struct FontFamily {
    char name[64];
    char category[32];   // Serif, Sans-Serif, Monospace, Display
    char styles[128];    // "Regular, Bold, Italic, Bold Italic"
    int  num_styles;
    int  num_glyphs;
    bool installed;
};

struct FontState {
    std::vector<FontFamily> fonts;
    int  selected_font = 0;
    char preview_text[256] = "The quick brown fox jumps over the lazy dog. 0123456789";
    char search_filter[128] = {};
    char install_path[256] = {};
    char gfonts_search[128] = {};
    int  filter_category = 0;
    bool show_install_dialog = false;

    static constexpr const char* categories[] = {
        "All", "Sans-Serif", "Serif", "Monospace", "Display"
    };
    static constexpr int num_categories = 5;

    void init() {
        fonts.push_back({"Inter", "Sans-Serif", "Regular, Medium, SemiBold, Bold", 4, 2548, true});
        fonts.push_back({"JetBrains Mono", "Monospace", "Regular, Medium, Bold, Italic", 4, 1456, true});
        fonts.push_back({"Noto Sans", "Sans-Serif", "Thin, Light, Regular, Medium, SemiBold, Bold, Black", 7, 3200, true});
        fonts.push_back({"Source Serif Pro", "Serif", "ExtraLight, Light, Regular, SemiBold, Bold, Black", 6, 1876, true});
        fonts.push_back({"Fira Code", "Monospace", "Light, Regular, Medium, SemiBold, Bold", 5, 1520, true});
        fonts.push_back({"Space Grotesk", "Sans-Serif", "Light, Regular, Medium, SemiBold, Bold", 5, 890, true});
        fonts.push_back({"IBM Plex Mono", "Monospace", "Thin, ExtraLight, Light, Regular, Medium, SemiBold, Bold", 7, 1340, true});
        fonts.push_back({"Playfair Display", "Serif", "Regular, Medium, SemiBold, Bold, ExtraBold, Black", 6, 820, false});
        fonts.push_back({"Orbitron", "Display", "Regular, Medium, SemiBold, Bold, ExtraBold, Black", 6, 450, false});
        fonts.push_back({"Cascadia Code", "Monospace", "ExtraLight, Light, SemiLight, Regular, SemiBold, Bold", 6, 1680, false});
    }
};

inline void render_font_panel(FontState& st) {
    if (st.fonts.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("FONT MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Toolbar
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##search", "Search fonts...", st.search_filter, sizeof(st.search_filter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::Combo("##cat", &st.filter_category, FontState::categories, FontState::num_categories);
    ImGui::SameLine();
    if (ImGui::Button("Install from File")) {
        st.show_install_dialog = true;
    }
    ImGui::Spacing();

    // Preview text input
    ImGui::Text("Preview:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##preview_text", st.preview_text, sizeof(st.preview_text));
    ImGui::Spacing();

    float list_w = ImGui::GetContentRegionAvail().x * 0.4f;

    // Font list with inline preview
    ImGui::BeginChild("##font_list", ImVec2(list_w, 0), true);
    ImGui::TextColored(accent, "Fonts");
    ImGui::Separator();

    for (int i = 0; i < (int)st.fonts.size(); ++i) {
        auto& f = st.fonts[i];

        // Apply filters
        if (st.filter_category > 0 &&
            strcmp(f.category, FontState::categories[st.filter_category]) != 0) continue;
        if (strlen(st.search_filter) > 0 &&
            strstr(f.name, st.search_filter) == nullptr) continue;

        ImGui::PushID(i);
        bool selected = (i == st.selected_font);

        // Font name with category badge
        ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##font_sel", selected, 0, ImVec2(0, 50))) {
            st.selected_font = i;
        }

        // Draw font info overlay
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddText(ImVec2(pos.x + 4, pos.y + 2), IM_COL32(220, 220, 220, 255), f.name);

        // Category badge
        ImU32 badge_col = IM_COL32(60, 60, 100, 255);
        if (strcmp(f.category, "Monospace") == 0) badge_col = IM_COL32(80, 40, 120, 255);
        else if (strcmp(f.category, "Serif") == 0) badge_col = IM_COL32(120, 60, 40, 255);
        else if (strcmp(f.category, "Display") == 0) badge_col = IM_COL32(40, 100, 80, 255);

        ImVec2 badge_pos(pos.x + ImGui::CalcTextSize(f.name).x + 12, pos.y + 3);
        ImVec2 badge_end(badge_pos.x + ImGui::CalcTextSize(f.category).x + 10, badge_pos.y + 16);
        draw->AddRectFilled(badge_pos, badge_end, badge_col, 3.0f);
        draw->AddText(ImVec2(badge_pos.x + 5, badge_pos.y + 1),
                      IM_COL32(200, 200, 200, 255), f.category);

        // Preview line (simulated - using default font since we can't load arbitrary fonts)
        draw->AddText(ImVec2(pos.x + 4, pos.y + 22),
                      IM_COL32(160, 160, 160, 255), st.preview_text);

        // Installed indicator
        if (f.installed) {
            draw->AddText(ImVec2(pos.x + 4, pos.y + 38),
                          IM_COL32(0, 200, 100, 200), "Installed");
        } else {
            draw->AddText(ImVec2(pos.x + 4, pos.y + 38),
                          IM_COL32(150, 150, 150, 200), "Not installed");
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Font detail panel
    ImGui::BeginChild("##font_detail", ImVec2(0, 0), true);
    if (st.selected_font >= 0 && st.selected_font < (int)st.fonts.size()) {
        auto& f = st.fonts[st.selected_font];

        ImGui::TextColored(accent, "%s", f.name);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Category:");  ImGui::SameLine(120); ImGui::Text("%s", f.category);
        ImGui::Text("Styles:");    ImGui::SameLine(120); ImGui::Text("%d", f.num_styles);
        ImGui::Text("Glyphs:");    ImGui::SameLine(120); ImGui::Text("%d", f.num_glyphs);
        ImGui::Text("Status:");    ImGui::SameLine(120);
        if (f.installed) ImGui::TextColored(accent, "Installed");
        else ImGui::TextDisabled("Not installed");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(accent, "Available Styles");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", f.styles);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(accent, "Preview");
        ImGui::Spacing();

        // Preview at different sizes
        float sizes[] = {12, 16, 20, 28, 36};
        const char* size_labels[] = {"12px", "16px", "20px", "28px", "36px"};
        for (int s = 0; s < 5; ++s) {
            ImGui::TextDisabled("%s:", size_labels[s]);
            ImGui::SameLine(60);
            // Simulated size preview using scale
            float scale = sizes[s] / 16.0f;
            ImGui::SetWindowFontScale(scale);
            ImGui::Text("%s", st.preview_text);
            ImGui::SetWindowFontScale(1.0f);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!f.installed) {
            if (ImGui::Button("Install Font", ImVec2(160, 32))) {
                f.installed = true;
            }
        } else {
            if (ImGui::Button("Uninstall Font", ImVec2(160, 32))) {
                f.installed = false;
            }
        }
    }
    ImGui::EndChild();

    // Install dialog
    if (st.show_install_dialog) {
        ImGui::OpenPopup("Install Font");
        st.show_install_dialog = false;
    }
    if (ImGui::BeginPopupModal("Install Font", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Install from file:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextWithHint("##path", "/path/to/font.ttf", st.install_path, sizeof(st.install_path));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Or search Google Fonts:");
        ImGui::SetNextItemWidth(400);
        ImGui::InputTextWithHint("##gfonts", "Search Google Fonts...", st.gfonts_search, sizeof(st.gfonts_search));
        ImGui::Spacing();
        if (ImGui::Button("Install", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::fontmgr
