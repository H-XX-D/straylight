// apps/wizard/pages/ml_setup.cpp
// ML setup page implementation
#include "ml_setup.h"

#include <straylight/log.h>

#include <imgui.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

namespace straylight::wizard {

namespace fs = std::filesystem;

static const char* kGpuProfiles[] = {"performance", "balanced", "power-save"};
static constexpr int kNumProfiles = 3;

/// Check if a Python import succeeds within a timeout.
static bool check_python_import(const char* module, int timeout_s = 2) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec python3 -c "import <module>"
        std::string cmd = std::string("import ") + module;
        execlp("python3", "python3", "-c", cmd.c_str(), nullptr);
        _exit(127);
    } else if (pid < 0) {
        return false;
    }

    // Parent: wait with timeout
    for (int i = 0; i < timeout_s * 10; ++i) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        usleep(100000);  // 100ms
    }

    // Timeout — kill child
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    return false;
}

MlSetupPage::MlSetupPage() {
    frameworks_ = {
        {"PyTorch", false},
        {"JAX", false},
        {"TensorFlow", false},
    };
}

void MlSetupPage::detect_frameworks() {
    SL_INFO("Detecting ML frameworks...");

    const char* modules[] = {"torch", "jax", "tensorflow"};
    for (size_t i = 0; i < frameworks_.size(); ++i) {
        frameworks_[i].present = check_python_import(modules[i]);
        SL_INFO("  {}: {}", frameworks_[i].name,
                frameworks_[i].present ? "found" : "not found");
    }

    // Detect GPU vendor
    if (fs::exists("/proc/driver/nvidia/version")) {
        gpu_vendor_ = "NVIDIA";
    } else {
        // Check lspci for AMD
        // (simplified — real impl would parse lspci output)
        gpu_vendor_ = "Unknown";
    }

    detected_ = true;
}

bool MlSetupPage::render() {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##WizardML", nullptr, flags);

    ImGui::SetCursorPosY(40.0f);
    ImGui::SetCursorPosX(40.0f);
    ImGui::Text("ML & GPU Setup");
    ImGui::Separator();
    ImGui::Spacing();

    // Detect button
    ImGui::SetCursorPosX(60.0f);
    if (!detected_) {
        if (ImGui::Button("Detect Frameworks & GPU", ImVec2(250, 36))) {
            detect_frameworks();
        }
    } else {
        // Show results
        ImGui::Text("GPU Vendor: %s", gpu_vendor_.c_str());
        ImGui::Spacing();

        ImGui::Text("Installed Frameworks:");
        for (const auto& fw : frameworks_) {
            ImGui::SetCursorPosX(80.0f);
            if (fw.present) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
                ImGui::Text("[OK] %s", fw.name.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("[--] %s", fw.name.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // GPU scheduling profile
        ImGui::SetCursorPosX(60.0f);
        ImGui::Text("GPU Scheduling Profile");
        ImGui::SetCursorPosX(60.0f);
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("##gpu_profile", &profile_index_,
                         kGpuProfiles, kNumProfiles)) {
            gpu_profile_ = kGpuProfiles[profile_index_];
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Buttons
    ImGui::SetCursorPos(ImVec2(40.0f, io.DisplaySize.y - 80.0f));
    bool advance = false;
    if (ImGui::Button("Next", ImVec2(120, 40))) {
        // Write GPU profile
        if (detected_) {
            const char* home = std::getenv("HOME");
            const char* xdg = std::getenv("XDG_CONFIG_HOME");
            std::string config_dir;
            if (xdg) {
                config_dir = std::string(xdg) + "/straylight";
            } else if (home) {
                config_dir = std::string(home) + "/.config/straylight";
            }

            if (!config_dir.empty()) {
                std::error_code ec;
                fs::create_directories(config_dir, ec);
                std::ofstream f(config_dir + "/gpu_profile.json",
                                std::ios::trunc);
                f << "{\n  \"profile\": \"" << gpu_profile_ << "\"\n}\n";
            }
        }
        advance = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Skip", ImVec2(80, 40))) {
        advance = true;
    }

    ImGui::End();
    return advance;
}

} // namespace straylight::wizard
