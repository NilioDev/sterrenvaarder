// SPDX-License-Identifier: MPL-2.0
#pragma once
// Small column-major 4x4 matrix and double-precision vector helpers for rendering.
#include <cmath>

#include "sterrenvaarder/math.hpp"

namespace sterrenvaarder::render {

struct DVec3 {
    double x = 0, y = 0, z = 0;
    DVec3 operator+(DVec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    DVec3 operator-(DVec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    DVec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
};

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * b.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }

    static Mat4 translate(float x, float y, float z) {
        Mat4 r;
        r.at(0, 3) = x;
        r.at(1, 3) = y;
        r.at(2, 3) = z;
        return r;
    }
    static Mat4 scale(float s) {
        Mat4 r;
        r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = s;
        return r;
    }
    // From a row-major 3x3 as stored in the game data (rows are basis vectors).
    static Mat4 from_rows3(const Mat3& r3) {
        Mat4 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.at(i, j) = r3[j * 3 + i];
        return r;
    }
    static Mat4 rotate_x(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.at(1, 1) = c; r.at(1, 2) = -s; r.at(2, 1) = s; r.at(2, 2) = c;
        return r;
    }
    static Mat4 rotate_y(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.at(0, 0) = c; r.at(0, 2) = s; r.at(2, 0) = -s; r.at(2, 2) = c;
        return r;
    }
    static Mat4 rotate_z(float a) {
        Mat4 r;
        float c = std::cos(a), s = std::sin(a);
        r.at(0, 0) = c; r.at(0, 1) = -s; r.at(1, 0) = s; r.at(1, 1) = c;
        return r;
    }
    // Euler angles in degrees as written in system INIs.
    static Mat4 euler_deg(Vec3 d) {
        constexpr float k = 3.14159265358979f / 180.f;
        return rotate_z(d.z * k) * rotate_y(d.y * k) * rotate_x(d.x * k);
    }
    // Right-handed, reverse-Z, infinite far plane. Depth 1 at near, 0 at infinity.
    static Mat4 perspective_reverse_z(float fovy, float aspect, float near_plane) {
        Mat4 r;
        float f = 1.0f / std::tan(fovy * 0.5f);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = 0;
        r.at(2, 3) = near_plane;
        r.at(3, 2) = -1;
        r.at(3, 3) = 0;
        return r;
    }
    // Rotation-only view matrix from orthonormal camera axes.
    static Mat4 view_rotation(Vec3 right, Vec3 up, Vec3 forward) {
        Mat4 r;
        r.at(0, 0) = right.x; r.at(0, 1) = right.y; r.at(0, 2) = right.z;
        r.at(1, 0) = up.x; r.at(1, 1) = up.y; r.at(1, 2) = up.z;
        r.at(2, 0) = -forward.x; r.at(2, 1) = -forward.y; r.at(2, 2) = -forward.z;
        return r;
    }
    // Inverse transpose of the upper 3x3 (for normals); valid for rotation + uniform scale.
    Mat4 normal_matrix() const {
        Mat4 r = *this;
        r.at(0, 3) = r.at(1, 3) = r.at(2, 3) = 0;
        float sx = std::sqrt(at(0, 0) * at(0, 0) + at(1, 0) * at(1, 0) + at(2, 0) * at(2, 0));
        float inv = sx > 0 ? 1.0f / (sx * sx) : 1.0f;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.at(i, j) *= inv;
        return r;
    }
};

inline Vec3 normalize(Vec3 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0 ? Vec3{v.x / l, v.y / l, v.z / l} : v;
}
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

}  // namespace sterrenvaarder::render
