// SPDX-License-Identifier: MPL-2.0
#pragma once
// Unit quaternion for orientations. Conventions match the game data: right-handed,
// +Y up, ships and cameras look down -Z.
#include <algorithm>
#include <cmath>

#include "render/mat.hpp"

namespace sterrenvaarder::game {

using render::DVec3;
using render::Mat4;

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline DVec3 to_d(Vec3 v) { return {v.x, v.y, v.z}; }

struct Quat {
    float w = 1, x = 0, y = 0, z = 0;

    static Quat axis_angle(Vec3 axis, float angle) {
        float s = std::sin(angle * 0.5f);
        return {std::cos(angle * 0.5f), axis.x * s, axis.y * s, axis.z * s};
    }
    Quat operator*(const Quat& b) const {
        return {w * b.w - x * b.x - y * b.y - z * b.z, w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w};
    }
    Quat conjugate() const { return {w, -x, -y, -z}; }
    Quat normalized() const {
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        return n > 0 ? Quat{w / n, x / n, y / n, z / n} : Quat{};
    }
    Vec3 rotate(Vec3 v) const {
        // v' = v + 2w(q x v) + 2 q x (q x v)
        Vec3 q{x, y, z};
        Vec3 t = render::cross(q, v) * 2.f;
        return v + t * w + render::cross(q, t);
    }
    Vec3 forward() const { return rotate({0, 0, -1}); }
    Vec3 up() const { return rotate({0, 1, 0}); }
    Vec3 right() const { return rotate({1, 0, 0}); }

    Mat4 matrix() const {
        Mat4 m;
        Vec3 r = right(), u = up(), b = rotate({0, 0, 1});
        m.at(0, 0) = r.x; m.at(1, 0) = r.y; m.at(2, 0) = r.z;
        m.at(0, 1) = u.x; m.at(1, 1) = u.y; m.at(2, 1) = u.z;
        m.at(0, 2) = b.x; m.at(1, 2) = b.y; m.at(2, 2) = b.z;
        return m;
    }

    static Quat slerp(Quat a, Quat b, float t) {
        float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
        if (d < 0) { b = {-b.w, -b.x, -b.y, -b.z}; d = -d; }
        if (d > 0.9995f)
            return Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}.normalized();
        float th = std::acos(std::clamp(d, -1.f, 1.f));
        float s = std::sin(th);
        float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        return {a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
    }

    // Orientation looking along `fwd` with `up` as a hint.
    static Quat look(Vec3 fwd, Vec3 up_hint = {0, 1, 0}) {
        Vec3 f = render::normalize(fwd);
        Vec3 r = render::cross(f, up_hint);
        if (length(r) < 1e-4f) r = render::cross(f, Vec3{0, 0, 1});
        r = render::normalize(r);
        Vec3 u = render::cross(r, f);
        Vec3 b = f * -1.f;
        // Rotation matrix columns r, u, b -> quaternion.
        float tr = r.x + u.y + b.z;
        Quat q;
        if (tr > 0) {
            float s = std::sqrt(tr + 1.f) * 2.f;
            q = {0.25f * s, (u.z - b.y) / s, (b.x - r.z) / s, (r.y - u.x) / s};
        } else if (r.x > u.y && r.x > b.z) {
            float s = std::sqrt(1.f + r.x - u.y - b.z) * 2.f;
            q = {(u.z - b.y) / s, 0.25f * s, (u.x + r.y) / s, (b.x + r.z) / s};
        } else if (u.y > b.z) {
            float s = std::sqrt(1.f + u.y - r.x - b.z) * 2.f;
            q = {(b.x - r.z) / s, (u.x + r.y) / s, 0.25f * s, (b.y + u.z) / s};
        } else {
            float s = std::sqrt(1.f + b.z - r.x - u.y) * 2.f;
            q = {(r.y - u.x) / s, (b.x + r.z) / s, (b.y + u.z) / s, 0.25f * s};
        }
        return q.normalized();
    }
};

}  // namespace sterrenvaarder::game
