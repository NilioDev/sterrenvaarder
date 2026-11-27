// SPDX-License-Identifier: MPL-2.0
// Sterrenvaarder: fly your own ship through a star system of your own game data.
//
//   sterrenvaarder --data <Freelancer DATA dir> [--mod <dir>]... [--system Li01]
//                  [--ship li_elite] [--width 2560 --height 1080] [--scale 0.77] [--msaa 4]
//                  [--lod-scale 1] [--hud-aspect 1.778] [--hud-config hud.ini] [--fov 70] [--free]
//                  [--screenshot out.png [--sim 4 --throttle 1 --cursor 0.3,-0.1]]
//                  [--camera x,y,z --look x,y,z]   (spawn point / free camera)
//                  [--new-game] [--load save.ini] [--save-file save.ini] [--exe <EXE dir>]
//                  [--buy good,count]... [--tab 0-2]  (screenshot runs: trade after docking)
//                  [--credits n] [--rank n] [--rep group,value]  (testing)
//
// Flight controls: mouse = steer (Space toggles mouse flight), W/S = throttle,
//   Shift+W = cruise, A/D = strafe, Q/E = roll, Z = stop engines, T = next target,
//   F3 = autopilot to the target (dock, trade lane, jump gate) / launch when docked,
//   F5 = quick save, F9 = quick load, F11 = fullscreen, Esc = quit.
// Docked: Up/Down = select, Right = buy, Left = sell (Shift = as many as possible).
// Free camera (--free): right mouse = look, WASD/QE = move, Shift = boost, wheel = speed.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/flight.hpp"
#include "game/hud.hpp"
#include "game/navigation.hpp"
#include "game/particles.hpp"
#include "game/trade.hpp"
#include "render/renderer.hpp"
#include "sterrenvaarder/economy.hpp"
#include "sterrenvaarder/effects.hpp"
#include "sterrenvaarder/factions.hpp"
#include "sterrenvaarder/pilots.hpp"
#include "sterrenvaarder/thn.hpp"
#include "sterrenvaarder/resources.hpp"
#include "sterrenvaarder/ships.hpp"
#include "sterrenvaarder/png.hpp"

using namespace sterrenvaarder;
using namespace sterrenvaarder::render;
using sterrenvaarder::game::Quat;

namespace {

DVec3 parse_vec(const std::string& s) {
    DVec3 v;
    std::sscanf(s.c_str(), "%lf,%lf,%lf", &v.x, &v.y, &v.z);
    return v;
}

struct Args {
    std::vector<std::string> mounts;
    std::string system = "Li01";
    int width = 1920, height = 1080;
    RenderSettings settings;
    std::string screenshot;
    bool has_camera = false, has_look = false;
    DVec3 camera, look;
    bool free = false;
    std::string ship = "li_elite";
    float hud_aspect = -1;  // < 0: from the HUD config
    std::string hud_config;
    float fov = 0;
    int npcs = 0;
    std::string npc_ship = "pi_fighter";
    bool cruise = false;    // screenshot runs: hold cruise (it charges first)
    double npc_die = -1;    // screenshot runs: test NPCs are destroyed at this time (s)
    std::string npc_pilot;  // empty: the pilot npcships.ini gives this ship, else a default
    bool fire = false;
    std::string dock;  // screenshot runs: autopilot to this object first
    bool ship_given = false;
    bool new_game = false;
    std::string load, save_file, exe;
    std::vector<std::pair<std::string, float>> reps;  // testing: --rep group,value
    std::vector<std::pair<std::string, int>> buys;  // screenshot runs: trades once docked
    int tab = 0;                                      // screenshot runs: base screen tab (0-2)
    long long credits = -1;                           // testing: override credits
    int rank = -1;                                    // testing: override level
    double sim = 0;
    float throttle = 1;
    float cursor_x = 0, cursor_y = 0;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + k);
            return argv[++i];
        };
        if (k == "--data" || k == "--mod") a.mounts.push_back(next());
        else if (k == "--system") a.system = next();
        else if (k == "--width") a.width = std::stoi(next());
        else if (k == "--height") a.height = std::stoi(next());
        else if (k == "--scale") a.settings.render_scale = std::stof(next());
        else if (k == "--sharpness") a.settings.sharpness = std::stof(next());
        else if (k == "--msaa") a.settings.msaa = std::stoi(next());
        else if (k == "--exposure") a.settings.exposure = std::stof(next());
        else if (k == "--lod-scale") a.settings.lod_scale = std::stof(next());
        else if (k == "--normal-strength") a.settings.normal_strength = std::stof(next());
        else if (k == "--screenshot") a.screenshot = next();
        else if (k == "--camera") { a.camera = parse_vec(next()); a.has_camera = true; }
        else if (k == "--look") { a.look = parse_vec(next()); a.has_look = true; }
        else if (k == "--free") a.free = true;
        else if (k == "--ship") { a.ship = next(); a.ship_given = true; }
        else if (k == "--new-game") a.new_game = true;
        else if (k == "--load") a.load = next();
        else if (k == "--save-file") a.save_file = next();
        else if (k == "--exe") a.exe = next();
        else if (k == "--tab") a.tab = std::stoi(next());
        else if (k == "--credits") a.credits = std::stoll(next());
        else if (k == "--rank") a.rank = std::stoi(next());
        else if (k == "--rep") {
            auto v = next();
            auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--rep wants group,value");
            a.reps.emplace_back(to_lower(v.substr(0, comma)), std::stof(v.substr(comma + 1)));
        }
        else if (k == "--buy") {
            auto v = next();
            auto comma = v.find(',');
            a.buys.emplace_back(v.substr(0, comma), comma == std::string::npos ? 1 : std::stoi(v.substr(comma + 1)));
        }
        else if (k == "--hud-aspect") a.hud_aspect = std::stof(next());
        else if (k == "--hud-config") a.hud_config = next();
        else if (k == "--fov") a.fov = std::stof(next());
        else if (k == "--npcs") a.npcs = std::stoi(next());
        else if (k == "--npc-ship") a.npc_ship = next();
        else if (k == "--npc-pilot") a.npc_pilot = next();
        else if (k == "--npc-die") a.npc_die = std::stod(next());
        else if (k == "--fire") a.fire = true;
        else if (k == "--dock") a.dock = next();
        else if (k == "--sim") a.sim = std::stod(next());
        else if (k == "--throttle") a.throttle = std::stof(next());
        else if (k == "--cruise") a.cruise = true;
        else if (k == "--cursor") std::sscanf(next().c_str(), "%f,%f", &a.cursor_x, &a.cursor_y);
        else throw std::runtime_error("unknown option " + k);
    }
    if (a.mounts.empty()) throw std::runtime_error("usage: sterrenvaarder --data <Freelancer DATA dir> [--system Li01]");
    return a;
}

void place_default_camera(Camera& cam, const StarSystem& sys) {
    // Start near the first planet or station, looking at it.
    for (auto& o : sys.objects) {
        if (o.archetype.find("planet") == std::string::npos) continue;
        DVec3 p{o.pos.x, o.pos.y, o.pos.z};
        cam.pos = p + DVec3{0, 2500, 12000};
        cam.look_at(p);
        return;
    }
}

// World-space direction through a screen point (where the guns converge).
Vec3 aim_through(const Camera& cam, float px, float py, float w, float h) {
    float t = std::tan(cam.fov_y * 0.5f);
    float nx = (px / w * 2.f - 1.f) * t * (w / h), ny = (1.f - py / h * 2.f) * t;
    Vec3 f = cam.forward(), r = cam.right(), u = cam.up();
    return render::normalize(Vec3{f.x + r.x * nx + u.x * ny, f.y + r.y * nx + u.y * ny, f.z + r.z * nx + u.z * ny});
}

// Guns for a ship from its loadout, at the hardpoints of its model.
std::vector<game::Gun> guns_for(const ShipDatabase& db, const Vfs& vfs, const ShipArchetype& arch, const Loadout* lo) {
    std::vector<game::Gun> guns;
    if (!lo) return guns;
    std::vector<Hardpoint> hps;
    try {
        hps = Model::load(vfs.load_with_fallback(arch.model, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); }))
                  .hardpoints_in_model_space();
    } catch (const std::exception&) {
    }
    for (auto& e : lo->equip) {
        const GunArchetype* g = db.gun(e.item);
        if (!g) continue;
        game::Gun gun;
        gun.arch = g;
        gun.munition = db.munition(g->projectile);
        for (auto& h : hps)
            if (iequals(h.name, e.hardpoint)) gun.offset = h.position;
        guns.push_back(gun);
    }
    return guns;
}

// An effect fixed to a point of a ship (engine flames at the HpEngine hardpoints).
struct AttachedFx {
    Vec3 offset;  // model space
    game::EffectInstance fx;
};

// The shield bubble: centre and half-extents a little beyond the box around the ship's
// hardpoints (leaving out the external camera point, which sits far behind the ship).
struct ShieldShape { Vec3 center{}, radii{4, 4, 4}; };
ShieldShape shield_shape_for(const std::vector<Hardpoint>& hps) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (auto& h : hps) {
        if (to_lower(h.name).find("cockpitex") != std::string::npos) continue;
        lo = {std::min(lo.x, h.position.x), std::min(lo.y, h.position.y), std::min(lo.z, h.position.z)};
        hi = {std::max(hi.x, h.position.x), std::max(hi.y, h.position.y), std::max(hi.z, h.position.z)};
    }
    ShieldShape s;
    if (lo.x > hi.x) return s;
    s.center = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    auto r = [](float a, float b) { return std::max(2.5f, (b - a) * 0.5f * 1.2f + 1.f); };
    s.radii = {r(lo.x, hi.x), r(lo.y, hi.y), r(lo.z, hi.z)};
    return s;
}

std::vector<Hardpoint> model_hardpoints(const Vfs& vfs, const ShipArchetype& arch) {
    try {
        return Model::load(vfs.load_with_fallback(arch.model, [](std::vector<std::uint8_t> b) { return UtfFile::parse(std::move(b)); }))
            .hardpoints_in_model_space();
    } catch (const std::exception&) {
        return {};
    }
}

// The engine's flame effect at each engine hardpoint of the ship's model.
std::vector<AttachedFx> engine_fx_for(const EffectDb& fxdb, const Vfs& vfs, const ShipArchetype& arch,
                                      const EngineArchetype* engine, std::uint32_t seed, std::vector<std::string>& textures) {
    std::vector<AttachedFx> out;
    if (!engine || engine->flame_effect.empty()) return out;
    ResolvedEffect fx = fxdb.resolve(engine->flame_effect);
    if (!fx) return out;
    for (auto& t : fx.vis->textures) textures.push_back(t);
    for (auto& h : model_hardpoints(vfs, arch))
        if (to_lower(h.name).starts_with("hpengine")) out.push_back({h.position, game::EffectInstance(fx, seed++)});
    return out;
}

// The engine's cruise trail (charge glow, cruise fire, contrail) at each engine hardpoint;
// the player's own ship uses trail_effect_player when the engine has one.
std::vector<AttachedFx> trail_fx_for(const EffectDb& fxdb, const Vfs& vfs, const ShipArchetype& arch,
                                     const EngineArchetype* engine, bool player, std::uint32_t seed,
                                     std::vector<std::string>& textures) {
    std::vector<AttachedFx> out;
    if (!engine) return out;
    const std::string& name = player && !engine->trail_effect_player.empty() ? engine->trail_effect_player : engine->trail_effect;
    if (name.empty()) return out;
    ResolvedEffect fx = fxdb.resolve(name);
    if (!fx) return out;
    for (auto& t : fx.vis->textures) textures.push_back(t);
    for (auto& h : model_hardpoints(vfs, arch))
        if (to_lower(h.name).starts_with("hpengine")) out.push_back({h.position, game::EffectInstance(fx, seed++)});
    return out;
}

