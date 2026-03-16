// shell/widgets/screenshot.cpp
// Screenshot capture stub implementation
#include "screenshot.h"

namespace straylight::shell {

void Screenshot::capture_fullscreen(const std::string& /*output_path*/) {
    // TODO(plan5): Use wlr-screencopy-unstable-v1 to capture output framebuffer
}

void Screenshot::capture_region(int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                const std::string& /*output_path*/) {
    // TODO(plan5): Region selection overlay + wlr-screencopy
}

} // namespace straylight::shell
