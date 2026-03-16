// shell/panels/app_launcher.cpp
// Application launcher with .desktop file scanning and search
#include "app_launcher.h"

#include <straylight/log.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

namespace straylight::shell {

namespace fs = std::filesystem;

static constexpr float kPanelWidth   = 300.0f;
static constexpr float kSlideSpeed   = 1.0f / 0.15f;  // 150ms animation
static const std::vector<std::string> kCategories = {
    "All", "Development", "Internet", "Settings", "Accessories"
};

AppLauncher::AppLauncher() {
    scan_desktop_files();
}

AppLauncher::~AppLauncher() = default;

void AppLauncher::scan_desktop_files() {
    entries_.clear();

    const std::vector<std::string> dirs = {
        "/usr/share/applications",
        std::string(getenv("HOME") ? getenv("HOME") : "") +
            "/.local/share/applications"
    };

    for (const auto& dir : dirs) {
        if (!fs::exists(dir)) continue;
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".desktop") {
                auto de = parse_desktop_file(entry.path().string());
                if (!de.name.empty() && !de.exec.empty()) {
                    entries_.push_back(std::move(de));
                }
            }
        }
    }

    SL_INFO("Scanned {} .desktop entries", entries_.size());
}

DesktopEntry AppLauncher::parse_desktop_file(const std::string& path) {
    DesktopEntry entry;
    std::ifstream file(path);
    if (!file.is_open()) return entry;

    std::string line;
    bool in_desktop_entry = false;

    while (std::getline(file, line)) {
        // Trim whitespace
        if (line.empty()) continue;

        if (line[0] == '[') {
            in_desktop_entry = (line == "[Desktop Entry]");
            continue;
        }

        if (!in_desktop_entry) continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "Name")        entry.name       = val;
        else if (key == "Exec")   entry.exec       = val;
        else if (key == "Icon")   entry.icon       = val;
        else if (key == "Categories") entry.categories = val;
        else if (key == "Comment")    entry.comment    = val;
    }

    return entry;
}

std::string AppLauncher::strip_exec_placeholders(const std::string& exec) {
    // Remove %U, %u, %F, %f, %i, %c, %k placeholders
    static const std::regex placeholder_re(R"(\s*%[UuFfick]\s*)");
    return std::regex_replace(exec, placeholder_re, " ");
}

std::vector<const DesktopEntry*> AppLauncher::filter(
    const std::vector<DesktopEntry>& entries,
    const std::string& search_term) {

    std::vector<const DesktopEntry*> result;

    if (search_term.empty()) {
        for (const auto& e : entries) {
            result.push_back(&e);
        }
        return result;
    }

    // Case-insensitive substring match on name and categories
    std::string lower_term = search_term;
    std::transform(lower_term.begin(), lower_term.end(),
                   lower_term.begin(), ::tolower);

    for (const auto& e : entries) {
        std::string lower_name = e.name;
        std::transform(lower_name.begin(), lower_name.end(),
                       lower_name.begin(), ::tolower);

        std::string lower_cat = e.categories;
        std::transform(lower_cat.begin(), lower_cat.end(),
                       lower_cat.begin(), ::tolower);

        if (lower_name.find(lower_term) != std::string::npos ||
            lower_cat.find(lower_term) != std::string::npos) {
            result.push_back(&e);
        }
    }

    return result;
}

void AppLauncher::launch(const std::string& exec) {
    std::string clean_exec = strip_exec_placeholders(exec);
    SL_INFO("Launching: {}", clean_exec);

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        setsid();
        execl("/bin/sh", "sh", "-c", clean_exec.c_str(), nullptr);
        _exit(127);
    } else if (pid < 0) {
        SL_ERROR("fork() failed for: {}", clean_exec);
    }
    // Parent does not wait — child is orphaned to init/systemd
}

void AppLauncher::toggle() {
    visible_ = !visible_;
}

void AppLauncher::show() {
    visible_ = true;
}

void AppLauncher::hide() {
    visible_ = false;
    search_buf_[0] = '\0';
}

bool AppLauncher::is_visible() const {
    return visible_ || slide_progress_ > 0.01f;
}

void AppLauncher::render() {
    // Animate slide-in / slide-out
    float dt = ImGui::GetIO().DeltaTime;
    float target = visible_ ? 1.0f : 0.0f;
    if (slide_progress_ < target) {
        slide_progress_ = std::min(slide_progress_ + dt * kSlideSpeed, 1.0f);
    } else if (slide_progress_ > target) {
        slide_progress_ = std::max(slide_progress_ - dt * kSlideSpeed, 0.0f);
    }

    if (slide_progress_ <= 0.01f && !visible_) return;

    // Lerp x position: -kPanelWidth (hidden) to 0 (visible)
    float x = -kPanelWidth * (1.0f - slide_progress_);

    ImGui::SetNextWindowPos(ImVec2(x, 32.0f));  // below top bar
    ImGui::SetNextWindowSize(
        ImVec2(kPanelWidth, ImGui::GetIO().DisplaySize.y - 32.0f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("##AppLauncher", nullptr, flags);

    // Search box
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Search", search_buf_, sizeof(search_buf_));

    ImGui::Separator();

    // Category tabs
    for (const auto& cat : kCategories) {
        bool active = (cat == active_category_);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::SmallButton(cat.c_str())) {
            active_category_ = cat;
        }
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    // Filter entries
    std::string search_term(search_buf_);
    auto filtered = filter(entries_, search_term);

    // Filter by category if not "All"
    if (active_category_ != "All") {
        std::vector<const DesktopEntry*> cat_filtered;
        for (const auto* e : filtered) {
            if (e->categories.find(active_category_) != std::string::npos) {
                cat_filtered.push_back(e);
            }
        }
        filtered = std::move(cat_filtered);
    }

    // Render entries
    for (const auto* entry : filtered) {
        if (ImGui::Selectable(entry->name.c_str())) {
            launch(entry->exec);
            hide();
        }
        if (!entry->comment.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", entry->comment.c_str());
        }
    }

    ImGui::End();
}

} // namespace straylight::shell
