// SPDX-License-Identifier: MPL-2.0
// Unit tests that need no game data. Run: sterrenvaarder_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/png.hpp"
#include "sterrenvaarder/texture.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vfs.hpp"

using namespace sterrenvaarder;

static int failures = 0;
#include "game/combat.hpp"
#include "game/flight.hpp"
#include "game/navigation.hpp"

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
static void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }

static void test_hash() {
    // Pairs observed in the retail data (name stored in a file, hash stored elsewhere).
    CHECK(fl_model_crc("metal03") == 0x1B4B96E1u);
    CHECK(fl_model_crc("metal03D") == 0xE3CBD3B4u);
    CHECK(fl_model_crc("data.ships.liberty.li_elite.li_elite.lod0-112.vms") == 0xE188C9BBu);
    CHECK(fl_model_crc("DATA.SHIPS.LIBERTY.LI_ELITE.LI_ELITE.LOD1-112.VMS") == 0x0CF9309Fu);
    CHECK((fl_create_id("li_elite") & 0x80000000u) != 0);
}

static void test_text_ini() {
    auto ini = parse_text_ini("; comment\n[Ship]\nnickname = li_elite ; trailing\nLODranges = 0, 75, 150.5\n[Ship]\nnickname = x\n");
    CHECK(ini.size() == 2);
    CHECK(ini[0].find("NICKNAME")->values[0].as_string() == "li_elite");
    auto* lod = ini[0].find("lodranges");
    CHECK(lod && lod->values.size() == 3);
    CHECK(lod->values[1].type == IniValue::Type::Int && lod->values[1].i == 75);
    CHECK(lod->values[2].type == IniValue::Type::Float && lod->values[2].f == 150.5f);
}

static void test_bini() {
    std::string strs = std::string("Ship\0nickname\0li_elite\0mass\0", 28);
    std::vector<std::uint8_t> b = {'B', 'I', 'N', 'I'};
    put32(b, 1);
    std::vector<std::uint8_t> body;
    put16(body, 0); put16(body, 2);                       // [Ship], 2 entries
    put16(body, 5); body.push_back(1);                    // nickname, 1 value
    body.push_back(3); put32(body, 14);                   //   string "li_elite"
    put16(body, 23); body.push_back(2);                   // mass, 2 values
    body.push_back(1); put32(body, 7);                    //   int 7
    float f = 2.5f; std::uint32_t fb; std::memcpy(&fb, &f, 4);
    body.push_back(2); put32(body, fb);                   //   float 2.5
    put32(b, static_cast<std::uint32_t>(12 + body.size()));
    b.insert(b.end(), body.begin(), body.end());
    b.insert(b.end(), strs.begin(), strs.end());
    auto ini = parse_ini(b);
    CHECK(ini.size() == 1 && ini[0].name == "Ship");
    CHECK(ini[0].find("nickname")->values[0].s == "li_elite");
    CHECK(ini[0].find("mass")->values[0].i == 7);
    CHECK(ini[0].find("mass")->values[1].f == 2.5f);
    CHECK(to_text(ini) == "[Ship]\nnickname = li_elite\nmass = 7, 2.5\n\n");
}

static void test_utf() {
    // Root "\" with one leaf "Type" holding "DcDt".
    std::vector<std::uint8_t> b = {'U', 'T', 'F', ' '};
    std::string names = std::string("\\\0Type\0", 7);
    std::uint32_t tree_off = 56, tree_size = 88, names_off = tree_off + tree_size;
    std::uint32_t data_off = names_off + static_cast<std::uint32_t>(names.size()) + 1;
    put32(b, 0x101); put32(b, tree_off); put32(b, tree_size); put32(b, 0); put32(b, 44);
    put32(b, names_off); put32(b, 8); put32(b, 8); put32(b, data_off);
    while (b.size() < tree_off) b.push_back(0);
    // root dir node
    put32(b, 0); put32(b, 0); put32(b, 0x10); put32(b, 0); put32(b, 44); put32(b, 0); put32(b, 0); put32(b, 0);
    put32(b, 0); put32(b, 0); put32(b, 0);
    // leaf
    put32(b, 0); put32(b, 2); put32(b, 0x80); put32(b, 0); put32(b, 0); put32(b, 5); put32(b, 5); put32(b, 5);
    put32(b, 0); put32(b, 0); put32(b, 0);
    b.insert(b.end(), names.begin(), names.end());
    b.push_back(0);
    const char* payload = "DcDt";
    b.insert(b.end(), payload, payload + 5);
    auto f = UtfFile::parse(b);
    CHECK(f->root().children.size() == 1);
    CHECK(f->root().find("type") && f->root().find("type")->as_string() == "DcDt");
}

