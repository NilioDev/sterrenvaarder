// SPDX-License-Identifier: MPL-2.0
#include "game/population.hpp"

#include <algorithm>
#include <cmath>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

Population::Population(std::vector<PopZone> zones, const std::vector<NpcShipArch>& archs, const FactionShips& factions,
                       const ShipClasses& classes, EncounterLookup encounters, std::uint32_t seed)
    : zones_(std::move(zones)), archs_(archs), factions_(factions), classes_(classes), encounters_(std::move(encounters)),
      live_(zones_.size()), rng_(seed) {}

bool Population::near(const PopZone& z, const DVec3& p) const {
    if (z.contains(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z))) return true;
    double reach = std::max({z.size[0], z.size[1], z.size[2]}) + wake_range;
    DVec3 d{p.x - z.pos[0], p.y - z.pos[1], p.z - z.pos[2]};
    return d.length() <= reach;
}

bool Population::blocked(const DVec3& p) const {
    for (auto& b : blocks_)
        if ((p - b.c).length() <= b.r) return true;
    return false;
}

void Population::block_sphere(const DVec3& centre, double radius, bool on) {
    std::erase_if(blocks_, [&](const Block& b) { return (b.c - centre).length() < 1 && std::abs(b.r - radius) < 1; });
    if (on) blocks_.push_back({centre, radius});
}

std::optional<DVec3> Population::spawn_point(const PopZone& z, const DVec3& player) {
    // A point inside the zone, out of sight of the player but not far off.
    double ext = std::max({z.size[0], z.size[1], z.size[2]});
    std::uniform_real_distribution<double> u(-ext, ext);
    for (int i = 0; i < 40; ++i) {
        DVec3 p{z.pos[0] + u(rng_), z.pos[1] + u(rng_), z.pos[2] + u(rng_)};
        if (!z.contains(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z))) continue;
        double d = (p - player).length();
        if (d < spawn_min || d > spawn_max || blocked(p)) continue;
        return p;
    }
    return std::nullopt;
}

bool Population::in_class(const NpcShipArch& a, std::string_view cls) const {
    std::vector<std::string> need;
    if (auto c = classes_.find(to_lower(cls)); c != classes_.end()) need = c->second;
    else need.push_back(std::string(cls));
    for (auto& t : need)
        if (std::none_of(a.npc_class.begin(), a.npc_class.end(), [&](const std::string& x) { return iequals(x, t); }))
            return false;
    return true;
}

int Population::class_count(const Live& l, std::string_view cls) const {
    for (auto& [c, n] : l.by_class)
        if (iequals(c, cls)) return n;
    return 0;
}

bool Population::fits_restrictions(const PopZone& z, const Live& l, const std::vector<const NpcShipArch*>& ships) const {
    for (auto& r : z.density_restrictions) {
        int extra = 0;
        for (auto* s : ships) extra += in_class(*s, r.ship_class);
        if (extra && class_count(l, r.ship_class) + extra > r.max) return false;
    }
    return true;
}

std::vector<PopSpawn> Population::update(float dt, const DVec3& player) {
    std::vector<PopSpawn> out;
    for (std::size_t zi = 0; zi < zones_.size(); ++zi) {
        auto& z = zones_[zi];
        auto& l = live_[zi];
        if (l.timer > 0) l.timer -= dt;
        if (!enabled_ || !z.populated() || l.timer > 0 || l.ships >= z.density || !near(z, player)) continue;
        l.timer = std::max(z.repop_time, 5.f);

        // Which encounter, faction and formation: each by its weight.
        auto pick = [&](auto& items, auto weight) -> int {
            double total = 0;
            for (auto& it : items) total += std::max(0.0, static_cast<double>(weight(it)));
            if (total <= 0) return -1;
            double r = std::uniform_real_distribution<double>(0, total)(rng_);
            for (std::size_t i = 0; i < items.size(); ++i) {
                r -= std::max(0.0, static_cast<double>(weight(items[i])));
                if (r <= 0) return static_cast<int>(i);
            }
            return static_cast<int>(items.size()) - 1;
        };
        int ei = pick(z.encounters, [](const PopZone::Encounter& e) { return e.chance; });
        if (ei < 0) continue;
        auto& enc = z.encounters[ei];
        const EncounterDef* def = encounters_(enc.name);
        if (!def || def->formations.empty()) continue;
        int fi = pick(enc.factions, [](const std::pair<std::string, float>& f) { return f.second; });
        if (fi < 0) continue;
        const std::string& faction = enc.factions[fi].first;
        int pi = pick(def->permutations, [](const std::pair<int, float>& p) { return p.second; });
        std::size_t form = pi >= 0 ? static_cast<std::size_t>(def->permutations[pi].first) : 0;
        if (form >= def->formations.size()) form = 0;
        auto& f = def->formations[form];

        PopSpawn s;
        s.zone = zi;
        s.faction = faction;
        s.behavior = f.behavior;
        s.formation = f.formation;
        bool ok = true;
        for (auto& slot : f.slots) {
            int count = std::uniform_int_distribution<int>(std::min(slot.min, slot.max), std::max(slot.min, slot.max))(rng_);
            if (count == 0) continue;
            auto choices = ships_for(archs_, factions_, classes_, faction, slot.ship_class,
                                     std::max(1, enc.difficulty + slot.level_offset));
            if (choices.empty()) {
                if (slot.min > 0) ok = false;
                continue;
            }
            for (int i = 0; i < count; ++i)
                s.ships.push_back(choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng_)]);
        }
        if (!ok || s.ships.empty()) continue;
        if (l.ships + static_cast<int>(s.ships.size()) > z.density + 2) continue;  // a full formation may go a little over
        if (!fits_restrictions(z, l, s.ships)) continue;
        auto at = spawn_point(z, player);
        if (!at) continue;
        s.pos = *at;
        l.ships += static_cast<int>(s.ships.size());
        for (auto& r : z.density_restrictions) {
            int extra = 0;
            for (auto* a : s.ships) extra += in_class(*a, r.ship_class);
            if (!extra) continue;
            auto it = std::find_if(l.by_class.begin(), l.by_class.end(), [&](auto& p) { return iequals(p.first, r.ship_class); });
            if (it == l.by_class.end()) l.by_class.emplace_back(r.ship_class, extra);
            else it->second += extra;
        }
        out.push_back(std::move(s));
    }
    return out;
}

void Population::ship_gone(std::size_t zone, const NpcShipArch* arch) {
    if (zone >= live_.size()) return;
    auto& l = live_[zone];
    l.ships = std::max(0, l.ships - 1);
    if (!arch) return;
    for (auto& [cls, n] : l.by_class)
        if (in_class(*arch, cls)) n = std::max(0, n - 1);
}

}  // namespace sterrenvaarder::game
