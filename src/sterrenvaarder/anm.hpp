// SPDX-License-Identifier: MPL-2.0
#pragma once
// Character animations (.anm): named scripts, each moving joints (bone to bone) and the
// whole object along keyframed channels.
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/utf.hpp"

namespace sterrenvaarder {

struct AnmChannel {
    int type = 0;          // bit flags: 0x02 position, 0x04 quaternion, 0x40 / 0x80 packed quaternion
    float interval = -1;   // seconds between frames; < 0: each frame stores its time
    std::vector<float> times;
    std::vector<Vec3> positions;
    std::vector<std::array<float, 4>> rotations;  // x, y, z, w
    bool has_position() const { return !positions.empty(); }
    bool has_rotation() const { return !rotations.empty(); }
    float length() const { return times.empty() ? 0 : times.back(); }
    // Bytes one frame takes for a channel type, or 0 for a type this reader doesn't know.
    static int frame_size(int type, bool timed);
};

struct AnmJointMap {
    std::string parent, child;  // child empty for an object map
    AnmChannel channel;
};

struct AnmScript {
    std::string name;  // without the "Sc_" prefix
    float root_height = 0;
    std::vector<AnmJointMap> joints, objects;
    float length() const;
};

class AnmLibrary {
public:
    void add(const UtfNode& root);  // throws FormatError
    const AnmScript* script(std::string_view name) const;  // with or without "Sc_", any case
    const std::vector<AnmScript>& scripts() const { return scripts_; }

private:
    std::vector<AnmScript> scripts_;
};

}  // namespace sterrenvaarder
