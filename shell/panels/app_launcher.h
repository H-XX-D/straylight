// shell/panels/app_launcher.h
// Slide-out application launcher panel
#pragma once

#include <string>
#include <vector>

namespace straylight::shell {

/// Represents a parsed .desktop entry.
struct DesktopEntry {
    std::string name;
    std::string exec;
    std::string icon;
    std::string categories;
    std::string comment;
};

/// Application launcher panel with search, categories, and slide animation.
/// Reads .desktop files from standard XDG directories and allows launching
/// applications via fork/exec.
class AppLauncher {
public:
    AppLauncher();
    ~AppLauncher();

    /// Render the launcher panel (if visible). Called once per frame.
    void render();

    /// Toggle visibility (opens/closes the launcher).
    void toggle();

    /// Show the launcher.
    void show();

    /// Hide the launcher.
    void hide();

    /// Returns true if the launcher is currently visible.
    [[nodiscard]] bool is_visible() const;

    /// Parse a .desktop file and return a DesktopEntry.
    /// Public for testability.
    static DesktopEntry parse_desktop_file(const std::string& path);

    /// Filter entries by search term. Matches name and categories.
    static std::vector<const DesktopEntry*> filter(
        const std::vector<DesktopEntry>& entries,
        const std::string& search_term);

    /// Strip %U, %u, %F, %f placeholders from an Exec string.
    static std::string strip_exec_placeholders(const std::string& exec);

    /// Launch an application by exec string.
    static void launch(const std::string& exec);

private:
    bool visible_ = false;
    float slide_progress_ = 0.0f;  // 0.0 = hidden, 1.0 = fully visible
    char search_buf_[256] = {};
    std::vector<DesktopEntry> entries_;
    std::string active_category_ = "All";

    void scan_desktop_files();
};

} // namespace straylight::shell
