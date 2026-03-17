// apps/boot-gui/boot_panel.h
// StrayLight Boot Manager panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

namespace straylight::boot {

struct KernelEntry {
    char version[64];
    char path[128];
    char initramfs[128];
    char date[32];
    bool is_default;
    bool is_fallback;
};

struct BootState {
    std::vector<KernelEntry> kernels;
    int  selected_kernel = 0;
    char boot_params[512] = "root=/dev/nvme0n1p3 rw quiet splash loglevel=3 nvidia-drm.modeset=1";
    int  timeout_sec = 5;

    bool show_rebuild_confirm = false;
    bool rebuilding = false;
    float rebuild_progress = 0.0f;

    void init() {
        kernels.push_back({"6.8.2-sl1-straylight", "/boot/vmlinuz-6.8.2-sl1",
            "/boot/initramfs-6.8.2-sl1.img", "2026-03-14", true, false});
        kernels.push_back({"6.8.2-sl1-straylight-fallback", "/boot/vmlinuz-6.8.2-sl1",
            "/boot/initramfs-6.8.2-sl1-fallback.img", "2026-03-14", false, true});
        kernels.push_back({"6.8.1-sl1-straylight", "/boot/vmlinuz-6.8.1-sl1",
            "/boot/initramfs-6.8.1-sl1.img", "2026-03-05", false, false});
        kernels.push_back({"6.7.9-sl1-straylight", "/boot/vmlinuz-6.7.9-sl1",
            "/boot/initramfs-6.7.9-sl1.img", "2026-02-20", false, false});
    }
};

inline void render_boot_panel(BootState& st) {
    if (st.kernels.empty()) st.init();

    const ImVec4 accent(0.0f, 1.0f, 0.67f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("BOOT MANAGER");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    float left_w = ImGui::GetContentRegionAvail().x * 0.45f;

    // Kernel list
    ImGui::BeginChild("##kernel_list", ImVec2(left_w, ImGui::GetContentRegionAvail().y * 0.6f), true);
    ImGui::TextColored(accent, "Installed Kernels");
    ImGui::Separator();

    for (int i = 0; i < (int)st.kernels.size(); ++i) {
        auto& k = st.kernels[i];
        ImGui::PushID(i);

        bool selected = (i == st.selected_kernel);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        char label[256];
        snprintf(label, 256, "%s%s%s", k.version,
                 k.is_default ? " [DEFAULT]" : "",
                 k.is_fallback ? " [FALLBACK]" : "");

        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 40))) {
            st.selected_kernel = i;
        }

        // Default marker badge
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (k.is_default) {
            ImVec2 bp(pos.x + ImGui::GetContentRegionAvail().x - 80, pos.y + 4);
            draw->AddRectFilled(bp, ImVec2(bp.x + 70, bp.y + 18),
                                IM_COL32(0, 100, 60, 255), 3.0f);
            draw->AddText(ImVec2(bp.x + 6, bp.y + 2), IM_COL32(255, 255, 255, 255), "DEFAULT");
        }
        if (k.is_fallback) {
            ImVec2 bp(pos.x + ImGui::GetContentRegionAvail().x - 80, pos.y + 4);
            draw->AddRectFilled(bp, ImVec2(bp.x + 70, bp.y + 18),
                                IM_COL32(120, 80, 0, 255), 3.0f);
            draw->AddText(ImVec2(bp.x + 4, bp.y + 2), IM_COL32(255, 255, 255, 255), "FALLBACK");
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Kernel details
    ImGui::BeginChild("##kernel_detail", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), true);
    if (st.selected_kernel >= 0 && st.selected_kernel < (int)st.kernels.size()) {
        auto& k = st.kernels[st.selected_kernel];

        ImGui::TextColored(accent, "%s", k.version);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Kernel Path:");  ImGui::SameLine(140); ImGui::Text("%s", k.path);
        ImGui::Text("Initramfs:");    ImGui::SameLine(140); ImGui::Text("%s", k.initramfs);
        ImGui::Text("Date:");         ImGui::SameLine(140); ImGui::Text("%s", k.date);
        ImGui::Text("Default:");      ImGui::SameLine(140);
        if (k.is_default) ImGui::TextColored(accent, "Yes");
        else ImGui::Text("No");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!k.is_default && !k.is_fallback) {
            if (ImGui::Button("Set as Default", ImVec2(160, 30))) {
                for (auto& kk : st.kernels) kk.is_default = false;
                k.is_default = true;
            }
            ImGui::SameLine();
        }

        if (ImGui::Button("Rebuild Initramfs", ImVec2(180, 30))) {
            st.show_rebuild_confirm = true;
        }

        if (!k.is_default && !k.is_fallback) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
            if (ImGui::Button("Remove", ImVec2(120, 30))) {
                st.kernels.erase(st.kernels.begin() + st.selected_kernel);
                st.selected_kernel = 0;
            }
            ImGui::PopStyleColor();
        }

        // Rebuild progress
        if (st.rebuilding) {
            ImGui::Spacing();
            st.rebuild_progress += ImGui::GetIO().DeltaTime * 0.15f;
            if (st.rebuild_progress >= 1.0f) {
                st.rebuilding = false;
                st.rebuild_progress = 0;
            }
            ImGui::ProgressBar(st.rebuild_progress, ImVec2(-1, 20), "Rebuilding initramfs...");
        }
    }
    ImGui::EndChild();

    // Boot parameters
    ImGui::BeginChild("##boot_params", ImVec2(-1, 0), true);
    ImGui::TextColored(accent, "Boot Configuration");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Boot Parameters:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##params", st.boot_params, sizeof(st.boot_params), ImVec2(-1, 60));

    ImGui::Spacing();
    ImGui::Text("Timeout:");
    ImGui::SameLine(100);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("##timeout", &st.timeout_sec);
    if (st.timeout_sec < 0) st.timeout_sec = 0;
    if (st.timeout_sec > 60) st.timeout_sec = 60;
    ImGui::SameLine();
    ImGui::Text("seconds");

    ImGui::Spacing();
    if (ImGui::Button("Save Configuration", ImVec2(200, 30))) {
        // Save boot config
    }

    ImGui::EndChild();

    // Rebuild confirmation
    if (st.show_rebuild_confirm) {
        ImGui::OpenPopup("Rebuild Initramfs");
        st.show_rebuild_confirm = false;
    }
    if (ImGui::BeginPopupModal("Rebuild Initramfs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Rebuild initramfs for %s?",
                     st.kernels[st.selected_kernel].version);
        ImGui::TextDisabled("This may take a moment.");
        ImGui::Spacing();
        if (ImGui::Button("Rebuild", ImVec2(120, 30))) {
            st.rebuilding = true;
            st.rebuild_progress = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace straylight::boot