static void test_vfs_normalize() {
    CHECK(Vfs::normalize("SHIPS\\Liberty\\..\\liberty\\li_elite.CMP") == "ships/liberty/li_elite.cmp");
    CHECK(Vfs::normalize("\\\\data//x") == "data/x");
}

static void test_vfs_layers() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_vfs_test";
    fs::remove_all(root);
    fs::create_directories(root / "base" / "SHIPS");
    fs::create_directories(root / "mod" / "ships");
    std::ofstream(root / "base" / "SHIPS" / "a.ini") << "[Ship]\nnickname = base\n";
    std::ofstream(root / "mod" / "ships" / "A.INI") << "broken";
    Vfs vfs;
    vfs.mount(root / "base", "base");
    vfs.mount(root / "mod", "mod");
    CHECK(vfs.layer_of("ships\\a.ini") == std::optional<std::string>("mod"));
    CHECK(vfs.resolve_all("ships/a.ini").size() == 2);
    // The mod's copy fails to load, so the original is used.
    auto nick = vfs.load_with_fallback("ships/a.ini", [](std::vector<std::uint8_t> b) {
        auto ini = parse_ini(b);
        if (ini.empty()) throw std::runtime_error("empty");
        return ini[0].find("nickname")->values[0].as_string();
    });
    CHECK(nick == "base");
    fs::remove_all(root);
}

