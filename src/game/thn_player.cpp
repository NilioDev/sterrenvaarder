// SPDX-License-Identifier: MPL-2.0
#include "game/thn_player.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
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

// Orientations keep the entity's axes as rows (right, up, back).
Vec3 to_world(const Mat3& o, Vec3 v) {
    return {o[0] * v.x + o[3] * v.y + o[6] * v.z, o[1] * v.x + o[4] * v.y + o[7] * v.z, o[2] * v.x + o[5] * v.y + o[8] * v.z};
}
Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    return r;
}
Vec3 cross3(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 norm3(Vec3 v) {
    float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-6f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 0, -1};
}
// Facing `dir`, +Y up as far as it can be.
Mat3 facing(Vec3 dir) {
    Vec3 back = norm3({-dir.x, -dir.y, -dir.z});
    Vec3 up_hint = std::fabs(back.y) > 0.999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    Vec3 right = norm3(cross3(up_hint, back));
    Vec3 up = cross3(back, right);
    return {right.x, right.y, right.z, up.x, up.y, up.z, back.x, back.y, back.z};
}

}  // namespace

std::array<float, 4> move_quat(const std::array<float, 4>& q) {
    float w = std::clamp(q[0], -1.f, 1.f);
    float th = std::acos(w), s = std::sin(th);
    if (s < 1e-5f) return {1, 0, 0, 0};
    float k = std::sin(th * 0.5f) / s;
    return {std::cos(th * 0.5f), q[1] * k, q[2] * k, q[3] * k};
}

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

void ThnPlayer::bind(std::string_view entity, Vec3 pos, const Mat3& orient) {
    bound_[to_lower(entity)] = {pos, orient, 0};
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
            continue;  // resolved below, in the path's world frame
        } else if (s.move) {
            State& st = now_[to_lower(s.move->target)];
            if (s.move->has_pos)
                st.pos = {st.pos.x + (s.move->pos.x - st.pos.x) * f, st.pos.y + (s.move->pos.y - st.pos.y) * f,
                          st.pos.z + (s.move->pos.z - st.pos.z) * f};
            if (s.move->has_orient) st.orient = thn_quat(slerp(to_quat(st.orient), move_quat(s.move->q), f));
        } else if (s.cam->fovh > 0) {
            State& st = now_[to_lower(s.cam->target)];
            st.fovh += (s.cam->fovh - st.fovh) * f;
        }
    }
    resolve(t);
    camera_.clear();
    float best = -1;
    for (auto& c : script_.cuts)
        if (c.time <= t && c.time >= best) {
            best = c.time;
            camera_ = c.camera;
        }
}

void ThnPlayer::resolve(float t) {
    // The latest path animation per entity, and the attachments in effect (they hold
    // from their start; the scene's own timing ends them by starting others).
    std::unordered_map<std::string, std::pair<const ThnPathAnim*, float>> paths;
    for (auto& a : script_.path_anims)
        if (a.time <= t) {
            auto& slot = paths[to_lower(a.target)];
            if (!slot.first || a.time >= slot.first->time)
                slot = {&a, a.duration > 0 ? std::clamp((t - a.time) / a.duration, 0.f, 1.f) : 1.f};
        }
    std::unordered_map<std::string, std::vector<const ThnAttach*>> attach;
    for (auto& a : script_.attachments)
        if (a.time <= t) attach[to_lower(a.child)].push_back(&a);
    std::unordered_map<std::string, State> world;
    std::unordered_map<std::string, bool> busy;
    std::function<State(const std::string&, int)> get = [&](const std::string& key, int depth) -> State {
        if (auto it = world.find(key); it != world.end()) return it->second;
        if (auto b = bound_.find(key); b != bound_.end()) return world[key] = b->second;
        State st = now_.count(key) ? now_[key] : State{};
        if (depth > 16 || busy[key]) return st;
        busy[key] = true;
        State local = st;
        bool path_moves = false;
        if (auto p = paths.find(key); p != paths.end()) {
            const ThnPathAnim& a = *p->second.first;
            float f = a.start + (a.stop - a.start) * p->second.second;
            if (auto it = script_.paths.find(to_lower(a.path)); it != script_.paths.end()) {
                State frame = get(to_lower(a.path), depth + 1);
                const ThnPath& path = it->second;
                if (a.position) {
                    Vec3 w = to_world(frame.orient, path.at(f));
                    st.pos = {frame.pos.x + w.x + a.offset.x, frame.pos.y + w.y + a.offset.y, frame.pos.z + w.z + a.offset.z};
                    path_moves = true;
                }
                if (a.look_along) {
                    float f2 = std::min(f + 0.01f, 1.f), f1 = f2 - 0.01f;
                    Vec3 p1 = path.at(f1), p2 = path.at(f2);
                    st.orient = mul(facing({p2.x - p1.x, p2.y - p1.y, p2.z - p1.z}), frame.orient);
                } else if (a.orientation && !path.orients.empty()) {
                    std::size_t i = std::min(path.orients.size() - 1,
                                             static_cast<std::size_t>(f * static_cast<float>(path.orients.size() - 1) + 0.5f));
                    Mat3 q = thn_quat(path.orients[i]);  // a rotation matrix: its columns are the axes
                    st.orient = mul({q[0], q[3], q[6], q[1], q[4], q[7], q[2], q[5], q[8]}, frame.orient);
                }
            }
        }
        if (auto at = attach.find(key); at != attach.end())
            for (const ThnAttach* a : at->second) {
                State parent = get(to_lower(a->parent), depth + 1);
                if (a->position && !path_moves) {
                    Vec3 off = a->relative ? to_world(parent.orient, {local.pos.x + a->offset.x, local.pos.y + a->offset.y,
                                                                       local.pos.z + a->offset.z})
                                           : a->offset;
                    st.pos = {parent.pos.x + off.x, parent.pos.y + off.y, parent.pos.z + off.z};
                }
                if (a->orientation) st.orient = a->orientation_relative ? mul(local.orient, parent.orient) : parent.orient;
                if (a->look_at) st.orient = facing({parent.pos.x - st.pos.x, parent.pos.y - st.pos.y, parent.pos.z - st.pos.z});
            }
        busy[key] = false;
        return world[key] = st;
    };
    for (auto& e : script_.entities) {
        std::string key = to_lower(e.name);
        State w = get(key, 0);
        now_[key].pos = w.pos;
        now_[key].orient = w.orient;
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

std::vector<std::size_t> ThnPlayer::sounds_between(float from, float to) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < script_.sounds.size(); ++i)
        if (script_.sounds[i].time > from && script_.sounds[i].time <= to) out.push_back(i);
    return out;
}

}  // namespace sterrenvaarder::game
