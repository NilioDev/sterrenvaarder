// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/population.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

std::string str(const IniEntry& e, std::size_t i) { return e.at(i) ? e.at(i)->as_string() : std::string(); }
float num(const IniEntry& e, std::size_t i, float fallback = 0) { return e.at(i) ? e.at(i)->as_float() : fallback; }
bool has_tag(const std::vector<std::string>& tags, std::string_view t) {
    return std::any_of(tags.begin(), tags.end(), [&](const std::string& x) { return iequals(x, t); });
}

}  // namespace

bool PopZone::contains(float x, float y, float z) const {
    // Into the zone's frame: undo the position, then the rotation (Rz * Ry * Rx, so its
    // transpose applies Rx^T, Ry^T, Rz^T in turn: first z, then y, then x).
    float p[3] = {x - pos[0], y - pos[1], z - pos[2]};
    constexpr float k = 3.14159265358979f / 180.f;
    auto rot = [&](int a, int b, float deg) {  // rotate the (a, b) plane by -deg
        float c = std::cos(deg * k), s = std::sin(deg * k);
        float u = p[a] * c + p[b] * s, v = -p[a] * s + p[b] * c;
        p[a] = u;
        p[b] = v;
    };
    rot(0, 1, rotate[2]);  // z
    rot(2, 0, rotate[1]);  // y
    rot(1, 2, rotate[0]);  // x
    auto sq = [](float v) { return v * v; };
    if (iequals(shape, "SPHERE")) return sq(p[0]) + sq(p[1]) + sq(p[2]) <= sq(size[0]);
    if (iequals(shape, "ELLIPSOID")) {
        float a = std::max(size[0], 1.f), b = std::max(size[1], 1.f), c = std::max(size[2], 1.f);
        return sq(p[0] / a) + sq(p[1] / b) + sq(p[2] / c) <= 1.f;
    }
    if (iequals(shape, "BOX"))
        return std::abs(p[0]) <= size[0] * 0.5f && std::abs(p[1]) <= size[1] * 0.5f && std::abs(p[2]) <= size[2] * 0.5f;
    if (iequals(shape, "CYLINDER") || iequals(shape, "RING"))
        return sq(p[0]) + sq(p[2]) <= sq(size[0]) && std::abs(p[1]) <= size[1] * 0.5f;
    return false;
}

std::vector<PopZone> parse_pop_zones(const IniFile& ini) {
    std::vector<PopZone> out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "Zone")) continue;
        PopZone z;
        PopZone::Encounter* enc = nullptr;
        for (auto& e : sec.entries) {
            const auto& k = e.key;
            if (iequals(k, "nickname")) z.nickname = str(e, 0);
            else if (iequals(k, "pos")) z.pos = {num(e, 0), num(e, 1), num(e, 2)};
            else if (iequals(k, "rotate")) z.rotate = {num(e, 0), num(e, 1), num(e, 2)};
            else if (iequals(k, "shape")) z.shape = str(e, 0);
            else if (iequals(k, "size")) {
                float a = num(e, 0);
                z.size = {a, num(e, 1, a), num(e, 2, a)};
            } else if (iequals(k, "toughness")) z.toughness = static_cast<int>(num(e, 0));
            else if (iequals(k, "density")) z.density = static_cast<int>(num(e, 0));
            else if (iequals(k, "repop_time")) z.repop_time = num(e, 0);
            else if (iequals(k, "max_battle_size")) z.max_battle_size = static_cast<int>(num(e, 0));
            else if (iequals(k, "relief_time")) z.relief_time = num(e, 0);
            else if (iequals(k, "density_restriction")) z.density_restrictions.push_back({static_cast<int>(num(e, 0)), str(e, 1)});
            else if (iequals(k, "encounter")) {
                z.encounters.push_back({str(e, 0), static_cast<int>(num(e, 1)), num(e, 2), {}});
                enc = &z.encounters.back();
            } else if (iequals(k, "faction") && enc) {
                enc->factions.emplace_back(str(e, 0), num(e, 1, 1));
            }
        }
        out.push_back(std::move(z));
    }
    return out;
}

EncounterDef parse_encounter(const IniFile& ini) {
    EncounterDef d;
    for (auto& sec : ini) {
        if (iequals(sec.name, "EncounterFormation")) {
            EncounterDef::Formation f;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "ship_by_class")) {
                    f.slots.push_back({static_cast<int>(num(e, 0)), static_cast<int>(num(e, 1)), str(e, 2),
                                       static_cast<int>(num(e, 3)), {}});
                } else if (iequals(e.key, "pilot_job") && !f.slots.empty()) {
                    f.slots.back().pilot_job = str(e, 0);
                } else if (iequals(e.key, "formation_by_class")) {
                    f.formation = str(e, 0);
                } else if (iequals(e.key, "behavior")) {
                    f.behavior = str(e, 0);
                } else if (iequals(e.key, "arrival")) {
                    for (std::size_t i = 0; i < e.values.size(); ++i) f.arrival.push_back(str(e, i));
                }
            }
            d.formations.push_back(std::move(f));
        } else if (iequals(sec.name, "Creation")) {
            for (auto* e : sec.find_all("permutation"))
                d.permutations.emplace_back(static_cast<int>(num(*e, 0)), num(*e, 1, 1));
        }
    }
    return d;
}

ShipClasses parse_ship_classes(const IniFile& ini) {
    ShipClasses out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "ShipClass")) continue;
        std::string nick;
        std::vector<std::string> members;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) nick = to_lower(str(e, 0));
            else if (iequals(e.key, "member"))
                for (std::size_t i = 0; i < e.values.size(); ++i) members.push_back(str(e, i));
        }
        if (!nick.empty()) out[nick] = std::move(members);
    }
    return out;
}

FactionShips parse_faction_ships(const IniFile& ini) {
    FactionShips out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "FactionProps")) continue;
        std::string group;
        std::vector<std::string> ships;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "affiliation")) group = to_lower(str(e, 0));
            else if (iequals(e.key, "npc_ship")) ships.push_back(str(e, 0));
        }
        if (!group.empty()) {
            auto& list = out[group];
            list.insert(list.end(), ships.begin(), ships.end());
        }
    }
    return out;
}

std::vector<const NpcShipArch*> ships_for(const std::vector<NpcShipArch>& archs, const FactionShips& factions,
                                          const ShipClasses& classes, std::string_view faction,
                                          std::string_view ship_class, int level) {
    std::vector<const NpcShipArch*> out;
    auto f = factions.find(to_lower(faction));
    if (f == factions.end()) return out;
    std::vector<std::string> need;
    if (auto c = classes.find(to_lower(ship_class)); c != classes.end()) need = c->second;
    else need.push_back(std::string(ship_class));
    std::string lvl = "d" + std::to_string(level);
    for (auto& nick : f->second)
        for (auto& a : archs) {
            if (!iequals(a.nickname, nick)) continue;
            bool ok = std::all_of(need.begin(), need.end(), [&](const std::string& t) { return has_tag(a.npc_class, t); });
            // Ships list the levels they fly at among their classes (d1, d2, ...).
            if (ok && level > 0) ok = has_tag(a.npc_class, lvl);
            if (ok) out.push_back(&a);
            break;
        }
    return out;
}

}  // namespace sterrenvaarder
