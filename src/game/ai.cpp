// SPDX-License-Identifier: MPL-2.0
#include "game/ai.hpp"

#include <algorithm>
#include <cmath>

namespace sterrenvaarder::game {

namespace {

constexpr float kDeg = 3.14159265f / 180.f;

Vec3 to_f(DVec3 v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

Vec3 norm(Vec3 v) {
    float l = length(v);
    return l > 1e-6f ? v * (1.f / l) : Vec3{0, 0, -1};
}

// Where to aim so a bolt at `speed` meets a target moving at constant velocity.
Vec3 lead_dir(const AiBody& self, const AiBody& target, float speed) {
    DVec3 to = target.pos - self.pos;
    double dist = to.length();
    DVec3 rel_vel = target.vel - self.vel;
    DVec3 lead = to + rel_vel * (dist / std::max(1.f, speed));
    return norm(to_f(lead));
}

float angle_between(Vec3 a, Vec3 b) { return std::acos(std::clamp(dot(norm(a), norm(b)), -1.f, 1.f)); }

}  // namespace

Vec3 steer_towards(const Quat& orient, Vec3 dir, float turn_throttle) {
    Vec3 local = orient.conjugate().rotate(norm(dir));
    float pitch = local.y, yaw = -local.x;
    if (local.z > 0) {  // behind us: turn as hard as allowed, on the shorter way round
        float m = std::max(std::abs(pitch), std::abs(yaw));
        if (m < 1e-3f) yaw = 1, m = 1;
        pitch /= m;
        yaw /= m;
    } else {
        pitch *= 3.f;
        yaw *= 3.f;
    }
    float t = std::clamp(turn_throttle, 0.f, 1.f);
    return {std::clamp(pitch, -1.f, 1.f) * t, std::clamp(yaw, -1.f, 1.f) * t, 0};
}

FighterAi::FighterAi(const Pilot& pilot, std::uint32_t seed) : p_(&pilot), rng_(seed * 2654435761u + 0x9e3779b9u) {
    if (rng_ == 0) rng_ = 1;
    head_away_distance_ = vary(p_->buzz_head_toward.min_distance, p_->buzz_head_toward.min_distance_variance);
    range_scale_ = vary(p_->gun.range_threshold, p_->gun.range_threshold_variance);
    pause_left_ = vary(p_->gun.no_burst_time, 0.5f);
    dodge_wait_ = vary(p_->evade_dodge.interval, p_->evade_dodge.interval_variance);
}

float FighterAi::rand01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xffffff) / static_cast<float>(0x1000000);
}

float FighterAi::vary(float value, float variance_percent) {
    float v = std::clamp(variance_percent, 0.f, 1.f);
    return value * (1.f + v * (rand01() * 2.f - 1.f));
}

std::string_view FighterAi::pick(const Weights& w, std::string_view fallback) {
    float total = 0;
    for (auto& [n, x] : w) total += std::max(0.f, x);
    if (total <= 0) return fallback;
    float r = rand01() * total;
    for (auto& [n, x] : w) {
        r -= std::max(0.f, x);
        if (r <= 0) return n;
    }
    return w.back().first;
}

void FighterAi::enter(State s) {
    state_ = s;
    state_time_ = 0;
    if (s == State::PassBy) {
        straight_by_ = pick(p_->buzz_pass_by.styles, "break_away") == "straight_by";
        turn_sign_ = pick(p_->buzz_pass_by.directions, "right") == "left" ? -1.f : 1.f;
    } else if (s == State::Break) {
        turn_sign_ = pick(p_->evade_break.directions, "right") == "left" ? -1.f : 1.f;
        break_outrun_ = pick(p_->evade_break.styles, "sideways") == "outrun";
    } else if (s == State::HeadAway) {
        head_away_distance_ = vary(p_->buzz_head_toward.min_distance, p_->buzz_head_toward.min_distance_variance);
    } else if (s == State::Trail) {
        trail_no_lock_ = 0;
    }
}

