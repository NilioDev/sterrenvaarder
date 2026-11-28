// SPDX-License-Identifier: MPL-2.0
#include "game/navigation.hpp"

namespace sterrenvaarder::game {

const SystemObject* find_object(const StarSystem& sys, std::string_view n) {
    for (auto& o : sys.objects)
        if (iequals(o.nickname, n)) return &o;
    return nullptr;
}

std::optional<LaneTrip> LaneTrip::start(const StarSystem& sys, const SystemObject& entry, float speed) {
    if (!entry.is_tradelane()) return std::nullopt;
    bool forward = !entry.next_ring.empty();
    LaneTrip t;
    t.speed_ = speed;
    const SystemObject* cur = &entry;
    for (int guard = 0; cur && guard < 256; ++guard) {
        t.rings_.push_back({cur->pos.x, cur->pos.y, cur->pos.z});
        t.names_.push_back(cur->nickname);
        const std::string& nx = forward ? cur->next_ring : cur->prev_ring;
        cur = nx.empty() ? nullptr : find_object(sys, nx);
    }
    if (t.rings_.size() < 2) return std::nullopt;
    t.pos_ = t.rings_[0];
    DVec3 d = t.rings_[1] - t.rings_[0];
    t.dir_ = render::normalize(Vec3{static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)});
    return t;
}

bool LaneTrip::update(double dt) {
    double travel = speed_ * dt;
    while (travel > 0 && next_ < rings_.size()) {
        DVec3 to = rings_[next_] - pos_;
        double len = to.length();
        if (len > 1e-6) dir_ = render::normalize(Vec3{static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z)});
        if (travel < len) {
            pos_ = pos_ + to * (travel / len);
            return true;
        }
        pos_ = rings_[next_];
        travel -= len;
        ++next_;
    }
    return next_ < rings_.size();
}

SpawnPoint exit_point(const SystemObject& gate, float distance) {
    Mat4 r = Mat4::euler_deg(gate.rotate_deg);
    Vec3 fwd{-r.at(0, 2), -r.at(1, 2), -r.at(2, 2)};  // the gate's -Z
    SpawnPoint s;
    s.pos = DVec3{gate.pos.x, gate.pos.y, gate.pos.z} + to_d(fwd) * distance;
    s.orient = Quat::look(fwd);
    return s;
}

SpawnPoint launch_point(const SystemObject& o, float distance) {
    SpawnPoint s = exit_point(o, distance);
    if (to_lower(o.archetype).find("ring") == std::string::npos || o.dock_with.empty()) return s;
    Vec3 fwd = s.orient.forward() * -1.f;
    s.pos = DVec3{o.pos.x, o.pos.y, o.pos.z} + to_d(fwd) * distance;
    s.orient = Quat::look(fwd);
    return s;
}

}  // namespace sterrenvaarder::game
