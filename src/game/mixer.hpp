// SPDX-License-Identifier: MPL-2.0
#pragma once
// Mixes sounds into one output: any number at once, each with its own volume, looping or
// not, resampled to the output rate. Output is interleaved 16-bit stereo.
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "sterrenvaarder/audio.hpp"

namespace sterrenvaarder::game {

class Mixer {
public:
    explicit Mixer(int output_rate = 44100) : rate_(output_rate) {}

    using Handle = std::uint32_t;  // 0 = none
    Handle play(std::shared_ptr<const Sound> sound, float gain = 1, bool loop = false);
    void stop(Handle h);
    void set_gain(Handle h, float gain);
    // Moves a sound's volume to `gain` over `seconds`; with stop_at_end it ends there.
    void fade(Handle h, float gain, float seconds, bool stop_at_end = false);
    bool playing(Handle h) const;
    void set_master(float gain) { master_ = gain; }

    // Fills `frames` stereo frames (2 * frames samples). Safe to call from the audio thread.
    void mix(std::int16_t* out, std::size_t frames);
    int rate() const { return rate_; }

private:
    struct Voice {
        Handle id = 0;
        std::shared_ptr<const Sound> sound;
        double pos = 0;  // in source frames
        float gain = 1;
        bool loop = false;
        float fade_to = 1, fade_step = 0;  // per output frame; 0 = no fade
        bool stop_after_fade = false;
    };
    int rate_;
    float master_ = 1;
    Handle next_ = 1;
    std::vector<Voice> voices_;
    mutable std::mutex lock_;
};

}  // namespace sterrenvaarder::game
