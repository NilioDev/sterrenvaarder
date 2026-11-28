// SPDX-License-Identifier: MPL-2.0
#pragma once
// Ship and equipment archetypes from the user's own game data (shiparch.ini, equipment
// INIs, loadouts). Only the values the engine needs are kept; unknown keys are ignored
// so modded data with extra fields loads unchanged.
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

struct ShipArchetype {
    std::string nickname;
    std::string type;    // FIGHTER, FREIGHTER, ...
    std::string model;   // DA_archetype
    std::vector<std::string> material_libraries;
    std::vector<float> lod_ranges;
    std::string explosion_arch;
    // fuse = name, delay, hit points: the fuse lights once the hull drops to that value.
    struct FuseRef { std::string name; float delay = 0, hit_pts = 0; };
    std::vector<FuseRef> fuses;
    float mass = 100;
    float linear_drag = 1;
    Vec3 steering_torque{20000, 20000, 60000};  // pitch, yaw, roll
    Vec3 angular_drag{20000, 20000, 60000};
    Vec3 rotation_inertia{5000, 5000, 5000};
    float strafe_force = 10000;
    float nudge_force = 0;
    float max_bank_angle = 30;      // degrees
    Vec2 camera_offset{8, 34};      // up, back
    float camera_angular_acceleration = 0.05f;
    float camera_horizontal_turn_angle = 17;
    float camera_vertical_turn_up_angle = 5;
    float camera_vertical_turn_down_angle = 25;
    float hit_pts = 1000;
    float hold_size = 0;
    int nanobot_limit = 0, shield_battery_limit = 0;
};

struct EngineArchetype {
    std::string nickname;
    float max_force = 48000;
    float linear_drag = 600;
    float reverse_fraction = 0.2f;
    float cruise_charge_time = 5;
    std::string flame_effect, trail_effect;  // visual effects at the ship's engine hardpoints
    std::string trail_effect_player;         // the trail as the player sees it on their own ship
    // Sound nicknames: the engine's hum, and cruise starting, running and stopping.
    std::string rumble_sound, character_loop_sound, cruise_start_sound, cruise_loop_sound, cruise_stop_sound,
        cruise_disrupt_sound;
};

struct MunitionArchetype {
    std::string nickname;
    float hull_damage = 0, energy_damage = 0;  // energy damage hits shields only
    float lifetime = 1;
    std::string const_effect;  // the bolt's own effect
    std::string hit_effect;    // munition_hit_effect
    std::string fire_sound;    // one_shot_sound: played as the gun fires it
};

struct GunArchetype {
    std::string nickname;
    std::string model;  // DA_archetype
    std::vector<std::string> material_libraries;
    std::string projectile;  // MunitionArchetype nickname
    float refire_delay = 0.2f;
    float muzzle_velocity = 600;
    float power_usage = 0;
    std::string flash_effect;  // flash_particle_name: muzzle flash
    std::string weapon_type;   // WeaponModDB entry: how hard it hits each shield type
};

struct ShieldArchetype {
    std::string nickname;
    float max_capacity = 0;
    float regeneration_rate = 0;
    float offline_rebuild_time = 0;
    float offline_threshold = 0;
    std::string shield_type;  // S_Graviton01 etc., for WeaponModDB
    // shield_hit_effects = damage, effect: the strongest entry at or below a hit's damage plays.
    std::vector<std::pair<float, std::string>> hit_effects;
    std::string collapse_sound, rebuilt_sound;  // shield_collapse_sound, shield_rebuilt_sound
    const std::string& hit_effect(float damage) const;
};

// How a piece of equipment looks floating in space as loot: its loot crate's model.
struct LootLook {
    std::string crate;  // [LootCrate] nickname
    std::string model;  // DA_archetype
    std::vector<std::string> material_libraries;
};

struct Loadout {
    std::string nickname;
    std::string archetype;
    struct Equip { std::string item, hardpoint; };
    std::vector<Equip> equip;
};

