// SPDX-License-Identifier: MPL-2.0
#include "game/combat.hpp"

namespace sterrenvaarder::game {

void Combatant::set_shield(const ShieldArchetype* s) {
    if (!s) {
        shield = max_shield = 0;
        return;
    }
    shield = max_shield = s->max_capacity;
    shield_regen = s->regeneration_rate;
    shield_rebuild_time = s->offline_rebuild_time;
    shield_rebuild_level = s->offline_threshold;
}

void Combatant::damage(float hull_damage, float energy_damage) {
    if (!alive()) return;
    if (shield > 0 && shield_offline <= 0) {
        shield -= hull_damage + energy_damage;
        if (shield <= 0) {
            shield = 0;
            shield_offline = shield_rebuild_time;
        }
        return;
    }
    hull = std::max(0.f, hull - hull_damage);
}

void Combatant::update(float dt) {
    if (max_shield <= 0 || !alive()) return;
    if (shield_offline > 0) {
        shield_offline -= dt;
        if (shield_offline <= 0) shield = max_shield * std::max(0.01f, shield_rebuild_level);
        return;
    }
    shield = std::min(max_shield, shield + shield_regen * dt);
}

void CombatWorld::update_guns(std::vector<Gun>& guns, float dt) {
    for (auto& g : guns) g.cooldown = std::max(0.f, g.cooldown - dt);
}

void CombatWorld::fire(std::vector<Gun>& guns, int owner, DVec3 ship_pos, const Quat& orient, DVec3 ship_vel, Vec3 aim) {
    for (auto& g : guns) {
        if (!g.arch || !g.munition || g.cooldown > 0) continue;
        g.cooldown = g.arch->refire_delay;
        Projectile p;
        p.pos = ship_pos + to_d(orient.rotate(g.offset));
        p.vel = ship_vel + to_d(aim) * g.arch->muzzle_velocity;
        p.life = g.munition->lifetime;
        p.hull_damage = g.munition->hull_damage;
        p.energy_damage = g.munition->energy_damage;
        p.owner = owner;
        p.gun = g.arch;
        p.munition = g.munition;
        projectiles_.push_back(p);
    }
}

void CombatWorld::update(float dt, std::vector<Combatant*>& targets,
                         const std::function<void(Combatant&, const Projectile&)>& on_hit) {
    for (auto& p : projectiles_) {
        DVec3 a = p.pos, d = p.vel * dt;
        p.pos = p.pos + d;
        p.life -= dt;
        double len2 = d.x * d.x + d.y * d.y + d.z * d.z;
        for (auto* t : targets) {
            if (!t->alive() || t->id == p.owner) continue;
            // Closest point on the segment a..a+d to the target centre.
            DVec3 m = t->pos - a;
            double u = len2 > 0 ? std::clamp((m.x * d.x + m.y * d.y + m.z * d.z) / len2, 0.0, 1.0) : 0.0;
            DVec3 c = a + d * u - t->pos;
            if (c.x * c.x + c.y * c.y + c.z * c.z <= double(t->radius) * t->radius) {
                if (on_hit) on_hit(*t, p);
                t->damage(p.hull_damage, p.energy_damage);
                p.life = 0;
                break;
            }
        }
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return p.life <= 0; });
}

}  // namespace sterrenvaarder::game
