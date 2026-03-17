// apps/player/visualizer.cpp
// StrayLight Player — audio visualiser implementation
#include "visualizer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace straylight::player {

static constexpr float kMinDb = -80.f;
static constexpr float kMaxDb =   0.f;
static constexpr float kSmoothFast  = 0.35f;
static constexpr float kSmoothSlow  = 0.10f;
static constexpr int   kPeakHoldFrames = 45;

// ---------------------------------------------------------------------------

float AudioVisualizer::db_to_linear(float db) {
    if (db <= kMinDb) return 0.f;
    return (db - kMinDb) / (kMaxDb - kMinDb);
}

uint32_t AudioVisualizer::level_colour(float n) {
    // green → yellow → red
    n = std::clamp(n, 0.f, 1.f);
    uint8_t r, g;
    if (n < 0.5f) {
        r = static_cast<uint8_t>(n * 2.f * 200.f);
        g = 200;
    } else {
        r = 200;
        g = static_cast<uint8_t>((1.f - (n - 0.5f) * 2.f) * 200.f);
    }
    return IM_COL32(r, g, 40, 230);
}

// ---------------------------------------------------------------------------

AudioVisualizer::AudioVisualizer() {
    std::fill(std::begin(bar_levels_),  std::end(bar_levels_),  0.f);
    std::fill(std::begin(bar_targets_), std::end(bar_targets_), 0.f);
    std::fill(std::begin(rms_history_), std::end(rms_history_), 0.f);
}

// ---------------------------------------------------------------------------

void AudioVisualizer::update(const PlaybackEngine::LevelData& lvl) {
    for (int ch = 0; ch < kChannels; ++ch) {
        const float peak = db_to_linear(static_cast<float>(lvl.peak_db[ch]));
        const float rms  = db_to_linear(static_cast<float>(lvl.rms_db[ch]));

        // Smooth towards new value
        smoothed_peak_[ch] = smoothed_peak_[ch] + kSmoothFast * (peak - smoothed_peak_[ch]);
        smoothed_rms_[ch]  = smoothed_rms_[ch]  + kSmoothSlow * (rms  - smoothed_rms_[ch]);

        // Peak hold
        if (peak >= peak_hold_[ch]) {
            peak_hold_[ch]       = peak;
            peak_hold_timer_[ch] = kPeakHoldFrames;
        } else if (peak_hold_timer_[ch] > 0) {
            --peak_hold_timer_[ch];
        } else {
            peak_hold_[ch] -= 0.005f;
            if (peak_hold_[ch] < 0.f) peak_hold_[ch] = 0.f;
        }
    }

    // RMS history for oscilloscope (average of channels)
    const float avg_rms = (smoothed_rms_[0] + smoothed_rms_[1]) * 0.5f;
    rms_history_[static_cast<size_t>(history_head_)] = avg_rms;
    history_head_ = (history_head_ + 1) % kHistLen;

    // Spectrum bars: distribute peak/rms energy across frequency bins using a
    // simple logarithmic simulation. Without actual FFT data we synthesise bars
    // based on the peak level with harmonic falloff.
    const float master = std::max(smoothed_peak_[0], smoothed_peak_[1]);
    for (int b = 0; b < kBars; ++b) {
        // Each bar falls off as a fraction of master with some spacing
        const float decay = 1.f - static_cast<float>(b) / static_cast<float>(kBars);
        // Add a bit of low-frequency "bass" bump for the first few bars
        float boost = (b < 3) ? (1.f + (3 - b) * 0.4f) : 1.f;
        bar_targets_[b] = master * decay * boost * 0.9f;
        // Random-ish variation per bar to make it look less static
        const float jitter = static_cast<float>((b * 7 + history_head_) % 5) * 0.02f;
        bar_targets_[b] = std::clamp(bar_targets_[b] + jitter * master, 0.f, 1.f);

        // Animate towards target
        const float k = (bar_targets_[b] > bar_levels_[b]) ? kSmoothFast : kSmoothSlow;
        bar_levels_[b] += k * (bar_targets_[b] - bar_levels_[b]);
    }
}

// ---------------------------------------------------------------------------

void AudioVisualizer::draw(ImDrawList* dl, ImVec2 pos, ImVec2 size) const {
    // Background
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      IM_COL32(15, 15, 25, 240));

    switch (mode_) {
    case VisMode::Spectrum:     draw_spectrum(dl, pos, size);    break;
    case VisMode::VUMeter:      draw_vu_meter(dl, pos, size);    break;
    case VisMode::Oscilloscope: draw_oscilloscope(dl, pos, size); break;
    }
}

// ---------------------------------------------------------------------------
// Spectrum view
// ---------------------------------------------------------------------------

