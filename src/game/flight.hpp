// SPDX-License-Identifier: MPL-2.0
#pragma once
// Newtonian flight model driven by the ship's own INI values (mass, drag, torque,
// inertia, engine force). Steady-state speed is max_force / (ship drag + engine drag),
// which gives the familiar ~80 m/s for a light fighter, and turn rate saturates at
// steering_torque / angular_drag. Runs at a fixed step so results are deterministic.
#include "game/quat.hpp"
#include "sterrenvaarder/ships.hpp"

namespace sterrenvaarder::game {

struct FlightInput {
    float throttle = 0;   // -1 (full reverse) .. 1
    Vec3 steer{};         // pitch (+ = nose up), yaw (+ = nose left), roll (+ = roll left); -1..1
    float strafe_x = 0;   // -1 left .. 1 right
    float strafe_y = 0;   // -1 down .. 1 up
    bool cruise = false;  // hold to engage cruise (charges first)
    bool auto_level = true;
};

struct FlightParams {
    float mass = 100, drag = 600;
    float max_force = 48000, reverse_fraction = 0.2f;
    Vec3 torque{}, angular_drag{}, inertia{};
    float strafe_force = 10000;
    float cruise_speed = 300, cruise_charge_time = 5;
    float max_bank_deg = 30;

    static FlightParams from(const ShipArchetype& ship, const EngineArchetype* engine, float cruise_speed);
    float max_speed() const { return drag > 0 ? max_force / drag : 0; }
};

struct ShipState {
    DVec3 pos;
    Quat orient;
    DVec3 vel;
    Vec3 ang_vel{};      // local frame: x pitch, y yaw, z roll (rad/s)
    float cruise_charge = 0;  // seconds charged
    bool cruising = false;
    float bank = 0;      // visual bank angle (rad), not part of the physics
    float speed() const { return static_cast<float>(vel.length()); }
};

class FlightModel {
public:
    explicit FlightModel(FlightParams p) : p_(p) {}
    const FlightParams& params() const { return p_; }

    // Advances `s` by `dt` seconds (internally in fixed steps of 1/120 s).
    void update(ShipState& s, const FlightInput& in, double dt);
    static constexpr double kStep = 1.0 / 120.0;

private:
    void step(ShipState& s, const FlightInput& in, float dt);
    FlightParams p_;
    double accumulator_ = 0;
};

// Converts a mouse position (pixels from the screen centre, scaled by half the screen
// height) into steering, with a small dead zone so the ship can fly straight.
Vec3 mouse_steer(float dx_norm, float dy_norm, float dead_zone = 0.05f);

}  // namespace sterrenvaarder::game