// Damage fuses: as the hull drops past each fuse's hit points, its effects (sparks, fire,
// smoke) start at one of the hardpoints it names and stay with the ship.
struct DamageFx {
    std::vector<Hardpoint> hardpoints;
    Vec3 shield_center{}, shield_radii{4, 4, 4};  // shield bubble in the ship's frame
    std::vector<bool> lit;
    std::vector<AttachedFx> fx;
};

// A computer-controlled ship: same flight model and weapons as the player.
struct NpcShip {
    std::string name;
    const ShipArchetype* arch = nullptr;
    game::FlightModel model{game::FlightParams{}};
    game::ShipState ship;
    game::FlightInput input;
    game::Combatant body;
    std::vector<game::Gun> guns;
    Renderer::ModelHandle mesh;
    float burst = 0;  // > 0 firing, < 0 pausing (seconds left)
    std::optional<game::FighterAi> ai;  // from the original pilot data, when found
    std::vector<AttachedFx> engine_fx, trail_fx;
    DamageFx damage;
};

struct Explosion {
    DVec3 pos;
    float age = 0, size = 30;
};

// The original base room behind the base screen: the set, props and lights from the
// room's scene scripts, seen through the room's own camera.
struct RoomView {
    std::optional<BaseRooms> rooms;
    std::string base, current;
    struct Model { Renderer::ModelHandle mesh; DVec3 pos; Mat4 rot; };
    std::vector<Model> models;
    // Ship dealer: the pads ([ForSaleShipPlacement] markers) and the ships shown on them.
    struct Pad { std::string name; Vec3 pos; Mat3 orient; };
    std::vector<Pad> pads;  // in room-file order
    std::vector<Model> ships;
    std::vector<std::string> shown_ships;  // ship nicknames on the pads, for change detection
    RoomLighting lighting;
    Camera cam;
    bool active() const { return !models.empty(); }

    void clear() {
        rooms.reset();
        base.clear();
        current.clear();
        models.clear();
        pads.clear();
        ships.clear();
        shown_ships.clear();
    }

    void load_base(const Vfs& vfs, const std::string& b) {
        clear();
        base = b;
        try {
            rooms = BaseRooms::load(vfs, base_file_of(vfs, b));
        } catch (const std::exception& e) {
            SDL_Log("base rooms of %s: %s", b.c_str(), e.what());
        }
    }

    // Shows a room (by nickname); falls back to the base's start room.
    void show(const Vfs& vfs, const PetalDb& petal, Renderer& r, std::string_view wanted) {
        if (!rooms) return;
        const RoomInfo* info = rooms->room(wanted);
        if (!info) info = rooms->room(rooms->start_room);
        if (!info || iequals(info->nickname, current)) return;
        current = info->nickname;
        models.clear();
        pads.clear();
        ships.clear();
        shown_ships.clear();
        lighting = {};
        std::vector<Pad> markers;
        std::vector<std::string> scripts{info->set_script};
        scripts.insert(scripts.end(), info->scenes.begin(), info->scenes.end());
        bool have_cam = false;
        Color3 light_amb{0, 0, 0};  // lights add their ambient term to the scene's
        for (auto& path : scripts) {
            if (path.empty() || !vfs.exists(path)) continue;
            ThnScript s;
            try {
                s = ThnScript::parse(vfs.read(path));
            } catch (const std::exception& e) {
                SDL_Log("%s: %s", path.c_str(), e.what());
                continue;
            }
            for (auto& e : s.entities) {
                if (e.type == "SCENE" && (lighting.ambient.r + lighting.ambient.g + lighting.ambient.b) == 0) lighting.ambient = e.ambient;
                if (e.type == "LIGHT" && e.light_on) {
                    SystemLight l;
                    l.nickname = e.name;
                    l.pos = e.pos;
                    l.color = e.diffuse;
                    l.range = e.range > 0 ? e.range : 50.f;
                    l.atten = e.atten;
                    l.directional = e.light_type == "L_DIRECT";
                    light_amb.r += e.light_ambient.r;
                    light_amb.g += e.light_ambient.g;
                    light_amb.b += e.light_ambient.b;
                    lighting.lights.push_back(l);
                }
                if (e.type == "CAMERA" && !have_cam && iequals(e.name, info->camera)) {
                    have_cam = true;
                    cam = Camera{};
                    cam.pos = {e.pos.x, e.pos.y, e.pos.z};
                    cam.set_basis(e.forward(), e.up());
                    float aspect = e.hvaspect > 0 ? e.hvaspect : 4.f / 3.f;
                    cam.fov_y = 2.f * std::atan(std::tan(e.fovh * 3.14159265f / 360.f) / aspect);
                    cam.near_plane = e.near_plane > 0 ? e.near_plane : 0.2f;
                }
                if (e.type == "MARKER") markers.push_back({e.name, e.pos, e.orient});
                if ((e.type == "COMPOUND" || e.type == "PROP") && e.flags.find("HIDDEN") == std::string::npos) {
                    const std::string* file = petal.model(e.template_name);
                    if (!file) continue;
                    try {
                        auto h = r.load_model(*file, {});
                        if (h) models.push_back({h, {e.pos.x, e.pos.y, e.pos.z}, Mat4::from_rows3(e.orient)});
                    } catch (const std::exception& ex) {
                        SDL_Log("room model %s: %s", file->c_str(), ex.what());
                    }
                }
            }
        }
        lighting.ambient.r += light_amb.r;
        lighting.ambient.g += light_amb.g;
        lighting.ambient.b += light_amb.b;
        SDL_Log("room %s/%s: %zu models, %zu lights, camera %s", base.c_str(), current.c_str(), models.size(),
                lighting.lights.size(), have_cam ? info->camera.c_str() : "(none)");
        if (!have_cam) models.clear();  // nothing sensible to show
        for (auto& want : info->ship_placements)
            for (auto& m : markers)
                if (iequals(m.name, want)) { pads.push_back(m); break; }
        // The pad the room camera looks at most directly comes first: it shows the
        // selected ship.
        auto facing = [&](const Pad& p) {
            Vec3 d{p.pos.x - static_cast<float>(cam.pos.x), p.pos.y - static_cast<float>(cam.pos.y),
                   p.pos.z - static_cast<float>(cam.pos.z)};
            float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            Vec3 f = cam.forward();
            return len > 0 ? (d.x * f.x + d.y * f.y + d.z * f.z) / len : -1.f;
        };
        std::stable_sort(pads.begin(), pads.end(), [&](const Pad& a, const Pad& b) { return facing(a) > facing(b); });
    }

    // Puts these ships (archetype nicknames, selected first) on the dealer's pads.
    void set_ships(const ShipDatabase& db, Renderer& r, const std::vector<std::string>& wanted) {
        if (wanted == shown_ships) return;
        shown_ships = wanted;
        ships.clear();
        for (std::size_t i = 0; i < wanted.size() && i < pads.size(); ++i) {
            const ShipArchetype* a = db.ship(wanted[i]);
            if (!a) continue;
            try {
                if (auto h = r.load_model(a->model, a->material_libraries))
                    ships.push_back({h, {pads[i].pos.x, pads[i].pos.y, pads[i].pos.z}, Mat4::from_rows3(pads[i].orient)});
            } catch (const std::exception& ex) {
                SDL_Log("dealer ship %s: %s", a->model.c_str(), ex.what());
            }
        }
    }

    void draw(Renderer& r) const {
        for (auto& m : models) r.draw_model(m.mesh, m.pos, m.rot, {});
        for (auto& m : ships) r.draw_model(m.mesh, m.pos, m.rot, {});
    }
};

// The player's ship, its flight model, the chase camera and the HUD state.
struct FlightSession {
    const ShipArchetype* arch = nullptr;
    game::FlightModel model{game::FlightParams{}};
    game::ShipState ship;
    game::FlightInput input;
    Renderer::ModelHandle mesh;
    Quat cam_orient;
    bool mouse_flight = true;
    int target = -1;  // index into contacts
    std::vector<game::HudContact> contacts;
    float win_fovx = 54.432f;  // cameras.ini [WinCamera]: the original interface view
    std::unordered_map<std::string, Renderer::ModelHandle> hud_models;
    bool hud_meshes_loaded = false;

    // Combat
    game::Combatant body;
    std::vector<game::Gun> guns;
    std::vector<NpcShip> npcs;
    game::CombatWorld world;
    std::vector<Explosion> explosions;
    struct Flash { DVec3 pos; float age = 0; bool shield = false; };
    std::vector<Flash> flashes;
    bool firing = false;
    Vec3 aim_dir{0, 0, -1};

    // Navigation: autopilot approach, trade lanes, docking and jumps.
    enum class Nav { Free, Approach, Lane, Docked };
    Nav nav = Nav::Free;
    std::string nav_target;            // object nickname
    std::optional<game::LaneTrip> lane;
    std::string pending_jump_system, pending_jump_object;  // handled by the main loop
    const Universe* universe = nullptr;

    // Player, economy and text
    const Economy* econ = nullptr;
    const GameStrings* strings = nullptr;
    const Factions* factions = nullptr;
    game::PlayerState player;
    RoomView room;
    const Vfs* vfs = nullptr;
    const PetalDb* petal = nullptr;
    const ShipDatabase* shipdb = nullptr;
    const PilotDb* pilots = nullptr;
    const EffectDb* fxdb = nullptr;

    // Effects from the original data: engine flames, muzzle flashes, impacts.
    std::vector<AttachedFx> engine_fx, trail_fx;
    DamageFx damage;
    int shield_hits = 0;  // shield hit effects played (reported by screenshot runs)
    // Shield bubbles flashing after a hit: which ship (combatant id), where (direction in
    // the ship's frame) and how long ago.
    struct ShieldFlash { int id; Vec3 local; float age; };
    std::vector<ShieldFlash> shield_flashes;
    static constexpr float kShieldFlashTime = 0.35f;
    struct LooseFx {
        DVec3 pos, vel;
        Quat orient;
        game::EffectInstance fx;
        float age = 0;
    };
    std::vector<LooseFx> loose_fx;
    std::vector<std::string> pending_textures;  // effect texture libraries the renderer still needs
    std::uint32_t fx_seed = 1;
    std::vector<game::Sprite> sprite_buf;

    // Starts a one-off effect (flash, impact) at a point.
    void spawn_fx(const std::string& name, DVec3 pos, DVec3 vel, const Quat& orient) {
        if (!fxdb || name.empty()) return;
        ResolvedEffect fx = fxdb->resolve(name);
        if (!fx) return;
        for (auto& t : fx.vis->textures) pending_textures.push_back(t);
        if (loose_fx.size() > 400) return;  // keep heavy fights bounded
        loose_fx.push_back({pos, vel, orient, game::EffectInstance(fx, fx_seed++)});
    }

    // Fires the guns and starts their muzzle flashes.
    void fire_guns(std::vector<game::Gun>& g, int owner, const game::ShipState& s, Vec3 aim) {
        std::size_t before = world.projectiles().size();
        world.fire(g, owner, s.pos, s.orient, s.vel, aim);
        auto& ps = world.projectiles();
        for (std::size_t i = before; i < ps.size(); ++i)
            if (ps[i].gun) spawn_fx(ps[i].gun->flash_effect, ps[i].pos, s.vel, Quat::look(aim));
    }

