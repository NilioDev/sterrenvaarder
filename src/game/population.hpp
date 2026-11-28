// SPDX-License-Identifier: MPL-2.0
#pragma once
// Random traffic. Zones near the player send encounters (a formation of ships of one
// faction) until they hold their density, one encounter per repop_time. The caller spawns
// the ships, flies them and reports when one is gone (destroyed, docked or out of range).
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "game/quat.hpp"
#include "sterrenvaarder/population.hpp"

namespace sterrenvaarder::game {

struct PopSpawn {
    std::size_t zone = 0;
    std::string faction, behavior, formation;
    std::vector<const NpcShipArch*> ships;  // leader first
    DVec3 pos;
};

class Population {
public:
    using EncounterLookup = std::function<const EncounterDef*(std::string_view name)>;

    Population(std::vector<PopZone> zones, const std::vector<NpcShipArch>& archs, const FactionShips& factions,
               const ShipClasses& classes, EncounterLookup encounters, std::uint32_t seed = 1);

    // Encounters to spawn now, near `player`.
    std::vector<PopSpawn> update(float dt, const DVec3& player);
    // A ship from a spawn is gone; frees its place in the zone.
    void ship_gone(std::size_t zone, const NpcShipArch* arch);

    // Mission controls: RandomPop on/off, and spheres without new traffic.
    void set_enabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    void block_sphere(const DVec3& centre, double radius, bool blocked);

    const std::vector<PopZone>& zones() const { return zones_; }
    int alive(std::size_t zone) const { return zone < live_.size() ? live_[zone].ships : 0; }
    // Distance at which zones wake up, and the band around the player new ships appear in.
    double wake_range = 4000, spawn_min = 2500, spawn_max = 7000;

private:
    struct Live {
        int ships = 0;
        float timer = 0;  // seconds until the next encounter may come
        std::vector<std::pair<std::string, int>> by_class;  // restricted class -> ships
    };
    bool near(const PopZone& z, const DVec3& p) const;
    bool blocked(const DVec3& p) const;
    std::optional<DVec3> spawn_point(const PopZone& z, const DVec3& player);
    int class_count(const Live& l, std::string_view cls) const;
    bool fits_restrictions(const PopZone& z, const Live& l, const std::vector<const NpcShipArch*>& ships) const;
    bool in_class(const NpcShipArch& a, std::string_view cls) const;

    std::vector<PopZone> zones_;
    const std::vector<NpcShipArch>& archs_;
    const FactionShips& factions_;
    const ShipClasses& classes_;
    EncounterLookup encounters_;
    std::vector<Live> live_;
    std::mt19937 rng_;
    bool enabled_ = true;
    struct Block { DVec3 c; double r; };
    std::vector<Block> blocks_;
};

}  // namespace sterrenvaarder::game
