// apps/audio_mixer/pipewire_client.h
// PipeWire core connection, node/port enumeration, volume control via spa_pod.
#pragma once

#include <straylight/result.h>
#include <straylight/error.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace straylight::mixer {

/// Represents a PipeWire node (application stream or device sink/source).
struct PwNode {
    uint32_t    id          = 0;
    std::string name;           ///< node.name property
    std::string app_name;       ///< application.name property
    std::string media_class;    ///< media.class e.g. "Stream/Output/Audio"

    /// Current channel volumes (linear, 0.0–1.0+).
    std::vector<float> channel_volumes;

    /// Mute state.
    bool muted = false;

    /// Most-recent peak meter values per channel (0.0–1.0, updated by callbacks).
    std::vector<float> peak_levels;

    /// True if this node is a hardware sink or source (not an app stream).
    [[nodiscard]] bool is_device() const;
    [[nodiscard]] bool is_sink_stream() const;
    [[nodiscard]] bool is_source_stream() const;
};

/// Callback invoked on the PipeWire thread whenever the node list changes.
using NodeListChangedFn = std::function<void()>;

/// Manages a pw_main_loop, pw_core, pw_registry, and pw_metadata connection.
/// All PipeWire callbacks execute on the internal pw_main_loop thread.
/// Public methods that mutate node state are thread-safe.
class PipeWireClient {
public:
    PipeWireClient() = default;
    ~PipeWireClient() { stop(); }

    PipeWireClient(const PipeWireClient&)            = delete;
    PipeWireClient& operator=(const PipeWireClient&) = delete;

    /// Connect to the PipeWire daemon and start the event loop thread.
    Result<void, SLError> start(NodeListChangedFn on_changed = {});

    /// Stop the event loop and disconnect.
    void stop();

    /// Returns true while the PipeWire event loop is running.
    [[nodiscard]] bool running() const { return running_.load(); }

    /// Thread-safe snapshot of currently known nodes.
    [[nodiscard]] std::vector<PwNode> nodes() const;

    /// Set per-channel volumes for the given node (values clamped 0.0–1.5).
    Result<void, SLError> set_volume(uint32_t node_id,
                                      const std::vector<float>& volumes);

    /// Set mute state for the given node.
    Result<void, SLError> set_mute(uint32_t node_id, bool muted);

    /// Route an application stream to the given sink node.
    Result<void, SLError> route_to_sink(uint32_t stream_node_id,
                                         uint32_t sink_node_id);

private:
    // PipeWire objects — accessed only from the pw_main_loop thread
    struct pw_main_loop*  loop_    = nullptr;
    struct pw_context*    ctx_     = nullptr;
    struct pw_core*       core_    = nullptr;
    struct pw_registry*   registry_ = nullptr;

    // Node table — guarded by mtx_
    mutable std::mutex       mtx_;
    std::vector<PwNode>      nodes_;
    NodeListChangedFn        on_changed_;
    std::atomic<bool>        running_{false};
    std::thread              thread_;

    // Internal helpers
    void pw_thread_main();

    void on_registry_global(uint32_t id, const char* type,
                             uint32_t version,
                             const struct spa_dict* props);

    void on_registry_global_remove(uint32_t id);

    void update_node_volumes(uint32_t id,
                              const struct spa_pod* param);

    // C-style trampoline callbacks (PipeWire API requires plain function ptrs)
    static void registry_event_global(void* data, uint32_t id, uint32_t perms,
                                       const char* type, uint32_t version,
                                       const struct spa_dict* props);
    static void registry_event_global_remove(void* data, uint32_t id);

    static void node_event_param(void* data, int seq, uint32_t id,
                                  uint32_t index, uint32_t next,
                                  const struct spa_pod* param);
    static void node_event_info(void* data,
                                 const struct pw_node_info* info);
};

} // namespace straylight::mixer