    void update_damage(DamageFx& d, const ShipArchetype* arch, const game::Combatant& b, const game::ShipState& s, double dt) {
        if (arch && fxdb) {
            d.lit.resize(arch->fuses.size(), false);
            for (std::size_t i = 0; i < arch->fuses.size(); ++i) {
                const auto& ref = arch->fuses[i];
                if (d.lit[i] || !b.alive() || b.hull > ref.hit_pts) continue;
                d.lit[i] = true;
                const Fuse* fu = fxdb->fuse(ref.name);
                if (!fu || fu->death_fuse) continue;
                for (auto& st : fu->starts) {
                    ResolvedEffect fx = fxdb->resolve(st.effect);
                    if (!fx) continue;
                    for (auto& t : fx.vis->textures) pending_textures.push_back(t);
                    std::vector<const Hardpoint*> cands;
                    for (auto& name : st.hardpoints)
                        for (auto& h : d.hardpoints)
                            if (iequals(h.name, name)) cands.push_back(&h);
                    Vec3 at{};
                    if (!cands.empty()) at = cands[(fx_seed++ * 2654435761u >> 8) % cands.size()]->position;
                    d.fx.push_back({at, game::EffectInstance(fx, fx_seed++)});
                }
            }
        }
        for (auto& e : d.fx) e.fx.update(dt, s.pos + game::to_d(s.orient.rotate(e.offset)), s.orient, s.vel, 0, b.alive());
    }

    // The cruise trail runs while cruise charges (spark = charge so far) and while cruising.
    static void update_trail_fx(std::vector<AttachedFx>& fx, const game::ShipState& s, const game::FlightParams& p,
                                double dt, bool alive) {
        float charge = p.cruise_charge_time > 0 ? std::clamp(s.cruise_charge / p.cruise_charge_time, 0.f, 1.f) : 0.f;
        float spark = s.cruising ? 1.f : charge;
        bool on = alive && (s.cruising || s.cruise_charge > 0);
        for (auto& e : fx) e.fx.update(dt, s.pos + game::to_d(s.orient.rotate(e.offset)), s.orient, s.vel, spark, on);
    }

    static void update_engine_fx(std::vector<AttachedFx>& fx, const game::ShipState& s, const game::FlightInput& in,
                                 double dt, bool alive) {
        float spark = s.cruising ? 1.f : std::clamp(in.throttle, 0.f, 1.f);
        for (auto& e : fx) e.fx.update(dt, s.pos + game::to_d(s.orient.rotate(e.offset)), s.orient, s.vel, spark, alive);
    }

    void update_effects(double dt) {
        update_engine_fx(engine_fx, ship, input, dt, body.alive());
        for (auto& n : npcs) update_engine_fx(n.engine_fx, n.ship, n.input, dt, n.body.alive());
        update_trail_fx(trail_fx, ship, model.params(), dt, body.alive());
        for (auto& n : npcs) update_trail_fx(n.trail_fx, n.ship, n.model.params(), dt, n.body.alive());
        update_damage(damage, arch, body, ship, dt);
        for (auto& n : npcs) update_damage(n.damage, n.arch, n.body, n.ship, dt);
        for (auto& f : loose_fx) {
            f.age += static_cast<float>(dt);
            f.pos = f.pos + f.vel * dt;
            f.fx.update(dt, f.pos, f.orient, f.vel, 0, true);
        }
        std::erase_if(loose_fx, [](const LooseFx& f) { return f.fx.finished() || f.age > 8.f; });
    }

    void draw_effects(Renderer& r) {
        for (auto& t : pending_textures) r.add_effect_textures(t);
        pending_textures.clear();
        sprite_buf.clear();
        for (auto& e : engine_fx) e.fx.sprites(sprite_buf);
        for (auto& n : npcs)
            for (auto& e : n.engine_fx) e.fx.sprites(sprite_buf);
        for (auto& e : damage.fx) e.fx.sprites(sprite_buf);
        for (auto& e : trail_fx) e.fx.sprites(sprite_buf);
        for (auto& n : npcs)
            for (auto& e : n.trail_fx) e.fx.sprites(sprite_buf);
        for (auto& n : npcs)
            for (auto& e : n.damage.fx) e.fx.sprites(sprite_buf);
        for (auto& f : loose_fx) f.fx.sprites(sprite_buf);
        for (auto& s : sprite_buf)
            if (s.texture) {
                if (s.flat) r.draw_flat_sprite(s.pos, s.size, s.axis_u, s.axis_v, s.color, s.alpha, *s.texture, s.additive, s.frame);
                else r.draw_sprite(s.pos, s.size, s.rotation, s.color, s.alpha, *s.texture, s.additive, s.frame);
            }
    }

    // A bolt drawn the way its munition's beam entry describes: a bright head and a
    // fading tail along its flight path.
    bool draw_beam(Renderer& r, const game::Projectile& p) {
        if (!fxdb || !p.munition) return false;
        const BeamSpear* b = fxdb->beam(p.munition->const_effect);
        if (!b || b->head_texture.empty()) return false;
        double speed = p.vel.length();
        if (speed <= 0) return false;
        DVec3 back = p.vel * (-1.0 / speed);
        // Widths in the data are full widths; the sprites take half-widths. The original shows
        // a bright round head and a thin tail with a white-hot core inside the outer colour.
        Color3 head{b->core_color.r * b->head_brightness, b->core_color.g * b->head_brightness, b->core_color.b * b->head_brightness};
        r.draw_sprite(p.pos, b->head_width * 0.5f, 0, head, 1, b->head_texture, true);
        r.draw_sprite(p.pos, b->head_width * 1.2f, 0, b->outer_color, 0.7f, b->head_texture, true);
        const std::string& tex = b->trail_texture.empty() ? b->head_texture : b->trail_texture;
        DVec3 end = p.pos + back * b->tail_length;
        float w = b->core_width * 0.5f;
        r.draw_streak(p.pos, end, w, w * 0.3f, b->outer_color, b->trail_brightness, b->tail_color, 0.f, tex, true);
        r.draw_streak(p.pos, p.pos + back * (b->tail_length * 0.6), w * 0.35f, w * 0.1f, b->core_color,
                      b->trail_brightness, b->outer_color, 0.f, tex, true);
        return true;
    }
    // The room that goes with the open dealer tab (the base's own room names).
    std::string room_for_tab() const {
        switch (trade_tab) {
            case Tab::Commodities: return "Trader";
            case Tab::Equipment: return "Equipment";
            case Tab::Ships: return "ShipDealer";
        }
        return {};
    }
    std::string notice;  // short message on the status line (e.g. docking refused)
    float notice_time = 0;
    int trade_sel = 0;
    std::string trade_msg;
    std::string docked_label;

    bool docked() const { return nav == Nav::Docked; }

    std::string label_of(const SystemObject& o) const {
        return strings ? strings->name(static_cast<std::uint32_t>(o.ids_name), o.nickname) : o.nickname;
    }
    std::string base_label(const std::string& base) const {
        const BaseInfo* b = econ ? econ->base(base) : nullptr;
        return b && strings ? strings->name(b->ids_name, base) : base;
    }

    void on_docked(const SystemObject& o) {
        nav = Nav::Docked;
        nav_target = o.nickname;
        ship.vel = {};
        input.cruise = false;
        player.base = !o.base.empty() ? o.base : o.dock_with;
        player.base_faction = o.reputation;
        docked_label = player.base.empty() ? label_of(o) : base_label(player.base);
        trade_tab = Tab::Commodities;
        trade_sel = 0;
        trade_msg.clear();
    }

    // Base screen tabs: commodity trader, equipment dealer, ship dealer.
    enum class Tab { Commodities, Equipment, Ships };
    Tab trade_tab = Tab::Commodities;
    bool refit_needed = false;  // ship or guns changed: the main loop rebuilds them

    std::string good_name(std::string_view good) const {
        return strings && econ ? strings->name(econ->name_id(good), good) : std::string(good);
    }
    std::string money(std::int64_t v) const { return game::format_money(v); }
    // "HpWeapon03" -> "WEAPON 3", "HpCM01" -> "CM 1".
    static std::string hardpoint_label(const std::string& hp) {
        std::string s = hp.size() > 2 && iequals(hp.substr(0, 2), "hp") ? hp.substr(2) : hp;
        std::size_t d = s.find_first_of("0123456789");
        if (d == std::string::npos) return s;
        std::size_t nz = s.find_first_not_of('0', d);
        return s.substr(0, d) + " " + (nz == std::string::npos ? "0" : s.substr(nz));
    }
    std::int64_t price_here(std::string_view good) const {
        return static_cast<std::int64_t>(std::lround(econ->price(player.base, good)));
    }

    // One line on the base screen and what Right (buy) / Left (sell) do with it.
    struct TradeRow {
        enum Kind { Heading, Commodity, BuyEquipment, MountedEquipment, Ship } kind = Heading;
        std::string good;
        std::size_t index = 0;  // MountedEquipment: index into player.equipment
        game::BaseScreenRow row;
    };

    std::vector<TradeRow> trade_rows() const {
        std::vector<TradeRow> out;
        if (!econ || player.base.empty()) return out;
        auto heading = [&](std::string t) {
            TradeRow r;
            r.row.name = std::move(t);
            r.row.heading = true;
            out.push_back(std::move(r));
        };
        if (trade_tab == Tab::Commodities) {
            // The market's commodities, plus anything in the hold the base will buy.
            for (auto& m : econ->market(player.base)) {
                const Good* g = econ->good(m.good);
                if (!g || g->category != GoodCategory::Commodity) continue;
                int owned = player.units(m.good);
                bool sold = game::offered(m, player);
                if (!sold && !owned) continue;
                TradeRow r;
                r.kind = TradeRow::Commodity;
                r.good = to_lower(m.good);
                r.row.name = good_name(m.good);
                r.row.price = money(price_here(m.good));
                r.row.col3 = owned ? std::to_string(owned) : "-";
                r.row.col4 = sold ? std::to_string(game::max_buy(*econ, player, m.good)) : "SELL ONLY";
                out.push_back(std::move(r));
            }
        } else if (trade_tab == Tab::Equipment) {
            for (auto& m : econ->market(player.base)) {
                const Good* g = econ->good(m.good);
                if (!g || g->category != GoodCategory::Equipment || !game::offered(m, player)) continue;
                std::string item = g->equipment.empty() ? g->nickname : g->equipment;
                if (econ->hardpoints_for(player.ship, item).empty()) continue;  // does not fit this ship
                TradeRow r;
                r.kind = TradeRow::BuyEquipment;
                r.good = to_lower(m.good);
                r.row.name = good_name(m.good);
                r.row.price = money(price_here(m.good));
                std::string hp = game::free_hardpoint(*econ, player, item);
                r.row.col3 = hp.empty() ? "-" : hardpoint_label(hp);
                r.row.col4 = hp.empty() ? "NO ROOM" : "BUY";
                r.row.dim = hp.empty();
                out.push_back(std::move(r));
            }
            heading("ON YOUR SHIP");
            for (std::size_t i = 0; i < player.equipment.size(); ++i) {
                auto& e = player.equipment[i];
                const EquipmentItem* it = econ->equipment(e.item);
                if (e.hardpoint.empty() || !it || it->hp_class.empty() || !econ->good(e.item)) continue;
                TradeRow r;
                r.kind = TradeRow::MountedEquipment;
                r.good = to_lower(e.item);
                r.index = i;
                r.row.name = good_name(e.item);
                r.row.price = money(price_here(e.item));
                r.row.col3 = hardpoint_label(e.hardpoint);
                r.row.col4 = "SELL";
                out.push_back(std::move(r));
            }
        } else {
            std::int64_t trade_in = game::trade_in_value(*econ, player);
            heading("YOUR SHIP: " + (strings ? strings->name(econ->ship_name_id(player.ship), player.ship) : player.ship) +
                    "   TRADE-IN " + money(trade_in));
            for (auto& m : econ->market(player.base)) {
                const Good* g = econ->good(m.good);
                if (!g || g->category != GoodCategory::Ship || !game::offered(m, player)) continue;
                const Good* hull = econ->good(g->hull);
                const ShipInfo* info = hull ? econ->ship(hull->ship) : nullptr;
                if (!info) continue;
                TradeRow r;
                r.kind = TradeRow::Ship;
                r.good = to_lower(m.good);
                r.row.name = strings ? strings->name(info->ids_name, info->nickname) : info->nickname;
                std::int64_t price = game::ship_price(*econ, player, m.good);
                r.row.price = money(price);
                r.row.col3 = std::to_string(static_cast<int>(info->hold_size));
                std::int64_t net = price - trade_in;
                r.row.col4 = net >= 0 ? "PAY " + money(net) : "GET " + money(-net);
                r.row.dim = price - trade_in > player.credits;
                out.push_back(std::move(r));
            }
        }
        return out;
    }

