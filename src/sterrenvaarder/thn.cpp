// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/thn.hpp"

#include <algorithm>
#include <cstdlib>

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

ThnPath ThnPath::parse(std::string_view data) {
    ThnPath p;
    std::size_t comma = data.find(',');
    std::string head(data.substr(0, comma == std::string_view::npos ? data.size() : comma));
    p.closed = head.find("CLOSED") != std::string::npos || head.find("closed") != std::string::npos;
    // Then brace groups: three numbers are a point, four a rotation for the point before.
    std::size_t i = comma == std::string_view::npos ? data.size() : comma;
    while ((i = data.find('{', i)) != std::string_view::npos) {
        std::size_t end = data.find('}', i);
        if (end == std::string_view::npos) break;
        std::vector<float> nums;
        std::string_view body = data.substr(i + 1, end - i - 1);
        std::size_t k = 0;
        while (k < body.size()) {
            while (k < body.size() && (body[k] == ',' || body[k] == ' ')) ++k;
            std::size_t start = k;
            while (k < body.size() && body[k] != ',') ++k;
            if (k > start) nums.push_back(std::strtof(std::string(body.substr(start, k - start)).c_str(), nullptr));
        }
        if (nums.size() == 3) p.points.push_back({nums[0], nums[1], nums[2]});
        else if (nums.size() == 4) p.orients.push_back({nums[0], nums[1], nums[2], nums[3]});
        i = end + 1;
    }
    return p;
}

Vec3 ThnPath::at(float f) const {
    if (points.empty()) return {};
    if (points.size() == 1) return points[0];
    std::size_t n = points.size();
    std::size_t segs = closed ? n : n - 1;
    float x = std::clamp(f, 0.f, 1.f) * static_cast<float>(segs);
    std::size_t seg = std::min(static_cast<std::size_t>(x), segs - 1);
    float t = x - static_cast<float>(seg);
    auto pt = [&](long i) -> const Vec3& {
        long m = static_cast<long>(n);
        if (closed) return points[static_cast<std::size_t>(((i % m) + m) % m)];
        return points[static_cast<std::size_t>(std::clamp(i, 0L, m - 1))];
    };
    const Vec3 &p0 = pt(static_cast<long>(seg) - 1), &p1 = pt(static_cast<long>(seg)), &p2 = pt(static_cast<long>(seg) + 1),
               &p3 = pt(static_cast<long>(seg) + 2);
    float t2 = t * t, t3 = t2 * t;
    auto cr = [&](float a, float b, float c, float d) {
        return 0.5f * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
    };
    return {cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z)};
}

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
        if (!e.is_table()) continue;
        const std::string& kind = e[2].str();
        auto target_of = [](const LuaValue& who) { return who.is_table() ? who[1].str() : who.str(); };
        if (kind == "SET_CAMERA") {
            // Targets: the monitor, then the camera.
            const LuaValue& who = e[3];
            ThnCameraCut c;
            c.time = static_cast<float>(e[1].num());
            c.camera = who.is_table() && who.size() >= 2 ? who[2].str() : target_of(who);
            if (!c.camera.empty()) out.cuts.push_back(std::move(c));
            continue;
        }
        if (kind == "START_SPATIAL_PROP_ANIM") {
            const LuaValue& p = e[4];
            const LuaValue& sp = p["spatialprops"];
            ThnMove m;
            m.time = static_cast<float>(e[1].num());
            m.duration = static_cast<float>(p["duration"].num());
            m.target = target_of(e[3]);
            if (sp["pos"].size() >= 3) {
                m.has_pos = true;
                m.pos = vec3(sp["pos"]);
            }
            if (const LuaValue& q = sp["q_orient"]; q.size() >= 4) {
                m.has_orient = true;
                for (int k = 0; k < 4; ++k) m.q[k] = static_cast<float>(q[k + 1].num());
            }
            if (!m.target.empty() && (m.has_pos || m.has_orient)) out.moves.push_back(std::move(m));
            continue;
        }
        if (kind == "START_PATH_ANIMATION") {
            const LuaValue& who = e[3];
            const LuaValue& p = e[4];
            ThnPathAnim a;
            a.time = static_cast<float>(e[1].num());
            a.duration = static_cast<float>(p["duration"].num());
            a.target = target_of(who);
            a.path = who.is_table() && who.size() >= 2 ? who[2].str() : std::string();
            a.start = static_cast<float>(p["start_percent"].num(0));
            a.stop = static_cast<float>(p["stop_percent"].num(1));
            a.offset = vec3(p["offset"]);
            if (!a.target.empty() && !a.path.empty()) out.path_anims.push_back(std::move(a));
            continue;
        }
        if (kind == "START_CAMERA_PROP_ANIM") {
            const LuaValue& p = e[4];
            ThnCameraProp c;
            c.time = static_cast<float>(e[1].num());
            c.duration = static_cast<float>(p["duration"].num());
            c.target = target_of(e[3]);
            c.fovh = static_cast<float>(p["cameraprops"]["fovh"].num());
            if (!c.target.empty()) out.camera_props.push_back(std::move(c));
            continue;
        }
        if (kind == "START_SOUND") {
            const LuaValue& p = e[4];
            ThnSound so;
            so.time = static_cast<float>(e[1].num());
            so.target = target_of(e[3]);
            so.start_time = static_cast<float>(p["start_time"].num());
            so.duration = static_cast<float>(p["duration"].num());
            so.loop = p["flags"].str().find("LOOP") != std::string::npos || static_cast<int>(p["flags"].num()) & 2;
            if (!so.target.empty()) out.sounds.push_back(std::move(so));
            continue;
        }
        if (kind != "START_MOTION") continue;
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
        if (t.type == "MOTION_PATH")
            if (const LuaValue& pp = e["pathprops"]; pp.is_table() && !pp["path_data"].str().empty())
                out.paths[to_lower(t.name)] = ThnPath::parse(pp["path_data"].str());
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
        if (t.type == "SOUND") {
            const LuaValue& a = e["audioprops"];
            t.attenuation = static_cast<float>(a["attenuation"].num());
            t.dmin = static_cast<float>(a["dmin"].num());
            t.dmax = static_cast<float>(a["dmax"].num());
            t.pan = static_cast<float>(a["pan"].num());
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
            } else if (iequals(rs.name, "Room_Sound")) {
                r.music = str(rs, "music");
                r.ambient = str(rs, "ambient");
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
