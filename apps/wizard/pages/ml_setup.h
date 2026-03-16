// apps/wizard/pages/ml_setup.h
// ML framework detection and GPU profile setup
#pragma once

#include <string>
#include <vector>

namespace straylight::wizard {

/// Detected ML framework.
struct MlFramework {
    std::string name;
    bool present = false;
};

/// ML setup page — detect installed frameworks and configure GPU profile.
class MlSetupPage {
public:
    MlSetupPage();
    ~MlSetupPage() = default;

    /// Render the page. Returns true to advance.
    bool render();

    /// Detect installed ML frameworks (for testing, accepts override).
    void detect_frameworks();

    /// Get detected frameworks.
    [[nodiscard]] const std::vector<MlFramework>& frameworks() const {
        return frameworks_;
    }

    /// Get selected GPU profile name.
    [[nodiscard]] const std::string& gpu_profile() const {
        return gpu_profile_;
    }

private:
    std::vector<MlFramework> frameworks_;
    std::string gpu_vendor_;
    std::string gpu_profile_ = "balanced";
    int profile_index_ = 1;
    bool detected_ = false;
};

} // namespace straylight::wizard