    // Ship dealer: the ships to put on the pads, the selected one first, then the rest
    // of the offer in market order. Empty on the other dealer tabs.
    std::vector<std::string> dealer_ships() const {
        std::vector<std::string> out;
        if (trade_tab != Tab::Ships || !econ) return out;
        auto rows = trade_rows();
        auto ship_of = [&](const TradeRow& r) -> std::string {
            const Good* g = econ->good(r.good);
            const Good* hull = g ? econ->good(g->hull) : nullptr;
            return hull ? to_lower(hull->ship) : std::string();
        };
        if (trade_sel >= 0 && trade_sel < static_cast<int>(rows.size()) && rows[trade_sel].kind == TradeRow::Ship)
            out.push_back(ship_of(rows[trade_sel]));
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (i != trade_sel && rows[i].kind == TradeRow::Ship) out.push_back(ship_of(rows[i]));
        return out;
    }

    void select_row(int step) {
        auto rows = trade_rows();
        int n = static_cast<int>(rows.size());
        if (!n) return;
        int i = std::clamp(trade_sel, 0, n - 1);
        for (int k = 0; k < n; ++k) {
            i = (i + step + n) % n;
            if (!rows[i].row.heading) break;
        }
        trade_sel = i;
    }

    void set_tab(Tab t) {
        trade_tab = t;
        trade_sel = -1;
        select_row(1);
        trade_msg.clear();
    }

    // dir > 0 buys, dir < 0 sells; `all` trades as many units as possible (commodities).
    void trade(int dir, bool all) {
        auto rows = trade_rows();
        if (rows.empty()) return;
        trade_sel = std::clamp(trade_sel, 0, static_cast<int>(rows.size()) - 1);
        auto& r = rows[trade_sel];
        game::TradeResult res = game::TradeResult::NotSold;
        std::string done;
        switch (r.kind) {
            case TradeRow::Heading: return;
            case TradeRow::Commodity: {
                int n = 1;
                if (all) n = dir > 0 ? game::max_buy(*econ, player, r.good) : player.units(r.good);
                if (n <= 0) n = 1;  // reports why nothing can be traded
                res = dir > 0 ? game::buy(*econ, player, r.good, n) : game::sell(*econ, player, r.good, n);
                done = std::string(dir > 0 ? "BOUGHT " : "SOLD ") + std::to_string(n) + " " + r.row.name;
                break;
            }
            case TradeRow::BuyEquipment:
                if (dir < 0) return;
                res = game::buy_equipment(*econ, player, r.good);
                done = "MOUNTED " + r.row.name;
                break;
            case TradeRow::MountedEquipment:
                if (dir > 0) return;
                res = game::sell_equipment(*econ, player, r.index);
                done = "SOLD " + r.row.name;
                break;
            case TradeRow::Ship:
                if (dir < 0) return;
                res = game::buy_ship(*econ, player, r.good);
                done = "NEW SHIP: " + r.row.name;
                break;
        }
        bool ok = res == game::TradeResult::Ok;
        trade_msg = ok ? done : game::trade_message(res);
        if (ok && r.kind != TradeRow::Commodity) refit_needed = true;
        if (ok) select_row(0);
    }

    game::BaseScreen base_screen(const StarSystem& sys) const {
        game::BaseScreen b;
        b.title = docked_label;
        b.system = sys.nickname;
        b.credits = player.credits;
        b.hold_used = econ ? game::cargo_used(*econ, player) : 0;
        b.hold_size = player.hold_size;
        b.tabs = {"COMMODITIES", "EQUIPMENT", "SHIPS"};
        b.tab = static_cast<int>(trade_tab);
        static const char* cols[3][4] = {{"COMMODITY", "PRICE", "IN HOLD", "CAN BUY"},
                                         {"EQUIPMENT", "PRICE", "HARDPOINT", ""},
                                         {"SHIP", "PRICE", "CARGO", "WITH TRADE-IN"}};
        for (int i = 0; i < 4; ++i) b.columns[i] = cols[b.tab][i];
        for (auto& r : trade_rows()) b.rows.push_back(r.row);
        b.selected = std::clamp(trade_sel, 0, std::max(0, static_cast<int>(b.rows.size()) - 1));
        b.message = trade_msg;
        b.help = trade_tab == Tab::Commodities ? "1-3 DEALER   UP/DOWN SELECT   RIGHT BUY   LEFT SELL   SHIFT = ALL   F3 LAUNCH"
                                               : "1-3 DEALER   UP/DOWN SELECT   RIGHT BUY   LEFT SELL   F3 LAUNCH   F5 SAVE";
        return b;
    }

    // Puts the ship docked at a base of this system (new games and loaded saves).
    bool dock_at_base(const StarSystem& sys, const std::string& base) {
        const SystemObject* best = nullptr;
        for (auto& o : sys.objects)
            if (iequals(o.base, base)) { best = &o; break; }
        if (!best)
            for (auto& o : sys.objects)
                if (iequals(o.dock_with, base)) { best = &o; break; }
        if (!best) return false;
        auto sp = game::exit_point(*best, approach_distance(*best) + 100.f);
        ship.pos = sp.pos;
        ship.orient = sp.orient;
        cam_orient = sp.orient;
        on_docked(*best);
        return true;
    }

    float approach_distance(const SystemObject& o) const {
        if (o.is_tradelane()) return 120.f;
        if (o.is_jump()) return 250.f;
        const SolarArchetype* a = universe ? universe->solar(o.archetype) : nullptr;
        return (a && a->solar_radius > 0 ? a->solar_radius : 200.f) + 250.f;
    }

    // F3 on the selected target: lanes, gates and bases. Pressing it again cancels.
    void dock_request(const StarSystem& sys) {
        if (nav == Nav::Docked) {  // launch
            if (auto* o = game::find_object(sys, nav_target)) {
                float d = approach_distance(*o) + 100.f;
                auto sp = game::exit_point(*o, d);
                ship.pos = sp.pos;
                ship.orient = sp.orient;
                ship.vel = game::to_d(sp.orient.forward()) * 40.0;
                cam_orient = ship.orient;
            }
            nav = Nav::Free;
            player.base.clear();
            trade_msg.clear();
            input.throttle = 0.5f;
            return;
        }
        if (nav != Nav::Free) { nav = Nav::Free; lane.reset(); return; }
        if (target < 0 || target >= static_cast<int>(contacts.size())) return;
        const SystemObject* o = game::find_object(sys, contacts[target].name);
        if (!o || !(o->is_tradelane() || o->is_jump() || o->dockable())) return;
        if (!o->reputation.empty() && attitude_of(player.rep_with(o->reputation)) == Attitude::Hostile) {
            notice = "DOCKING REFUSED: " + label_of(*o) + " IS HOSTILE";
            notice_time = 4.f;
            return;
        }
        nav_target = o->nickname;
        nav = Nav::Approach;
    }

    std::string nav_status() const {
        if (notice_time > 0) return notice;
        switch (nav) {
            case Nav::Approach: return "AUTOPILOT: " + nav_target;
            case Nav::Lane: return "TRADE LANE";
            case Nav::Docked: return "DOCKED: " + docked_label + "  (F3 = LAUNCH)";
            default: return {};
        }
    }

