// apps/audio_mixer/mixer.cpp
// Mixer model: channel management, peak meter decay, device routing
#include "mixer.h"

#include <straylight/log.h>

#include <algorithm>
#include <cmath>

namespace straylight::mixer {

// ---------------------------------------------------------------------------
// MixerChannel helpers
// ---------------------------------------------------------------------------

float MixerChannel::master_volume() const {
    if (node.channel_volumes.empty()) return 0.0f;
    float sum = 0.0f;
    for (float v : node.channel_volumes) sum += v;
    return sum / float(node.channel_volumes.size());
}

const std::string& MixerChannel::label() const {
    return node.app_name.empty() ? node.name : node.app_name;
}

// ---------------------------------------------------------------------------
// Mixer helpers
// ---------------------------------------------------------------------------

float Mixer::to_db(float linear) {
    if (linear <= 0.0f) return kMinDb;
    return std::max(kMinDb, 20.0f * std::log10(linear));
}

float Mixer::from_db(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// ---------------------------------------------------------------------------
// refresh() — rebuild the channel list from the PW snapshot
// ---------------------------------------------------------------------------

void Mixer::refresh() {
    const auto nodes = pw_.nodes();

    std::lock_guard lock(mtx_);

    // Preserve existing meter data for nodes that still exist
    for (auto& ch : channels_) {
        bool found = false;
        for (const auto& n : nodes) {
            if (n.id == ch.node.id) {
                found = true;
                // Update volume/mute state from fresh data, keep meters
                ch.node.channel_volumes = n.channel_volumes;
                ch.node.muted           = n.muted;
                ch.node.name            = n.name;
                ch.node.app_name        = n.app_name;
                // Resize meters if channel count changed
                if (ch.meters.size() != n.channel_volumes.size()) {
                    ch.meters.assign(n.channel_volumes.size(), PeakSample{});
                }
                break;
            }
        }
        if (!found) ch.node.id = 0; // Mark for removal
    }

    // Remove stale channels
    channels_.erase(
        std::remove_if(channels_.begin(), channels_.end(),
                       [](const MixerChannel& c){ return c.node.id == 0; }),
        channels_.end());

    // Add new nodes
    for (const auto& n : nodes) {
        bool existing = false;
        for (const auto& ch : channels_) {
            if (ch.node.id == n.id) { existing = true; break; }
        }
        if (!existing) {
            MixerChannel ch;
            ch.node   = n;
            ch.meters.assign(
                std::max(size_t(2), n.channel_volumes.size()),
                PeakSample{});
            channels_.push_back(std::move(ch));
        }
    }

    // Sort: sinks first, then streams alphabetically by label
    std::stable_sort(channels_.begin(), channels_.end(),
        [](const MixerChannel& a, const MixerChannel& b) {
            bool a_dev = a.node.is_device();
            bool b_dev = b.node.is_device();
            if (a_dev != b_dev) return a_dev > b_dev;
            return a.label() < b.label();
        });
}

// ---------------------------------------------------------------------------
// tick() — decay peak meters each frame
// ---------------------------------------------------------------------------

void Mixer::tick(float dt) {
    std::lock_guard lock(mtx_);
    const float decay = kPeakDecayPerSecond * dt;
    for (auto& ch : channels_) {
        for (auto& m : ch.meters) {
            // Exponential-ish decay
            m.peak = std::max(0.0f, m.peak - decay);
            m.rms  = std::max(0.0f, m.rms  - decay * 2.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// push_peak()
// ---------------------------------------------------------------------------

void Mixer::push_peak(uint32_t node_id, const std::vector<float>& peaks) {
    std::lock_guard lock(mtx_);
    for (auto& ch : channels_) {
        if (ch.node.id == node_id) {
            if (ch.meters.size() < peaks.size()) {
                ch.meters.resize(peaks.size());
            }
            for (size_t i = 0; i < peaks.size(); ++i) {
                float p = std::clamp(peaks[i], 0.0f, 1.0f);
                if (p > ch.meters[i].peak) ch.meters[i].peak = p;
                ch.meters[i].rms = p * 0.7f + ch.meters[i].rms * 0.3f;
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// channels() / sinks()
// ---------------------------------------------------------------------------

std::vector<MixerChannel> Mixer::channels() const {
    std::lock_guard lock(mtx_);
    return channels_;
}

std::vector<MixerChannel> Mixer::sinks() const {
    std::lock_guard lock(mtx_);
    std::vector<MixerChannel> result;
    for (const auto& ch : channels_) {
        if (ch.node.media_class == "Audio/Sink") result.push_back(ch);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Volume / mute control
// ---------------------------------------------------------------------------

void Mixer::set_master_volume(uint32_t node_id, float vol) {
    vol = std::clamp(vol, 0.0f, 1.5f);

    // Update local state immediately for responsive UI
    {
        std::lock_guard lock(mtx_);
        for (auto& ch : channels_) {
            if (ch.node.id == node_id) {
                for (auto& v : ch.node.channel_volumes) v = vol;
                break;
            }
        }
    }

    // Build volume vector matching channel count
    std::vector<float> vols;
    {
        std::lock_guard lock(mtx_);
        for (const auto& ch : channels_) {
            if (ch.node.id == node_id) {
                vols.assign(ch.node.channel_volumes.size(), vol);
                break;
            }
        }
    }
    if (vols.empty()) vols = {vol, vol};
    (void)pw_.set_volume(node_id, vols);
}

void Mixer::toggle_mute(uint32_t node_id) {
    bool new_mute = false;
    {
        std::lock_guard lock(mtx_);
        for (auto& ch : channels_) {
            if (ch.node.id == node_id) {
                ch.node.muted = !ch.node.muted;
                new_mute = ch.node.muted;
                break;
            }
        }
    }
    (void)pw_.set_mute(node_id, new_mute);
}

void Mixer::route_stream(uint32_t stream_node_id, uint32_t sink_node_id) {
    (void)pw_.route_to_sink(stream_node_id, sink_node_id);
    SL_INFO("Mixer: routing stream {} to sink {}", stream_node_id, sink_node_id);
}

} // namespace straylight::mixer
