// SPDX-License-Identifier: MPL-2.0
#pragma once
// Little-endian reading helpers over a byte span, with bounds checks.
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace sterrenvaarder {

using Bytes = std::span<const std::uint8_t>;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <typename T>
inline T read_le(Bytes b, std::size_t off) {
    if (off > b.size() || b.size() - off < sizeof(T))
        throw FormatError("read past end of buffer at offset " + std::to_string(off));
    T v;
    std::memcpy(&v, b.data() + off, sizeof(T));  // all supported targets are little-endian
    return v;
}

// Reads a NUL-terminated string starting at off (bounded by the buffer end).
inline std::string_view read_cstr(Bytes b, std::size_t off) {
    if (off >= b.size()) throw FormatError("string offset out of range");
    const char* p = reinterpret_cast<const char*>(b.data() + off);
    std::size_t max = b.size() - off;
    std::size_t n = strnlen(p, max);
    return {p, n};
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

inline std::string to_lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return r;
}

}  // namespace sterrenvaarder
