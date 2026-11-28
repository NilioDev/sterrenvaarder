// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/universe.hpp"

namespace sterrenvaarder {

namespace {

std::string str(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

float num(const IniSection& s, std::string_view key, float fallback = 0) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_float() : fallback;
}

}  // namespace

Color3 ini_color(const IniEntry* e, Color3 fallback) {
    if (!e || e->values.size() < 3) return fallback;
    return {e->values[0].as_float() / 255.f, e->values[1].as_float() / 255.f, e->values[2].as_float() / 255.f};
}

Vec3 ini_vec3(const IniEntry* e, Vec3 fallback) {
    if (!e || e->values.size() < 3) return fallback;
    return {e->values[0].as_float(), e->values[1].as_float(), e->values[2].as_float()};
}

Universe::Universe(const Vfs& vfs) : vfs_(vfs) {
    for (auto& sec : parse_ini(vfs.read("universe/universe.ini"))) {
        if (!iequals(sec.name, "system")) continue;
        auto nick = to_lower(str(sec, "nickname"));
        auto file = str(sec, "file");
        if (nick.empty() || file.empty()) continue;
        system_order_.push_back(nick);
        system_files_[nick] = "universe/" + file;
    }
    for (auto& sec : parse_ini(vfs.read("solar/solararch.ini"))) {
        if (!iequals(sec.name, "Solar")) continue;
        SolarArchetype a;
        a.nickname = str(sec, "nickname");
        a.type = str(sec, "type");
        a.model = str(sec, "DA_archetype");
        for (auto* m : sec.find_all("material_library"))
            if (!m->values.empty()) a.material_libraries.push_back(m->values[0].as_string());
        if (auto l = sec.find("LODranges"))
            for (auto& v : l->values) a.lod_ranges.push_back(v.as_float());
        a.solar_radius = num(sec, "solar_radius");
        if (auto d = sec.find("destructible"); d && !d->values.empty()) a.destructible = d->values[0].as_bool();
        a.hit_pts = num(sec, "hit_pts");
        a.loadout = str(sec, "loadout");
        a.explosion_arch = str(sec, "explosion_arch");
        solars_[to_lower(a.nickname)] = std::move(a);
    }
    if (vfs.exists("solar/stararch.ini")) {
        for (auto& sec : parse_ini(vfs.read("solar/stararch.ini"))) {
            if (iequals(sec.name, "Texture")) {
                star_txm_ = str(sec, "file");
            } else if (iequals(sec.name, "star")) {
                StarArchetype s;
                s.nickname = str(sec, "nickname");
                s.glow = str(sec, "star_glow");
                s.center = str(sec, "star_center");
                s.radius = num(sec, "radius");
                stars_[to_lower(s.nickname)] = std::move(s);
            } else if (iequals(sec.name, "star_glow")) {
                StarGlow g;
                g.nickname = str(sec, "nickname");
                g.shape = str(sec, "shape");
                g.scale = num(sec, "scale", 1);
                auto c = [&](std::string_view k) {
                    auto e = sec.find(k);
                    if (!e || e->values.size() < 3) return Color3{1, 1, 1};
                    return Color3{e->values[0].as_float(), e->values[1].as_float(), e->values[2].as_float()};
                };
                g.inner = c("inner_color");
                g.outer = c("outer_color");
                glows_[to_lower(g.nickname)] = std::move(g);
            }
        }
    }
}

const SolarArchetype* Universe::solar(std::string_view n) const {
    auto it = solars_.find(to_lower(n));
    return it == solars_.end() ? nullptr : &it->second;
}
const StarArchetype* Universe::star(std::string_view n) const {
    auto it = stars_.find(to_lower(n));
    return it == stars_.end() ? nullptr : &it->second;
}
const StarGlow* Universe::star_glow(std::string_view n) const {
    auto it = glows_.find(to_lower(n));
    return it == glows_.end() ? nullptr : &it->second;
}

std::optional<StarSystem> Universe::load_system(std::string_view nickname) const {
    auto it = system_files_.find(to_lower(nickname));
    if (it == system_files_.end()) return std::nullopt;
    StarSystem sys;
    sys.nickname = std::string(nickname);
    for (auto& sec : parse_ini(vfs_.read(it->second))) {
        if (iequals(sec.name, "Ambient")) {
            sys.ambient = ini_color(sec.find("color"), {});
        } else if (iequals(sec.name, "SystemInfo")) {
            sys.space_color = ini_color(sec.find("space_color"), {});
        } else if (iequals(sec.name, "Background")) {
            sys.basic_stars = str(sec, "basic_stars");
            sys.complex_stars = str(sec, "complex_stars");
            sys.nebulae = str(sec, "nebulae");
        } else if (iequals(sec.name, "LightSource")) {
            SystemLight l;
            l.nickname = str(sec, "nickname");
            l.pos = ini_vec3(sec.find("pos"));
            l.color = ini_color(sec.find("color"), {1, 1, 1});
            l.range = num(sec, "range");
            l.directional = iequals(str(sec, "type"), "DIRECTIONAL");
            sys.lights.push_back(std::move(l));
        } else if (iequals(sec.name, "Object")) {
            SystemObject o;
            o.nickname = str(sec, "nickname");
            o.archetype = str(sec, "archetype");
            o.star = str(sec, "star");
            o.pos = ini_vec3(sec.find("pos"));
            o.rotate_deg = ini_vec3(sec.find("rotate"));
            o.spin = ini_vec3(sec.find("spin"));
            o.atmosphere_range = num(sec, "atmosphere_range");
            o.ids_name = static_cast<int>(num(sec, "ids_name"));
            o.base = str(sec, "base");
            o.dock_with = str(sec, "dock_with");
            o.voice = str(sec, "voice");
            o.reputation = str(sec, "reputation");
            o.next_ring = str(sec, "next_ring");
            o.prev_ring = str(sec, "prev_ring");
            if (auto g = sec.find("goto"); g && g->values.size() >= 2) {
                o.goto_system = g->values[0].as_string();
                o.goto_object = g->values[1].as_string();
                if (g->values.size() > 2) o.goto_tunnel = g->values[2].as_string();
            }
            sys.objects.push_back(std::move(o));
        }
    }
    return sys;
}

}  // namespace sterrenvaarder
