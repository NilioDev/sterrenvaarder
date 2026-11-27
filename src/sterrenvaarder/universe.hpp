// SPDX-License-Identifier: MPL-2.0
#pragma once
// Universe and star system data: universe.ini, system INIs, solar and star archetypes.
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {


struct SolarArchetype {
    std::string nickname;
    std::string type;  // PLANET, SUN, JUMP_GATE, DOCKING_RING, STATION, ...
    std::string model;  // DA_archetype (game path)
    std::vector<std::string> material_libraries;
    std::vector<float> lod_ranges;  // LODranges: switch distances, last one = draw distance
    float solar_radius = 0;
};

struct StarGlow {
    std::string nickname, shape;
    float scale = 1;
    Color3 inner{1, 1, 1}, outer{1, 1, 1};
};

struct StarArchetype {
    std::string nickname, glow, center;
    float radius = 0;
};

struct SystemObject {
    std::string nickname, archetype, star;
    Vec3 pos;
    Vec3 rotate_deg;  // Euler angles in degrees, as in the INI
    Vec3 spin;
    float atmosphere_range = 0;
    int ids_name = 0;                  // string id (resources); 0 = none
    std::string base, dock_with;       // dockable base (stations, planets)
    std::string reputation;            // owning faction group (li_p_grp, ...)
    std::string next_ring, prev_ring;  // trade lane chain
    // Jump gates and holes: target system, object there, tunnel effect.
    std::string goto_system, goto_object, goto_tunnel;
    bool is_tradelane() const { return !next_ring.empty() || !prev_ring.empty(); }
    bool is_jump() const { return !goto_system.empty(); }
    bool dockable() const { return !dock_with.empty() || !base.empty(); }
};

struct SystemLight {
    std::string nickname;
    Vec3 pos;
    Color3 color{1, 1, 1};
    float range = 0;
    Vec3 atten{1, 0, 0};  // constant, linear, quadratic
    bool directional = false;
};

struct StarSystem {
    std::string nickname;
    Color3 ambient;
    Color3 space_color;
    std::string basic_stars, complex_stars, nebulae;  // background models
    std::vector<SystemObject> objects;
    std::vector<SystemLight> lights;
};

class Universe {
public:
    explicit Universe(const Vfs& vfs);

    const std::vector<std::string>& system_names() const { return system_order_; }
    std::optional<StarSystem> load_system(std::string_view nickname) const;

    const SolarArchetype* solar(std::string_view nickname) const;
    const StarArchetype* star(std::string_view nickname) const;
    const StarGlow* star_glow(std::string_view nickname) const;
    const std::string& star_texture_library() const { return star_txm_; }

private:
    const Vfs& vfs_;
    std::vector<std::string> system_order_;
    std::unordered_map<std::string, std::string> system_files_;
    std::unordered_map<std::string, SolarArchetype> solars_;
    std::unordered_map<std::string, StarArchetype> stars_;
    std::unordered_map<std::string, StarGlow> glows_;
    std::string star_txm_;
};

Color3 ini_color(const IniEntry* e, Color3 fallback);  // "r, g, b" in 0..255
Vec3 ini_vec3(const IniEntry* e, Vec3 fallback = {});

}  // namespace sterrenvaarder
