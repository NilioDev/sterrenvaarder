// SPDX-License-Identifier: MPL-2.0
#pragma once
// Plays a scene script (.thn) over time: where each entity is, which camera is looking and
// with what field of view. Character motions are left to the caller (ThnScript::motions).
#include <string>
#include <string_view>
#include <unordered_map>

#include "sterrenvaarder/thn.hpp"

namespace sterrenvaarder::game {

class ThnPlayer {
public:
    struct State {
        Vec3 pos;
        Mat3 orient = kIdentity3;
        float fovh = 0;  // cameras
    };
    explicit ThnPlayer(const ThnScript& script);

    // Puts every entity where the script has it at `time` (seconds from the start).
    void seek(float time);
    const State* state(std::string_view entity) const;
    const std::string& camera() const { return camera_; }  // empty until the first SET_CAMERA
    float duration() const { return duration_; }
    bool done(float time) const { return time >= duration_; }

private:
    const ThnScript& script_;
    std::unordered_map<std::string, State> start_, now_;  // lower-case entity name
    std::string camera_;
    float duration_ = 0;
};

// q_orient as stored (w, x, y, z) as a rotation matrix (unverified against the original).
Mat3 thn_quat(const std::array<float, 4>& q);

}  // namespace sterrenvaarder::game