    // Returns true while navigation owns the controls.
    bool update_nav(const StarSystem& sys, double dt) {
        notice_time = std::max(0.f, notice_time - static_cast<float>(dt));
        if (nav == Nav::Free) return false;
        if (nav == Nav::Docked) { ship.vel = {}; return true; }
        if (nav == Nav::Lane) {
            if (!lane || !lane->update(dt)) {
                // Leave the lane at the last ring, moving at normal flight speed.
                ship.vel = game::to_d(ship.orient.forward()) * 80.0;
                lane.reset();
                nav = Nav::Free;
                input.throttle = 1.f;
                return false;
            }
            ship.pos = lane->position();
            ship.orient = Quat::look(lane->direction());
            ship.vel = game::to_d(lane->direction()) * lane->speed();
            return true;
        }
        // Approach: fly the ship towards the target with the normal flight model.
        const SystemObject* o = game::find_object(sys, nav_target);
        if (!o) { nav = Nav::Free; return false; }
        DVec3 tp{o->pos.x, o->pos.y, o->pos.z};
        DVec3 to = tp - ship.pos;
        double dist = to.length();
        if (dist < approach_distance(*o)) {
            if (o->is_tradelane()) {
                lane = game::LaneTrip::start(sys, *o);
                nav = lane ? Nav::Lane : Nav::Free;
            } else if (o->is_jump()) {
                pending_jump_system = o->goto_system;
                pending_jump_object = o->goto_object;
                nav = Nav::Free;
            } else {
                on_docked(*o);
            }
            input.cruise = false;
            return true;
        }
        Vec3 dir = render::normalize(Vec3{static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z)});
        Vec3 local = ship.orient.conjugate().rotate(dir);
        input.steer = {std::clamp(local.y * 3.f, -1.f, 1.f), std::clamp(-local.x * 3.f, -1.f, 1.f), 0};
        input.throttle = 1.f;
        input.cruise = dist > 3000 && -local.z > 0.97f;
        return false;  // the flight model still runs, with these inputs
    }

    void setup_combat(const ShipDatabase& db, const Vfs& vfs, const Loadout* lo, Renderer& r, const Args& args) {
        body.id = 0;
        body.radius = 8;
        body.hull = body.max_hull = arch->hit_pts;
        if (lo) body.set_shield(db.shield_of(*lo));
        guns = guns_for(db, vfs, *arch, lo);
        if (args.npcs <= 0) return;
        const ShipArchetype* na = db.ship(args.npc_ship);
        if (!na) { SDL_Log("unknown NPC ship %s", args.npc_ship.c_str()); return; }
        const Loadout* nlo = db.loadout_for_ship(args.npc_ship);
        auto mesh = r.load_model(na->model, na->material_libraries);
        auto nguns = guns_for(db, vfs, *na, nlo);
        const Pilot* pilot = pilots ? pilots->pilot(pilot_for(vfs, args)) : nullptr;
        if (pilots && !pilot) SDL_Log("no pilot data for %s; using the simple test AI", args.npc_ship.c_str());
        else if (pilot) SDL_Log("NPC pilot: %s", pilot->nickname.c_str());
        for (int i = 0; i < args.npcs; ++i) {
            NpcShip n;
            n.name = na->nickname + " " + std::to_string(i + 1);
            n.arch = na;
            n.model = game::FlightModel(game::FlightParams::from(*na, nlo ? db.engine_of(*nlo) : nullptr, db.cruise_speed()));
            Vec3 off = ship.orient.rotate({(i - (args.npcs - 1) * 0.5f) * 90.f, 25.f * (i % 2), -700.f - 60.f * i});
            n.ship.pos = ship.pos + game::to_d(off);
            Vec3 f = ship.orient.forward();
            n.ship.orient = Quat::look({-f.x, -f.y, -f.z});  // head-on
            n.body.id = i + 1;
            n.body.radius = 8;
            n.body.hull = n.body.max_hull = na->hit_pts;
            if (nlo) n.body.set_shield(db.shield_of(*nlo));
            n.guns = nguns;
            n.mesh = mesh;
            if (fxdb) {
                n.engine_fx = engine_fx_for(*fxdb, vfs, *na, nlo ? db.engine_of(*nlo) : nullptr, fx_seed += 16, pending_textures);
                n.trail_fx = trail_fx_for(*fxdb, vfs, *na, nlo ? db.engine_of(*nlo) : nullptr, false, fx_seed += 16, pending_textures);
            }
            n.damage.hardpoints = model_hardpoints(vfs, *na);
            auto nsh = shield_shape_for(n.damage.hardpoints);
            n.damage.shield_center = nsh.center;
            n.damage.shield_radii = nsh.radii;
            if (pilot) {
                n.ai.emplace(*pilot, static_cast<std::uint32_t>(i + 1));
                float rx, uy;
                game::FighterAi::wing_slot(i, rx, uy);
                n.ai->set_trail_offset(rx, uy);
            }
            npcs.push_back(std::move(n));
        }
    }

    // The pilot for test NPCs: --npc-pilot, else the first npcships.ini entry flying this
    // ship, else the population default for light fighters.
    static std::string pilot_for(const Vfs& vfs, const Args& args) {
        if (!args.npc_pilot.empty()) return args.npc_pilot;
        if (vfs.exists("missions/npcships.ini"))
            for (auto& sec : parse_ini(vfs.read("missions/npcships.ini"))) {
                auto a = sec.find("ship_archetype");
                auto p = sec.find("pilot");
                if (a && p && !a->values.empty() && !p->values.empty() && iequals(a->values[0].as_string(), args.npc_ship))
                    return p->values[0].as_string();
            }
        return "light_fighter_default";
    }

    // Original-data AI: the pilot's blocks decide how it flies and shoots.
    void npc_think_pilot(NpcShip& n, float dt) {
        if (!body.alive()) { n.input = {}; n.input.throttle = 0.5f; return; }
        game::AiBody self{n.ship.pos, n.ship.vel, n.ship.orient};
        game::AiBody tgt{ship.pos, ship.vel, ship.orient};
        game::AiWeapons w;
        if (!n.guns.empty() && n.guns[0].arch) w.muzzle_speed = n.guns[0].arch->muzzle_velocity;
        if (!n.guns.empty() && n.guns[0].munition) w.range = w.muzzle_speed * n.guns[0].munition->lifetime;
        float hull = n.body.max_hull > 0 ? n.body.hull / n.body.max_hull : 1.f;
        auto out = n.ai->update(self, tgt, hull, true, w, dt);
        n.input = out.input;
        if (out.fire && !ally_in_line(n, out.aim_dir, w.muzzle_speed, w.range)) fire_guns(n.guns, n.body.id, n.ship, out.aim_dir);
    }

    // Pilots hold fire while a wingman is in the line of fire, also beyond the target,
    // where the bolts that miss still fly on.
    // Allies are checked where they will be when the bolt passes them.
    bool ally_in_line(const NpcShip& n, Vec3 dir, float muzzle_speed, float range) const {
        for (auto& o : npcs) {
            if (&o == &n || !o.body.alive()) continue;
            DVec3 now = o.ship.pos - n.ship.pos;
            double t = now.length() / std::max(1.f, muzzle_speed);
            DVec3 to = now + (o.ship.vel - n.ship.vel) * t;
            double along = to.x * dir.x + to.y * dir.y + to.z * dir.z;
            if (along <= 0 || along > range) continue;
            double off = (to - game::to_d(dir) * along).length();
            if (off < o.body.radius * 2.0 + along * 0.08) return true;  // the accuracy cone spreads bolts
        }
        return false;
    }

    // Our own simple pursuit AI: steer towards where the player will be, fire when the
    // guns line up. Good enough to test combat; proper AI comes with the mission system.
    void npc_think(NpcShip& n) {
        if (!body.alive()) { n.input.throttle = 0.5f; n.input.steer = {}; return; }
        float speed = n.guns.empty() || !n.guns[0].arch ? 600.f : n.guns[0].arch->muzzle_velocity;
        DVec3 to = ship.pos - n.ship.pos;
        double dist = to.length();
        DVec3 lead = ship.pos + ship.vel * (dist / speed) - n.ship.pos;
        Vec3 dir = render::normalize(Vec3{static_cast<float>(lead.x), static_cast<float>(lead.y), static_cast<float>(lead.z)});
        Vec3 local = n.ship.orient.conjugate().rotate(dir);
        n.input.steer = {std::clamp(local.y * 3.f, -1.f, 1.f), std::clamp(-local.x * 3.f, -1.f, 1.f), 0};
        n.input.throttle = dist < 150 ? 0.2f : 1.f;
        n.input.cruise = false;
        bool aligned = -local.z > 0.985f;
        float range = speed * (n.guns.empty() || !n.guns[0].munition ? 1.f : n.guns[0].munition->lifetime);
        // Bursts of about a second with pauses, and a small aiming error, so a group of
        // pilots does not focus every bolt on the player at once.
        if (aligned && dist < range * 0.9 && n.burst > 0) {
            float a = std::sin(static_cast<float>(sim_time) * 7.3f + n.body.id * 1.7f) * 0.03f;
            float b = std::cos(static_cast<float>(sim_time) * 5.1f + n.body.id * 2.3f) * 0.03f;
            Vec3 r = n.ship.orient.right(), u = n.ship.orient.up();
            Vec3 d = render::normalize(Vec3{dir.x + r.x * a + u.x * b, dir.y + r.y * a + u.y * b, dir.z + r.z * a + u.z * b});
            fire_guns(n.guns, n.body.id, n.ship, d);
        }
    }

    double sim_time = 0;

    void update_combat(double dt_d) {
        float dt = static_cast<float>(dt_d);
        sim_time += dt_d;
        for (auto& n : npcs) {
            n.burst = n.burst > 0 ? n.burst - dt : n.burst + dt;
            if (n.burst <= 0 && n.burst > -dt) n.burst = -1.5f;  // just ended: pause
            else if (n.burst < 0 && n.burst + dt >= 0) n.burst = 1.0f;
        }
        world.update_guns(guns, dt);
        if (firing && body.alive()) fire_guns(guns, body.id, ship, aim_dir);
        for (auto& n : npcs) {
            if (!n.body.alive()) continue;
            world.update_guns(n.guns, dt);
            if (n.ai) npc_think_pilot(n, dt);
            else npc_think(n);
            n.model.update(n.ship, n.input, dt_d);
            n.body.pos = n.ship.pos;
            n.body.update(dt);
        }
        body.pos = ship.pos;
        body.update(dt);
        std::vector<game::Combatant*> targets{&body};
        for (auto& n : npcs) targets.push_back(&n.body);
        world.update(dt, targets, [&](game::Combatant& t, const game::Projectile& p) {
            // A hit on a raised shield plays the shield's effect, turned to face out from the
            // ship at the point of impact; otherwise the munition's own hit effect.
            const std::string* shield_fx = nullptr;
            if (t.shield > 0 && t.shield_arch && fxdb) {
                auto& name = t.shield_arch->hit_effect(p.hull_damage + p.energy_damage);
                if (!name.empty() && fxdb->vis(name)) shield_fx = &name;
            }
            if (shield_fx) {
                DVec3 out = p.pos - t.pos;
                double len = out.length();
                Vec3 n = len > 1e-6 ? Vec3{static_cast<float>(out.x / len), static_cast<float>(out.y / len), static_cast<float>(out.z / len)}
                                    : Vec3{0, 1, 0};
                Vec3 axis{-n.z, 0, n.x};  // +Y x n
                float s2 = std::sqrt(axis.x * axis.x + axis.z * axis.z);
                Quat q = s2 > 1e-5f ? Quat::axis_angle({axis.x / s2, 0, axis.z / s2}, std::atan2(s2, n.y))
                                    : (n.y > 0 ? Quat{} : Quat::axis_angle({1, 0, 0}, 3.14159265f));
                spawn_fx(*shield_fx, p.pos, {}, q);
                ++shield_hits;
                const game::ShipState* hs = t.id == 0 ? &ship : nullptr;
                for (auto& nn : npcs)
                    if (nn.body.id == t.id) hs = &nn.ship;
                if (hs) shield_flashes.push_back({t.id, hs->orient.conjugate().rotate(n), 0});
                if (shield_flashes.size() > 64) shield_flashes.erase(shield_flashes.begin());
            } else if (p.munition && fxdb && fxdb->vis(p.munition->hit_effect)) {
                spawn_fx(p.munition->hit_effect, p.pos, {}, Quat{});
            } else {
                flashes.push_back({p.pos, 0, t.shield > 0});
            }
        });
        for (auto& n : npcs)
            if (!n.body.alive() && n.mesh) {
                // The ship's explosion_arch names the effect it dies with (fx/explosions.ini).
                const std::string& boom = fxdb && n.arch ? fxdb->explosion_effect(n.arch->explosion_arch) : std::string();
                if (!boom.empty() && fxdb->vis(boom))
                    spawn_fx(boom, n.ship.pos, n.ship.vel * 0.5, n.ship.orient);
                else
                    explosions.push_back({n.ship.pos, 0, 35});
                n.mesh = {};  // wreck removed; debris comes with the effects system
            }
        for (auto& e : explosions) e.age += dt;
        std::erase_if(explosions, [](const Explosion& e) { return e.age > 2.0f; });
        for (auto& f : shield_flashes) f.age += dt;
        std::erase_if(shield_flashes, [](const ShieldFlash& f) { return f.age > kShieldFlashTime; });
        for (auto& f : flashes) f.age += dt;
        std::erase_if(flashes, [](const Flash& f) { return f.age > 0.15f; });
        update_effects(dt_d);
    }

    void draw_combat(Renderer& r) {
        for (auto& n : npcs)
            if (n.body.alive() && n.mesh)
                r.draw_model(n.mesh, n.ship.pos, n.ship.orient.matrix() * Mat4::rotate_z(n.ship.bank), n.arch->lod_ranges);
        for (auto& p : world.projectiles()) {
            if (draw_beam(r, p)) continue;
            Color3 c = p.owner == 0 ? Color3{0.4f, 0.8f, 1.f} : Color3{1.f, 0.35f, 0.2f};
            // A short streak: a few glows along the last few metres of travel.
            DVec3 back = p.vel * (-1.0 / std::max(1.0, p.vel.length()));
            for (int i = 0; i < 4; ++i) r.draw_glow(p.pos + back * (i * 2.0), 0.9f - i * 0.15f, c, 2.2f - i * 0.45f);
        }
        for (auto& f : flashes)
            r.draw_glow(f.pos, 6.f + f.age * 40.f, f.shield ? Color3{0.4f, 0.7f, 1.f} : Color3{1.f, 0.7f, 0.3f}, 3.f * (1.f - f.age / 0.15f));
        for (auto& e : explosions) {
            float t = e.age / 2.0f;
            r.draw_glow(e.pos, e.size * (0.3f + t * 1.4f), {1.f, 0.55f, 0.2f}, 8.f * (1.f - t) * (1.f - t));
            r.draw_glow(e.pos, e.size * 0.4f * (1.f + t), {1.f, 0.9f, 0.7f}, 12.f * std::max(0.f, 1.f - t * 3.f));
        }
        for (auto& f : shield_flashes) {
            const game::ShipState* hs = f.id == 0 ? &ship : nullptr;
            const DamageFx* d = f.id == 0 ? &damage : nullptr;
            for (auto& nn : npcs)
                if (nn.body.id == f.id && nn.body.alive()) {
                    hs = &nn.ship;
                    d = &nn.damage;
                }
            if (!hs || !d) continue;
            Vec3 rr = d->shield_radii;
            float k = 1.f - f.age / kShieldFlashTime;
            auto axis = [&](Vec3 a, float len) {
                Vec3 v = hs->orient.rotate(a);
                return Vec3{v.x * len, v.y * len, v.z * len};
            };
            DVec3 centre = hs->pos + game::to_d(hs->orient.rotate(d->shield_center));
            r.draw_shield(centre, axis({1, 0, 0}, rr.x), axis({0, 1, 0}, rr.y), axis({0, 0, 1}, rr.z), f.local,
                          {0.45f, 0.65f, 1.f}, 0.6f * k * k);
        }
        draw_effects(r);
    }

    // Original interface models come from the player's own data, loaded on first use.
    Renderer::ModelHandle hud_model(Renderer& r, const std::string& path) {
        if (auto it = hud_models.find(path); it != hud_models.end()) return it->second;
        Renderer::ModelHandle h;
        try {
            if (!hud_meshes_loaded) {
                hud_meshes_loaded = true;
                r.add_mesh_library("interface/interface.generic.vms");
            }
            h = r.load_model(path, {"interface/hud/hud.txm"});
        } catch (const std::exception& e) {
            SDL_Log("HUD model %s: %s", path.c_str(), e.what());
        }
        return hud_models[path] = h;
    }

    void spawn(const StarSystem& sys, const Args& args) {
        // Default: in front of the first station, facing it.
        DVec3 focus{0, 0, 0};
        bool found = false;
        for (auto& o : sys.objects)
            if (o.archetype.find("station") != std::string::npos || o.archetype.find("Station") != std::string::npos) {
                focus = {o.pos.x, o.pos.y, o.pos.z};
                found = true;
                break;
            }
        if (!found && !sys.objects.empty()) focus = {sys.objects[0].pos.x, sys.objects[0].pos.y, sys.objects[0].pos.z};
        ship.pos = focus + DVec3{0, 150, 2200};
        if (args.has_camera) ship.pos = args.camera;
        DVec3 look = args.has_look ? args.look : focus;
        DVec3 d = look - ship.pos;
        ship.orient = Quat::look({static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)});
        cam_orient = ship.orient;
    }

    void update_contacts(const StarSystem& sys) {
        std::string selected = target >= 0 && target < static_cast<int>(contacts.size()) ? contacts[target].name : "";
        contacts.clear();
        for (auto& o : sys.objects) {
            game::HudContact c;
            c.name = o.nickname;
            c.label = label_of(o);
            if (!o.reputation.empty()) {
                auto att = attitude_of(player.rep_with(o.reputation));
                c.hostile = att == Attitude::Hostile;
                c.friendly = att == Attitude::Friendly;
                const Faction* f = factions ? factions->faction(o.reputation) : nullptr;
                if (f && strings) c.faction = strings->name(f->ids_name, "");
            }
            c.pos = {o.pos.x, o.pos.y, o.pos.z};
            c.radius = 200;
            contacts.push_back(std::move(c));
        }
        for (auto& n : npcs) {
            if (!n.body.alive()) continue;
            game::HudContact c;
            c.name = n.name;
            c.pos = n.ship.pos;
            c.radius = 12;
            c.hostile = true;
            c.hull = n.body.hull / std::max(1.f, n.body.max_hull);
            c.shield = n.body.max_shield > 0 ? n.body.shield / n.body.max_shield : 0;
            contacts.push_back(std::move(c));
        }
        std::sort(contacts.begin(), contacts.end(), [&](auto& a, auto& b) {
            return (a.pos - ship.pos).length() < (b.pos - ship.pos).length();
        });
        target = -1;
        for (std::size_t i = 0; i < contacts.size(); ++i)
            if (contacts[i].name == selected) target = static_cast<int>(i);
        if (target >= 0) contacts[target].selected = true;
    }

    void next_target() {
        if (contacts.empty()) return;
        target = (target + 1) % static_cast<int>(std::min<std::size_t>(contacts.size(), 10));
        for (auto& c : contacts) c.selected = false;
        contacts[target].selected = true;
    }

    // Chase camera: trails the ship's orientation with a little lag, from the ship's
    // own camera_offset (up, back), so turns read on screen as in the original.
    void update_camera(Camera& cam, double dt) {
        float k = 1.f - std::exp(-static_cast<float>(dt) * 7.f);
        cam_orient = Quat::slerp(cam_orient, ship.orient, k).normalized();
        Vec3 off = cam_orient.rotate({0, arch ? arch->camera_offset.x : 8.f, arch ? arch->camera_offset.y : 34.f});
        cam.pos = ship.pos + game::to_d(off);
        cam.set_basis(cam_orient.forward(), cam_orient.up());
    }

    void draw(Renderer& r) {
        Mat4 rot = ship.orient.matrix() * Mat4::rotate_z(ship.bank);
        if (body.alive()) r.draw_model(mesh, ship.pos, rot, arch ? arch->lod_ranges : std::vector<float>{});
        draw_combat(r);
    }

    game::HudState hud_state(const StarSystem& sys, float cursor_x, float cursor_y) const {
        game::HudState h;
        h.speed = ship.speed();
        h.max_speed = model.params().max_speed();
        h.throttle = input.throttle;
        h.cruise_charge = model.params().cruise_charge_time > 0 ? ship.cruise_charge / model.params().cruise_charge_time : 0;
        h.cruising = ship.cruising;
        h.mouse_flight = mouse_flight;
        h.cursor_x = cursor_x;
        h.cursor_y = cursor_y;
        h.system_name = sys.nickname;
        h.status = docked() && econ ? std::string() : nav_status();  // the base screen names the base
        h.hull = body.max_hull > 0 ? body.hull / body.max_hull : 1;
        h.shield = body.max_shield > 0 ? body.shield / body.max_shield : 0;
        h.contacts = contacts;
        return h;
    }
};

