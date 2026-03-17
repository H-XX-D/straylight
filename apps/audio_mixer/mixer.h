// apps/audio_mixer/mixer.h
// Per-application volume model with peak meter data and device routing.
#pragma once

#include "pipewire_client.h"

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace straylight::mixer {

/// Peak meter sample: exponential-decay peak + RMS for display.
struct PeakSample {
    float peak = 0.0f;   ///< held peak (decays over ~1.5 s)
    float rms  = 0.0f;   ///< short-term RMS
};

/// Extended node model used by the mixer UI.
struct MixerChannel {
    PwNode              node;
    std::vector<PeakSample> meters;  ///< one entry per channel

    /// Master volume (average of channel volumes), 0.0–1.0.
    [[nodiscard]] float master_volume() const;

    /// Display label: prefer app_name, fallback to node.name.
    [[nodiscard]] const std::string& label() const;
};

/// Aggregates PipeWire node data into channels suitable for the mixer UI.
/// Handles peak meter decay and device routing bookkeeping.
class Mixer {
public:
    explicit Mixer(PipeWireClient& pw) : pw_(pw) {}

    /// Refresh channel list from the PipeWire client snapshot.
    void refresh();

    /// Update peak meter decay (call once per frame, dt in seconds).
    void tick(float dt);

    /// Push a new peak sample for the given node (called from PW callback).
    void push_peak(uint32_t node_id, const std::vector<float>& peaks);

    /// Thread-safe channel snapshot.
    [[nodiscard]] std::vector<MixerChannel> channels() const;

    /// Sink-only channel list for the output device selector.
    [[nodiscard]] std::vector<MixerChannel> sinks() const;

    /// Set master volume for a channel (broadcasts to all sub-channels).
    void set_master_volume(uint32_t node_id, float vol);

    /// Toggle mute for a channel.
    void toggle_mute(uint32_t node_id);

    /// Route a stream channel to the given sink node.
    void route_stream(uint32_t stream_node_id, uint32_t sink_node_id);

    static constexpr float kPeakDecayPerSecond = 0.8f;  ///< linear decay per second
    static constexpr float kMinDb              = -60.0f;
    static constexpr float kMaxDb              = 0.0f;

    /// Convert linear (0–1) to dB.
    static float to_db(float linear);

    /// Convert dB to linear.
    static float from_db(float db);

private:
    PipeWireClient& pw_;
    mutable std::mutex  mtx_;
    std::vector<MixerChannel> channels_;
};

} // namespace straylight::mixer
