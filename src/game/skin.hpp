// SPDX-License-Identifier: MPL-2.0
#pragma once
// Skinning for deformable models: where each bone is (from the joints and an animation),
// and the points of a mesh moved along with their bones.
#include <string>
#include <vector>

#include "sterrenvaarder/anm.hpp"
#include "sterrenvaarder/dfm.hpp"

namespace sterrenvaarder::game {

// A rotation and a translation: p' = r * p + t (r stored row by row).
struct Xform {
    Mat3 r = kIdentity3;
    Vec3 t;
    Xform operator*(const Xform& b) const;  // apply b first, then this
    Xform inverse() const;                   // for rigid transforms
    Vec3 apply(Vec3 p) const;
    Vec3 rotate(Vec3 v) const;
    static Xform from_bone(const std::array<float, 12>& to_root);  // the bone's place, from "Bone to root" as stored
    static Xform from_quat(float x, float y, float z, float w);
};

// Each bone's transform to the model root (by bone index). Without an animation, or
// for joints it doesn't move, the joints' own bind rotation is used. Bones no joint
// reaches keep their stored "Bone to root".
std::vector<Xform> pose_bones(const DfmModel& model, const AnmScript* anim = nullptr, float time = 0);

// The points of a mesh moved by their bones: sum of weight * (pose * bind^-1) * point.
std::vector<Vec3> skin_points(const DfmModel& model, const DfmMesh& mesh, const std::vector<Xform>& pose);

}  // namespace sterrenvaarder::game