// The player's mounted equipment as a loadout (engine, shield and guns come from it).
Loadout player_loadout(const game::PlayerState& p) {
    Loadout l;
    l.nickname = "player";
    l.archetype = p.ship;
    for (auto& m : p.equipment) l.equip.push_back({m.item, m.hardpoint});
    return l;
}

// Rebuilds the ship from the player state: model (when the ship changed), flight model,
// guns, shield and hold. NPCs are spawned only when asked (scripted test runs).
void refit(FlightSession& fs, const ShipDatabase& db, const Vfs& vfs, Renderer& r, const Args& args, bool spawn_npcs) {
    const ShipArchetype* arch = db.ship(fs.player.ship);
    if (!arch) throw std::runtime_error("unknown ship " + fs.player.ship);
    Loadout lo = player_loadout(fs.player);
    if (arch != fs.arch) {
        fs.arch = arch;
        fs.mesh = r.load_model(arch->model, arch->material_libraries);
    }
    fs.model = game::FlightModel(game::FlightParams::from(*arch, db.engine_of(lo), db.cruise_speed()));
    if (fs.fxdb) {
        fs.engine_fx = engine_fx_for(*fs.fxdb, vfs, *arch, db.engine_of(lo), fs.fx_seed += 16, fs.pending_textures);
        fs.trail_fx = trail_fx_for(*fs.fxdb, vfs, *arch, db.engine_of(lo), true, fs.fx_seed += 16, fs.pending_textures);
    }
    fs.damage = {};
    fs.damage.hardpoints = model_hardpoints(vfs, *arch);
    auto psh = shield_shape_for(fs.damage.hardpoints);
    fs.damage.shield_center = psh.center;
    fs.damage.shield_radii = psh.radii;
    fs.player.ship = arch->nickname;
    fs.player.hold_size = arch->hold_size;
    fs.npcs.clear();
    Args a = args;
    if (!spawn_npcs) a.npcs = 0;
    fs.setup_combat(db, vfs, &lo, r, a);
}

// A ship with the equipment of a loadout from the data (its first one if none is named).
void set_ship(FlightSession& fs, const ShipDatabase& db, const Vfs& vfs, Renderer& r, const Args& args, const std::string& ship,
              const std::string& loadout, bool spawn_npcs) {
    const Loadout* lo = loadout.empty() ? nullptr : db.loadout(loadout);
    if (!lo) lo = db.loadout_for_ship(ship);
    fs.player.ship = ship;
    fs.player.loadout = lo ? lo->nickname : std::string();
    fs.player.equipment.clear();
    if (lo)
        for (auto& e : lo->equip) fs.player.equipment.push_back({e.item, e.hardpoint});
    refit(fs, db, vfs, r, args, spawn_npcs);
}

// The game's EXE folder (Freelancer.ini and the text DLLs): --exe, or EXE next to DATA.
std::filesystem::path find_ci(const std::filesystem::path& dir, std::string_view name) {
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (iequals(e.path().filename().string(), name)) return e.path();
    return {};
}

std::filesystem::path find_exe_dir(const Args& args) {
    if (!args.exe.empty()) return args.exe;
    auto data = std::filesystem::absolute(args.mounts[0]).lexically_normal();
    if (!data.has_filename()) data = data.parent_path();
    return find_ci(data.parent_path(), "EXE");
}

