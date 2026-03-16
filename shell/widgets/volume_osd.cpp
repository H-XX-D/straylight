// shell/widgets/volume_osd.cpp
// Volume OSD stub implementation
#include "volume_osd.h"

namespace straylight::shell {

void VolumeOsd::show(float level) {
    level_ = level;
    visible_ = true;
    // TODO(plan5): Start fade timer, trigger OSD layer surface
}

void VolumeOsd::render() {
    // TODO(plan5): Render volume bar overlay via ImGui on kOverlay layer surface
}

} // namespace straylight::shell
