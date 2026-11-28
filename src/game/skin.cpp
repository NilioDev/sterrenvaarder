// SPDX-License-Identifier: MPL-2.0
#include "game/skin.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

Xform Xform::operator*(const Xform& b) const {
    Xform o;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o.r[i * 3 + j] = r[i * 3] * b.r[j] + r[i * 3 + 1] * b.r[3 + j] + r[i * 3 + 2] * b.r[6 + j];
    o.t = apply(b.t);
    return o;
}

Vec3 Xform::rotate(Vec3 v) const {
    return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z, r[6] * v.x + r[7] * v.y + r[8] * v.z};
}

Vec3 Xform::apply(Vec3 p) const {
    Vec3 q = rotate(p);
    return {q.x + t.x, q.y + t.y, q.z + t.z};
}

Xform Xform::inverse() const {
    Xform o;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) o.r[i * 3 + j] = r[j * 3 + i];
    Vec3 q = o.rotate(t);
    o.t = {-q.x, -q.y, -q.z};
    return o;
}

Xform Xform::from_bone(const std::array<float, 12>& b) {
    // Despite its name the stored matrix takes root space into the bone's space (rows, then
    // the translation); the joints' bind pose matches its inverse across all character meshes.
    Xform o;
    for (int i = 0; i < 9; ++i) o.r[i] = b[i];
    o.t = {b[9], b[10], b[11]};
    return o.inverse();
}

Xform Xform::from_quat(float x, float y, float z, float w) {
    Xform o;
    o.r = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
           2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
           2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    return o;
}

namespace {

// The channel's rotation at a time, spherically blended between the two keys around it.
bool sample_rotation(const AnmChannel& c, float time, std::array<float, 4>& q) {
    if (c.rotations.empty()) return false;
    std::size_t n = std::min(c.rotations.size(), c.times.size());
    if (n == 0) return false;
    if (n == 1 || time <= c.times[0]) { q = c.rotations[0]; return true; }
    if (time >= c.times[n - 1]) { q = c.rotations[n - 1]; return true; }
    std::size_t i = 1;
    while (i < n && c.times[i] < time) ++i;
    float t0 = c.times[i - 1], t1 = c.times[i], f = t1 > t0 ? (time - t0) / (t1 - t0) : 0;
    auto a = c.rotations[i - 1], b = c.rotations[i];
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0) {
        for (auto& v : b) v = -v;
        dot = -dot;
    }
    float wa = 1 - f, wb = f;
    if (dot < 0.9995f) {
        float th = std::acos(std::clamp(dot, -1.f, 1.f)), s = std::sin(th);
        wa = std::sin((1 - f) * th) / s;
        wb = std::sin(f * th) / s;
    }
    float len = 0;
    for (int k = 0; k < 4; ++k) {
        q[k] = a[k] * wa + b[k] * wb;
        len += q[k] * q[k];
    }
    len = std::sqrt(len);
    if (len > 0)
        for (auto& v : q) v /= len;
    return true;
}

}  // namespace

std::vector<Xform> pose_bones(const DfmModel& model, const AnmScript* anim, float time) {
    std::vector<Xform> out(model.bones.size());
    std::unordered_map<std::string, std::size_t> by_name;  // lower-case part name -> bone
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        out[i] = Xform::from_bone(model.bones[i].to_root);
        if (!model.bones[i].name.empty()) by_name[to_lower(model.bones[i].name)] = i;
    }
    const std::vector<Xform> bind = out;
    std::unordered_map<std::string, const AnmChannel*> moves;  // lower-case child -> channel
    if (anim)
        for (auto& j : anim->joints) moves[to_lower(j.child)] = &j.channel;
    // Walk the joints from the root down; a few passes settle any order.
    std::unordered_map<std::string, Xform> world;
    world["root"] = Xform{};
    if (auto it = by_name.find("root"); it != by_name.end()) out[it->second] = Xform{};
    for (int pass = 0; pass < 16; ++pass) {
        bool changed = false;
        for (auto& j : model.joints) {
            std::string parent = to_lower(j.parent), child = to_lower(j.child);
            auto p = world.find(parent);
            if (p == world.end() || world.count(child)) continue;
            // At rest a bone turns as its bind transform says: body joints store no
            // rotation of their own (their animations give it), hand joints store the same.
            Xform local;
            local.r = j.rotation;
            auto pb = by_name.find(parent), cb = by_name.find(child);
            if (cb != by_name.end())
                local.r = ((pb != by_name.end() ? bind[pb->second].inverse() : Xform{}) * bind[cb->second]).r;
            local.t = j.origin;
            if (auto m = moves.find(child); m != moves.end()) {
                std::array<float, 4> q;
                if (sample_rotation(*m->second, time, q)) local.r = Xform::from_quat(q[0], q[1], q[2], q[3]).r;
            }
            Xform w = p->second * local;
            world[child] = w;
            if (cb != by_name.end()) out[cb->second] = w;
            changed = true;
        }
        if (!changed) break;
    }
    return out;
}

std::vector<Vec3> skin_points(const DfmModel& model, const DfmMesh& mesh, const std::vector<Xform>& pose) {
    std::vector<Xform> skin(model.bones.size());
    for (std::size_t i = 0; i < model.bones.size() && i < pose.size(); ++i)
        skin[i] = pose[i] * Xform::from_bone(model.bones[i].to_root).inverse();
    std::vector<Vec3> out(mesh.points.size());
    for (std::size_t p = 0; p < mesh.points.size(); ++p) {
        std::uint32_t first = p < mesh.bone_first.size() ? mesh.bone_first[p] : 0;
        std::uint32_t count = p < mesh.bone_count.size() ? mesh.bone_count[p] : 0;
        if (count == 0) {
            out[p] = mesh.points[p];
            continue;
        }
        Vec3 acc{};
        float total = 0;
        for (std::uint32_t k = first; k < first + count && k < mesh.bone_ids.size() && k < mesh.bone_weights.size(); ++k) {
            std::uint32_t b = mesh.bone_ids[k];
            if (b >= skin.size()) continue;
            float w = mesh.bone_weights[k];
            Vec3 q = skin[b].apply(mesh.points[p]);
            acc = {acc.x + q.x * w, acc.y + q.y * w, acc.z + q.z * w};
            total += w;
        }
        out[p] = total > 0 ? Vec3{acc.x / total, acc.y / total, acc.z / total} : mesh.points[p];
    }
    return out;
}

}  // namespace sterrenvaarder::game