// Standing of a new player: the house lines of EXE/MPNewCharacter.FL, or else how the
// new character's faction (NewCharacter.ini rep_group) feels about everyone.
std::map<std::string, float> initial_rep(const std::filesystem::path& exe, const Factions& factions,
                                         const std::optional<NewGameProfile>& start) {
    if (!exe.empty())
        if (auto f = find_ci(exe, "mpnewcharacter.fl"); !f.empty()) {
            auto b = read_file(f);
            auto rep = read_house_lines(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
            if (!rep.empty()) return rep;
        }
    if (start)
        if (auto* g = factions.faction(start->rep_group)) return g->rep;
    return {};
}

// Puts a saved or new player into the world: system, ship, then docked or in space.
void apply_player(FlightSession& fs, game::PlayerState p, const ShipDatabase& db, const Vfs& vfs, Renderer& r, Camera& cam,
                  const Args& args) {
    if (!iequals(r.system().nickname, p.system)) r.load_system(p.system);
    fs.nav = FlightSession::Nav::Free;
    fs.lane.reset();
    std::string base = p.base;
    p.base.clear();
    fs.player = std::move(p);
    if (fs.player.equipment.empty()) set_ship(fs, db, vfs, r, args, fs.player.ship, fs.player.loadout, false);
    else refit(fs, db, vfs, r, args, false);
    fs.ship.vel = {};
    fs.input = {};
    if (!base.empty() && !fs.dock_at_base(r.system(), base)) SDL_Log("base %s not found in %s", base.c_str(), fs.player.system.c_str());
    if (base.empty() && fs.player.position) {
        fs.ship.pos = {fs.player.position->x, fs.player.position->y, fs.player.position->z};
        fs.cam_orient = fs.ship.orient;
    }
    fs.target = -1;
    fs.update_contacts(r.system());
    fs.update_camera(cam, 1.0);
}

std::string save_path(const Args& args) {
    if (!args.save_file.empty()) return args.save_file;
    char* pref = SDL_GetPrefPath("Sterrenvaarder", "Sterrenvaarder");
    std::string dir = pref ? pref : "";
    SDL_free(pref);
    return dir + "quicksave.ini";
}

void save_game(FlightSession& fs, const Renderer& r, const std::string& path) {
    game::PlayerState p = fs.player;
    p.system = r.system().nickname;
    if (fs.docked()) {
        p.position.reset();
    } else {
        p.base.clear();
        p.position = Vec3{static_cast<float>(fs.ship.pos.x), static_cast<float>(fs.ship.pos.y), static_cast<float>(fs.ship.pos.z)};
    }
    std::ofstream f(path, std::ios::binary);
    f << game::write_save(p);
    fs.trade_msg = f ? "GAME SAVED" : "SAVE FAILED";
    SDL_Log("%s: %s", fs.trade_msg.c_str(), path.c_str());
}

std::optional<game::PlayerState> read_save_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return game::read_save(text);
}


// Reads Freelancer.ini ([Data] file lists) and the text resources, if the EXE folder is there.
GameStrings load_strings(const std::filesystem::path& exe, DataFiles& files) {
    GameStrings gs;
    if (exe.empty()) {
        SDL_Log("no EXE folder found next to DATA (use --exe): names fall back to nicknames");
        return gs;
    }
    std::vector<std::string> dlls;
    if (auto ini = find_ci(exe, "freelancer.ini"); !ini.empty()) {
        auto b = read_file(ini);
        std::string_view text(reinterpret_cast<const char*>(b.data()), b.size());
        files.read_freelancer_ini(text);
        dlls = resource_dlls(text);
    }
    dlls.insert(dlls.begin(), "resources.dll");
    for (std::size_t i = 0; i < dlls.size(); ++i) {
        auto path = find_ci(exe, dlls[i]);
        if (path.empty()) continue;
        try {
            gs.add(static_cast<std::uint32_t>(i), PeResources::parse(read_file(path)));
        } catch (const std::exception& e) {
            SDL_Log("%s: %s", dlls[i].c_str(), e.what());
        }
    }
    SDL_Log("text resources: %zu strings from %s", gs.size(), exe.string().c_str());
    return gs;
}

// Completes a jump the navigation asked for: load the target system and appear in
// front of the destination gate or hole.
void handle_jump(FlightSession& fs, Renderer& renderer, Camera& cam) {
    if (fs.pending_jump_system.empty()) return;
    std::string sys = fs.pending_jump_system, obj = fs.pending_jump_object;
    fs.pending_jump_system.clear();
    fs.pending_jump_object.clear();
    try {
        renderer.load_system(sys);
    } catch (const std::exception& e) {
        SDL_Log("jump to %s failed: %s", sys.c_str(), e.what());
        return;
    }
    fs.npcs.clear();
    if (auto* o = game::find_object(renderer.system(), obj)) {
        auto sp = game::exit_point(*o, 600.f);
        fs.ship.pos = sp.pos;
        fs.ship.orient = sp.orient;
        fs.ship.vel = game::to_d(sp.orient.forward()) * 60.0;
        fs.cam_orient = sp.orient;
    }
    fs.target = -1;
    fs.update_contacts(renderer.system());
    fs.update_camera(cam, 1.0);
    SDL_Log("jumped to %s (%s)", sys.c_str(), obj.c_str());
}

void render_frame(Gpu& gpu, Renderer& renderer, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* target, Uint32 w, Uint32 h,
                  const Camera& cam, const RenderSettings& settings, double time, FlightSession* fs, const game::HudConfig& hud_cfg,
                  float cursor_x, float cursor_y, HudBatch& hud) {
    (void)gpu;
    const Camera* view = &cam;
    renderer.set_room(nullptr);
    if (fs && fs->docked() && fs->vfs && fs->petal) {
        if (!iequals(fs->room.base, fs->player.base)) fs->room.load_base(*fs->vfs, fs->player.base);
        fs->room.show(*fs->vfs, *fs->petal, renderer, fs->room_for_tab());
        if (fs->shipdb) fs->room.set_ships(*fs->shipdb, renderer, fs->dealer_ships());
    }
    bool in_room = fs && fs->docked() && fs->room.active();
    if (in_room) {
        renderer.set_room(&fs->room.lighting);
        fs->room.draw(renderer);
        view = &fs->room.cam;
    }
    if (fs) {
        if (!in_room) fs->draw(renderer);
        hud.clear();
        auto layout = game::HudLayout::make(static_cast<float>(w), static_cast<float>(h), hud_cfg.max_aspect, hud_cfg.scale);
        // Docked: only the base screen, as the original hides the flight HUD at bases.
        bool base_screen = fs->docked() && fs->econ;
        if (base_screen) game::build_base_screen(hud, layout, fs->base_screen(renderer.system()));
        else game::build_flight_hud(hud, layout, hud_cfg, cam, fs->ship.pos, fs->hud_state(renderer.system(), cursor_x, cursor_y));
        renderer.set_hud(&hud);
        if (!base_screen && hud_cfg.style == game::HudStyle::Classic)
            for (auto& d : game::classic_hud_models(layout, hud_cfg, fs->win_fovx))
                renderer.draw_hud_model(fs->hud_model(renderer, d.model), d.px, d.py, d.ppu, d.tint);
    }
    renderer.render(cmd, target, w, h, *view, settings, time);
    renderer.set_room(nullptr);
}

