// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/hash.hpp"

#include <array>

namespace sterrenvaarder {
namespace {

constexpr std::uint32_t clmul8(std::uint32_t a, std::uint32_t b) {
    std::uint32_t r = 0;
    while (a) {
        if (a & 1) r ^= b;
        a >>= 1;
        b <<= 1;
    }
    return r & 0xFFu;
}

// Derived by comparing hashes stored in the original model files with the standard
// CRC-32 table: entries match in the low 24 bits, and the top byte is XORed with the
// carry-less product i * 0x7E. Verified against VMeshRef and material ids in game data.
constexpr std::array<std::uint32_t, 256> make_model_table() {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        t[i] = c ^ (clmul8(i, 0x7Eu) << 24);
    }
    return t;
}

constexpr std::array<std::uint32_t, 256> make_id_table() {
    // 16-bit polynomial 0xA001 placed at the top of a 30-bit register.
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t x = i;
        for (int k = 0; k < 8; ++k) x = (x & 1) ? (x >> 1) ^ (0xA001u << 14) : x >> 1;
        t[i] = x;
    }
    return t;
}

constexpr auto kModelTable = make_model_table();
constexpr auto kIdTable = make_id_table();

inline std::uint8_t lower(char c) {
    auto u = static_cast<std::uint8_t>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<std::uint8_t>(u + 32) : u;
}

}  // namespace

std::uint32_t fl_model_crc(std::string_view name) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (char ch : name) c = (c >> 8) ^ kModelTable[(c ^ lower(ch)) & 0xFFu];
    return ~c;
}

std::uint32_t fl_model_crc_exact(std::string_view name) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (char ch : name) c = (c >> 8) ^ kModelTable[(c ^ static_cast<std::uint8_t>(ch)) & 0xFFu];
    return ~c;
}

std::uint32_t fl_create_id(std::string_view nickname) {
    std::uint32_t h = 0;
    for (char ch : nickname) h = (h >> 8) ^ kIdTable[(h & 0xFFu) ^ lower(ch)];
    h = (h >> 24) | ((h >> 8) & 0x0000FF00u) | ((h << 8) & 0x00FF0000u) | (h << 24);
    return (h >> 2) | 0x80000000u;
}

}  // namespace sterrenvaarder
