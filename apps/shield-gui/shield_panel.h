// apps/shield-gui/shield_panel.h
// StrayLight Shield GUI — Security Audit panel
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace straylight::shield {

struct AuditFinding {
    std::string category;
    std::string description;
    int         severity; // 0=pass, 1=info, 2=medium, 3=high, 4=critical
    std::string fix_command;
    bool        fixed = false;
};

struct ShieldState {
    float security_score = 0.0f;
    std::vector<AuditFinding> findings;
    bool  auditing = false;
    float audit_progress = 0.0f;

    // Harden
    bool show_harden_dialog = false;
    int  harden_level = 1; // 0=basic, 1=standard, 2=paranoid
    bool hardening = false;
    float harden_progress = 0.0f;

    // Diff
    bool show_diff = false;
    std::vector<std::string> harden_changes;

    void init() {
        // Filesystem
        findings.push_back({"Filesystem", "/tmp has noexec mount option", 0, "", false});
        findings.push_back({"Filesystem", "/home has nosuid mount option", 0, "", false});
        findings.push_back({"Filesystem", "World-writable directory /var/tmp detected", 2, "chmod 1777 /var/tmp", false});
        findings.push_back({"Filesystem", "SUID binary /usr/bin/pkexec found", 2, "chmod u-s /usr/bin/pkexec", false});

        // Network
        findings.push_back({"Network", "Firewall (nftables) is active", 0, "", false});
        findings.push_back({"Network", "SSH port 22 open without fail2ban", 3, "systemctl enable --now fail2ban", false});
        findings.push_back({"Network", "IP forwarding disabled", 0, "", false});
        findings.push_back({"Network", "No open ports above 1024 (non-root)", 0, "", false});
        findings.push_back({"Network", "DNS-over-TLS not configured", 2, "straylight-probe configure-dot --server 1.1.1.1", false});

        // Users
        findings.push_back({"Users", "Root login via SSH disabled", 0, "", false});
        findings.push_back({"Users", "No users with empty passwords", 0, "", false});
        findings.push_back({"Users", "User 'guest' has shell access", 3, "usermod -s /usr/sbin/nologin guest", false});
        findings.push_back({"Users", "Password aging not enforced for 2 users", 2, "chage -M 90 -m 7 -W 14 <user>", false});

        // Kernel
        findings.push_back({"Kernel", "ASLR enabled (kernel.randomize_va_space=2)", 0, "", false});
        findings.push_back({"Kernel", "dmesg restricted to root", 0, "", false});
        findings.push_back({"Kernel", "Kernel module loading not restricted", 3, "echo 'install usb-storage /bin/false' >> /etc/modprobe.d/straylight-hardening.conf", false});
        findings.push_back({"Kernel", "Core dumps enabled for all users", 2, "echo '* hard core 0' >> /etc/security/limits.d/straylight.conf", false});

        // Services
        findings.push_back({"Services", "47 services running, 2 unnecessary", 2, "systemctl disable --now avahi-daemon cups", false});
        findings.push_back({"Services", "Automatic security updates not enabled", 3, "straylight-migrate enable-auto-updates --security-only", false});
        findings.push_back({"Services", "All services use dedicated user accounts", 0, "", false});
        findings.push_back({"Services", "AppArmor profiles loaded for 38/47 services", 1, "aa-enforce /etc/apparmor.d/*", false});

        // Calculate score
        recalc_score();

        // Harden changes preview
        harden_changes = {
            "--- /etc/ssh/sshd_config",
            "+++ /etc/ssh/sshd_config",
            "@@ -12,3 +12,3 @@",
            "-#MaxAuthTries 6",
            "+MaxAuthTries 3",
            "-#PermitEmptyPasswords no",
            "+PermitEmptyPasswords no",
            "",
            "--- /etc/sysctl.d/99-straylight-hardening.conf (new file)",
            "+++ /etc/sysctl.d/99-straylight-hardening.conf",
            "+kernel.kptr_restrict = 2",
            "+net.ipv4.conf.all.rp_filter = 1",
            "+net.ipv4.conf.all.send_redirects = 0",
            "+net.ipv6.conf.all.accept_redirects = 0",
            "",
            "--- /etc/security/limits.d/straylight.conf (new file)",
            "+++ /etc/security/limits.d/straylight.conf",
            "+* hard core 0",
            "+* soft nproc 4096",
        };
    }

    void recalc_score() {
        int total = 0, passed = 0;
        for (auto& f : findings) {
            total++;
            if (f.severity == 0 || f.fixed) passed++;
        }
        security_score = total > 0 ? (float)passed / (float)total * 100.0f : 0.0f;
    }
};

inline void render_shield_panel(ShieldState& st) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.67f, 1.0f));
    ImGui::Text("STRAYLIGHT SHIELD");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("Close")) {}
    ImGui::Separator();
    ImGui::Spacing();

    // Top bar: score gauge + buttons
    float top_h = 120.0f;
    if (ImGui::BeginChild("##top_bar", ImVec2(0, top_h), false)) {
        // Security score gauge
        if (ImGui::BeginChild("##score_gauge", ImVec2(200, -1), false)) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 center = ImGui::GetCursorScreenPos();
            center.x += 100;
            center.y += 55;
            float radius = 45.0f;

            for (float a = 3.14159f; a < 3.14159f * 2.0f; a += 0.02f) {
                float x1 = center.x + cosf(a) * radius;
                float y1 = center.y + sinf(a) * radius;
                float x2 = center.x + cosf(a) * (radius - 10.0f);
                float y2 = center.y + sinf(a) * (radius - 10.0f);
                draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(40, 40, 60, 255), 2.0f);
            }
            float frac = st.security_score / 100.0f;
            float end_angle = 3.14159f + 3.14159f * frac;
            ImU32 col = st.security_score >= 80 ? IM_COL32(0, 200, 130, 255)
                      : st.security_score >= 60 ? IM_COL32(200, 180, 40, 255)
                      : IM_COL32(200, 60, 60, 255);
            for (float a = 3.14159f; a < end_angle; a += 0.02f) {
                float x1 = center.x + cosf(a) * radius;
                float y1 = center.y + sinf(a) * radius;
                float x2 = center.x + cosf(a) * (radius - 10.0f);
                float y2 = center.y + sinf(a) * (radius - 10.0f);
                draw->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), col, 3.0f);
            }
            char score_text[16];
            snprintf(score_text, sizeof(score_text), "%.0f", st.security_score);
            ImVec2 ts = ImGui::CalcTextSize(score_text);
            draw->AddText(nullptr, 24.0f, ImVec2(center.x - ts.x, center.y - 15), col, score_text);
            draw->AddText(ImVec2(center.x - 30, center.y + 8), IM_COL32(160, 160, 160, 255), "Security Score");
            ImGui::Dummy(ImVec2(0, 110));
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Buttons
        if (ImGui::BeginChild("##buttons", ImVec2(0, -1), false)) {
            ImGui::Spacing();
            if (st.auditing) {
                ImGui::BeginDisabled();
                ImGui::Button("Running Audit...", ImVec2(160, 32));
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::ProgressBar(st.audit_progress, ImVec2(200, 32));
                st.audit_progress += ImGui::GetIO().DeltaTime * 0.15f;
                if (st.audit_progress >= 1.0f) { st.auditing = false; st.audit_progress = 1.0f; }
            } else {
                if (ImGui::Button("Run Audit", ImVec2(140, 32))) {
                    st.auditing = true;
                    st.audit_progress = 0.0f;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Harden System", ImVec2(140, 32))) {
                st.show_harden_dialog = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(st.show_diff ? "Hide Changes" : "Show Changes", ImVec2(140, 32))) {
                st.show_diff = !st.show_diff;
            }

            ImGui::Spacing();
            // Summary counts
            int pass = 0, info = 0, med = 0, high = 0, crit = 0;
            for (auto& f : st.findings) {
                if (f.fixed) { pass++; continue; }
                switch (f.severity) {
                    case 0: pass++; break;
                    case 1: info++; break;
                    case 2: med++; break;
                    case 3: high++; break;
                    case 4: crit++; break;
                }
            }
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "%d Pass", pass);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "%d Info", info);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%d Medium", med);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%d High", high);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%d Critical", crit);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Spacing();

    // Main content: findings + optional diff
    float findings_w = st.show_diff ? ImGui::GetContentRegionAvail().x * 0.6f : ImGui::GetContentRegionAvail().x;

    if (ImGui::BeginChild("##findings", ImVec2(findings_w, -1), false)) {
        std::string current_cat;
        for (int i = 0; i < (int)st.findings.size(); ++i) {
            auto& f = st.findings[i];
            if (f.category != current_cat) {
                current_cat = f.category;
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "%s", current_cat.c_str());
                ImGui::Separator();
            }

            ImGui::PushID(i);

            // Severity icon
            ImVec4 sev_col;
            const char* sev_icon;
            if (f.fixed) {
                sev_col = ImVec4(0.2f, 1.0f, 0.5f, 1.0f);
                sev_icon = "[FIXED]";
            } else {
                switch (f.severity) {
                    case 0: sev_col = ImVec4(0.2f, 1.0f, 0.5f, 1.0f); sev_icon = "[PASS]"; break;
                    case 1: sev_col = ImVec4(0.5f, 0.7f, 1.0f, 1.0f); sev_icon = "[INFO]"; break;
                    case 2: sev_col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); sev_icon = "[MED] "; break;
                    case 3: sev_col = ImVec4(1.0f, 0.5f, 0.2f, 1.0f); sev_icon = "[HIGH]"; break;
                    default: sev_col = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); sev_icon = "[CRIT]"; break;
                }
            }

            ImGui::TextColored(sev_col, "%s", sev_icon);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", f.description.c_str());

            // Fix button (only for unfixed findings with a command)
            if (!f.fixed && !f.fix_command.empty() && f.severity > 0) {
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.55f, 0.38f, 0.8f));
                if (ImGui::SmallButton("Fix")) {
                    f.fixed = true;
                    st.recalc_score();
                }
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Diff panel
    if (st.show_diff) {
        ImGui::SameLine();
        if (ImGui::BeginChild("##diff_panel", ImVec2(0, -1), true)) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.67f, 1.0f), "Hardening Changes Preview");
            ImGui::Separator();
            ImGui::Spacing();
            for (auto& line : st.harden_changes) {
                if (line.empty()) {
                    ImGui::Spacing();
                } else if (line[0] == '+') {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", line.c_str());
                } else if (line[0] == '-') {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", line.c_str());
                } else if (line[0] == '@') {
                    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "%s", line.c_str());
                } else {
                    ImGui::Text("%s", line.c_str());
                }
            }
        }
        ImGui::EndChild();
    }

    // Harden dialog
    if (st.show_harden_dialog) {
        ImGui::OpenPopup("Harden System");
        st.show_harden_dialog = false;
    }
    if (ImGui::BeginPopupModal("Harden System", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select hardening level:");
        ImGui::Spacing();
        const char* levels[] = {"Basic - Essential security fixes only",
                                "Standard - Recommended security posture",
                                "Paranoid - Maximum lockdown (may break some tools)"};
        for (int i = 0; i < 3; ++i) {
            ImGui::RadioButton(levels[i], &st.harden_level, i);
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "A snapshot will be created before hardening.");
        ImGui::Spacing();

        if (st.hardening) {
            ImGui::ProgressBar(st.harden_progress, ImVec2(-1, 30));
            st.harden_progress += ImGui::GetIO().DeltaTime * 0.1f;
            if (st.harden_progress >= 1.0f) {
                st.hardening = false;
                st.harden_progress = 0.0f;
                // Fix applicable findings
                for (auto& f : st.findings) {
                    if (!f.fix_command.empty() && f.severity <= (st.harden_level + 2)) {
                        f.fixed = true;
                    }
                }
                st.recalc_score();
                ImGui::CloseCurrentPopup();
            }
        } else {
            if (ImGui::Button("Apply", ImVec2(120, 30))) {
                st.hardening = true;
                st.harden_progress = 0.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 30))) { ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }
}

} // namespace straylight::shield