int run(const Args& args) {
    Vfs vfs;
    for (std::size_t i = 0; i < args.mounts.size(); ++i) vfs.mount(args.mounts[i], i == 0 ? "base" : args.mounts[i]);
    Universe universe(vfs);
    DataFiles files;
    const auto exe_dir = find_exe_dir(args);
    GameStrings strings = load_strings(exe_dir, files);
    Factions factions(vfs);
    PetalDb petal(vfs);
    PilotDb pilots(vfs);

    const bool offscreen = !args.screenshot.empty();
    if (offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());

    SDL_Window* window = nullptr;
    if (!offscreen) {
        window = SDL_CreateWindow("Sterrenvaarder", args.width, args.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window) throw std::runtime_error(SDL_GetError());
    }
    int rc = 0;
    {
        Gpu gpu(window);
        SDL_Log("GPU driver: %s", gpu.driver());
        Renderer renderer(gpu, vfs, universe);
        Uint64 t0 = SDL_GetTicksNS();
        renderer.load_system(args.system);
        SDL_Log("loaded %s in %.2f s: %zu objects", args.system.c_str(), (SDL_GetTicksNS() - t0) / 1e9,
                renderer.system().objects.size());

        Camera cam;
        place_default_camera(cam, renderer.system());
        if (args.has_camera) cam.pos = args.camera;
        if (args.has_look) cam.look_at(args.look);
        RenderSettings settings = args.settings;

        std::unique_ptr<ShipDatabase> ships;
        std::unique_ptr<EffectDb> fx_db;
        std::unique_ptr<Economy> econ;
        std::unique_ptr<FlightSession> fs;
        if (!args.free) {
            ships = std::make_unique<ShipDatabase>(vfs, files);
            econ = std::make_unique<Economy>(vfs, files);
            fs = std::make_unique<FlightSession>();
            fs->econ = econ.get();
            fs->strings = &strings;
            fs->factions = &factions;
            fs->vfs = &vfs;
            fs->petal = &petal;
            fs->shipdb = ships.get();
            fs->pilots = &pilots;
            auto fx_files = files.effects;
            fx_files.insert(fx_files.end(), files.explosions.begin(), files.explosions.end());
            fx_db = std::make_unique<EffectDb>(vfs, fx_files);
            fx_db->add_fuses(files.fuses);
            fs->fxdb = fx_db.get();
            renderer.add_effect_textures("fx/beam.txm");
            auto start = read_new_character(vfs);
            fs->player.credits = start ? start->money : 2000;
            const auto start_rep = initial_rep(exe_dir, factions, start);
            fs->player.rep = start_rep;
            // Field of view: the chase camera's fovx from cameras.ini was defined for 4:3.
            // Keep that vertical angle and let wider screens see more to the sides (Hor+).
            float fovx = 70;
            if (vfs.exists("cameras.ini"))
                for (auto& sec : parse_ini(vfs.read("cameras.ini")))
                    if (auto* e = sec.find("fovx"); e && !e->values.empty()) {
                        if (iequals(sec.name, "ChaseCamera")) fovx = e->values[0].as_float();
                        else if (iequals(sec.name, "WinCamera")) fs->win_fovx = e->values[0].as_float();
                    }
            if (args.fov > 0) fovx = std::clamp(args.fov, 40.f, 120.f);
            cam.fov_y = 2.f * std::atan(std::tan(fovx * 3.14159265f / 360.f) * 0.75f);
            fs->universe = &universe;
            fs->spawn(renderer.system(), args);
            set_ship(*fs, *ships, vfs, renderer, args, args.ship, {}, true);
            fs->player.system = renderer.system().nickname;
            if (!args.load.empty()) {
                auto p = read_save_file(args.load);
                if (!p) throw std::runtime_error("cannot read save " + args.load);
                if (p->rep.empty()) p->rep = start_rep;
                apply_player(*fs, *p, *ships, vfs, renderer, cam, args);
            } else if (args.new_game) {
                if (!start) throw std::runtime_error("characters/newcharacter.ini not found");
                game::PlayerState p;
                p.credits = start->money;
                p.base = start->base;
                p.system = econ->base(start->base) ? econ->base(start->base)->system : args.system;
                p.ship = args.ship_given ? args.ship : start->ship;
                p.loadout = args.ship_given ? std::string() : start->loadout;
                p.rep = start_rep;
                apply_player(*fs, p, *ships, vfs, renderer, cam, args);
            }
            if (args.credits >= 0) fs->player.credits = args.credits;
            if (args.rank >= 0) fs->player.rank = args.rank;
            for (auto& [g, v] : args.reps) fs->player.rep[g] = v;
            SDL_Log("factions: %zu, player standing with %zu", factions.size(), fs->player.rep.size());
            fs->update_contacts(renderer.system());
            if (!fs->docked()) fs->next_target();
            fs->update_camera(cam, 1.0);
            SDL_Log("ship %s: top speed %.0f m/s, cruise %.0f m/s", fs->arch->nickname.c_str(), fs->model.params().max_speed(),
                    fs->model.params().cruise_speed);
        }
        HudBatch hud;
        // HUD layout: built-in defaults, then sterrenvaarder/hud.ini from the data layers
        // (so a mod can ship one), then --hud-config, then --hud-aspect.
        game::HudConfig hud_cfg;
        auto apply_hud = [&](std::string_view text, const std::string& from) {
            for (auto& w : hud_cfg.apply_ini(text)) SDL_Log("%s: %s", from.c_str(), w.c_str());
        };
        if (vfs.exists("sterrenvaarder/hud.ini")) {
            auto b = vfs.read("sterrenvaarder/hud.ini");
            apply_hud(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()), "hud.ini");
        }
        if (!args.hud_config.empty()) {
            std::ifstream f(args.hud_config, std::ios::binary);
            if (!f) throw std::runtime_error("cannot read " + args.hud_config);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            apply_hud(text, args.hud_config);
        }
        if (args.hud_aspect >= 0) hud_cfg.max_aspect = args.hud_aspect;

        if (offscreen) {
            float cx = args.width * 0.5f + args.cursor_x * args.height * 0.5f;
            float cy = args.height * 0.5f + args.cursor_y * args.height * 0.5f;
            if (fs) {
                // Scripted flight so screenshots show the ship in motion.
                fs->input.throttle = args.throttle;
                fs->input.cruise = args.cruise;
                fs->input.steer = game::mouse_steer(args.cursor_x, args.cursor_y);
                fs->firing = args.fire;
                if (!args.dock.empty()) {
                    for (std::size_t i = 0; i < fs->contacts.size(); ++i) fs->contacts[i].selected = false;
                    fs->contacts.push_back({args.dock, {}, 0, true});
                    fs->target = static_cast<int>(fs->contacts.size() - 1);
                    fs->dock_request(renderer.system());
                    SDL_Log("autopilot to %s: %s", args.dock.c_str(), fs->nav_status().c_str());
                }
                for (double t = 0; t < args.sim; t += 1.0 / 60) {
                    bool nav_owns = fs->update_nav(renderer.system(), 1.0 / 60);
                    if (!nav_owns) fs->model.update(fs->ship, fs->input, 1.0 / 60);
                    handle_jump(*fs, renderer, cam);
                    fs->update_camera(cam, 1.0 / 60);
                    fs->aim_dir = aim_through(cam, cx, cy, static_cast<float>(args.width), static_cast<float>(args.height));
                    if (args.npc_die >= 0 && t < args.npc_die && t + 1.0 / 60 >= args.npc_die)
                        for (auto& n : fs->npcs) n.body.hull = 0;
                    fs->update_combat(1.0 / 60);
                }
                for (auto& [good, n] : args.buys) {
                    const Good* g = econ->good(good);
                    game::TradeResult res;
                    if (g && g->category == GoodCategory::Ship) res = game::buy_ship(*econ, fs->player, good);
                    else if (g && g->category == GoodCategory::Equipment && n >= 0) res = game::buy_equipment(*econ, fs->player, good);
                    else if (g && g->category == GoodCategory::Equipment) {
                        res = game::TradeResult::NoneOwned;
                        for (std::size_t i = 0; i < fs->player.equipment.size(); ++i)
                            if (iequals(fs->player.equipment[i].item, good)) {
                                res = game::sell_equipment(*econ, fs->player, i);
                                break;
                            }
                    } else res = n >= 0 ? game::buy(*econ, fs->player, good, n) : game::sell(*econ, fs->player, good, -n);
                    if (res == game::TradeResult::Ok && g && g->category != GoodCategory::Commodity)
                        refit(*fs, *ships, vfs, renderer, args, false);
                    fs->trade_msg = game::trade_message(res);
                    SDL_Log("trade %s %d: %s, credits %lld", good.c_str(), n, fs->trade_msg.c_str(), static_cast<long long>(fs->player.credits));
                }
                if (fs->docked() && args.tab > 0) {
                    std::string msg = fs->trade_msg;
                    fs->set_tab(static_cast<FlightSession::Tab>(std::clamp(args.tab, 0, 2)));
                    fs->trade_msg = msg;
                }
                if (!args.save_file.empty()) save_game(*fs, renderer, args.save_file);
                fs->update_contacts(renderer.system());
                SDL_Log("after %.1f s: %s in %s at %.0f, %.0f, %.0f", args.sim, fs->nav_status().c_str(),
                        renderer.system().nickname.c_str(), fs->ship.pos.x, fs->ship.pos.y, fs->ship.pos.z);
                if (!fs->npcs.empty()) SDL_Log("player hull %.0f / %.0f, shield %.0f; %d shield hits", fs->body.hull, fs->body.max_hull, fs->body.shield, fs->shield_hits);
                for (auto& n : fs->npcs)
                    SDL_Log("  %s: %s, hull %.0f / %.0f, %.0f m away", n.name.c_str(),
                            !n.body.alive() ? "destroyed" : n.ai ? std::string(game::FighterAi::name(n.ai->state())).c_str() : "test AI",
                            n.body.hull, n.body.max_hull, (n.ship.pos - fs->ship.pos).length());
            }
            renderer.set_output_format(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            auto* out = gpu.render_target(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, args.width, args.height);
            auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
            render_frame(gpu, renderer, cmd, out, args.width, args.height, cam, settings, args.sim, fs.get(), hud_cfg, cx, cy,
                         hud);
            SDL_SubmitGPUCommandBuffer(cmd);
            Image img;
            img.width = args.width;
            img.height = args.height;
            img.rgba = gpu.download_rgba8(out, args.width, args.height);
            auto png = encode_png(img);
            std::ofstream(args.screenshot, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
            auto& st = renderer.stats();
            SDL_Log("wrote %s (%dx%d): %d objects drawn, %d beyond draw distance, %d draw calls, %d triangles",
                    args.screenshot.c_str(), args.width, args.height, st.objects_drawn, st.culled_by_lod, st.draw_calls, st.triangles);
            if (fs) SDL_Log("ship speed %.1f m/s", fs->ship.speed());
            SDL_ReleaseGPUTexture(gpu.device(), out);
        } else {
            renderer.set_output_format(SDL_GetGPUSwapchainTextureFormat(gpu.device(), window));
            bool running = true, looking = false;
            double speed = 300.0;
            Uint64 last = SDL_GetTicksNS();
            double time = 0;
            while (running) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT) running = false;
                    else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
                    else if (e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_F11 || (args.free && e.key.key == SDLK_F)))
                        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
                    else if (fs && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                        if (e.key.key == SDLK_SPACE) fs->mouse_flight = !fs->mouse_flight;
                        else if (e.key.key == SDLK_T) fs->next_target();
                        else if (e.key.key == SDLK_F3) fs->dock_request(renderer.system());
                        else if (e.key.key == SDLK_F5) save_game(*fs, renderer, save_path(args));
                        else if (e.key.key == SDLK_F9) {
                            if (auto p = read_save_file(save_path(args))) {
                                apply_player(*fs, *p, *ships, vfs, renderer, cam, args);
                                fs->trade_msg = "GAME LOADED";
                            }
                        } else if (fs->docked() && (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN)) {
                            fs->select_row(e.key.key == SDLK_UP ? -1 : 1);
                        } else if (fs->docked() && e.key.key >= SDLK_1 && e.key.key <= SDLK_3) {
                            fs->set_tab(static_cast<FlightSession::Tab>(e.key.key - SDLK_1));
                        } else if (fs->docked() && (e.key.key == SDLK_RIGHT || e.key.key == SDLK_LEFT)) {
                            fs->trade(e.key.key == SDLK_RIGHT ? 1 : -1, (e.key.mod & SDL_KMOD_SHIFT) != 0);
                            if (fs->refit_needed) {
                                fs->refit_needed = false;
                                refit(*fs, *ships, vfs, renderer, args, false);
                            }
                        }
                        else if (e.key.key == SDLK_Z) { fs->input.throttle = 0; fs->input.cruise = false; }
                        else if (e.key.key == SDLK_W && (e.key.mod & SDL_KMOD_SHIFT)) fs->input.cruise = !fs->input.cruise;
                        else if (e.key.key == SDLK_S) fs->input.cruise = false;
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = true;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
                        looking = false;
                        SDL_SetWindowRelativeMouseMode(window, false);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                        cam.yaw -= e.motion.xrel * 0.003f;
                        cam.pitch = std::clamp(cam.pitch - e.motion.yrel * 0.003f, -1.55f, 1.55f);
                    } else if (!fs && e.type == SDL_EVENT_MOUSE_WHEEL) {
                        speed = std::clamp(speed * (e.wheel.y > 0 ? 1.25 : 0.8), 10.0, 200000.0);
                    }
                }
                Uint64 now = SDL_GetTicksNS();
                double dt = (now - last) / 1e9;
                last = now;
                time += dt;
                const bool* keys = SDL_GetKeyboardState(nullptr);

                int ww = 0, wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                float mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                if (fs) {
                    auto& in = fs->input;
                    bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
                    if (keys[SDL_SCANCODE_W] && !shift) in.throttle = std::min(1.f, in.throttle + static_cast<float>(dt) * 1.2f);
                    if (keys[SDL_SCANCODE_S]) in.throttle = std::max(-1.f, in.throttle - static_cast<float>(dt) * 1.2f);
                    in.strafe_x = (keys[SDL_SCANCODE_D] ? 1.f : 0.f) - (keys[SDL_SCANCODE_A] ? 1.f : 0.f);
                    float roll = (keys[SDL_SCANCODE_Q] ? 1.f : 0.f) - (keys[SDL_SCANCODE_E] ? 1.f : 0.f);
                    float half = wh > 0 ? wh * 0.5f : 1.f;
                    in.steer = fs->mouse_flight ? game::mouse_steer((mx - ww * 0.5f) / half, (my - wh * 0.5f) / half) : Vec3{};
                    in.steer.z = roll;
                    bool nav_owns = fs->update_nav(renderer.system(), dt);
                    if (!nav_owns) fs->model.update(fs->ship, in, dt);
                    handle_jump(*fs, renderer, cam);
                    fs->update_camera(cam, dt);
                    fs->firing = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
                    fs->aim_dir = aim_through(cam, mx, my, static_cast<float>(ww), static_cast<float>(wh));
                    fs->update_combat(dt);
                    fs->update_contacts(renderer.system());
                } else {
                    double v = speed * dt * (keys[SDL_SCANCODE_LSHIFT] ? 10.0 : 1.0);
                    Vec3 f = cam.forward(), r = cam.right();
                    auto move = [&](Vec3 d, double amt) { cam.pos = cam.pos + DVec3{d.x, d.y, d.z} * amt; };
                    if (keys[SDL_SCANCODE_W]) move(f, v);
                    if (keys[SDL_SCANCODE_S]) move(f, -v);
                    if (keys[SDL_SCANCODE_D]) move(r, v);
                    if (keys[SDL_SCANCODE_A]) move(r, -v);
                    if (keys[SDL_SCANCODE_E]) move({0, 1, 0}, v);
                    if (keys[SDL_SCANCODE_Q]) move({0, 1, 0}, -v);
                }

                auto* cmd = SDL_AcquireGPUCommandBuffer(gpu.device());
                SDL_GPUTexture* swap = nullptr;
                Uint32 w = 0, h = 0;
                if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &w, &h) && swap && w && h) {
                    // Mouse is in window points; the swapchain may be in pixels (HiDPI).
                    float sx = ww > 0 ? static_cast<float>(w) / ww : 1.f, sy = wh > 0 ? static_cast<float>(h) / wh : 1.f;
                    render_frame(gpu, renderer, cmd, swap, w, h, cam, settings, time, fs.get(), hud_cfg, mx * sx, my * sy,
                                 hud);
                }
                SDL_SubmitGPUCommandBuffer(cmd);
            }
        }
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
