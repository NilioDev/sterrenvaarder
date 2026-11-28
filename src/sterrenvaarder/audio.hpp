// SPDX-License-Identifier: MPL-2.0
#pragma once
// Sounds from the player's own data: RIFF WAVE files holding PCM or MP3 (format 0x55),
// decoded to 16-bit samples.
#include <cstdint>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

struct Sound {
    int rate = 0, channels = 0;
    std::vector<std::int16_t> samples;  // interleaved
    float seconds() const {
        return rate > 0 && channels > 0 ? static_cast<float>(samples.size()) / static_cast<float>(rate * channels) : 0.f;
    }
};

// Throws FormatError for a file that is not a WAVE or has a format we can't read.
Sound decode_wav(Bytes data);

}  // namespace sterrenvaarder
