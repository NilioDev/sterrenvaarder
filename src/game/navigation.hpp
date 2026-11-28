// SPDX-License-Identifier: MPL-2.0
#pragma once
// Getting around a system: trade lane travel, docking approach and jump targets.
// Pure logic on the system data, unit-tested; the client drives it.
#include <optional>
#include <string>
#include <vector>

#include "game/quat.hpp"
#include "sterrenvaarder/universe.hpp"

namespace sterrenvaarder::game {

// A trip along a chain of trade lane rings, starting at `entry`. Direction follows
// next_ring when the entry ring has one, else prev_ring (the lane's other side).
class LaneTrip {
public:
    static std::optional<LaneTrip> start(const StarSystem& sys, const SystemObject& entry, float speed = 2000.f);

    // Advances by dt; returns false once the last ring has been passed.
    bool update(double dt);
    DVec3 position() const { return pos_; }
    Vec3 direction() const { return dir_; }
    const std::vector<DVec3>& rings() const { return rings_; }
    std::size_t next_index() const { return next_; }
    float speed() const { return speed_; }

private:
    std::vector<DVec3> rings_;
    std::size_t next_ = 1;
    DVec3 pos_;
    Vec3 dir_{0, 0, -1};
    float speed_ = 2000;
};

const SystemObject* find_object(const StarSystem& sys, std::string_view nickname);

// Where a ship appears after jumping: a little in front of the target gate or hole,
// facing away from it, along the gate's own forward axis.
struct SpawnPoint { DVec3 pos; Quat orient; };
SpawnPoint exit_point(const SystemObject& gate, float distance = 400.f);
// Where a ship leaves a base object: in front of a station; for a planet's dock ring on
// its far side, away from the planet (the ring's -Z faces the planet in the data: all 26).
SpawnPoint launch_point(const SystemObject& o, float distance);

}  // namespace sterrenvaarder::game
