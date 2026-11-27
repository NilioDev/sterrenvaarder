// SPDX-License-Identifier: MPL-2.0
#include "game/flight.hpp"

namespace sterrenvaarder::game {

FlightParams FlightParams::from(const ShipArchetype& ship, const EngineArchetype* engine, float cruise_speed) {
    FlightParams p;
    p.mass = std::max(1.f, ship.mass);
    p.drag = ship.linear_drag + (engine ? engine->linear_drag : 600.f);
    p.max_force = engine ? engine->max_force : 48000.f;
    p.reverse_fraction = engine ? engine->reverse_fraction : 0.2f;
    p.cruise_charge_time = engine ? engine->cruise_charge_time : 5.f;
    p.torque = ship.steering_torque;
    p.angular_drag = ship.angular_drag;
    p.inertia = ship.rotation_inertia;
    p.strafe_force = ship.strafe_force;
    p.cruise_speed = cruise_speed;
    p.max_bank_deg = ship.max_bank_angle;
    return p;
}

Vec3 mouse_steer(float dx, float dy, float dead) {
    auto shape = [dead](float v) {
        float a = std::fabs(v);
        if (a < dead) return 0.f;
        a = std::min(1.f, (a - dead) / (1.f - dead));
        return std::copysign(a, v);
    };
    // Cursor right -> yaw right (negative yaw), cursor up (dy < 0) -> pitch up.
    return {shape(-dy), shape(-dx), 0};
}

void FlightModel::update(ShipState& s, const FlightInput& in, double dt) {
    accumulator_ += std::min(dt, 0.25);
    while (accumulator_ >= kStep) {
        step(s, in, static_cast<float>(kStep));
        accumulator_ -= kStep;
    }
}

void FlightModel::step(ShipState& s, const FlightInput& in, float dt) {
    // ---- cruise: charge while held, drop out when released or when braking
    if (in.cruise && in.throttle >= 0) {
        s.cruise_charge = std::min(p_.cruise_charge_time, s.cruise_charge + dt);
        s.cruising = s.cruise_charge >= p_.cruise_charge_time;
    } else {
        s.cruise_charge = 0;
        s.cruising = false;
    }

    // ---- linear: thrust along the nose, strafing sideways, drag against velocity
    Vec3 fwd = s.orient.forward(), right = s.orient.right(), up = s.orient.up();
    float thrust;
    if (s.cruising) thrust = p_.cruise_speed * p_.drag;  // terminal speed = cruise speed
    else if (in.throttle >= 0) thrust = in.throttle * p_.max_force;
    else thrust = in.throttle * p_.max_force * p_.reverse_fraction;
    Vec3 f = fwd * thrust + right * (std::clamp(in.strafe_x, -1.f, 1.f) * p_.strafe_force) +
             up * (std::clamp(in.strafe_y, -1.f, 1.f) * p_.strafe_force);
    // Drag is applied implicitly so large drag/mass ratios stay stable at any step size.
    double k = p_.drag / p_.mass * dt;
    DVec3 accel = to_d(f) * (1.0 / p_.mass);
    s.vel = (s.vel + accel * dt) * (1.0 / (1.0 + k));
    s.pos = s.pos + s.vel * dt;

    // ---- angular: per local axis, torque against angular drag
    Vec3 steer = in.steer;
    if (in.auto_level) {
        // Keep the wings level with the system plane, like the original: roll towards
        // a right vector with no vertical component, unless the pilot rolls.
        if (std::fabs(steer.z) < 0.01f && std::fabs(fwd.y) < 0.98f) {
            float err = std::asin(std::clamp(right.y, -1.f, 1.f));  // > 0: right wing up
            steer.z = std::clamp(-err * 2.5f - s.ang_vel.z * 0.3f, -1.f, 1.f);
        }
    }
    float in_axes[3] = {std::clamp(steer.x, -1.f, 1.f), std::clamp(steer.y, -1.f, 1.f), std::clamp(steer.z, -1.f, 1.f)};
    float torque[3] = {p_.torque.x, p_.torque.y, p_.torque.z};
    float drag[3] = {p_.angular_drag.x, p_.angular_drag.y, p_.angular_drag.z};
    float inertia[3] = {p_.inertia.x, p_.inertia.y, p_.inertia.z};
    float* w[3] = {&s.ang_vel.x, &s.ang_vel.y, &s.ang_vel.z};
    for (int i = 0; i < 3; ++i) {
        float I = std::max(1.f, inertia[i]);
        float kd = drag[i] / I * dt;
        *w[i] = (*w[i] + in_axes[i] * torque[i] / I * dt) / (1.f + kd);
    }
    Vec3 wv = s.ang_vel;
    float ang = length(wv) * dt;
    if (ang > 0) s.orient = (s.orient * Quat::axis_angle(render::normalize(wv), ang)).normalized();

    // ---- visual bank into turns (the hull leans, the flight path does not)
    float max_turn = p_.angular_drag.y > 0 ? p_.torque.y / p_.angular_drag.y : 1.f;
    float target_bank = std::clamp(s.ang_vel.y / std::max(0.01f, max_turn), -1.f, 1.f) * p_.max_bank_deg * 3.14159265f / 180.f;
    s.bank += (target_bank - s.bank) * std::min(1.f, dt * 4.f);
}

}  // namespace sterrenvaarder::game
