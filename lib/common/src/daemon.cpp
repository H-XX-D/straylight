// lib/common/src/daemon.cpp
#include <straylight/daemon.h>

namespace straylight {

std::atomic<bool> DaemonBase::g_shutdown_{false};

int DaemonBase::run(const Config& cfg) {
    struct sigaction sa{};
    sa.sa_handler = [](int) { g_shutdown_.store(true); };
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    auto r = init(cfg);
    if (!r.has_value()) {
        SL_ERROR("daemon init failed: {}", r.error().message());
        return 1;
    }
    SL_INFO("daemon started (pid={})", getpid());

    while (!g_shutdown_.load()) {
        auto tr = tick();
        if (!tr.has_value()) {
            SL_ERROR("tick error: {}", tr.error().message());
            break;
        }
    }
    shutdown();
    SL_INFO("daemon stopped");
    return 0;
}

} // namespace straylight
