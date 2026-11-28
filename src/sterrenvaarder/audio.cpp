// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/audio.hpp"

#include <string>

#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

namespace sterrenvaarder {
namespace {

void decode_mp3(Bytes data, Sound& out) {
    mp3dec_t dec;
    mp3dec_init(&dec);
    std::vector<std::int16_t> pcm(MINIMP3_MAX_SAMPLES_PER_FRAME);
    std::size_t pos = 0;
    while (pos < data.size()) {
        mp3dec_frame_info_t info{};
        int n = mp3dec_decode_frame(&dec, data.data() + pos, static_cast<int>(data.size() - pos), pcm.data(), &info);
        if (info.frame_bytes == 0) break;  // no more frames
        pos += static_cast<std::size_t>(info.frame_bytes);
        if (n <= 0) continue;  // skipped data (tags, junk)
        if (!out.rate) {
            out.rate = info.hz;
            out.channels = info.channels;
        }
        out.samples.insert(out.samples.end(), pcm.begin(), pcm.begin() + n * info.channels);
    }
}

}  // namespace

Sound decode_wav(Bytes d) {
    if (d.size() < 12 || std::string(reinterpret_cast<const char*>(d.data()), 4) != "RIFF" ||
        std::string(reinterpret_cast<const char*>(d.data()) + 8, 4) != "WAVE")
        throw FormatError("not a WAVE file");
    Sound s;
    int tag = 0, bits = 0;
    Bytes body;
    for (std::size_t i = 12; i + 8 <= d.size();) {
        std::string id(reinterpret_cast<const char*>(d.data()) + i, 4);
        std::size_t size = read_le<std::uint32_t>(d, i + 4);
        std::size_t start = i + 8, len = std::min(size, d.size() - start);
        if (id == "fmt " && len >= 16) {
            tag = read_le<std::uint16_t>(d, start);
            s.channels = read_le<std::uint16_t>(d, start + 2);
            s.rate = static_cast<int>(read_le<std::uint32_t>(d, start + 4));
            bits = read_le<std::uint16_t>(d, start + 14);
        } else if (id == "data") {
            body = d.subspan(start, len);
        }
        i = start + size + (size & 1);  // chunks are padded to even sizes
    }
    if (tag == 1) {
        if (bits == 16)
            for (std::size_t k = 0; k + 1 < body.size(); k += 2) s.samples.push_back(static_cast<std::int16_t>(read_le<std::uint16_t>(body, k)));
        else if (bits == 8)
            for (auto b : body) s.samples.push_back(static_cast<std::int16_t>((static_cast<int>(b) - 128) << 8));
        else
            throw FormatError("PCM with " + std::to_string(bits) + " bits");
    } else if (tag == 0x55) {
        int rate = s.rate, ch = s.channels;
        s.rate = s.channels = 0;
        decode_mp3(body, s);
        if (!s.rate) {  // nothing decoded: keep the header's description
            s.rate = rate;
            s.channels = ch;
        }
    } else {
        throw FormatError("WAVE format " + std::to_string(tag));
    }
    return s;
}

}  // namespace sterrenvaarder
