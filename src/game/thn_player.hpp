// SPDX-License-Identifier: MPL-2.0
#pragma once
// Plays a scene script (.thn) over time: where each entity is, which camera is looking and
// with what field of view. Character motions are left to the caller (ThnScript::motions).
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    // Attachments (ATTACH_ENTITY) and paths are resolved into world placements.
    void seek(float time);
    // Pins an entity to a place outside the scene (e.g. the object a space scene is about).
    void bind(std::string_view entity, Vec3 pos, const Mat3& orient);
    const State* state(std::string_view entity) const;
    const std::string& camera() const { return camera_; }  // empty until the first SET_CAMERA
    float duration() const { return duration_; }
    bool done(float time) const { return time >= duration_; }
    // The START_SOUND events (indices into script.sounds) that start after `from` and at or
    // before `to`. Pass a negative `from` on the first call so sounds at 0 are included.
    std::vector<std::size_t> sounds_between(float from, float to) const;

    // The view through the active camera: false before the first cut or for an unknown camera.
    struct View {
        Vec3 pos, forward, up;
        float fov_y = 0;      // radians, from the camera's horizontal view and aspect
        float near_plane = 0.2f;
    };
    bool view(View& out) const;

private:
    void resolve(float time);
    const ThnScript& script_;
    std::unordered_map<std::string, State> start_, now_;  // lower-case entity name
    std::unordered_map<std::string, State> bound_;
    std::string camera_;
    float duration_ = 0;
};

// A quaternion (w, x, y, z) as an orientation matrix (rows: the entity's axes).
Mat3 thn_quat(const std::array<float, 4>& q);
// START_SPATIAL_PROP_ANIM q_orient carries the whole turn angle where a quaternion has half
// of it: (cos a, sin a * axis). Checked against the scenes: when a move takes an entity to
// another entity's place, this gives that entity's orientation (1762 of 3492 within 0.1,
// against 197 when read as a plain quaternion). Returns the plain quaternion.
std::array<float, 4> move_quat(const std::array<float, 4>& q);

}  // namespace sterrenvaarder::game
