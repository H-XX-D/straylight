// shell/widgets/screenshot.h
// Screenshot capture utility (stub)
#pragma once

#include <string>

namespace straylight::shell {

/// Screenshot capture utility.
/// TODO(plan5): Implement wlr-screencopy-unstable-v1 integration.
class Screenshot {
public:
    Screenshot() = default;
    ~Screenshot() = default;

    /// Capture the full screen to a PNG file.
    void capture_fullscreen(const std::string& output_path);

    /// Capture a rectangular region.
    void capture_region(int x, int y, int w, int h,
                        const std::string& output_path);
};

} // namespace straylight::shell
