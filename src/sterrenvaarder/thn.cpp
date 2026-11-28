// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/thn.hpp"

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

namespace {

Vec3 vec3(const LuaValue& v, Vec3 fallback = {}) {
    if (v.size() < 3) return fallback;
    return {static_cast<float>(v[1].num()), static_cast<float>(v[2].num()), static_cast<float>(v[3].num())};
}

Color3 color(const LuaValue& v, Color3 fallback, float scale) {
    if (v.size() < 3) return fallback;
    return {static_cast<float>(v[1].num()) * scale, static_cast<float>(v[2].num()) * scale, static_cast<float>(v[3].num()) * scale};
}

std::string str(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

}  // namespace

const ThnEntity* ThnScript::entity(std::string_view name) const {
    for (auto& e : entities)
        if (iequals(e.name, name)) return &e;
    return nullptr;
}

ThnScript ThnScript::parse(Bytes data) {
    LuaChunk chunk = LuaChunk::parse(data);
    ThnScript out;
    out.duration = chunk.global("duration").num();
    out.events = chunk.global("events");
    for (std::size_t i = 1; i <= out.events.size(); ++i) {
        const LuaValue& e = out.events[i];
        if (!e.is_table() || e[2].str() != "START_MOTION") continue;
        const LuaValue& who = e[3];
        const LuaValue& p = e[4];
        ThnMotion m;
        m.time = static_cast<float>(e[1].num());
        m.target = who.is_table() ? who[1].str() : who.str();
        m.animation = p["animation"].str();
        m.start_time = static_cast<float>(p["start_time"].num());
        m.duration = static_cast<float>(p["duration"].num());
        m.time_scale = static_cast<float>(p["time_scale"].num(1));
        m.loop = p["event_flags"].str().find("LOOP") != std::string::npos ||
                 static_cast<int>(p["event_flags"].num()) & 2;
        if (!m.animation.empty()) out.motions.push_back(std::move(m));
    }
    const LuaValue& ents = chunk.global("entities");
    for (std::size_t i = 1; i <= ents.size(); ++i) {
        const LuaValue& e = ents[i];
        if (!e.is_table()) continue;
        ThnEntity t;
        t.name = e["entity_name"].str();
        t.type = e["type"].str();
        t.template_name = e["template_name"].str();
        t.flags = e["flags"].str();
        t.category = e["userprops"]["category"].str();
        t.light_group = static_cast<int>(e["lt_grp"].num());
        t.sort_group = static_cast<int>(e["srt_grp"].num());
        const LuaValue& sp = e["spatialprops"];
        t.pos = vec3(sp["pos"]);
        const LuaValue& o = sp["orient"];
        if (o.size() == 3)
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) t.orient[r * 3 + c] = static_cast<float>(o[r + 1][c + 1].num(r == c ? 1 : 0));
        if (const LuaValue& cp = e["cameraprops"]; cp.is_table()) {
            t.fovh = static_cast<float>(cp["fovh"].num());
            t.hvaspect = static_cast<float>(cp["hvaspect"].num(4.0 / 3.0));
            t.near_plane = static_cast<float>(cp["nearplane"].num());
            t.far_plane = static_cast<float>(cp["farplane"].num());
        }
        if (const LuaValue& lp = e["lightprops"]; lp.is_table()) {
            t.light_on = lp["on"].str() != "N";
            t.light_type = lp["type"].str();
            t.diffuse = color(lp["diffuse"], {1, 1, 1}, 1.f);
            t.light_ambient = color(lp["ambient"], {}, 1.f);
            t.range = static_cast<float>(lp["range"].num());
            t.atten = vec3(lp["atten"], {1, 0, 0});
            t.direction = vec3(lp["direction"], {0, 0, 1});
        }
        if (t.type == "SCENE") t.ambient = color(e["ambient"], {}, 1.f / 255.f);
        out.entities.push_back(std::move(t));
    }
    return out;
}

PetalDb::PetalDb(const Vfs& vfs, std::string_view path) {
    if (!vfs.exists(path)) return;
    for (auto& sec : parse_ini(vfs.read(path)))
        for (auto& e : sec.entries)
            if (e.values.size() >= 2) models_[to_lower(e.values[0].as_string())] = e.values[1].as_string();
}

const std::string* PetalDb::model(std::string_view name) const {
    auto it = models_.find(to_lower(name));
    return it == models_.end() ? nullptr : &it->second;
}

const RoomInfo* BaseRooms::room(std::string_view nickname) const {
    for (auto& r : rooms)
        if (iequals(r.nickname, nickname)) return &r;
    return nullptr;
}

std::optional<BaseRooms> BaseRooms::load(const Vfs& vfs, std::string_view base_file) {
    if (base_file.empty() || !vfs.exists(base_file)) return std::nullopt;
    BaseRooms b;
    for (auto& sec : parse_ini(vfs.read(base_file))) {
        if (iequals(sec.name, "BaseInfo")) b.start_room = str(sec, "start_room");
        if (!iequals(sec.name, "Room")) continue;
        RoomInfo r;
        r.nickname = str(sec, "nickname");
        std::string file = str(sec, "file");
        if (r.nickname.empty() || file.empty() || !vfs.exists(file)) continue;
        for (auto& rs : parse_ini(vfs.read(file))) {
            if (iequals(rs.name, "Room_Info")) {
                r.set_script = str(rs, "set_script");
                for (auto* s : rs.find_all("scene"))
                    if (s->values.size() >= 3) r.scenes.push_back(s->values[2].as_string());
            } else if (iequals(rs.name, "Camera")) {
                r.camera = str(rs, "name");
            } else if (iequals(rs.name, "ForSaleShipPlacement")) {
                if (auto n = str(rs, "name"); !n.empty()) r.ship_placements.push_back(n);
            } else if (iequals(rs.name, "Hotspot")) {
                r.hotspots.push_back({str(rs, "name"), str(rs, "behavior"), str(rs, "room_switch")});
            }
        }
        b.rooms.push_back(std::move(r));
    }
    if (b.rooms.empty()) return std::nullopt;
    return b;
}

std::string base_file_of(const Vfs& vfs, std::string_view base) {
    if (!vfs.exists("universe/universe.ini")) return {};
    for (auto& sec : parse_ini(vfs.read("universe/universe.ini")))
        if (iequals(sec.name, "Base") && iequals(str(sec, "nickname"), base)) return str(sec, "file");
    return {};
}

}  // namespace sterrenvaarder
