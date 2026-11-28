// SPDX-License-Identifier: MPL-2.0
#pragma once
// Random traffic: the population zones of a system, the encounter files they name, ship
// classes and the ships each faction flies. Everything here is read from the player's own
// data; picking and spawning ships happens in the game layer.
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/mission.hpp"

namespace sterrenvaarder {

// A [Zone] of a system file, with what the population needs from it.
struct PopZone {
    std::string nickname;
    std::array<float, 3> pos{0, 0, 0};
    std::array<float, 3> rotate{0, 0, 0};  // degrees, x y z
    std::string shape;                      // SPHERE, ELLIPSOID, BOX, CYLINDER, RING
    std::array<float, 3> size{0, 0, 0};     // one value for a sphere
    int toughness = 0;                      // the ship level (d1..d19) spawned here
    int density = 0;                        // how many ships the zone wants around
    float repop_time = 0;                   // seconds between new encounters
    int max_battle_size = 0;
    float relief_time = 0;
    struct Restriction { int max = 0; std::string ship_class; };
    std::vector<Restriction> density_restrictions;
    struct Encounter {
        std::string name;      // file in missions/encounters
        int difficulty = 0;
        float chance = 0;      // relative weight
        std::vector<std::pair<std::string, float>> factions;  // group, weight
    };
    std::vector<Encounter> encounters;
    bool populated() const { return density > 0 && !encounters.empty(); }
    // True when a point is inside the zone's shape.
    bool contains(float x, float y, float z) const;
};
std::vector<PopZone> parse_pop_zones(const IniFile& system_ini);

// An encounter file: formations of ships by class, and how often each is used.
struct EncounterDef {
    struct Slot {
        int min = 0, max = 0;
        std::string ship_class;  // a [ShipClass] nickname
        int level_offset = 0;    // fourth value: ship level relative to the zone's
        std::string pilot_job;
    };
    struct Formation {
        std::vector<Slot> slots;
        std::string formation, behavior;  // formation_by_class, behavior (wander, trade, patrol_path, ...)
        std::vector<std::string> arrival;
    };
    std::vector<Formation> formations;
    std::vector<std::pair<int, float>> permutations;  // formation index, weight
};
EncounterDef parse_encounter(const IniFile& ini);

// missions/shipclasses.ini: class nickname -> the npc_class tags a ship must have.
using ShipClasses = std::unordered_map<std::string, std::vector<std::string>>;  // lower-case keys
ShipClasses parse_ship_classes(const IniFile& ini);

// missions/faction_prop.ini: faction group -> its npc_ship nicknames.
using FactionShips = std::unordered_map<std::string, std::vector<std::string>>;  // lower-case keys
FactionShips parse_faction_ships(const IniFile& ini);

// The ships a faction can send for a class at a level ("d3"), from the faction's own list.
std::vector<const NpcShipArch*> ships_for(const std::vector<NpcShipArch>& archs, const FactionShips& factions,
                                          const ShipClasses& classes, std::string_view faction,
                                          std::string_view ship_class, int level);

}  // namespace sterrenvaarder