void FighterAi::wing_slot(int n, float& right, float& up) {
    static const float k[][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    const float spacing = 60;
    int i = n % 9, ring = n / 9 + 1;
    right = k[i][0] * spacing * ring;
    up = k[i][1] * spacing * ring;
}

std::string_view FighterAi::name(State s) {
    switch (s) {
        case State::HeadToward: return "head toward";
        case State::PassBy: return "pass by";
        case State::HeadAway: return "head away";
        case State::Trail: return "trail";
        case State::Break: return "break";
        case State::Flee: return "flee";
    }
    return "?";
}

AiOutput FighterAi::update(const AiBody& self, const AiBody& target, float hull, bool target_is_player,
                           const AiWeapons& w, float dt) {
    AiOutput out;
    out.input.auto_level = true;
    state_time_ += dt;
    break_cooldown_ = std::max(0.f, break_cooldown_ - dt);

    Vec3 to = to_f(target.pos - self.pos);
    float dist = length(to);
    Vec3 to_dir = norm(to);
    Vec3 fwd = self.orient.forward();
    Vec3 lead = lead_dir(self, target, w.muzzle_speed);

    // Damage taken recently, as a fraction of max hull.
    float loss = std::max(0.f, last_hull_ - hull);
    last_hull_ = hull;
    loss_window_ += dt;
    recent_loss_ += loss;
    if (loss_window_ > 2.f) loss_window_ = 0, recent_loss_ = 0;

    // State changes.
    float flee_at = p_->job.flee_when_hull_damaged;
    if (state_ != State::Flee && !fled_ && flee_at > 0 && 1.f - hull >= flee_at) enter(State::Flee);
    if (state_ != State::Flee && state_ != State::Break && p_->has_evade_break && break_cooldown_ <= 0 &&
        recent_loss_ > 0 && recent_loss_ >= p_->damage_reaction.evade_break) {
        enter(State::Break);
        recent_loss_ = 0;
    }

    // Are we behind the target with it in front of us? Then trail it.
    Vec3 tfwd = target.orient.forward();
    bool behind_target = angle_between(tfwd, to_dir) < (90.f * kDeg);  // target flies away from us
    bool locked = angle_between(fwd, to_dir) < p_->trail.lock_cone_deg * kDeg;

    switch (state_) {
        case State::HeadToward:
            if (p_->has_trail && behind_target && locked && dist < p_->trail.distance * 3.f) enter(State::Trail);
            else if (dist < p_->buzz_pass_by.distance) enter(State::PassBy);
            break;
        case State::PassBy:
            if (state_time_ > p_->buzz_pass_by.time) enter(State::HeadAway);
            break;
        case State::HeadAway:
            if (dist > head_away_distance_ || state_time_ > p_->buzz_head_toward.max_time_to_head_away)
                enter(State::HeadToward);
            break;
        case State::Trail:
            trail_no_lock_ = locked ? 0.f : trail_no_lock_ + dt;
            if (trail_no_lock_ > std::max(0.2f, p_->trail.break_time)) enter(State::HeadAway);
            else if (dist < p_->buzz_pass_by.distance * 0.5f) enter(State::PassBy);
            break;
        case State::Break:
            if (state_time_ > std::max(0.5f, p_->evade_break.time)) {
                break_cooldown_ = p_->evade_break.interval;
                enter(State::HeadAway);
            }
            break;
        case State::Flee:
            // Our estimate: a ship runs until it is well clear (3 km, or 40 s), then turns
            // back; it does not run a second time.
            if (dist > 3000.f || state_time_ > 40.f) {
                fled_ = true;
                enter(State::HeadToward);
            }
            break;
    }

    // Steering for the state.
    Vec3 up = self.orient.up();
    Vec3 side = norm(render::cross(to_dir, up));  // to the right of the line to the target
    float turn = 1, throttle = 1;
    Vec3 want = lead;
    switch (state_) {
        case State::HeadToward:
            turn = p_->buzz_head_toward.turn_throttle;
            throttle = p_->buzz_head_toward.engine_throttle;
            break;
        case State::PassBy:
            if (straight_by_) want = fwd;
            else want = norm(fwd + side * turn_sign_);
            turn = p_->buzz_pass_by.break_turn_throttle;
            break;
        case State::HeadAway:
            want = norm(to_dir * -1.f + side * (turn_sign_ * 0.4f));
            break;
        case State::Trail: {
            turn = p_->trail.max_turn_throttle;
            if (trail_offset_right_ != 0 || trail_offset_up_ != 0) {
                Vec3 spot = to + target.orient.right() * trail_offset_right_ + target.orient.up() * trail_offset_up_;
                want = norm(lead + norm(spot) * 0.6f);
            }
            float err = (dist - p_->trail.distance) / std::max(50.f, p_->trail.distance);
            float tspeed = static_cast<float>(target.vel.length());
            float sspeed = static_cast<float>(self.vel.length());
            throttle = std::clamp(0.5f + err + (tspeed - sspeed) * 0.01f, 0.f, 1.f);
            break;
        }
        case State::Break: {
            want = break_outrun_ ? to_dir * -1.f : norm(side * turn_sign_ + to_dir * -0.3f);
            turn = p_->evade_break.turn_throttle;
            out.input.steer.z = p_->evade_break.roll_throttle * turn_sign_;
            break;
        }
        case State::Flee:
            want = to_dir * -1.f;
            break;
    }

    // Dodging: the target aims at us (evade dodge) or we are on a head-on run (buzz dodge).
    bool target_aims = angle_between(tfwd, to_dir * -1.f) < std::max(1.f, p_->evade_dodge.cone_deg) * kDeg;
    bool in_range = p_->evade_dodge.activate_range <= 0 || dist < p_->evade_dodge.activate_range;
    dodge_wait_ -= dt;
    if (dodge_left_ > 0) {
        dodge_left_ -= dt;
    } else if (dodge_wait_ <= 0) {
        bool evade = p_->has_evade_dodge && target_aims && in_range && state_ != State::Flee;
        bool buzz = state_ == State::HeadToward && p_->buzz_head_toward.dodge_cone_deg > 0;
        if (evade || buzz) {
            dodge_left_ = evade ? std::max(0.5f, p_->evade_dodge.time) : 1.f;
            dodge_sign_ = pick(p_->evade_dodge.directions, "right") == "left" ? -1.f : 1.f;
        }
        dodge_wait_ = evade ? vary(p_->evade_dodge.interval, p_->evade_dodge.interval_variance)
                            : vary(p_->buzz_head_toward.dodge_interval, p_->buzz_head_toward.dodge_interval_variance);
    }
    if (dodge_left_ > 0 && state_ != State::Break) {
        float cone = (state_ == State::HeadToward && p_->buzz_head_toward.dodge_cone_deg > 0)
                         ? p_->buzz_head_toward.dodge_cone_deg
                         : std::max(15.f, p_->evade_dodge.cone_deg * 0.5f);
        float wag = std::sin(state_time_ * 3.f) * dodge_sign_;  // waggle side to side
        want = norm(want + side * (std::tan(cone * kDeg) * wag));
        out.input.strafe_x = p_->evade_dodge.slide_throttle * dodge_sign_;
    }

    Vec3 s = steer_towards(self.orient, want, turn);
    out.input.steer.x = s.x;
    out.input.steer.y = s.y;
    out.input.throttle = throttle;

    // Guns: bursts of shots separated by pauses, fired when the target is in range and
    // roughly ahead, with an aiming error drawn inside the accuracy cone.
    const auto& g = p_->gun;
    if (burst_left_ > 0) {
        burst_left_ -= dt;
        if (burst_left_ <= 0) pause_left_ = vary(g.no_burst_time, g.burst_variance);
    } else {
        pause_left_ -= dt;
        if (pause_left_ <= 0) burst_left_ = vary(g.burst_time, g.burst_variance);
    }
    aim_switch_ -= dt;
    if (aim_switch_ <= 0) {
        aim_switch_ = std::max(0.1f, g.target_point_switch_time);
        float power = target_is_player ? g.accuracy_power : g.accuracy_power_npc;
        float a = g.accuracy_cone_deg * kDeg * std::pow(rand01(), std::max(0.1f, power));
        float phi = rand01() * 6.2831853f;
        aim_error_ = {std::cos(phi) * a, std::sin(phi) * a, 0};
    }
    Vec3 tup = norm(render::cross(side, lead));
    out.aim_dir = norm(lead + side * std::tan(aim_error_.x) + tup * std::tan(aim_error_.y));
    shot_wait_ -= dt;
    bool ahead = angle_between(fwd, lead) < 12.f * kDeg;
    bool reach = dist < w.range * range_scale_;
    if (burst_left_ > 0 && ahead && reach && state_ != State::Flee && shot_wait_ <= 0) {
        out.fire = true;
        shot_wait_ = vary(g.fire_interval, g.fire_interval_variance);
    }
    return out;
}

}  // namespace sterrenvaarder::game
