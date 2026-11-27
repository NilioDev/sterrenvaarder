// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/png.hpp"

#include <array>
#include <string>

namespace sterrenvaarder {
namespace {

std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t x = i;
            for (int k = 0; k < 8; ++k) x = (x & 1) ? (x >> 1) ^ 0xEDB88320u : x >> 1;
            t[i] = x;
        }
        return t;
    }();
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s));
}

void chunk(std::vector<std::uint8_t>& o, const char* type, const std::vector<std::uint8_t>& body) {
    be32(o, static_cast<std::uint32_t>(body.size()));
    std::size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), body.begin(), body.end());
    be32(o, crc32(&o[start], o.size() - start) ^ 0xFFFFFFFFu);
}

}  // namespace

std::vector<std::uint8_t> encode_png(const Image& img) {
    std::vector<std::uint8_t> raw;
    std::size_t row = std::size_t(img.width) * 4;
    raw.reserve((row + 1) * img.height);
    for (std::uint32_t y = 0; y < img.height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), img.rgba.begin() + y * row, img.rgba.begin() + (y + 1) * row);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (auto v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (std::size_t off = 0; off < raw.size() || off == 0; off += 65535) {
        std::size_t n = std::min<std::size_t>(65535, raw.size() - off);
        bool last = off + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n));
        z.push_back(static_cast<std::uint8_t>(~n >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        if (last) break;
    }
    be32(z, (b << 16) | a);

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, img.width);
    be32(ihdr, img.height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

}  // namespace sterrenvaarder