// Default data file lists, as given in an unmodified Freelancer.ini. Freelancer.ini
// itself lives next to the executable, so it is read when the user points us at it.
struct DataFiles {
    std::string weapon_moddb = "equipment/weaponmoddb.ini";
    std::vector<std::string> ships{"ships/shiparch.ini", "ships/rtc_shiparch.ini"};
    std::vector<std::string> equipment{"equipment/light_equip.ini", "equipment/select_equip.ini",
                                       "equipment/misc_equip.ini",  "equipment/engine_equip.ini",
                                       "equipment/st_equip.ini",    "equipment/weapon_equip.ini",
                                       "equipment/prop_equip.ini"};
    std::vector<std::string> loadouts{"ships/loadouts.ini", "ships/loadouts_special.ini",
                                      "ships/loadouts_utility.ini", "solar/loadouts.ini"};
    std::vector<std::string> goods{"equipment/goods.ini", "equipment/engine_good.ini", "equipment/misc_good.ini",
                                   "equipment/st_good.ini", "equipment/weapon_good.ini"};
    std::vector<std::string> effects{"fx/effect_types.ini",          "fx/beam_effects.ini",
                                     "fx/engines/engines_ale.ini",    "fx/equipment/equipment_ale.ini",
                                     "fx/explosions/explosions_ale.ini", "fx/hull_hits/hull_hits_ale.ini",
                                     "fx/misc/misc_ale.ini",          "fx/shields/shields_ale.ini",
                                     "fx/space/space_ale.ini",        "fx/weapons/weapons_ale.ini",
                                     "fx/effects.ini",                "fx/effects_explosion.ini"};
    std::vector<std::string> explosions{"fx/explosions.ini"};
    std::vector<std::string> fuses{"fx/fuse.ini"};
    std::vector<std::string> markets{"equipment/market_misc.ini", "equipment/market_ships.ini",
                                     "equipment/market_commodities.ini"};
    // Reads the [Data] section of a Freelancer.ini (text), replacing the defaults for
    // every list it mentions.
    void read_freelancer_ini(std::string_view text);
};

class ShipDatabase {
public:
    ShipDatabase(const Vfs& vfs, const DataFiles& files = {});

    const ShipArchetype* ship(std::string_view nickname) const;
    const EngineArchetype* engine(std::string_view nickname) const;
    const Loadout* loadout(std::string_view nickname) const;
    // First loadout for this ship archetype, if any (used for NPC and test ships).
    const Loadout* loadout_for_ship(std::string_view ship_nickname) const;
    // The engine named in a loadout, or nullptr.
    const EngineArchetype* engine_of(const Loadout& l) const;
    const GunArchetype* gun(std::string_view nickname) const;
    const MunitionArchetype* munition(std::string_view nickname) const;
    const ShieldArchetype* shield(std::string_view nickname) const;
    const ShieldArchetype* shield_of(const Loadout& l) const;
    // WeaponModDB: damage multiplier for a weapon type hitting a shield type (1 if unlisted).
    float shield_mod(std::string_view weapon_type, std::string_view shield_type) const;
    // [Armor] hit_pts_scale of the loadout's armour (1 without armour).
    float armor_scale_of(const Loadout& l) const;
    // The loot crate an equipment item (or commodity) shows as (loot_appearance), if any.
    const LootLook* loot_look(std::string_view equipment) const;
    // [Tractor] max_length of the loadout's tractor beam (0 without one).
    float tractor_range_of(const Loadout& l) const;

    // Cruise speed: 300 m/s unless the data sets [EngineEquipConsts] CRUISING_SPEED.
    float cruise_speed() const { return cruise_speed_; }
    const std::vector<std::string>& ship_order() const { return ship_order_; }

private:
    std::unordered_map<std::string, ShipArchetype> ships_;
    std::unordered_map<std::string, EngineArchetype> engines_;
    std::unordered_map<std::string, Loadout> loadouts_;
    std::unordered_map<std::string, GunArchetype> guns_;
    std::unordered_map<std::string, MunitionArchetype> munitions_;
    std::unordered_map<std::string, ShieldArchetype> shields_;
    std::unordered_map<std::string, float> shield_mods_;
    std::unordered_map<std::string, std::string> loot_appearance_;  // equipment -> crate
    std::unordered_map<std::string, LootLook> crates_;
    std::unordered_map<std::string, float> tractors_;               // [Tractor] max_length
    std::unordered_map<std::string, float> armor_;  // [Armor] nickname -> hit_pts_scale  // "weapon|shield", lower case
    std::unordered_map<std::string, std::string> first_loadout_;
    std::vector<std::string> ship_order_;
    float cruise_speed_ = 300;
};

}  // namespace sterrenvaarder
