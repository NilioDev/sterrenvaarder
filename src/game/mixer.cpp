// SPDX-License-Identifier: MPL-2.0
#include "game/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace sterrenvaarder::game {

Mixer::Handle Mixer::play(std::shared_ptr<const Sound> sound, float gain, bool loop) {
    if (!sound || sound->samples.empty() || sound->rate <= 0 || sound->channels <= 0) return 0;
    std::lock_guard g(lock_);
    Voice v;
    v.id = next_++;
    if (next_ == 0) next_ = 1;
    v.sound = std::move(sound);
    v.gain = gain;
    v.loop = loop;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Mixer::stop(Handle h) {
    std::lock_guard g(lock_);
    std::erase_if(voices_, [&](const Voice& v) { return v.id == h; });
}

void Mixer::set_gain(Handle h, float gain) {
    std::lock_guard g(lock_);
    for (auto& v : voices_)
        if (v.id == h) v.gain = gain;
}

void Mixer::fade(Handle h, float gain, float seconds, bool stop_at_end) {
    std::lock_guard g(lock_);
    for (auto& v : voices_) {
        if (v.id != h) continue;
        v.fade_to = gain;
        v.stop_after_fade = stop_at_end;
        float frames = std::max(seconds, 0.f) * static_cast<float>(rate_);
        if (frames < 1) {
            v.gain = gain;
            v.fade_step = 0;
            if (stop_at_end) v.pos = 1e18;  // ends at the next mix
            v.loop = v.loop && !stop_at_end;
        } else {
            v.fade_step = (gain - v.gain) / frames;
        }
    }
}

bool Mixer::playing(Handle h) const {
    std::lock_guard g(lock_);
    return std::any_of(voices_.begin(), voices_.end(), [&](const Voice& v) { return v.id == h; });
}

void Mixer::mix(std::int16_t* out, std::size_t frames) {
    std::vector<float> acc(frames * 2, 0.f);
    {
        std::lock_guard g(lock_);
        for (auto& v : voices_) {
            const Sound& s = *v.sound;
            const std::size_t n = s.samples.size() / static_cast<std::size_t>(s.channels);
            const double step = static_cast<double>(s.rate) / rate_;
            auto sample = [&](std::size_t f, int c) {
                int ch = std::min(c, s.channels - 1);  // mono plays on both sides
                return static_cast<float>(s.samples[f * static_cast<std::size_t>(s.channels) + static_cast<std::size_t>(ch)]);
            };
            for (std::size_t i = 0; i < frames; ++i) {
                if (v.pos >= static_cast<double>(n)) {
                    if (!v.loop) break;
                    v.pos = std::fmod(v.pos, static_cast<double>(n));
                }
                // Linear between the two source frames around the position.
                std::size_t a = static_cast<std::size_t>(v.pos);
                std::size_t b = a + 1 < n ? a + 1 : (v.loop ? 0 : a);
                float t = static_cast<float>(v.pos - static_cast<double>(a));
                for (int c = 0; c < 2; ++c)
                    acc[i * 2 + static_cast<std::size_t>(c)] += v.gain * (sample(a, c) * (1 - t) + sample(b, c) * t);
                v.pos += step;
                if (v.fade_step != 0) {
                    v.gain += v.fade_step;
                    if ((v.fade_step > 0 && v.gain >= v.fade_to) || (v.fade_step < 0 && v.gain <= v.fade_to)) {
                        v.gain = v.fade_to;
                        v.fade_step = 0;
                        if (v.stop_after_fade) {
                            v.loop = false;
                            v.pos = 1e18;
                            break;
                        }
                    }
                }
            }
        }
        std::erase_if(voices_, [](const Voice& v) {
            return !v.loop && v.pos >= static_cast<double>(v.sound->samples.size() / static_cast<std::size_t>(v.sound->channels));
        });
    }
    for (std::size_t i = 0; i < frames * 2; ++i)
        out[i] = static_cast<std::int16_t>(std::clamp(acc[i] * master_, -32768.f, 32767.f));
}

}  // namespace sterrenvaarder::game
