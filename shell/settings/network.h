// shell/settings/network.h
// Network settings panel (stub)
#pragma once

namespace straylight::shell {

/// Network settings panel — WiFi, Ethernet, VPN configuration.
/// TODO(plan5): Implement full NetworkManager D-Bus integration.
class NetworkSettings {
public:
    NetworkSettings() = default;
    ~NetworkSettings() = default;

    void render();
};

} // namespace straylight::shell
