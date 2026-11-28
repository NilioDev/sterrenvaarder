// SPDX-License-Identifier: MPL-2.0
#include "game/thn_player.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {
namespace {

std::array<float, 4> to_quat(const Mat3& m) {  // w, x, y, z
    float tr = m[0] + m[4] + m[8];
    std::array<float, 4> q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1) * 2;
        q = {0.25f * s, (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1 + m[0] - m[4] - m[8]) * 2;
        q = {(m[7] - m[5]) / s, 0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s};
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1 + m[4] - m[0] - m[8]) * 2;
        q = {(m[2] - m[6]) / s, (m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s};
    } else {
        float s = std::sqrt(1 + m[8] - m[0] - m[4]) * 2;
        q = {(m[3] - m[1]) / s, (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s};
    }
    return q;
}

std::array<float, 4> slerp(std::array<float, 4> a, std::array<float, 4> b, float f) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        for (auto& v : b) v = -v;
        dot = -dot;
    }
    float wa = 1 - f, wb = f;
    if (dot < 0.9995f) {
        float th = std::acos(std::min(dot, 1.f)), s = std::sin(th);
        wa = std::sin((1 - f) * th) / s;
        wb = std::sin(f * th) / s;
    }
    std::array<float, 4> q;
    float len = 0;
    for (int k = 0; k < 4; ++k) {
        q[k] = a[k] * wa + b[k] * wb;
        len += q[k] * q[k];
    }
    len = std::sqrt(len);
    if (len > 0)
        for (auto& v : q) v /= len;
    return q;
}

}  // namespace

Mat3 thn_quat(const std::array<float, 4>& q) {
    float w = q[0], x = q[1], y = q[2], z = q[3];
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}

ThnPlayer::ThnPlayer(const ThnScript& script) : script_(script) {
    for (auto& e : script.entities) start_[to_lower(e.name)] = {e.pos, e.orient, e.fovh};
    duration_ = static_cast<float>(script.duration);
    for (auto& m : script.moves) duration_ = std::max(duration_, m.time + m.duration);
    for (auto& c : script.cuts) duration_ = std::max(duration_, c.time);
    for (auto& a : script.path_anims) duration_ = std::max(duration_, a.time + a.duration);
    seek(0);
}

void ThnPlayer::seek(float t) {
    now_ = start_;
    // Moves and camera changes in the order they start; each blends from where the
    // entity was when it began.
    struct Step { float time, duration; const ThnMove* move; const ThnCameraProp* cam; const ThnPathAnim* path = nullptr; };
    std::vector<Step> steps;
    for (auto& m : script_.moves)
        if (m.time <= t) steps.push_back({m.time, m.duration, &m, nullptr});
    for (auto& c : script_.camera_props)
        if (c.time <= t) steps.push_back({c.time, c.duration, nullptr, &c});
    for (auto& a : script_.path_anims)
        if (a.time <= t) steps.push_back({a.time, a.duration, nullptr, nullptr, &a});
    std::stable_sort(steps.begin(), steps.end(), [](const Step& a, const Step& b) { return a.time < b.time; });
    for (auto& s : steps) {
        float f = s.duration > 0 ? std::clamp((t - s.time) / s.duration, 0.f, 1.f) : 1.f;
        if (s.path) {
            // Along the path, in the path entity's frame as it stands now.
            auto it = script_.paths.find(to_lower(s.path->path));
            if (it == script_.paths.end()) continue;
            const State& frame = now_[to_lower(s.path->path)];
            Vec3 local = it->second.at(s.path->start + (s.path->stop - s.path->start) * f);
            const Mat3& r = frame.orient;
            State& st = now_[to_lower(s.path->target)];
            st.pos = {frame.pos.x + r[0] * local.x + r[1] * local.y + r[2] * local.z + s.path->offset.x,
                      frame.pos.y + r[3] * local.x + r[4] * local.y + r[5] * local.z + s.path->offset.y,
                      frame.pos.z + r[6] * local.x + r[7] * local.y + r[8] * local.z + s.path->offset.z};
        } else if (s.move) {
            State& st = now_[to_lower(s.move->target)];
            if (s.move->has_pos)
                st.pos = {st.pos.x + (s.move->pos.x - st.pos.x) * f, st.pos.y + (s.move->pos.y - st.pos.y) * f,
                          st.pos.z + (s.move->pos.z - st.pos.z) * f};
            if (s.move->has_orient) st.orient = thn_quat(slerp(to_quat(st.orient), s.move->q, f));
        } else if (s.cam->fovh > 0) {
            State& st = now_[to_lower(s.cam->target)];
            st.fovh += (s.cam->fovh - st.fovh) * f;
        }
    }
    camera_.clear();
    float best = -1;
    for (auto& c : script_.cuts)
        if (c.time <= t && c.time >= best) {
            best = c.time;
            camera_ = c.camera;
        }
}

bool ThnPlayer::view(View& out) const {
    if (camera_.empty()) return false;
    const ThnEntity* e = script_.entity(camera_);
    const State* st = state(camera_);
    if (!e || !st) return false;
    out.pos = st->pos;
    out.forward = {-st->orient[6], -st->orient[7], -st->orient[8]};  // entities look down -z
    out.up = {st->orient[3], st->orient[4], st->orient[5]};
    float aspect = e->hvaspect > 0 ? e->hvaspect : 4.f / 3.f;
    float fovh = st->fovh > 0 ? st->fovh : 60.f;
    out.fov_y = 2.f * std::atan(std::tan(fovh * 3.14159265f / 360.f) / aspect);
    if (e->near_plane > 0) out.near_plane = e->near_plane;
    return true;
}

const ThnPlayer::State* ThnPlayer::state(std::string_view entity) const {
    auto it = now_.find(to_lower(entity));
    return it == now_.end() ? nullptr : &it->second;
}

}  // namespace sterrenvaarder::game
