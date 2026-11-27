// SPDX-License-Identifier: MPL-2.0
#pragma once
// Fighter AI driven by the original pilot data (missions/pilots_*.ini). The behaviour is
// our own reading of what each block's values describe: head-on buzz runs that pass the
// target and turn back, trailing a target from behind, dodging when the target aims at
// us, breaking away after heavy damage, and firing in bursts with a limited accuracy.
#include <cstdint>
#include <string_view>

#include "game/flight.hpp"
#include "game/quat.hpp"
#include "sterrenvaarder/pilots.hpp"

namespace sterrenvaarder::game {

struct AiBody {
    DVec3 pos, vel;
    Quat orient;
};

struct AiWeapons {
    float muzzle_speed = 600;  // m/s
    float range = 600;         // m
};

struct AiOutput {
    FlightInput input;
    bool fire = false;
    Vec3 aim_dir{0, 0, -1};  // world space, where the guns should point
};

class FighterAi {
public:
    enum class State { HeadToward, PassBy, HeadAway, Trail, Break, Flee };

    FighterAi(const Pilot& pilot, std::uint32_t seed);

    // hull: fraction of max hull left (0..1). target_is_player picks the accuracy power.
    AiOutput update(const AiBody& self, const AiBody& target, float hull, bool target_is_player, const AiWeapons& w,
                    float dt);

    State state() const { return state_; }
    // Wingmen trail the target from different spots (metres, in the target's right/up
    // frame), so they do not stack up behind it and shoot each other.
    void set_trail_offset(float right, float up) { trail_offset_right_ = right, trail_offset_up_ = up; }
    // Where wing slot n (0-based) trails: behind, then alternately right/left/up/down.
    static void wing_slot(int n, float& right, float& up);
    static std::string_view name(State s);

private:
    const Pilot* p_;
    std::uint32_t rng_;
    State state_ = State::HeadToward;
    float state_time_ = 0;
    float turn_sign_ = 1;     // -1 left, +1 right for breaks and pass-bys
    bool straight_by_ = false;
    bool break_outrun_ = false;
    float head_away_distance_ = 0;
    float trail_no_lock_ = 0;
    float trail_offset_right_ = 0, trail_offset_up_ = 0;
    float break_cooldown_ = 0;

    // Damage bookkeeping for the damage reactions.
    float last_hull_ = 1, recent_loss_ = 0, loss_window_ = 0;

    // Dodging (evade dodge and buzz dodge share the waggle).
    float dodge_left_ = 0, dodge_wait_ = 0, dodge_sign_ = 1;

    // Guns.
    float burst_left_ = 0, pause_left_ = 0, shot_wait_ = 0;
    float aim_switch_ = 0;
    Vec3 aim_error_{};  // offset added to the aim direction (radians in the target's frame)
    float range_scale_ = 1;

    float rand01();
    float vary(float value, float variance_percent);
    std::string_view pick(const Weights& w, std::string_view fallback);
    void enter(State s);
};

// Steering input that turns a ship towards a world direction (shared with scripted flight).
Vec3 steer_towards(const Quat& orient, Vec3 dir, float turn_throttle);

}  // namespace sterrenvaarder::game
