// shell/settings/input.h
// Input settings panel (stub)
#pragma once

namespace straylight::shell {

/// Input settings panel — keyboard layout, repeat rate, touchpad options.
/// TODO(plan5): Implement libinput configuration via compositor IPC.
class InputSettings {
public:
    InputSettings() = default;
    ~InputSettings() = default;

    void render();
};

} // namespace straylight::shell
