// SPDX-License-Identifier: MPL-2.0
#pragma once
// Projectiles, shields and hull damage. Values come from the player's own equipment
// INIs (muzzle_velocity, refire_delay, hull_damage, energy_damage, lifetime, shield
// capacity and regeneration). Pure simulation: no rendering, unit-tested headless.
#include <functional>
#include <vector>

#include "game/quat.hpp"
#include "sterrenvaarder/ships.hpp"

namespace sterrenvaarder::game {

struct Projectile {
    DVec3 pos, vel;
    float life = 1;
    float hull_damage = 0, energy_damage = 0;
    int owner = -1;  // combatant id that fired it
    const GunArchetype* gun = nullptr;
    const MunitionArchetype* munition = nullptr;
};

struct Combatant {
    int id = -1;
    DVec3 pos;
    float radius = 10;  // hit sphere
    float hull = 1000, max_hull = 1000;
    float shield = 0, max_shield = 0;
    float shield_regen = 0;          // per second
    float shield_rebuild_time = 0;   // seconds offline after collapsing
    float shield_rebuild_level = 0;  // fraction restored when it comes back
    float shield_offline = 0;        // seconds left offline
    const ShieldArchetype* shield_arch = nullptr;
    float hull_floor = 0;            // hits stop here (mission Invulnerable); > 0 cannot die
    bool alive() const { return hull > 0; }

    void set_shield(const ShieldArchetype* s);
    // Applies one hit. Shields take both damage types while up; once down, the hull
    // takes hull damage only (energy damage has nothing to drain).
    // `shield_mod` scales what the shield takes (WeaponModDB: weapon type vs shield type).
    void damage(float hull_damage, float energy_damage, float shield_mod = 1);
    void update(float dt);
};

struct Gun {
    const GunArchetype* arch = nullptr;
    const MunitionArchetype* munition = nullptr;
    Vec3 offset;     // hardpoint position in ship space
    float cooldown = 0;
};

class CombatWorld {
public:
    // Fires every gun that is ready, aimed at `aim_dir` (world space, normalised).
    // Projectiles inherit the shooter's velocity, as in the original.
    void fire(std::vector<Gun>& guns, int owner, DVec3 ship_pos, const Quat& orient, DVec3 ship_vel, Vec3 aim_dir);
    void update_guns(std::vector<Gun>& guns, float dt);

    // Moves projectiles, tests them against every live combatant except the shooter,
    // and calls on_hit(target, projectile) for each hit before applying damage.
    void update(float dt, std::vector<Combatant*>& targets,
                const std::function<void(Combatant&, const Projectile&)>& on_hit = {});

    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    // WeaponModDB lookups for shield hits (none = every weapon hits every shield at 1x).
    void set_shield_mods(const ShipDatabase* db) { mods_ = db; }

private:
    const ShipDatabase* mods_ = nullptr;
    std::vector<Projectile> projectiles_;
};

}  // namespace sterrenvaarder::game
