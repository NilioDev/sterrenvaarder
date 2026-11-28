// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/anm.hpp"

#include <algorithm>
#include <cmath>

namespace sterrenvaarder {
namespace {

AnmChannel load_channel(const UtfNode& ch) {
    const UtfNode* header = ch.child("Header");
    const UtfNode* frames = ch.child("Frames");
    if (!header || header->data.size() < 12) throw FormatError("animation channel without a header");
    AnmChannel c;
    int count = read_le<std::int32_t>(header->data, 0);
    c.interval = read_le<float>(header->data, 4);
    c.type = read_le<std::int32_t>(header->data, 8);
    bool timed = c.interval < 0;
    int stride = AnmChannel::frame_size(c.type, timed);
    if (count < 0 || !frames) throw FormatError("animation channel without frames");
    if (stride == 0) throw FormatError("animation channel of an unknown type " + std::to_string(c.type));
    if (frames->data.size() < static_cast<std::size_t>(count) * stride) throw FormatError("animation channel too short");
    for (int i = 0; i < count; ++i) {
        std::size_t o = static_cast<std::size_t>(i) * stride;
        if (timed) {
            c.times.push_back(read_le<float>(frames->data, o));
            o += 4;
        } else {
            c.times.push_back(static_cast<float>(i) * c.interval);
        }
        if (c.type & 0x02) {
            c.positions.push_back({read_le<float>(frames->data, o), read_le<float>(frames->data, o + 4), read_le<float>(frames->data, o + 8)});
            o += 12;
        }
        if (c.type & 0x04) {
            // Full quaternion, stored w first.
            float w = read_le<float>(frames->data, o), x = read_le<float>(frames->data, o + 4);
            float y = read_le<float>(frames->data, o + 8), z = read_le<float>(frames->data, o + 12);
            c.rotations.push_back({x, y, z, w});
        } else if (c.type & (0x40 | 0x80)) {
            // Packed: the vector part in 16-bit steps; w follows from unit length.
            // (Both packed kinds decode this way for now; 0x40 never exceeds about 115
            // degrees in the data, so it may scale the angle differently.)
            float x = read_le<std::int16_t>(frames->data, o) / 32767.f;
            float y = read_le<std::int16_t>(frames->data, o + 2) / 32767.f;
            float z = read_le<std::int16_t>(frames->data, o + 4) / 32767.f;
            float w = std::sqrt(std::max(0.f, 1.f - (x * x + y * y + z * z)));
            c.rotations.push_back({x, y, z, w});
        }
    }
    return c;
}

}  // namespace

int AnmChannel::frame_size(int type, bool timed) {
    // 0x02 position (3 floats), 0x04 quaternion (4 floats), 0x40 / 0x80 packed quaternion
    // (3 shorts); 0x10 and 0x20 add no data.
    if (type & ~(0x02 | 0x04 | 0x10 | 0x20 | 0x40 | 0x80)) return 0;
    int rotations = ((type & 0x04) != 0) + ((type & 0x40) != 0) + ((type & 0x80) != 0);
    if (rotations > 1) return 0;
    int n = 0;
    if (type & 0x02) n += 12;
    if (type & 0x04) n += 16;
    if (type & (0x40 | 0x80)) n += 6;
    return n == 0 ? 0 : n + (timed ? 4 : 0);
}

float AnmScript::length() const {
    float l = 0;
    for (auto& j : joints) l = std::max(l, j.channel.length());
    for (auto& j : objects) l = std::max(l, j.channel.length());
    return l;
}

void AnmLibrary::add(const UtfNode& root) {
    const UtfNode* list = root.find("Animation/Script");
    if (!list) throw FormatError("no Animation/Script in the animation file");
    for (auto& sc : list->children) {
        if (sc.leaf) continue;
        AnmScript s;
        s.name = sc.name.rfind("Sc_", 0) == 0 ? sc.name.substr(3) : sc.name;
        for (auto& m : sc.children) {
            if (iequals(m.name, "Root height") && m.leaf && m.data.size() >= 4) {
                s.root_height = m.as<float>();
                continue;
            }
            if (m.leaf) continue;
            const UtfNode* ch = m.child("Channel");
            if (!ch) continue;
            AnmJointMap jm;
            if (auto* p = m.child("Parent name")) jm.parent = p->as_string();
            if (auto* c = m.child("Child name")) jm.child = c->as_string();
            jm.channel = load_channel(*ch);
            (m.name.rfind("Object map", 0) == 0 ? s.objects : s.joints).push_back(std::move(jm));
        }
        scripts_.push_back(std::move(s));
    }
}

const AnmScript* AnmLibrary::script(std::string_view name) const {
    if (name.size() > 3 && iequals(name.substr(0, 3), "Sc_")) name.remove_prefix(3);
    for (auto& s : scripts_)
        if (iequals(s.name, name)) return &s;
    return nullptr;
}

}  // namespace sterrenvaarder