void AudioVisualizer::draw_spectrum(ImDrawList* dl, ImVec2 pos, ImVec2 size) const {
    const float bar_w   = (size.x - 4.f) / static_cast<float>(kBars);
    const float padding = bar_w * 0.15f;

    for (int b = 0; b < kBars; ++b) {
        const float level = bar_levels_[b];
        const float bar_h = level * (size.y - 4.f);
        const float x0    = pos.x + 2.f + static_cast<float>(b) * bar_w + padding;
        const float x1    = x0 + bar_w - padding * 2.f;
        const float y0    = pos.y + size.y - 2.f - bar_h;
        const float y1    = pos.y + size.y - 2.f;

        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                          level_colour(level));

        // Peak cap (bright white line at top of bar)
        if (bar_h > 2.f) {
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y0),
                        IM_COL32(240, 240, 240, 180), 1.5f);
        }
    }

    // Frequency label
    dl->AddText(ImVec2(pos.x + 4.f, pos.y + 2.f),
                IM_COL32(120, 120, 140, 200), "Spectrum");
}

// ---------------------------------------------------------------------------
// VU Meter view
// ---------------------------------------------------------------------------

void AudioVisualizer::draw_vu_meter(ImDrawList* dl, ImVec2 pos, ImVec2 size) const {
    const float ch_w   = (size.x - 6.f) * 0.5f;
    const float seg_h  = 4.f;
    const float seg_gap = 1.f;
    const int   n_segs  = static_cast<int>((size.y - 20.f) / (seg_h + seg_gap));

    for (int ch = 0; ch < kChannels; ++ch) {
        const float x0 = pos.x + 3.f + static_cast<float>(ch) * (ch_w + 3.f);
        const float x1 = x0 + ch_w;

        const int active_segs = static_cast<int>(smoothed_peak_[ch] *
                                                  static_cast<float>(n_segs));
        const int peak_seg    = static_cast<int>(peak_hold_[ch] *
                                                  static_cast<float>(n_segs));

        for (int s = 0; s < n_segs; ++s) {
            const float y1 = pos.y + size.y - 4.f - static_cast<float>(s) * (seg_h + seg_gap);
            const float y0 = y1 - seg_h;

            const float n  = static_cast<float>(s) / static_cast<float>(n_segs);
            const uint32_t on_col = level_colour(n);
            const uint32_t off_col = IM_COL32(20, 20, 20, 180);

            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                              s < active_segs ? on_col : off_col);

            // Peak hold marker
            if (s == peak_seg && peak_seg > 0) {
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                                  IM_COL32(255, 255, 255, 230));
            }
        }

        // Channel label
        const char* label = (ch == 0) ? "L" : "R";
        dl->AddText(ImVec2(x0 + ch_w * 0.35f, pos.y + 4.f),
                    IM_COL32(160, 160, 180, 220), label);
    }
}

// ---------------------------------------------------------------------------
// Oscilloscope view
// ---------------------------------------------------------------------------

void AudioVisualizer::draw_oscilloscope(ImDrawList* dl, ImVec2 pos, ImVec2 size) const {
    // Draw waveform from the circular RMS history buffer
    const float cx = pos.x + 4.f;
    const float cy = pos.y + size.y * 0.5f;
    const float w  = size.x - 8.f;
    const float h  = (size.y - 8.f) * 0.5f;
    const float step = w / static_cast<float>(kHistLen);

    // Grid centre line
    dl->AddLine(ImVec2(pos.x + 2.f, cy), ImVec2(pos.x + size.x - 2.f, cy),
                IM_COL32(50, 50, 70, 200));

    // Waveform
    for (int i = 0; i < kHistLen - 1; ++i) {
        const int idx0 = (history_head_ + i)     % kHistLen;
        const int idx1 = (history_head_ + i + 1) % kHistLen;
        const float v0 = rms_history_[static_cast<size_t>(idx0)];
        const float v1 = rms_history_[static_cast<size_t>(idx1)];

        const float x0 = cx + static_cast<float>(i)     * step;
        const float x1 = cx + static_cast<float>(i + 1) * step;

        // Draw both upper and lower halves for a mirrored oscilloscope look
        dl->AddLine(ImVec2(x0, cy - v0 * h), ImVec2(x1, cy - v1 * h),
                    IM_COL32(50, 200, 255, 200), 1.5f);
        dl->AddLine(ImVec2(x0, cy + v0 * h), ImVec2(x1, cy + v1 * h),
                    IM_COL32(50, 200, 255, 120), 1.5f);
    }

    dl->AddText(ImVec2(pos.x + 4.f, pos.y + 2.f),
                IM_COL32(120, 120, 140, 200), "Oscilloscope");
}

} // namespace straylight::player