static void test_png() {
    Image img;
    img.width = 2;
    img.height = 2;
    img.rgba.assign(16, 200);
    auto png = encode_png(img);
    CHECK(png.size() > 33 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
}

void test_flight() {
    using namespace sterrenvaarder::game;
    ShipArchetype ship;  // values of a typical light fighter
    ship.mass = 150;
    ship.linear_drag = 1;
    ship.steering_torque = {50000, 50000, 230000};
    ship.angular_drag = {40000, 40000, 141000};
    ship.rotation_inertia = {8400, 8400, 8400};
    EngineArchetype engine;
    engine.max_force = 48000;
    engine.linear_drag = 599;
    FlightModel fm(FlightParams::from(ship, &engine, 300));
    CHECK(std::fabs(fm.params().max_speed() - 80.f) < 0.01f);
    ShipState s;
    FlightInput in;
    in.throttle = 1;
    for (int i = 0; i < 100; ++i) fm.update(s, in, 0.1);  // update() caps one call at 0.25 s
    CHECK(std::fabs(s.speed() - 80.f) < 0.5f);
    CHECK(s.pos.z < -500);  // flew forward along -Z
    in.cruise = true;
    for (int i = 0; i < 30 * 10; ++i) fm.update(s, in, 0.1);
    CHECK(s.cruising && std::fabs(s.speed() - 300.f) < 2.f);
    // Yaw: turn rate saturates at torque / drag (1.25 rad/s).
    ShipState t;
    FlightInput turn;
    turn.steer = {0, 1, 0};
    for (int i = 0; i < 30; ++i) fm.update(t, turn, 0.1);
    CHECK(std::fabs(t.ang_vel.y - 1.25f) < 0.02f);
    CHECK(t.orient.forward().x < -0.1f || t.orient.forward().z > 0);  // nose swung left
    CHECK(mouse_steer(0.01f, 0).y == 0);  // dead zone
    CHECK(mouse_steer(1, 0).y < -0.99f && mouse_steer(0, -1).x > 0.99f);
}

void test_combat() {
    using namespace sterrenvaarder::game;
    ShieldArchetype sh;
    sh.max_capacity = 600;
    sh.regeneration_rate = 13.3f;
    sh.offline_rebuild_time = 12;
    sh.offline_threshold = 0.15f;
    Combatant c;
    c.id = 2;
    c.radius = 10;
    c.hull = c.max_hull = 1600;
    c.set_shield(&sh);
    c.damage(16.3f, 0);
    CHECK(std::fabs(c.shield - 583.7f) < 0.01f && c.hull == 1600);
    c.damage(1000, 0);  // collapses the shield, hull untouched by that hit
    CHECK(c.shield == 0 && c.hull == 1600 && c.shield_offline == 12);
    c.damage(100, 50);  // shield down: hull takes hull damage only
    CHECK(c.hull == 1500);
    for (int i = 0; i < 121; ++i) c.update(0.1f);
    CHECK(c.shield > 89 && c.shield < 92);  // back at 15 % (+ a little regeneration)

    GunArchetype ga;
    ga.refire_delay = 0.12f;
    ga.muzzle_velocity = 750;
    MunitionArchetype mu;
    mu.hull_damage = 16.3f;
    mu.lifetime = 0.8f;
    std::vector<Gun> guns{{&ga, &mu, {1, 0, 0}, 0}, {&ga, &mu, {-1, 0, 0}, 0}};
    CombatWorld w;
    Quat q;
    w.fire(guns, 1, {0, 0, 0}, q, {0, 0, 0}, {0, 0, -1});
    CHECK(w.projectiles().size() == 2);
    w.fire(guns, 1, {0, 0, 0}, q, {0, 0, 0}, {0, 0, -1});  // still cooling down
    CHECK(w.projectiles().size() == 2);
    Combatant target;
    target.id = 2;
    target.pos = {0, 0, -300};
    target.radius = 5;
    target.hull = 100;
    std::vector<Combatant*> ts{&target};
    int hits = 0;
    for (int i = 0; i < 60; ++i) w.update(1.f / 60, ts, [&](Combatant&, const Projectile&) { ++hits; });
    CHECK(hits == 2 && std::fabs(target.hull - (100 - 2 * 16.3f)) < 0.01f);
    CHECK(w.projectiles().empty());
}

void test_lanes() {
    using namespace sterrenvaarder::game;
    StarSystem sys;
    auto ring = [&](const char* n, float z, const char* next, const char* prev) {
        SystemObject o;
        o.nickname = n;
        o.pos = {0, 0, z};
        o.next_ring = next;
        o.prev_ring = prev;
        sys.objects.push_back(o);
    };
    ring("r1", 0, "r2", "");
    ring("r2", -7000, "r3", "r1");
    ring("r3", -14000, "", "r2");
    auto t = LaneTrip::start(sys, sys.objects[0], 2000);
    CHECK(t && t->rings().size() == 3 && t->direction().z < -0.99f);
    int steps = 0;
    while (t->update(0.1) && steps < 1000) ++steps;
    CHECK(steps == 69 && std::fabs(t->position().z + 14000) < 1e-6);  // 14 km at 2 km/s
    auto back = LaneTrip::start(sys, sys.objects[2], 2000);  // far end runs the other way
    CHECK(back && back->rings().size() == 3 && back->direction().z > 0.99f);
    SystemObject gate;
    gate.pos = {100, 0, 0};
    gate.rotate_deg = {0, 180, 0};
    auto sp = exit_point(gate, 400);
    CHECK(std::fabs(sp.pos.z - 400) < 0.01 && std::fabs(sp.pos.x - 100) < 0.01);
}

int main() {
    test_flight();
    test_lanes();
    test_combat();
    test_hash();
    test_text_ini();
    test_bini();
    test_utf();
    test_vfs_normalize();
    test_vfs_layers();
    test_png();
    std::printf(failures ? "%d test(s) failed\n" : "all unit tests passed\n", failures);
    return failures ? 1 : 0;
}
