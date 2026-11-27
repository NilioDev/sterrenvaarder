// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/ini.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace sterrenvaarder {

float IniValue::as_float() const {
    switch (type) {
        case Type::Int: return static_cast<float>(i);
        case Type::Float: return f;
        default: {
            float v = 0;
            std::from_chars(s.data(), s.data() + s.size(), v);
            return v;
        }
    }
}

std::int32_t IniValue::as_int() const {
    switch (type) {
        case Type::Int: return i;
        case Type::Float: return static_cast<std::int32_t>(f);
        default: {
            std::int32_t v = 0;
            std::from_chars(s.data(), s.data() + s.size(), v);
            return v;
        }
    }
}

std::string IniValue::as_string() const {
    char buf[64];
    switch (type) {
        case Type::Int: return std::to_string(i);
        case Type::Float: {
            // Shortest form that round-trips, always with a decimal point.
            auto r = std::to_chars(buf, buf + sizeof buf, f);
            std::string out(buf, r.ptr);
            if (out.find_first_of(".eEn") == std::string::npos) out += ".0";
            return out;
        }
        default: return s;
    }
}

bool IniValue::as_bool() const {
    if (type == Type::String) return iequals(s, "true") || s == "1" || iequals(s, "yes");
    return as_int() != 0;
}

const IniEntry* IniSection::find(std::string_view key) const {
    for (auto& e : entries)
        if (iequals(e.key, key)) return &e;
    return nullptr;
}

std::vector<const IniEntry*> IniSection::find_all(std::string_view key) const {
    std::vector<const IniEntry*> r;
    for (auto& e : entries)
        if (iequals(e.key, key)) r.push_back(&e);
    return r;
}

bool is_bini(Bytes d) { return d.size() >= 12 && std::memcmp(d.data(), "BINI", 4) == 0; }

IniFile parse_ini(Bytes d) {
    if (is_bini(d)) return parse_bini(d);
    return parse_text_ini(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
}

// BINI layout: "BINI", u32 version (1), u32 string-table offset, then sections up to the
// string table. Section: u16 name, u16 entry count. Entry: u16 name, u8 value count.
// Value: u8 type (1 int, 2 float, 3 string-offset) + 4 bytes.
IniFile parse_bini(Bytes d) {
    auto version = read_le<std::uint32_t>(d, 4);
    if (version != 1) throw FormatError("unsupported BINI version " + std::to_string(version));
    auto strtab = read_le<std::uint32_t>(d, 8);
    if (strtab > d.size()) throw FormatError("BINI string table out of range");
    auto str = [&](std::uint32_t off) { return std::string(read_cstr(d, std::size_t(strtab) + off)); };

    IniFile out;
    std::size_t p = 12;
    while (p < strtab) {
        IniSection sec;
        sec.name = str(read_le<std::uint16_t>(d, p));
        auto n = read_le<std::uint16_t>(d, p + 2);
        p += 4;
        sec.entries.reserve(n);
        for (unsigned e = 0; e < n; ++e) {
            IniEntry ent;
            ent.key = str(read_le<std::uint16_t>(d, p));
            auto vc = read_le<std::uint8_t>(d, p + 2);
            p += 3;
            ent.values.reserve(vc);
            for (unsigned v = 0; v < vc; ++v) {
                IniValue val;
                auto t = read_le<std::uint8_t>(d, p);
                switch (t) {
                    case 1: val.type = IniValue::Type::Int; val.i = read_le<std::int32_t>(d, p + 1); break;
                    case 2: val.type = IniValue::Type::Float; val.f = read_le<float>(d, p + 1); break;
                    case 3: val.type = IniValue::Type::String; val.s = str(read_le<std::uint32_t>(d, p + 1)); break;
                    default: throw FormatError("BINI: unknown value type " + std::to_string(t));
                }
                p += 5;
                ent.values.push_back(std::move(val));
            }
            sec.entries.push_back(std::move(ent));
        }
        out.push_back(std::move(sec));
    }
    return out;
}

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '\xEF' || s.front() == '\xBB' || s.front() == '\xBF')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

IniValue guess_value(std::string_view s) {
    IniValue v;
    v.s = std::string(s);
    if (s.empty()) return v;
    std::int32_t i;
    auto ri = std::from_chars(s.data(), s.data() + s.size(), i);
    if (ri.ec == std::errc() && ri.ptr == s.data() + s.size()) {
        v.type = IniValue::Type::Int;
        v.i = i;
        return v;
    }
    float f;
    auto rf = std::from_chars(s.data(), s.data() + s.size(), f);
    if (rf.ec == std::errc() && rf.ptr == s.data() + s.size() && std::isfinite(f)) {
        v.type = IniValue::Type::Float;
        v.f = f;
    }
    return v;
}

}  // namespace

IniFile parse_text_ini(std::string_view text) {
    IniFile out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (auto c = line.find(';'); c != std::string_view::npos) line = line.substr(0, c);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            auto close = line.find(']');
            out.push_back(IniSection{std::string(trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1))), {}});
            continue;
        }
        if (out.empty()) continue;  // entries before any section are ignored, like the game
        IniEntry ent;
        auto eq = line.find('=');
        ent.key = std::string(trim(line.substr(0, eq)));
        if (eq != std::string_view::npos) {
            std::string_view rest = line.substr(eq + 1);
            std::size_t s = 0;
            while (s <= rest.size()) {
                std::size_t comma = rest.find(',', s);
                if (comma == std::string_view::npos) comma = rest.size();
                ent.values.push_back(guess_value(trim(rest.substr(s, comma - s))));
                s = comma + 1;
            }
        }
        out.back().entries.push_back(std::move(ent));
    }
    return out;
}

std::string to_text(const IniFile& ini) {
    std::string out;
    for (auto& sec : ini) {
        out += "[" + sec.name + "]\n";
        for (auto& e : sec.entries) {
            out += e.key;
            if (!e.values.empty()) {
                out += " = ";
                for (std::size_t i = 0; i < e.values.size(); ++i) {
                    if (i) out += ", ";
                    out += e.values[i].as_string();
                }
            }
            out += "\n";
        }
        out += "\n";
    }
    return out;
}

}  // namespace sterrenvaarder
