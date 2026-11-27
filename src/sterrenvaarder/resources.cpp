// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/resources.hpp"

#include <algorithm>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

namespace {

constexpr std::uint32_t kRtString = 6;
constexpr std::uint32_t kRtHtml = 23;

struct Section {
    std::uint32_t vaddr, vsize, raw_ptr, raw_size;
};

// Maps a relative virtual address to a file offset through the section table.
std::size_t rva_to_offset(const std::vector<Section>& sections, std::uint32_t rva, std::size_t file_size) {
    for (auto& s : sections) {
        std::uint32_t span = std::max(s.vsize, s.raw_size);
        if (rva >= s.vaddr && rva < s.vaddr + span) {
            std::size_t off = std::size_t(s.raw_ptr) + (rva - s.vaddr);
            if (off < file_size) return off;
        }
    }
    throw FormatError("resource address outside file");
}

}  // namespace

std::string utf16le_to_utf8(Bytes d) {
    std::string out;
    out.reserve(d.size() / 2);
    for (std::size_t i = 0; i + 1 < d.size(); i += 2) {
        std::uint32_t c = d[i] | (d[i + 1] << 8);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < d.size()) {
            std::uint32_t lo = d[i + 2] | (d[i + 3] << 8);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (c == 0xFEFF && out.empty()) continue;  // byte order mark
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

PeResources PeResources::parse(Bytes pe) {
    if (pe.size() < 0x40 || pe[0] != 'M' || pe[1] != 'Z') throw FormatError("not a PE file");
    std::uint32_t nt = read_le<std::uint32_t>(pe, 0x3C);
    if (read_le<std::uint32_t>(pe, nt) != 0x00004550) throw FormatError("missing PE signature");
    std::size_t coff = nt + 4;
    std::uint16_t nsections = read_le<std::uint16_t>(pe, coff + 2);
    std::uint16_t opt_size = read_le<std::uint16_t>(pe, coff + 16);
    std::size_t opt = coff + 20;
    std::uint16_t magic = read_le<std::uint16_t>(pe, opt);
    std::size_t dirs = opt + (magic == 0x20B ? 112 : 96);
    std::uint32_t ndirs = read_le<std::uint32_t>(pe, dirs - 4);
    PeResources out;
    if (ndirs < 3) return out;
    std::uint32_t res_rva = read_le<std::uint32_t>(pe, dirs + 2 * 8);
    if (res_rva == 0) return out;

    std::vector<Section> sections;
    std::size_t st = opt + opt_size;
    for (std::uint16_t i = 0; i < nsections; ++i) {
        std::size_t s = st + i * 40;
        sections.push_back({read_le<std::uint32_t>(pe, s + 12), read_le<std::uint32_t>(pe, s + 8),
                            read_le<std::uint32_t>(pe, s + 20), read_le<std::uint32_t>(pe, s + 16)});
    }
    std::size_t root = rva_to_offset(sections, res_rva, pe.size());

    // Directory entries: {id or name, offset}; the high bit of the offset marks a subdirectory.
    auto entries = [&](std::size_t dir) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> e;
        std::uint16_t named = read_le<std::uint16_t>(pe, dir + 12), ids = read_le<std::uint16_t>(pe, dir + 14);
        if (named + ids > 4096) throw FormatError("resource directory too large");
        for (std::size_t i = 0; i < std::size_t(named) + ids; ++i)
            e.emplace_back(read_le<std::uint32_t>(pe, dir + 16 + i * 8), read_le<std::uint32_t>(pe, dir + 20 + i * 8));
        return e;
    };
    // First data entry (any language) below a name-level entry.
    auto leaf_data = [&](std::uint32_t off) -> Bytes {
        while (off & 0x80000000u) {
            auto langs = entries(root + (off & 0x7FFFFFFFu));
            if (langs.empty()) return {};
            off = langs.front().second;
        }
        std::size_t de = root + off;
        std::uint32_t rva = read_le<std::uint32_t>(pe, de), size = read_le<std::uint32_t>(pe, de + 4);
        std::size_t at = rva_to_offset(sections, rva, pe.size());
        if (size > pe.size() - at) throw FormatError("resource data past end of file");
        return pe.subspan(at, size);
    };

    for (auto [type, type_off] : entries(root)) {
        if ((type & 0x80000000u) || !(type_off & 0x80000000u)) continue;  // named types are not used
        if (type != kRtString && type != kRtHtml) continue;
        for (auto [name, name_off] : entries(root + (type_off & 0x7FFFFFFFu))) {
            if (name & 0x80000000u) continue;
            Bytes d = leaf_data(name_off);
            if (type == kRtHtml) {
                out.infocards[name] = utf16le_to_utf8(d);
                continue;
            }
            // A string block holds 16 strings: ids (name - 1) * 16 .. + 15, each a u16 length
            // followed by that many UTF-16 code units.
            std::size_t o = 0;
            for (std::uint32_t i = 0; i < 16 && o + 2 <= d.size(); ++i) {
                std::size_t n = read_le<std::uint16_t>(d, o);
                o += 2;
                if (n * 2 > d.size() - o) break;
                if (n) out.strings[(name - 1) * 16 + i] = utf16le_to_utf8(d.subspan(o, n * 2));
                o += n * 2;
            }
        }
    }
    return out;
}

void GameStrings::add(std::uint32_t index, PeResources res) {
    for (auto& [id, s] : res.strings) strings_[(index << 16) | (id & 0xFFFF)] = std::move(s);
    for (auto& [id, s] : res.infocards) infocards_[(index << 16) | (id & 0xFFFF)] = std::move(s);
}

std::optional<std::string> GameStrings::string(std::uint32_t id) const {
    auto it = strings_.find(id);
    if (it == strings_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> GameStrings::infocard(std::uint32_t id) const {
    auto it = infocards_.find(id);
    if (it == infocards_.end()) return std::nullopt;
    return it->second;
}

std::string GameStrings::name(std::uint32_t id, std::string_view fallback) const {
    if (id) {
        auto it = strings_.find(id);
        if (it != strings_.end() && it->second.find_first_not_of(" \t\r\n") != std::string::npos) return it->second;
    }
    return std::string(fallback);
}

std::vector<std::string> resource_dlls(std::string_view freelancer_ini) {
    std::vector<std::string> out;
    for (auto& sec : parse_text_ini(freelancer_ini)) {
        if (!iequals(sec.name, "Resources")) continue;
        for (auto& e : sec.entries)
            if (iequals(e.key, "DLL") && !e.values.empty()) out.push_back(e.values[0].as_string());
    }
    return out;
}

}  // namespace sterrenvaarder
