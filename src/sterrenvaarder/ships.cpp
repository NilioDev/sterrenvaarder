// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/ships.hpp"

#include "sterrenvaarder/universe.hpp"

namespace sterrenvaarder {

namespace {

std::string str(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

float num(const IniSection& s, std::string_view key, float fallback) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_float() : fallback;
}

Vec2 vec2(const IniSection& s, std::string_view key, Vec2 fallback) {
    auto e = s.find(key);
    if (!e || e->values.size() < 2) return fallback;
    return {e->values[0].as_float(), e->values[1].as_float()};
}

std::string normalize_path(std::string p) {
    for (auto& c : p)
        if (c == '\\') c = '/';
    while (!p.empty() && (p.back() == ' ' || p.back() == '\t')) p.pop_back();
    return to_lower(p);
}

template <class F>
void each_section(const Vfs& vfs, const std::vector<std::string>& files, F f) {
    for (auto& path : files) {
        if (!vfs.exists(path)) continue;
        for (auto& sec : parse_ini(vfs.read(path))) f(sec);
    }
}

}  // namespace

void DataFiles::read_freelancer_ini(std::string_view text) {
    std::vector<std::string> s, e, l, g, m, fx, ex;
    for (auto& sec : parse_text_ini(text)) {
        if (!iequals(sec.name, "Data")) continue;
        for (auto& entry : sec.entries) {
            if (entry.values.empty()) continue;
            auto v = normalize_path(entry.values[0].as_string());
            if (iequals(entry.key, "ships")) s.push_back(v);
            else if (iequals(entry.key, "equipment")) e.push_back(v);
            else if (iequals(entry.key, "loadouts")) l.push_back(v);
            else if (iequals(entry.key, "goods")) g.push_back(v);
            else if (iequals(entry.key, "markets")) m.push_back(v);
            else if (iequals(entry.key, "effects")) fx.push_back(v);
            else if (iequals(entry.key, "explosions")) ex.push_back(v);
        }
    }
    if (!s.empty()) ships = s;
    if (!e.empty()) equipment = e;
    if (!l.empty()) loadouts = l;
    if (!g.empty()) goods = g;
    if (!m.empty()) markets = m;
    if (!fx.empty()) effects = fx;
    if (!ex.empty()) explosions = ex;
}

ShipDatabase::ShipDatabase(const Vfs& vfs, const DataFiles& files) {
    each_section(vfs, files.ships, [&](const IniSection& sec) {
        if (!iequals(sec.name, "Ship")) return;
        ShipArchetype a;
        a.nickname = str(sec, "nickname");
        if (a.nickname.empty()) return;
        a.type = str(sec, "type");
        a.model = str(sec, "DA_archetype");
        for (auto* m : sec.find_all("material_library"))
            if (!m->values.empty()) a.material_libraries.push_back(m->values[0].as_string());
        if (auto l = sec.find("LODranges"))
            for (auto& v : l->values) a.lod_ranges.push_back(v.as_float());
        a.mass = num(sec, "mass", a.mass);
        a.linear_drag = num(sec, "linear_drag", a.linear_drag);
        a.steering_torque = ini_vec3(sec.find("steering_torque"), a.steering_torque);
        a.angular_drag = ini_vec3(sec.find("angular_drag"), a.angular_drag);
        a.rotation_inertia = ini_vec3(sec.find("rotation_inertia"), a.rotation_inertia);
        a.strafe_force = num(sec, "strafe_force", a.strafe_force);
        a.nudge_force = num(sec, "nudge_force", a.nudge_force);
        a.max_bank_angle = num(sec, "max_bank_angle", a.max_bank_angle);
        a.camera_offset = vec2(sec, "camera_offset", a.camera_offset);
        a.camera_angular_acceleration = num(sec, "camera_angular_acceleration", a.camera_angular_acceleration);
        a.camera_horizontal_turn_angle = num(sec, "camera_horizontal_turn_angle", a.camera_horizontal_turn_angle);
        a.camera_vertical_turn_up_angle = num(sec, "camera_vertical_turn_up_angle", a.camera_vertical_turn_up_angle);
        a.camera_vertical_turn_down_angle = num(sec, "camera_vertical_turn_down_angle", a.camera_vertical_turn_down_angle);
        a.hit_pts = num(sec, "hit_pts", a.hit_pts);
        a.explosion_arch = str(sec, "explosion_arch");
        a.hold_size = num(sec, "hold_size", a.hold_size);
        a.nanobot_limit = static_cast<int>(num(sec, "nanobot_limit", 0));
        a.shield_battery_limit = static_cast<int>(num(sec, "shield_battery_limit", 0));
        auto key = to_lower(a.nickname);
        if (!ships_.count(key)) ship_order_.push_back(key);
        ships_[key] = std::move(a);
    });
    each_section(vfs, files.equipment, [&](const IniSection& sec) {
        if (iequals(sec.name, "Gun")) {
            GunArchetype g;
            g.nickname = str(sec, "nickname");
            if (g.nickname.empty()) return;
            g.model = str(sec, "DA_archetype");
            for (auto* m : sec.find_all("material_library"))
                if (!m->values.empty()) g.material_libraries.push_back(m->values[0].as_string());
            g.projectile = str(sec, "projectile_archetype");
            g.refire_delay = num(sec, "refire_delay", g.refire_delay);
            g.muzzle_velocity = num(sec, "muzzle_velocity", g.muzzle_velocity);
            g.power_usage = num(sec, "power_usage", g.power_usage);
            g.flash_effect = str(sec, "flash_particle_name");
            guns_[to_lower(g.nickname)] = std::move(g);
            return;
        }
        if (iequals(sec.name, "Munition")) {
            MunitionArchetype m;
            m.nickname = str(sec, "nickname");
            if (m.nickname.empty()) return;
            m.hull_damage = num(sec, "hull_damage", 0);
            m.energy_damage = num(sec, "energy_damage", 0);
            m.lifetime = num(sec, "lifetime", m.lifetime);
            m.const_effect = str(sec, "const_effect");
            m.hit_effect = str(sec, "munition_hit_effect");
            munitions_[to_lower(m.nickname)] = std::move(m);
            return;
        }
        if (iequals(sec.name, "ShieldGenerator")) {
            ShieldArchetype sh;
            sh.nickname = str(sec, "nickname");
            if (sh.nickname.empty()) return;
            sh.max_capacity = num(sec, "max_capacity", 0);
            sh.regeneration_rate = num(sec, "regeneration_rate", 0);
            sh.offline_rebuild_time = num(sec, "offline_rebuild_time", 0);
            sh.offline_threshold = num(sec, "offline_threshold", 0);
            shields_[to_lower(sh.nickname)] = std::move(sh);
            return;
        }
        if (!iequals(sec.name, "Engine")) return;
        EngineArchetype e;
        e.nickname = str(sec, "nickname");
        if (e.nickname.empty()) return;
        e.max_force = num(sec, "max_force", e.max_force);
        e.linear_drag = num(sec, "linear_drag", e.linear_drag);
        e.reverse_fraction = num(sec, "reverse_fraction", e.reverse_fraction);
        e.cruise_charge_time = num(sec, "cruise_charge_time", e.cruise_charge_time);
        e.flame_effect = str(sec, "flame_effect");
        e.trail_effect = str(sec, "trail_effect");
        engines_[to_lower(e.nickname)] = std::move(e);
    });
    each_section(vfs, files.loadouts, [&](const IniSection& sec) {
        if (!iequals(sec.name, "Loadout")) return;
        Loadout l;
        l.nickname = str(sec, "nickname");
        if (l.nickname.empty()) return;
        l.archetype = str(sec, "archetype");
        for (auto* e : sec.find_all("equip")) {
            if (e->values.empty()) continue;
            l.equip.push_back({e->values[0].as_string(), e->values.size() > 1 ? e->values[1].as_string() : std::string()});
        }
        auto arch = to_lower(l.archetype);
        if (!arch.empty() && !first_loadout_.count(arch)) first_loadout_[arch] = to_lower(l.nickname);
        loadouts_[to_lower(l.nickname)] = std::move(l);
    });
    if (vfs.exists("constants.ini"))
        for (auto& sec : parse_ini(vfs.read("constants.ini")))
            if (iequals(sec.name, "EngineEquipConsts")) cruise_speed_ = num(sec, "CRUISING_SPEED", cruise_speed_);
}

const ShipArchetype* ShipDatabase::ship(std::string_view n) const {
    auto it = ships_.find(to_lower(n));
    return it == ships_.end() ? nullptr : &it->second;
}
const EngineArchetype* ShipDatabase::engine(std::string_view n) const {
    auto it = engines_.find(to_lower(n));
    return it == engines_.end() ? nullptr : &it->second;
}
const Loadout* ShipDatabase::loadout(std::string_view n) const {
    auto it = loadouts_.find(to_lower(n));
    return it == loadouts_.end() ? nullptr : &it->second;
}
const Loadout* ShipDatabase::loadout_for_ship(std::string_view s) const {
    auto it = first_loadout_.find(to_lower(s));
    return it == first_loadout_.end() ? nullptr : loadout(it->second);
}
const EngineArchetype* ShipDatabase::engine_of(const Loadout& l) const {
    for (auto& e : l.equip)
        if (auto* eng = engine(e.item)) return eng;
    return nullptr;
}

const GunArchetype* ShipDatabase::gun(std::string_view n) const {
    auto it = guns_.find(to_lower(n));
    return it == guns_.end() ? nullptr : &it->second;
}
const MunitionArchetype* ShipDatabase::munition(std::string_view n) const {
    auto it = munitions_.find(to_lower(n));
    return it == munitions_.end() ? nullptr : &it->second;
}
const ShieldArchetype* ShipDatabase::shield(std::string_view n) const {
    auto it = shields_.find(to_lower(n));
    return it == shields_.end() ? nullptr : &it->second;
}
const ShieldArchetype* ShipDatabase::shield_of(const Loadout& l) const {
    for (auto& e : l.equip)
        if (auto* s = shield(e.item)) return s;
    return nullptr;
}

}  // namespace sterrenvaarder
