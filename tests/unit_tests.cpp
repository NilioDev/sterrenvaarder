// SPDX-License-Identifier: MPL-2.0
// Unit tests that need no game data. Run: sterrenvaarder_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/png.hpp"
#include "sterrenvaarder/postfx.hpp"
#include "sterrenvaarder/fonts.hpp"
#include "sterrenvaarder/texture.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vfs.hpp"

using namespace sterrenvaarder;

static int failures = 0;
#include "game/ai.hpp"
#include "game/combat.hpp"
#include "game/flight.hpp"
#include "game/navigation.hpp"
#include "game/particles.hpp"
#include "game/mission.hpp"
#include "game/vibes.hpp"
#include "game/population.hpp"
#include "game/skin.hpp"
#include "game/thn_player.hpp"
#include "game/mixer.hpp"
#include "game/music.hpp"
#include "game/radio.hpp"
#include "game/trade.hpp"
#include "sterrenvaarder/economy.hpp"
#include "sterrenvaarder/factions.hpp"
#include "sterrenvaarder/ale.hpp"
#include "sterrenvaarder/effects.hpp"
#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/lua32.hpp"
#include "sterrenvaarder/population.hpp"
#include "sterrenvaarder/dfm.hpp"
#include "sterrenvaarder/mbases.hpp"
#include "sterrenvaarder/audio.hpp"
#include "sterrenvaarder/sounds.hpp"
#include "sterrenvaarder/voices.hpp"
#include "sterrenvaarder/anm.hpp"
#include "sterrenvaarder/characters.hpp"
#include "sterrenvaarder/pilots.hpp"
#include "sterrenvaarder/resources.hpp"
#include "sterrenvaarder/thn.hpp"

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
    // A hull floor (mission Invulnerable) stops hits there: the ship cannot die.
    Combatant tough;
    tough.hull = tough.max_hull = 1000;
    tough.hull_floor = 900;
    tough.damage(50, 0);
    CHECK(tough.hull == 950);
    tough.damage(500, 0);
    CHECK(tough.hull == 900 && tough.alive());
    tough.player_floor = 1;  // the player's hits go past the floor, but cannot destroy it
    tough.damage(500, 0, 1, true);
    CHECK(tough.hull == 400);
    tough.damage(5000, 0, 1, true);
    CHECK(tough.hull == 1 && tough.alive());
    tough.hull_floor = 0;
    tough.damage(5000, 0);
    CHECK(!tough.alive());

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

    // WeaponModDB: a weapon type hits each shield type with its own multiplier.
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_moddb_test";
    fs::remove_all(root);
    fs::create_directories(root / "equipment");
    std::ofstream(root / "equipment/weaponmoddb.ini")
        << "[WeaponType]\nnickname = W_Laser01\nshield_mod = S_Graviton01, 0.8\nshield_mod = S_Positron01, 1.2\n";
    std::ofstream(root / "equipment/st_equip.ini") << "[ShieldGenerator]\nnickname = sh1\nshield_type = S_Positron01\nmax_capacity = 100\n";
    std::ofstream(root / "equipment/weapon_equip.ini") << "[Gun]\nnickname = g1\nweapon_type = W_Laser01\n";
    std::ofstream(root / "equipment/select_equip.ini") << "[Armor]\nnickname = armor_scale_3\nhit_pts_scale = 1.6\n";
    {
        Vfs vfs;
        vfs.mount(root, "base");
        ShipDatabase db(vfs, DataFiles{});
        CHECK(db.shield_mod("w_laser01", "S_GRAVITON01") == 0.8f && db.shield_mod("W_Laser01", "S_Molecular01") == 1.f);
        const ShieldArchetype* s1 = db.shield("sh1");
        const GunArchetype* g1 = db.gun("g1");
        CHECK(s1 && s1->shield_type == "S_Positron01" && g1 && g1->weapon_type == "W_Laser01");
        Loadout lo;
        lo.equip.push_back({"Armor_Scale_3", ""});
        CHECK(db.armor_scale_of(lo) == 1.6f && db.armor_scale_of(Loadout{}) == 1.f);
        if (s1 && g1) {
            Combatant t2;
            t2.id = 3;
            t2.pos = {0, 0, -100};
            t2.radius = 5;
            t2.set_shield(s1);
            MunitionArchetype m2;
            m2.hull_damage = 10;
            m2.lifetime = 1;
            std::vector<Gun> g{{g1, &m2, {0, 0, 0}, 0}};
            CombatWorld w2;
            w2.set_shield_mods(&db);
            w2.fire(g, 1, {0, 0, 0}, q, {0, 0, 0}, {0, 0, -1});
            std::vector<Combatant*> t2s{&t2};
            for (int i = 0; i < 30; ++i) w2.update(1.f / 60, t2s);
            CHECK(std::fabs(t2.shield - (100 - 12)) < 0.01f);
        }
    }
    fs::remove_all(root);
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
    // Toward a ring two along, from the middle: that way, stopping there.
    auto mid = LaneTrip::start(sys, sys.objects[1], 2000, "r1");
    CHECK(mid && mid->rings().size() == 2 && mid->ring_names().back() == "r1" && mid->direction().z > 0.99f);
    auto far = LaneTrip::start(sys, sys.objects[0], 2000, "r3");
    CHECK(far && far->rings().size() == 3 && far->ring_names().back() == "r3");
    auto stop = LaneTrip::start(sys, sys.objects[0], 2000, "r2");
    CHECK(stop && stop->rings().size() == 2 && stop->ring_names().back() == "r2");
    SystemObject gate;
    gate.pos = {100, 0, 0};
    gate.rotate_deg = {0, 180, 0};
    auto sp = exit_point(gate, 400);
    CHECK(std::fabs(sp.pos.z - 400) < 0.01 && std::fabs(sp.pos.x - 100) < 0.01);
    // A planet's dock ring is left on the side away from the planet (its +Z).
    SystemObject dock_ring;
    dock_ring.archetype = "dock_ring";
    dock_ring.dock_with = "X_Base";
    auto lp = launch_point(dock_ring, 300);
    CHECK(std::fabs(lp.pos.z - 300) < 0.01 && lp.orient.forward().z > 0.99f);
    auto station = launch_point(gate, 400);
    CHECK(std::fabs(station.pos.z - sp.pos.z) < 0.01);  // other objects: as exit_point
}

static void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) { b[o] = v & 0xFF; b[o + 1] = v >> 8; }
static void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) { put16(b, o, v & 0xFFFF); put16(b, o + 2, v >> 16); }

// A minimal PE32 with one string block (ids 16..31) in a .rsrc section.
static void test_resources() {
    // Infocard markup to plain text.
    CHECK(rdl_to_text("<?xml version=\"1.0\"?><RDL><PUSH/><TEXT>Fly to the &amp; point.</TEXT><PARA/>"
                      "<TEXT>Then &lt;dock&gt;.</TEXT><PARA/><POP/></RDL>") == "Fly to the & point.\nThen <dock>.");
    CHECK(rdl_to_text("plain words") == "plain words");
    std::vector<std::uint8_t> pe(0x200 + 0x100, 0);
    pe[0] = 'M'; pe[1] = 'Z';
    put32(pe, 0x3C, 0x40);
    put32(pe, 0x40, 0x00004550);
    std::size_t coff = 0x44, opt = coff + 20;
    put16(pe, coff + 2, 1);       // sections
    put16(pe, coff + 16, 224);    // optional header size
    put16(pe, opt, 0x10B);        // PE32
    put32(pe, opt + 92, 16);      // data directories
    put32(pe, opt + 96 + 16, 0x1000);  // resource directory rva
    std::size_t sec = opt + 224;
    std::memcpy(&pe[sec], ".rsrc", 5);
    put32(pe, sec + 8, 0x100);    // virtual size
    put32(pe, sec + 12, 0x1000);  // virtual address
    put32(pe, sec + 16, 0x100);   // raw size
    put32(pe, sec + 20, 0x200);   // raw pointer
    std::size_t r = 0x200;
    put16(pe, r + 14, 1); put32(pe, r + 16, 6); put32(pe, r + 20, 0x80000018);               // RT_STRING
    put16(pe, r + 0x18 + 14, 1); put32(pe, r + 0x28, 2); put32(pe, r + 0x2C, 0x80000030);   // block 2
    put16(pe, r + 0x30 + 14, 1); put32(pe, r + 0x40, 0x409); put32(pe, r + 0x44, 0x48);     // language
    std::vector<std::uint16_t> block{2, 'H', 'i', 0, 2, 'N', 0xE9};
    for (int i = 3; i < 16; ++i) block.push_back(0);
    put32(pe, r + 0x48, 0x1000 + 0x58);
    put32(pe, r + 0x4C, static_cast<std::uint32_t>(block.size() * 2));
    for (std::size_t i = 0; i < block.size(); ++i) put16(pe, r + 0x58 + i * 2, block[i]);
    auto res = PeResources::parse(pe);
    CHECK(res.strings.size() == 2 && res.strings[16] == "Hi" && res.strings[18] == "N\xC3\xA9");
    GameStrings gs;
    gs.add(3, std::move(res));
    CHECK(gs.string((3u << 16) | 18) == std::optional<std::string>("N\xC3\xA9"));
    CHECK(gs.name(0, "fallback") == "fallback" && gs.name((3u << 16) | 16, "x") == "Hi");
    CHECK(resource_dlls("[Resources]\nDLL = InfoCards.dll ; text\nDLL = NameResources.dll\n[Data]\n").size() == 2);
}

static void test_trade() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_trade_test";
    fs::remove_all(root);
    auto put = [&](const char* path, const char* text) {
        fs::create_directories((root / path).parent_path());
        std::ofstream(root / path) << text;
    };
    put("equipment/goods.ini",
        "[Good]\nnickname = commodity_gold\nequipment = commodity_gold\ncategory = commodity\nprice = 425\n"
        "[Good]\nnickname = commodity_water\nequipment = commodity_water\ncategory = commodity\nprice = 20\n"
        "[Good]\nnickname = s_hull\ncategory = shiphull\nship = s_ship\nprice = 6000\nids_name = 12013\n");
    put("equipment/select_equip.ini",
        "[Commodity]\nnickname = commodity_gold\nids_name = 261610\nvolume = 1\n"
        "[Commodity]\nnickname = commodity_water\nids_name = 261611\nvolume = 2\n");
    put("equipment/market_commodities.ini",
        "[BaseGood]\nbase = Li01_01_base\nMarketGood = commodity_gold, 0, -1, 150, 500, 0, 1.08\n"
        "MarketGood = commodity_water, 0, -1, 0, 0, 1, 0.5\n"
        "[BaseGood]\nbase = Li01_02_base\nMarketGood = commodity_gold, 0, -1, 0, 0, 1, 2\n");
    put("universe/universe.ini", "[Base]\nnickname = Li01_01_Base\nsystem = Li01\nstrid_name = 196766\n");
    put("characters/newcharacter.ini",
        "[Package]\nnickname = p\nship = s_ship\nloadout = l\nmoney = 2000\n"
        "[Faction]\nnickname = new_player\nbase = Li01_01_Base\nPackage = p\n");
    Vfs vfs;
    vfs.mount(root, "base");
    Economy econ(vfs);
    CHECK(econ.good_count() == 3 && econ.market_count() == 2);
    CHECK(econ.base("li01_01_base") && econ.base("li01_01_base")->ids_name == 196766);
    CHECK(std::lround(econ.price("Li01_01_Base", "commodity_gold")) == 459);
    CHECK(econ.volume("commodity_water") == 2 && econ.name_id("commodity_gold") == 261610);
    CHECK(econ.hull_of_ship("S_SHIP") && econ.hull_of_ship("S_SHIP")->price == 6000);

    auto start = read_new_character(vfs);
    CHECK(start && start->money == 2000 && start->ship == "s_ship" && start->base == "Li01_01_Base");

    game::PlayerState p;
    p.credits = 2000;
    p.base = "Li01_01_Base";
    p.system = "Li01";
    p.ship = "s_ship";
    p.hold_size = 3;
    CHECK(game::max_buy(econ, p, "commodity_gold") == 3);  // hold limits before money (4 affordable)
    CHECK(game::buy(econ, p, "commodity_water", 1) == game::TradeResult::NotSold);
    CHECK(game::buy(econ, p, "commodity_gold", 4) == game::TradeResult::NoRoom);
    CHECK(game::buy(econ, p, "commodity_gold", 2) == game::TradeResult::Ok);
    CHECK(p.credits == 2000 - 2 * 459 && p.units("Commodity_Gold") == 2);
    CHECK(game::max_buy(econ, p, "commodity_gold") == 1);
    p.base = "Li01_02_base";
    CHECK(game::buy(econ, p, "commodity_gold", 1) == game::TradeResult::NotSold);
    CHECK(game::sell(econ, p, "commodity_gold", 3) == game::TradeResult::NoneOwned);
    CHECK(game::sell(econ, p, "commodity_gold", 2) == game::TradeResult::Ok);
    CHECK(p.credits == 2000 - 2 * 459 + 2 * 850 && p.cargo.empty());

    p.cargo["commodity_water"] = 1;
    p.credits = 5000000000LL;  // beyond 32 bits
    auto text = game::write_save(p);
    auto back = game::read_save(text);
    CHECK(back && back->credits == 5000000000LL && back->base == "Li01_02_base" && back->system == "Li01");
    CHECK(back && back->units("commodity_water") == 1 && back->hold_size == 3 && !back->position);
    p.base.clear();
    p.position = Vec3{1, 2, 3};
    back = game::read_save(game::write_save(p));
    CHECK(back && back->base.empty() && back->position && back->position->z == 3);
    CHECK(!game::read_save("[Player]\nsystem = Li01\n"));
    fs::remove_all(root);
}

static void test_dealers() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_dealer_test";
    fs::remove_all(root);
    auto put = [&](const char* path, const char* text) {
        fs::create_directories((root / path).parent_path());
        std::ofstream(root / path) << text;
    };
    put("ships/shiparch.ini",
        "[Ship]\nnickname = s_ship\nids_name = 5\nhold_size = 3\n"
        "hp_type = hp_gun_special_1, HpWeapon01, HpWeapon02\nhp_type = hp_thruster, HpThruster01\n"
        "[Ship]\nnickname = s_ship2\nids_name = 6\nhold_size = 10\nhp_type = hp_gun_special_1, HpWeapon01\n");
    put("equipment/weapon_equip.ini", "[Gun]\nnickname = gun1\nids_name = 7\nhp_gun_type = hp_gun_special_1\n");
    put("equipment/misc_equip.ini", "[Thruster]\nnickname = thr1\n[Power]\nnickname = pow1\n");
    put("equipment/goods.ini",
        "[Good]\nnickname = gun1\nequipment = gun1\ncategory = equipment\nprice = 500\n"
        "[Good]\nnickname = thr1\nequipment = thr1\ncategory = equipment\nprice = 300\n"
        "[Good]\nnickname = hull1\ncategory = shiphull\nship = s_ship\nprice = 1000\n"
        "[Good]\nnickname = hull2\ncategory = shiphull\nship = s_ship2\nprice = 4000\n"
        "[Good]\nnickname = pkg2\ncategory = ship\nhull = hull2\naddon = pow1, internal, 1\naddon = gun1, HpWeapon01, 1\n");
    put("equipment/market_misc.ini",
        "[BaseGood]\nbase = b1\nMarketGood = gun1, 0, -1, 10, 10, 0, 1\nMarketGood = thr1, 5, -1, 10, 10, 0, 1\n"
        "MarketGood = pkg2, 0, -1, 1, 1, 0, 1.5\n");
    Vfs vfs;
    vfs.mount(root, "base");
    Economy econ(vfs);
    CHECK(econ.hardpoints_for("s_ship", "gun1").size() == 2 && econ.hardpoints_for("s_ship", "thr1").size() == 1);
    CHECK(econ.hardpoints_for("s_ship", "pow1").empty() && econ.ship("S_SHIP2")->hold_size == 10);

    game::PlayerState p;
    p.base = "b1";
    p.system = "Li01";
    p.ship = "s_ship";
    p.hold_size = 3;
    p.credits = 1200;
    CHECK(game::buy_equipment(econ, p, "gun1") == game::TradeResult::Ok);
    CHECK(game::buy_equipment(econ, p, "gun1") == game::TradeResult::Ok);
    CHECK(p.equipment.size() == 2 && p.equipment[1].hardpoint == "HpWeapon02" && p.credits == 200);
    p.credits = 5000;
    CHECK(game::buy_equipment(econ, p, "gun1") == game::TradeResult::NoHardpoint);
    CHECK(game::buy_equipment(econ, p, "thr1") == game::TradeResult::LevelTooLow);
    p.rank = 5;
    CHECK(game::buy_equipment(econ, p, "thr1") == game::TradeResult::Ok && p.credits == 4700);
    CHECK(game::sell_equipment(econ, p, 0) == game::TradeResult::Ok && p.credits == 5200 && p.equipment.size() == 2);
    CHECK(game::free_hardpoint(econ, p, "gun1") == "HpWeapon01");

    // Trade-in: hull 1000 + gun 500 + thruster 300 = 1800; new ship 4000 * 1.5 = 6000.
    CHECK(game::trade_in_value(econ, p) == 1800 && game::ship_price(econ, p, "pkg2") == 6000);
    p.credits = 4199;
    CHECK(game::buy_ship(econ, p, "pkg2") == game::TradeResult::NoMoney);
    p.credits = 4200;
    p.cargo["x"] = 1;
    CHECK(game::buy_ship(econ, p, "pkg2") == game::TradeResult::Ok);
    CHECK(p.credits == 0 && p.ship == "s_ship2" && p.hold_size == 10 && p.equipment.size() == 2);
    CHECK(p.equipment[0].hardpoint.empty() && p.equipment[1].item == "gun1");

    auto back = game::read_save(game::write_save(p));
    CHECK(back && back->equipment.size() == 2 && back->equipment[1].hardpoint == "HpWeapon01" && back->rank == 5);
    fs::remove_all(root);
}

static void test_factions() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_faction_test";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "initialworld.ini") << "[Group]\nnickname = li_n_grp\nids_name = 10\nrep = 0.91, li_p_grp\nrep = -0.65, fc_x_grp\n";
    Vfs vfs;
    vfs.mount(root, "base");
    Factions f(vfs);
    CHECK(f.size() == 1 && f.rep("LI_N_GRP", "fc_x_grp") == -0.65f && f.rep("li_n_grp", "nobody") == 0);
    CHECK(attitude_of(-0.65f) == Attitude::Hostile && attitude_of(0) == Attitude::Neutral && attitude_of(0.91f) == Attitude::Friendly);
    auto house = read_house_lines("[Player]\nname = %%NAME%%\n;house = 1, x_grp\nhouse = 0.91, li_n_grp\r\nhouse=-0.3, fc_b_grp ; note\n");
    CHECK(house.size() == 2 && house["li_n_grp"] == 0.91f && house["fc_b_grp"] == -0.3f);

    MarketEntry m;
    m.rep_required = 0.1f;
    game::PlayerState p;
    p.base_faction = "li_p_grp";
    p.rep["li_p_grp"] = 0.05f;
    CHECK(!game::offered(m, p));
    p.rep["li_p_grp"] = 0.2f;
    CHECK(game::offered(m, p));
    p.system = "Li01";
    p.ship = "s";
    auto back = game::read_save(game::write_save(p));
    CHECK(back && std::fabs(back->rep_with("LI_P_GRP") - 0.2f) < 1e-3f && back->base_faction.empty());
    fs::remove_all(root);
}

// Builds a Lua 3.2 chunk by hand: header, main function with the given constants and code.
static std::vector<std::uint8_t> lua_chunk(const std::vector<std::string>& consts, std::vector<std::uint8_t> code) {
    std::vector<std::uint8_t> b{0x1B, 'L', 'u', 'a', 0x32, 0};
    auto be32 = [&](std::uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back((v >> (8 * i)) & 0xFF); };
    auto str = [&](const std::string& t) { be32(static_cast<std::uint32_t>(t.size() + 1)); b.insert(b.end(), t.begin(), t.end()); b.push_back(0); };
    be32(0);
    str("test");
    code.insert(code.begin(), {8, 0});  // stack size, parameter count
    be32(static_cast<std::uint32_t>(code.size()));
    b.insert(b.end(), code.begin(), code.end());
    be32(0);  // locals
    be32(static_cast<std::uint32_t>(consts.size()));
    for (auto& c : consts) { b.push_back(2); str(c); }
    return b;
}

static void test_thn() {
    enum : std::uint8_t { END = 0, NUM = 0x07, NEG = 0x09, K = 0x0B, GET = 0x0F, TABLE = 0x16, SETG = 0x19, LIST = 0x1D, MAP = 0x1E };
    // entities = { { entity_name = "Cam", type = CAMERA, cameraprops = { fovh = 40 },
    //                spatialprops = { pos = { 1, -2, 3 } } } }  duration = 5
    std::vector<std::string> k{"entities", "entity_name", "Cam", "type", "CAMERA", "duration", "cameraprops", "fovh", "spatialprops", "pos"};
    auto bytes = lua_chunk(k, {TABLE, 1,
                               TABLE, 4,
                               K, 1, K, 2,
                               K, 3, GET, 4,
                               K, 6, TABLE, 1, K, 7, NUM, 40, MAP, 0,
                               K, 8, TABLE, 1, K, 9, TABLE, 3, NUM, 1, NEG, 2, NUM, 3, LIST, 0, 3, MAP, 0,
                               MAP, 3,
                               LIST, 0, 1,
                               SETG, 0,
                               NUM, 5, SETG, 5,
                               END});
    auto chunk = LuaChunk::parse(bytes);
    const LuaValue& ents = chunk.global("entities");
    CHECK(ents.size() == 1 && ents[1]["entity_name"].str() == "Cam" && ents[1]["type"].is_symbol());
    CHECK(ents[1]["spatialprops"]["pos"][2].num() == -2);
    auto thn = ThnScript::parse(bytes);
    CHECK(thn.duration == 5 && thn.entities.size() == 1);
    auto cam = thn.entity("cam");
    CHECK(cam && cam->type == "CAMERA" && cam->fovh == 40 && cam->pos.x == 1 && cam->pos.y == -2 && cam->pos.z == 3);

    // Some compiled scripts hold the type as a number (3: a camera).
    auto numbered = lua_chunk(k, {TABLE, 1,
                                  TABLE, 4,
                                  K, 1, K, 2,
                                  K, 3, NUM, 3,
                                  K, 6, TABLE, 1, K, 7, NUM, 40, MAP, 0,
                                  K, 8, TABLE, 1, K, 9, TABLE, 3, NUM, 1, NEG, 2, NUM, 3, LIST, 0, 3, MAP, 0,
                                  MAP, 3,
                                  LIST, 0, 1,
                                  SETG, 0,
                                  NUM, 5, SETG, 5,
                                  END});
    auto thn2 = ThnScript::parse(numbered);
    CHECK(thn2.entity("cam") && thn2.entity("cam")->type == "CAMERA");

    // Anything beyond table building is refused instead of run.
    bool threw = false;
    try { LuaChunk::parse(lua_chunk({}, {0x40, END})); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { LuaChunk::parse(lua_chunk({}, {TABLE, 1})); } catch (const FormatError&) { threw = true; }
    CHECK(threw);

    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / "sv_room_test";
    fs::remove_all(root);
    fs::create_directories(root / "universe");
    std::ofstream(root / "universe/universe.ini") << "[Base]\nnickname = X_Base\nfile = universe\\x_base.ini\n";
    std::ofstream(root / "universe/x_base.ini") << "[BaseInfo]\nstart_room = Deck\n[Room]\nnickname = Deck\nfile = universe\\deck.ini\n"
                                                   "[Room]\nnickname = Gone\nfile = universe\\missing.ini\n";
    std::ofstream(root / "universe/deck.ini") << "[Room_Info]\nset_script = s\\deck.thn\nscene = all, ambient, s\\amb.thn\n"
                                                 "[Camera]\nname = Camera_0\n[Hotspot]\nname = IDS_HOTSPOT_BAR\nbehavior = ExitDoor\nroom_switch = Bar\n"
                                                 "[ForSaleShipPlacement]\nname = X/Pad/01\n[ForSaleShipPlacement]\nname = X/Pad/02\n"
                                                 "[Room_Sound]\nmusic = tune_a\nambient = hum_a\n";
    Vfs vfs;
    vfs.mount(root, "base");
    auto file = base_file_of(vfs, "x_base");
    auto rooms = BaseRooms::load(vfs, file);
    CHECK(rooms && rooms->start_room == "Deck" && rooms->rooms.size() == 1);
    auto deck = rooms ? rooms->room("deck") : nullptr;
    CHECK(deck && deck->camera == "Camera_0" && deck->scenes.size() == 1 && deck->hotspots.size() == 1 &&
          deck->hotspots[0].room_switch == "Bar");
    CHECK(deck && deck->ship_placements.size() == 2 && deck->ship_placements[1] == "X/Pad/02");
    CHECK(deck && deck->music == "tune_a" && deck->ambient == "hum_a");
    fs::remove_all(root);
}

static void test_ale() {
    using namespace sterrenvaarder;
    std::vector<std::uint8_t> b;
    auto u8 = [&](unsigned v) { b.push_back(static_cast<std::uint8_t>(v)); };
    auto u16 = [&](unsigned v) { u8(v & 0xff); u8(v >> 8); };
    auto u32 = [&](std::uint32_t v) { u16(v & 0xffff); u16(v >> 16); };
    auto f32 = [&](float f) { std::uint32_t v; std::memcpy(&v, &f, 4); u32(v); };
    auto str = [&](const std::string& s) {
        u16(static_cast<unsigned>(s.size() + 1));
        for (char c : s) u8(static_cast<unsigned char>(c));
        u8(0);
        if ((s.size() + 1) & 1) u8(0);
    };
    // Node library: one emitter, one appearance.
    f32(1.1f); u32(2);
    str("FxSphereEmitter");
    u16(0x0103); u32(ale::kName); str("Puff.emt");
    u16(0x0003); u32(ale::kLifespan); f32(2.5f);
    u16(0x0105); u32(ale::kTransform); u16(0x0304); u16(0x0005);
    u16(0x0202); u32(ale::kFrequency); u8(4); u8(2);  // 10/s at param 0, 30/s at param 1
    f32(0); f32(10); u16(0); u16(0);
    f32(1); f32(0); u16(0); u16(1); f32(0); f32(30); f32(0); f32(0);
    u16(0);
    str("FxBasicAppearance");
    u16(0x0103); u32(ale::kName); str("Puff.app");
    u16(0x8001); u32(0x12345678u);
    u16(0x0200); u32(ale::kSize); u8(4); u8(1); f32(0); u8(4); u8(2); f32(0); f32(1); f32(1); f32(3);
    u16(0x0201); u32(ale::kColor); u8(4); u8(1); f32(0); u8(4); u8(1); f32(0); f32(1); f32(0.5f); f32(0);
    u16(0x0104); u32(ale::kBlend); u32(5); u32(2);
    u16(0);
    std::vector<std::uint8_t> nodes = b;
    b.clear();
    // Effect library (version 1.0: no per-effect floats).
    f32(1.0f); u32(1);
    str("puff");
    u32(3);
    u32(1); u32(kAleRoot); u32(0x8000); u32(1);
    u32(0); u32(fl_model_crc_exact("Puff.emt")); u32(1); u32(2);
    u32(0); u32(fl_model_crc_exact("Puff.app")); u32(1); u32(3);
    u32(1); u32(2); u32(3);

    AleLibrary lib;
    lib.parse_nodes(nodes);
    lib.parse_effects(b);
    CHECK(lib.nodes().size() == 2 && lib.effects().size() == 1);
    auto* e = lib.effect("PUFF");
    CHECK(e && e->refs.size() == 3 && e->pairs.size() == 1 && e->pairs[0].second == 3);
    const AleNode* em = e ? lib.node(e->refs[1].node_crc) : nullptr;
    const AleNode* ap = e ? lib.node(e->refs[2].node_crc) : nullptr;
    CHECK(em && em->is_emitter() && em->name == "Puff.emt" && std::abs(em->number(ale::kLifespan, 0) - 2.5f) < 1e-6f);
    CHECK(ap && ap->is_appearance() && ap->flag(0x12345678u, false));
    if (em) {
        auto* f = em->get<AleCurve>(ale::kFrequency);
        CHECK(f && std::abs(f->eval(0, 0) - 10) < 1e-4f && std::abs(f->eval(1, 0) - 30) < 1e-4f &&
              std::abs(f->eval(0.5f, 0) - 20) < 1e-4f);
    }
    if (ap) {
        auto* sz = ap->get<AleAnimFloat>(ale::kSize);
        CHECK(sz && std::abs(sz->eval(0, 0.5f) - 2) < 1e-4f);
        auto* c = ap->get<AleAnimColor>(ale::kColor);
        CHECK(c && std::abs(c->eval(0, 0.3f).g - 0.5f) < 1e-4f);
        auto* bl = ap->get<AleBlend>(ale::kBlend);
        CHECK(bl && bl->src == 5 && bl->dst == 2);
    }
    // Run the effect: 10 particles a second at spark 0, each living the default second,
    // until the emitter's 2.5 s lifespan is over.
    auto shared = std::make_shared<AleLibrary>(std::move(lib));
    ResolvedEffect fx{nullptr, shared, shared->effect("puff")};
    game::EffectInstance inst(fx, 7);
    for (int i = 0; i < 10; ++i) inst.update(0.1, {}, game::Quat{}, {}, 0.f);
    CHECK(inst.particle_count() >= 9 && inst.particle_count() <= 11);
    std::vector<game::Sprite> sprites;
    inst.sprites(sprites);
    CHECK(!sprites.empty() && sprites[0].additive && sprites[0].size > 0 && sprites[0].size <= 3);
    CHECK(!inst.finished());
    for (int i = 0; i < 40; ++i) inst.update(0.1, {}, game::Quat{}, {}, 0.f);
    CHECK(inst.particle_count() == 0 && inst.finished());
}

static void test_pilots_and_ai() {
    using namespace sterrenvaarder;
    using namespace sterrenvaarder::game;
    const char* text =
        "[GunBlock]\nnickname = gun_a\ngun_fire_interval_time = 0.1\ngun_fire_burst_interval_time = 1\n"
        "gun_fire_no_burst_interval_time = 1\ngun_fire_accuracy_cone_angle = 2\ngun_range_threshold = 1\n"
        "[GunBlock]\nnickname = gun_b\ngun_fire_accuracy_cone_angle = 9\n"
        "[BuzzHeadTowardBlock]\nnickname = buzz_a\nbuzz_min_distance_to_head_toward = 600\nbuzz_max_time_to_head_away = 4\n"
        "buzz_head_toward_engine_throttle = 1\nbuzz_head_toward_turn_throttle = 1\n"
        "[BuzzPassByBlock]\nnickname = pass_a\nbuzz_distance_to_pass_by = 150\nbuzz_pass_by_time = 1\n"
        "buzz_pass_by_style_weight = straight_by, 1.0\n"
        "[EvadeBreakBlock]\nnickname = break_a\nevade_break_time = 2\nevade_break_interval_time = 5\n"
        "evade_break_style_weight = outrun, 1.0\nevade_break_direction_weight = left, 1.0\n"
        "[DamageReactionBlock]\nnickname = dmg_a\nevade_break_damage_trigger_percent = 0.1\n"
        "[JobBlock]\nnickname = job_a\nflee_when_hull_damaged_percent = 0.8\n"
        "[Pilot]\nnickname = base\ngun_id = gun_a\nbuzz_head_toward_id = buzz_a\nbuzz_pass_by_id = pass_a\n"
        "evade_break_id = break_a\ndamage_reaction_id = dmg_a\njob_id = job_a\n"
        "[Pilot]\nnickname = child\ninherit = base\ngun_id = gun_b\n";
    PilotDb db;
    db.add(parse_text_ini(text));
    CHECK(db.size() == 2);
    const Pilot* base = db.pilot("BASE");
    const Pilot* child = db.pilot("child");
    CHECK(base && child && !db.pilot("nobody"));
    if (!base || !child) return;
    CHECK(std::abs(base->gun.accuracy_cone_deg - 2) < 1e-4f && std::abs(child->gun.accuracy_cone_deg - 9) < 1e-4f);
    CHECK(child->has_buzz && std::abs(child->buzz_head_toward.min_distance - 600) < 1e-3f);  // inherited
    CHECK(child->buzz_pass_by.styles.size() == 1 && child->buzz_pass_by.styles[0].first == "straight_by");

    // A head-on run: approach, pass by, head away, then come round again.
    game::FighterAi ai(*base, 7);
    game::AiBody self{{0, 0, 0}, {0, 0, -80}, {}};
    game::AiBody tgt{{0, 0, -1000}, {0, 0, 0}, game::Quat::look({0, 0, 1})};
    game::AiWeapons w{600, 700};
    bool fired = false, passed = false, away = false;
    for (int i = 0; i < 400; ++i) {
        auto out = ai.update(self, tgt, 1.f, true, w, 0.05f);
        fired |= out.fire;
        if (ai.state() == game::FighterAi::State::PassBy) passed = true;
        if (passed && ai.state() == game::FighterAi::State::HeadAway) away = true;
        // Crude motion: fly where the steering points at a fixed speed.
        Vec3 f = self.orient.forward();
        self.orient = game::Quat::look(f + Vec3{out.input.steer.y * -0.05f, out.input.steer.x * 0.05f, 0});
        self.vel = game::to_d(self.orient.forward() * 80.f);
        self.pos = self.pos + self.vel * 0.05;
    }
    CHECK(fired && passed && away);

    // Heavy damage: break away; more: flee.
    game::FighterAi hurt(*base, 3);
    game::AiBody me{{0, 0, 0}, {}, {}};
    hurt.update(me, tgt, 1.f, true, w, 0.1f);
    hurt.update(me, tgt, 0.85f, true, w, 0.1f);
    CHECK(hurt.state() == game::FighterAi::State::Break);
    hurt.update(me, tgt, 0.1f, true, w, 0.1f);
    CHECK(hurt.state() == game::FighterAi::State::Flee);
    CHECK(!hurt.update(me, tgt, 0.1f, true, w, 0.1f).fire);
    // Once well clear it turns back, and does not run again.
    game::AiBody far_away{{0, 0, 5000}, {}, {}};
    hurt.update(far_away, tgt, 0.1f, true, w, 0.1f);
    CHECK(hurt.state() != game::FighterAi::State::Flee);
    hurt.update(me, tgt, 0.05f, true, w, 0.1f);
    CHECK(hurt.state() != game::FighterAi::State::Flee);
}

static void test_costumes() {
    CharacterParts cp;
    cp.add_body_parts(parse_text_ini(
        "[Animations]\nanim = anims\\a.anm\n[Skeleton]\nsex = male\n[Body]\nnickname = body_a\nmesh = b\\body_a.dfm\n"
        "[Skeleton]\nsex = female\n[Head]\nnickname = head_b\nmesh = h\\head_b.dfm\n"
        "[Accessory]\nnickname = hat\nmesh = p\\hat.3db\nhardpoint = hp_hat\nbody_hardpoint = hp_head\n"));
    cp.add_costumes(parse_text_ini("[Costume]\nnickname = look_a\nhead = head_b\nbody = body_a\nlefthand = l\nrighthand = r\naccessory = hat\n"));
    CHECK(cp.part_count() == 3 && cp.animation_files().size() == 1);
    CHECK(cp.part("BODY_A") && cp.part("body_a")->sex == "male" && cp.part("head_b")->sex == "female");
    CHECK(cp.part("hat")->kind == BodyPart::Kind::Accessory && cp.part("hat")->body_hardpoint == "hp_head");
    auto* c = cp.costume("look_a");
    CHECK(c && c->body == "body_a" && c->left_hand == "l" && c->accessories.size() == 1);
}

static void test_anm_frames() {
    // Frame sizes per channel type: time, position, rotation (full or packed).
    CHECK(AnmChannel::frame_size(0x80, true) == 10);
    CHECK(AnmChannel::frame_size(0x82, true) == 22);
    CHECK(AnmChannel::frame_size(0x22, true) == 16);
    CHECK(AnmChannel::frame_size(0x04, true) == 20);
    CHECK(AnmChannel::frame_size(0x42, false) == 18);
    CHECK(AnmChannel::frame_size(0x10, true) == 0);    // no data at all
    CHECK(AnmChannel::frame_size(0xC0, true) == 0);    // two rotations
    CHECK(AnmChannel::frame_size(0x100, true) == 0);   // unknown bit
}

static void test_dfm_strips() {
    // Two strips joined by repeated corners: 0 1 2 3 | 3 3 4 | 4 5 6.
    DfmMesh m;
    m.point_index.resize(7);
    DfmMesh::Group g;
    g.strip = {0, 1, 2, 3, 3, 4, 4, 5, 6};
    auto t = m.triangles(g);
    // Strip one gives (0 1 2) and (2 1 3), turned to keep the winding; the joins give
    // nothing; strip two gives one triangle from 4 5 6.
    CHECK(t.size() == 9);
    CHECK(t[0] == 0 && t[1] == 1 && t[2] == 2);
    CHECK(t[3] == 2 && t[4] == 1 && t[5] == 3);
    CHECK((t[6] == 4 || t[6] == 5) && t[8] == 6);
}

static void test_skinning() {
    using game::Xform;
    // Two bones: "Root" at the origin and "Arm" one unit up the y axis, joined by a sphere
    // joint. "Bone to root" holds the inverse of each bone's place, as the game files do.
    DfmModel m;
    m.bones.resize(2);
    m.bones[0].name = "Root";
    m.bones[1].name = "Arm";
    m.bones[1].index = 1;
    m.bones[1].to_root = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, -1, 0};
    Joint j;
    j.type = JointType::Sphere;
    j.parent = "Root";
    j.child = "Arm";
    j.origin = {0, 1, 0};
    m.joints.push_back(j);
    auto near = [](Vec3 a, Vec3 b) { return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f && std::abs(a.z - b.z) < 1e-4f; };
    CHECK(near(Xform::from_bone(m.bones[1].to_root).t, Vec3{0, 1, 0}));
    auto rest = game::pose_bones(m);
    CHECK(rest.size() == 2 && near(rest[1].t, Vec3{0, 1, 0}));

    // One point on the arm's tip, one shared half and half.
    DfmMesh mesh;
    mesh.points = {{0, 2, 0}, {0, 1, 0}};
    mesh.bone_first = {0, 1};
    mesh.bone_count = {1, 2};
    mesh.bone_ids = {1, 0, 1};
    mesh.bone_weights = {1, 0.5f, 0.5f};
    auto same = game::skin_points(m, mesh, rest);
    CHECK(near(same[0], mesh.points[0]) && near(same[1], mesh.points[1]));

    // An animation turns the arm 90 degrees about z: its tip swings from +y to -x.
    AnmScript anim;
    anim.joints.push_back({});
    anim.joints[0].child = "arm";
    const float h = std::sqrt(0.5f);
    anim.joints[0].channel.times = {0, 1};
    anim.joints[0].channel.rotations = {{0, 0, 0, 1}, {0, 0, h, h}};
    auto bent = game::skin_points(m, mesh, game::pose_bones(m, &anim, 1));
    CHECK(near(bent[0], Vec3{-1, 1, 0}));
    CHECK(near(bent[1], Vec3{0, 1, 0}));  // on the joint itself: does not move
    auto half = game::pose_bones(m, &anim, 0.5f);  // halfway: 45 degrees
    CHECK(near(half[1].rotate(Vec3{0, 1, 0}), Vec3{-h, h, 0}));

    // A hand hangs on the arm's hardpoint: the hand's own hardpoint lands on it.
    m.bones[1].hardpoints.push_back({"hp_hand", {0, 1, 0}, kIdentity3});
    DfmModel hand;
    hand.bones.resize(1);
    hand.bones[0].name = "Root";
    hand.bones[0].hardpoints.push_back({"HP_HAND", {0, -0.25f, 0}, kIdentity3});
    Xform at;
    auto bent_pose = game::pose_bones(m, &anim, 1);
    CHECK(game::attach_part(m, bent_pose, hand, game::pose_bones(hand), "hp_hand", at));
    CHECK(near(at.apply(Vec3{0, -0.25f, 0}), Vec3{-1, 1, 0}));  // the hand's hardpoint at the arm's tip
    CHECK(!game::attach_part(m, bent_pose, hand, game::pose_bones(hand), "hp_none", at));
}

static void test_mbases() {
    // Self-written: one base with a bar and a fixed bartender, one guest, then a second base.
    auto bases = parse_mbases(parse_text_ini(
        "[MBase]\nnickname = Tst01_01_Base\nlocal_faction = grp_a\n"
        "[MRoom]\nnickname = bar\ncharacter_density = 4\nfixture = tst_barkeep, Zs/NPC/Bar/01/Stand, scripts\\idle.thn, bartender\n"
        "[GF_NPC]\nnickname = tst_guest\nbody = body_a\nhead = head_a\nlefthand = hand_l\nrighthand = hand_r\n"
        "accessory = hat_a\nindividual_name = 123\naffiliation = grp_a\nvoice = v1\nroom = bar\n"
        "[MBase]\nnickname = Tst01_02_Base\n"));
    CHECK(bases.size() == 2);
    auto& b = bases[0];
    CHECK(b.local_faction == "grp_a" && b.rooms.size() == 1 && b.npcs.size() == 1);
    const BaseRoomCrowd* bar = b.room("BAR");
    CHECK(bar && bar->density == 4 && bar->fixtures.size() == 1);
    CHECK(bar->fixtures[0].spot == "Zs/NPC/Bar/01/Stand" && bar->fixtures[0].role == "bartender");
    const BaseNpc* g = b.npc("tst_guest");
    CHECK(g && g->head == "head_a" && g->right_hand == "hand_r" && g->accessories.size() == 1 && g->name_id == 123);
    CHECK(g->room == "bar" && bases[1].npcs.empty());
}

static void test_thn_player() {
    // A self-written scene: a box slides 10 m along x over 2 s while turning 90 degrees about
    // y, and the view cuts from camera A to camera B at 1 s, which narrows its view.
    ThnScript s;
    s.duration = 3;
    ThnEntity box;
    box.name = "Box";
    s.entities.push_back(box);
    ThnEntity cam_b;
    cam_b.name = "CamB";
    cam_b.type = "CAMERA";
    cam_b.fovh = 60;
    s.entities.push_back(cam_b);
    const float h = std::sqrt(0.5f);
    ThnMove mv;
    mv.time = 0;
    mv.duration = 2;
    mv.target = "box";
    mv.has_pos = mv.has_orient = true;
    mv.pos = {10, 0, 0};
    mv.q = {0, 0, 1, 0};  // w, x, y, z as scenes store it: cos and sin of the whole 90 degrees
    s.moves.push_back(mv);
    auto mq = game::move_quat(mv.q);
    CHECK(std::abs(mq[0] - h) < 1e-5f && std::abs(mq[2] - h) < 1e-5f);
    s.cuts.push_back({0, "CamA"});
    s.cuts.push_back({1, "CamB"});
    ThnCameraProp zoom;
    zoom.time = 1;
    zoom.duration = 1;
    zoom.target = "CamB";
    zoom.fovh = 30;
    s.camera_props.push_back(zoom);

    s.sounds.push_back({0, 0, 0, "Hum", true});
    s.sounds.push_back({1.25f, 0, 0, "Click", false});

    game::ThnPlayer p(s);
    CHECK(p.duration() == 3);
    // Sounds start once each: the one at 0 on the first look, the other when its time passes.
    CHECK(p.sounds_between(-1, 0).size() == 1 && p.sounds_between(-1, 0)[0] == 0);
    CHECK(p.sounds_between(0, 1).empty());
    CHECK(p.sounds_between(1, 1.25f).size() == 1 && p.sounds_between(1, 1.25f)[0] == 1);
    CHECK(p.sounds_between(1.25f, 3).empty());
    p.seek(0.5f);
    CHECK(p.camera() == "CamA");
    CHECK(std::abs(p.state("BOX")->pos.x - 2.5f) < 1e-4f);
    p.seek(1.f);
    CHECK(p.camera() == "CamB");
    auto* b = p.state("box");
    CHECK(std::abs(b->pos.x - 5) < 1e-4f);
    // Halfway through the turn: 45 degrees, so x turns towards -z.
    CHECK(std::abs(b->orient[0] - h) < 1e-3f && std::abs(b->orient[6] + h) < 1e-3f);
    p.seek(1.5f);
    CHECK(std::abs(p.state("CamB")->fovh - 45) < 1e-3f);
    p.seek(2.5f);
    CHECK(std::abs(p.state("box")->pos.x - 10) < 1e-4f && std::abs(p.state("box")->orient[2] - 1) < 1e-3f);
    CHECK(std::abs(p.state("CamB")->fovh - 30) < 1e-3f && !p.done(2.5f) && p.done(3));
    // The view through camera B: it looks down its -z, with a vertical view from its
    // 30 degree horizontal view at the default 4:3.
    game::ThnPlayer::View v;
    CHECK(p.view(v) && std::abs(v.forward.z + 1) < 1e-4f && std::abs(v.up.y - 1) < 1e-4f);
    CHECK(std::abs(v.fov_y - 2 * std::atan(std::tan(15 * 3.14159265f / 180) * 0.75f)) < 1e-4f);
    p.seek(0.5f);
    CHECK(!p.view(v));  // camera A is not in the scene

    // A path: an open spline through three points, placed 100 m up by its entity. A box rides
    // it from 0 to 100% over 4 s, starting at 10 s.
    ThnPath path = ThnPath::parse("OPEN, {0,0,0}, {1,0,0,0}, {10,0,0}, {1,0,0,0}, {20,0,0}, {1,0,0,0}");
    CHECK(!path.closed && path.points.size() == 3 && path.orients.size() == 3);
    CHECK(std::abs(path.at(0.5f).x - 10) < 1e-4f && std::abs(path.at(1).x - 20) < 1e-4f);
    CHECK(ThnPath::parse("CLOSED, {1,2,3}").closed);
    ThnEntity rail;
    rail.name = "Rail";
    rail.type = "MOTION_PATH";
    rail.pos = {0, 100, 0};
    s.entities.push_back(rail);
    s.paths["rail"] = path;
    ThnPathAnim ride;
    ride.time = 10;
    ride.duration = 4;
    ride.target = "Box";
    ride.path = "Rail";
    s.path_anims.push_back(ride);
    game::ThnPlayer q(s);
    CHECK(q.duration() == 14);
    q.seek(12);
    CHECK(std::abs(q.state("box")->pos.x - 10) < 1e-3f && std::abs(q.state("box")->pos.y - 100) < 1e-3f);
}

static void test_wav() {
    // A self-made tone: 0.1 s of 16-bit mono at 8000 Hz, then the same as 8-bit stereo.
    auto wav = [](int bits, int channels, const std::vector<std::uint8_t>& body) {
        std::vector<std::uint8_t> d;
        auto u32 = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) d.push_back(static_cast<std::uint8_t>(v >> (8 * k))); };
        auto u16 = [&](std::uint16_t v) { d.push_back(static_cast<std::uint8_t>(v)); d.push_back(static_cast<std::uint8_t>(v >> 8)); };
        d.insert(d.end(), {'R', 'I', 'F', 'F'});
        u32(static_cast<std::uint32_t>(4 + 8 + 16 + 8 + body.size()));
        d.insert(d.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        u32(16);
        u16(1);
        u16(static_cast<std::uint16_t>(channels));
        u32(8000);
        u32(static_cast<std::uint32_t>(8000 * channels * bits / 8));
        u16(static_cast<std::uint16_t>(channels * bits / 8));
        u16(static_cast<std::uint16_t>(bits));
        d.insert(d.end(), {'d', 'a', 't', 'a'});
        u32(static_cast<std::uint32_t>(body.size()));
        d.insert(d.end(), body.begin(), body.end());
        return d;
    };
    std::vector<std::uint8_t> pcm16;
    for (int i = 0; i < 800; ++i) {
        auto v = static_cast<std::int16_t>(std::lround(10000 * std::sin(i * 0.2)));
        pcm16.push_back(static_cast<std::uint8_t>(v));
        pcm16.push_back(static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8));
    }
    auto data16 = wav(16, 1, pcm16);
    Sound a = decode_wav(data16);
    CHECK(a.rate == 8000 && a.channels == 1 && a.samples.size() == 800 && std::abs(a.seconds() - 0.1f) < 1e-4f);
    CHECK(a.samples[5] == static_cast<std::int16_t>(std::lround(10000 * std::sin(1.0))));
    auto data8 = wav(8, 2, std::vector<std::uint8_t>(1600, 128 + 64));
    Sound b = decode_wav(data8);
    CHECK(b.channels == 2 && b.samples.size() == 1600 && b.samples[0] == 64 << 8 && std::abs(b.seconds() - 0.1f) < 1e-4f);
    bool threw = false;
    std::vector<std::uint8_t> junk(20, 0);
    try { decode_wav(junk); } catch (const FormatError&) { threw = true; }
    CHECK(threw);
}

static void test_mixer() {
    // Self-made sounds: a constant mono level at half the output rate, and a short stereo click.
    auto level = std::make_shared<Sound>();
    level->rate = 4000;
    level->channels = 1;
    level->samples.assign(400, 1000);  // 0.1 s
    auto click = std::make_shared<Sound>();
    click->rate = 8000;
    click->channels = 2;
    click->samples = {2000, -2000, 2000, -2000};  // 2 frames
    game::Mixer m(8000);
    auto a = m.play(level, 0.5f);
    auto b = m.play(click);
    std::vector<std::int16_t> out(2 * 4);
    m.mix(out.data(), 4);
    CHECK(out[0] == 500 + 2000 && out[1] == 500 - 2000);  // both sounds, mono on both sides
    CHECK(out[4] == 500 && out[5] == 500);                // the click is over
    CHECK(m.playing(a) && !m.playing(b));
    // The level lasts 0.1 s at any output rate: 800 frames here, then it stops.
    std::vector<std::int16_t> rest(2 * 900);
    m.mix(rest.data(), 900);
    CHECK(rest[2 * 790] == 500 && rest[2 * 899] == 0 && !m.playing(a));
    // Looping keeps going; stop ends it; gain and master scale it.
    auto c = m.play(level, 1, true);
    m.set_master(2);
    m.mix(rest.data(), 900);
    CHECK(m.playing(c) && rest[2 * 899] == 2000);
    m.set_gain(c, 0.25f);
    m.mix(rest.data(), 1);
    CHECK(rest[0] == 500);
    m.stop(c);
    m.mix(rest.data(), 1);
    CHECK(!m.playing(c) && rest[0] == 0);
    // Loud sums clip instead of wrapping around.
    auto loud = std::make_shared<Sound>(*level);
    loud->samples.assign(400, 30000);
    m.set_master(1);
    m.play(loud);
    m.play(loud);
    m.mix(rest.data(), 1);
    CHECK(rest[0] == 32767);

    // Fades: a looping level fades from 1 to 0 over 0.1 s (800 frames) and then stops.
    game::Mixer f(8000);
    auto fl = f.play(level, 1, true);
    f.fade(fl, 0, 0.1f, true);
    std::vector<std::int16_t> fo(2 * 900);
    f.mix(fo.data(), 900);
    CHECK(fo[0] == 1000 && fo[2 * 400] > 400 && fo[2 * 400] < 600 && fo[2 * 850] == 0 && !f.playing(fl));
    auto up = f.play(level, 0, true);
    f.fade(up, 0.5f, 0.05f);
    f.mix(fo.data(), 900);
    CHECK(f.playing(up) && fo[2 * 899] == 500);
}

static void test_music_mood() {
    using game::MusicMood;
    game::MusicInput calm;
    CHECK(game::choose_mood(calm, MusicMood::Space) == MusicMood::Space);
    game::MusicInput near_foe;
    near_foe.nearest_hostile = 4000;
    CHECK(game::choose_mood(near_foe, MusicMood::Space) == MusicMood::Danger);
    near_foe.nearest_hostile = 6000;  // between enter and leave: stays as it was
    CHECK(game::choose_mood(near_foe, MusicMood::Space) == MusicMood::Space);
    CHECK(game::choose_mood(near_foe, MusicMood::Danger) == MusicMood::Danger);
    near_foe.nearest_hostile = 7000;
    CHECK(game::choose_mood(near_foe, MusicMood::Danger) == MusicMood::Space);
    game::MusicInput fight;
    fight.nearest_hostile = 800;
    fight.since_combat = 3;
    CHECK(game::choose_mood(fight, MusicMood::Space) == MusicMood::Battle);
    fight.since_combat = 30;  // the shooting is over, the enemy still near
    CHECK(game::choose_mood(fight, MusicMood::Battle) == MusicMood::Danger);
}

static void test_sound_db() {
    // Self-written sound list and system music.
    SoundDb db;
    db.add(parse_text_ini("[Sound]\nnickname = tune_a\nfile = audio\\music\\a.wav\nattenuation = -6\ntype = music\nstreamer = true\n"
                          "[Sound]\nnickname = Tune_B\nfile = audio\\music\\b.wav\n[Other]\nnickname = not_a_sound\n"));
    CHECK(db.size() == 2 && db.find("TUNE_A") && db.find("tune_b") && !db.find("not_a_sound"));
    const SoundDef* a = db.find("tune_a");
    CHECK(a->streamer && a->type == "music" && std::abs(a->gain() - 0.501f) < 0.01f && db.find("tune_b")->gain() == 1);
    auto m = parse_system_music(parse_text_ini("[SystemInfo]\nname = x\n[Music]\nspace = tune_a\ndanger = tune_b\nbattle = tune_c\n"));
    CHECK(m.space == "tune_a" && m.danger == "tune_b" && m.battle == "tune_c");
    // Distance: full up to the near range, half at twice that (before the taper), silent at the far range.
    db.add(parse_text_ini("[Sound]\nnickname = zap\nfile = z.wav\nrange = 100, 1100\n"));
    const SoundDef* z = db.find("zap");
    CHECK(z && z->range_min == 100 && z->range_max == 1100);
    CHECK(z->gain_at(50) == 1 && z->gain_at(1100) == 0 && z->gain_at(5000) == 0);
    CHECK(std::abs(z->gain_at(200) - 0.5f * 0.9f) < 1e-4f);
    CHECK(a->gain_at(1e6f) == 1);  // no range: heard everywhere
}

static void test_mission_music() {
    using game::MusicMood;
    SystemMusic sys{"sys_space", "sys_danger", "sys_battle"};
    // Keep the system's mood tracks, play a motif over them once, with a 3 s fade.
    auto m = game::parse_play_music({"none", "none", "none", "motif_a", "3", "true"});
    CHECK(m.tracks.space.empty() && m.motif == "motif_a" && m.fade == 3 && m.once);
    CHECK(game::music_track(m, sys, MusicMood::Battle) == "motif_a");
    m.motif.clear();
    CHECK(game::music_track(m, sys, MusicMood::Battle) == "sys_battle");
    // Stand-in tracks: the mission's space track, its missing danger track falls back to its space.
    auto n = game::parse_play_music({"msn_space", "none", "none", "none"});
    CHECK(n.fade == 2 && !n.once && n.motif.empty());
    CHECK(game::music_track(n, sys, MusicMood::Space) == "msn_space" && game::music_track(n, sys, MusicMood::Danger) == "msn_space");
    // no_params gives the music back to the system.
    auto r = game::parse_play_music({"no_params"});
    CHECK(game::music_track(r, sys, MusicMood::Danger) == "sys_danger");
}

static void test_radio() {
    auto ok = game::dock_reply_lines("X_Base", true);
    CHECK(ok.size() == 3 && ok[0] == "gcs_misc_thisis+" && ok[1] == "gcs_refer_base_x_base-" && ok[2] == "gcs_dockrequest_granted_01-");
    auto no = game::dock_reply_lines("", false);
    CHECK(no.size() == 1 && no[0] == "gcs_dockrequest_responsehostile_01-");
    CHECK(game::dock_kind("Dock_Ring") == game::DockKind::Ring && game::dock_kind("x_battleship") == game::DockKind::Moor &&
          game::dock_kind("largestation1") == game::DockKind::Berth);
    CHECK(game::launch_clear_lines(game::DockKind::Ring)[0] == "gcs_docklaunch_clear_ring_01-");
}

static IniValue str_value(const char* v) {
    IniValue x;
    x.s = v;
    return x;
}

static void test_mission_start() {
    // Self-written script: a start trigger bound to a system, which switches on a bar
    // scene that waits for the player to enter a base.
    MissionScript ms;
    MsnTrigger start, bar, later;
    start.nickname = "go";
    start.system = "Sys01";
    start.initially_active = true;
    start.conditions.push_back({"True", {}});
    start.actions.push_back({"ActTrig", {str_value("bar")}});
    bar.nickname = "bar";
    bar.conditions.push_back({"LocEnter", {str_value("Bar"), str_value("Sys01_02_Base")}});
    bar.actions.push_back({"ActTrig", {str_value("go")}});  // loops back: visited once
    later.nickname = "later";
    later.conditions.push_back({"BaseEnter", {str_value("Sys02_01_Base")}});
    ms.triggers = {start, bar, later};
    auto w = game::mission_start(ms);
    CHECK(w.system == "Sys01");
    // "later" is never switched on
    CHECK(w.places.size() == 1 && w.places[0].base == "Sys01_02_Base" && w.places[0].room == "Bar");
    CHECK(game::next_story_mission("m01a") == "M01b" && game::next_story_mission("M12") == "M13");
    CHECK(game::next_story_mission("M13").empty() && game::next_story_mission("x").empty());

    // RTCDone matches the cutscene file however its path is written.
    CHECK(game::same_data_path("Missions\\\\A\\\\b.ini", "//missions//a//B.ini"));
    CHECK(!game::same_data_path("missions/a/b.ini", "missions/a/c.ini"));
    MissionScript rs = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_done\nInitState = ACTIVE\nCnd_RTCDone = scenes\\one.ini\nAct_Ping = x\n"));
    struct DockedWorld : game::MissionWorld {
        std::string system() const override { return "Sys01"; }
        bool in_space() const override { return false; }
        std::string base() const override { return "Sys01_01_Base"; }
        std::optional<game::DVec3> position(std::string_view) const override { return std::nullopt; }
        float health(std::string_view) const override { return 1; }
    } dw;
    game::MissionRunner rr(rs);
    int pings = 0;
    auto act = [&](const MsnCommand& c, const MsnTrigger&) { pings += c.name == "Ping"; };
    rr.start();
    rr.update(0.1f, dw, act);
    CHECK(pings == 0);
    rr.event({game::MissionEvent::Type::RTCDone, "scenes/two.ini", "", ""});
    rr.update(0.1f, dw, act);
    CHECK(pings == 0);
    rr.event({game::MissionEvent::Type::RTCDone, "SCENES/one.ini", "", ""});
    rr.update(0.1f, dw, act);
    CHECK(pings == 1);
    // Message boxes and talking to someone in a room.
    MissionScript ps = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_pop\nInitState = ACTIVE\nCnd_PopUpDialog = CLOSE\nAct_Ping = x\n"
        "[Trigger]\nnickname = t_talk\nInitState = ACTIVE\nCnd_CharSelect = guide, Bar, Sys01_01_Base\nAct_Ping = x\n"));
    game::MissionRunner pr(ps);
    pings = 0;
    pr.start();
    CHECK(pr.waiting_on("CharSelect").size() == 1 && pr.waiting_on("CharSelect")[0].arg(0) == "guide");
    pr.event({game::MissionEvent::Type::CharSelect, "guide", "Bar", "Sys02_01_Base"});
    pr.update(0.1f, dw, act);
    CHECK(pings == 0);
    pr.event({game::MissionEvent::Type::CharSelect, "GUIDE", "bar", "sys01_01_base"});
    pr.event({game::MissionEvent::Type::PopUpDialog, "close", "", ""});
    pr.update(0.1f, dw, act);
    CHECK(pings == 2 && pr.waiting_on("CharSelect").empty());
    // WatchTrigger: ACTIVE while switched on, COMPLETE once it has fired.
    MissionScript ws = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_target\nInitState = ACTIVE\nCnd_CommComplete = line_a\nAct_Ping = x\n"
        "[Trigger]\nnickname = t_on\nInitState = ACTIVE\nCnd_WatchTrigger = t_target, ACTIVE\nAct_Pong = x\n"
        "[Trigger]\nnickname = t_done\nInitState = ACTIVE\nCnd_WatchTrigger = t_target, COMPLETE\nAct_Ping = x\n"));
    game::MissionRunner wr(ws);
    int pongs = 0;
    pings = 0;
    auto act2 = [&](const MsnCommand& c, const MsnTrigger&) { pings += c.name == "Ping"; pongs += c.name == "Pong"; };
    wr.start();
    wr.update(0.1f, dw, act2);
    CHECK(pongs == 1 && pings == 0);
    wr.event({game::MissionEvent::Type::CommComplete, "line_a", "", ""});
    wr.update(0.1f, dw, act2);
    wr.update(0.1f, dw, act2);
    CHECK(pings == 2);
    // Moving away from a point (TICK_AWAY) and asking for a maneuver.
    MissionScript ts = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_away\nInitState = ACTIVE\nCnd_DistShip = outside, Player, buoy, 1000, 0.25, TICK_AWAY\nAct_Ping = x\n"
        "[Trigger]\nnickname = t_dock\nInitState = ACTIVE\nCnd_PlayerManeuver = Dock, ring_1\nAct_Ping = x\n"));
    struct MovingWorld : game::MissionWorld {
        double player_x = 2000;
        std::string system() const override { return "Sys01"; }
        bool in_space() const override { return true; }
        std::string base() const override { return {}; }
        std::optional<game::DVec3> position(std::string_view o) const override {
            if (o == "Player") return game::DVec3{player_x, 0, 0};
            return game::DVec3{0, 0, 0};
        }
        float health(std::string_view) const override { return 1; }
    } mw;
    game::MissionRunner tr(ts);
    pings = 0;
    tr.start();
    tr.update(0.1f, mw, act);
    mw.player_x = 1900;  // outside, but closing in
    tr.update(0.1f, mw, act);
    mw.player_x = 1940;  // turned back: not closing in, but not yet for 0.25 s
    tr.update(0.1f, mw, act);
    tr.update(0.1f, mw, act);
    mw.player_x = 1850;  // closing in again starts the count over
    tr.update(0.1f, mw, act);
    mw.player_x = 1960;
    tr.update(0.1f, mw, act);
    tr.update(0.1f, mw, act);
    CHECK(pings == 0);
    tr.update(0.1f, mw, act);
    tr.update(0.1f, mw, act);
    CHECK(pings == 1);
    tr.event({game::MissionEvent::Type::PlayerManeuver, "Dock", "ring_2", ""});
    tr.update(0.1f, mw, act);
    CHECK(pings == 1);
    tr.event({game::MissionEvent::Type::PlayerManeuver, "dock", "RING_1", ""});
    tr.update(0.1f, mw, act);
    CHECK(pings == 2);
    // DistCircle: crossing the ring's plane within its opening, not just being near it.
    MissionScript cs = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_ring\nInitState = ACTIVE\nCnd_DistCircle = Player, ring_1\nAct_Ping = x\n"));
    struct RingWorld : MovingWorld {
        double player_y = 0;
        std::optional<game::DVec3> position(std::string_view o) const override {
            if (o == "Player") return game::DVec3{player_x, player_y, 0};
            return game::DVec3{0, 0, 0};
        }
        std::optional<game::DVec3> axis(std::string_view) const override { return game::DVec3{1, 0, 0}; }
    } rw;
    game::MissionRunner cr(cs);
    pings = 0;
    cr.start();
    rw.player_x = -100;  // lined up behind it
    cr.update(0.1f, rw, act);
    rw.player_x = -20;
    cr.update(0.1f, rw, act);
    CHECK(pings == 0);
    rw.player_y = 400;  // past its plane, but beside the ring
    rw.player_x = 30;
    cr.update(0.1f, rw, act);
    CHECK(pings == 0);
    rw.player_y = 0;
    rw.player_x = -30;
    cr.update(0.1f, rw, act);
    rw.player_x = 30;  // through it
    cr.update(0.1f, rw, act);
    CHECK(pings == 1);
    // Destroyed = ship, count, how, killer: only kills by that one count.
    MissionScript ks = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_kill\nInitState = ACTIVE\nCnd_Destroyed = plat_1, 1, ALL_IGNORE_LANDING, Player\nAct_Ping = x\n"));
    game::MissionRunner kr(ks);
    pings = 0;
    kr.start();
    kr.event({game::MissionEvent::Type::Destroyed, "plat_1", "bandit_1", ""});
    kr.update(0.1f, rw, act);
    CHECK(pings == 0);
    kr.event({game::MissionEvent::Type::Destroyed, "PLAT_1", "player", ""});
    kr.update(0.1f, rw, act);
    CHECK(pings == 1);
    // A trigger of the system just left still sees the player leave it.
    MissionScript xs = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_exit\nsystem = Sys02\nInitState = ACTIVE\nCnd_SystemExit = Sys02\nAct_Ping = x\n"));
    game::MissionRunner xr(xs);
    pings = 0;
    xr.start();
    xr.event({game::MissionEvent::Type::SystemExit, "Sys02", "", ""});
    xr.update(0.1f, mw, act);  // mw is in Sys01 now
    CHECK(pings == 1);
    MissionScript ys = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_true\nsystem = Sys02\nInitState = ACTIVE\nCnd_True = no_params\nAct_Ping = x\n"));
    game::MissionRunner yr(ys);
    pings = 0;
    yr.start();
    yr.update(0.1f, mw, act);  // True fires in Sys01 too
    CHECK(pings == 1);
    // "outside ..., distance, seconds": only after staying out that long without a break.
    MissionScript hs = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_held\nInitState = ACTIVE\nCnd_DistShip = outside, Player, buoy, 1000, 2\nAct_Ping = x\n"));
    game::MissionRunner hr(hs);
    pings = 0;
    hr.start();
    mw.player_x = 2000;
    for (int i = 0; i < 15; ++i) hr.update(0.1f, mw, act);
    mw.player_x = 500;  // back inside: the count starts over
    hr.update(0.1f, mw, act);
    mw.player_x = 2000;
    for (int i = 0; i < 15; ++i) hr.update(0.1f, mw, act);
    CHECK(pings == 0);
    for (int i = 0; i < 6; ++i) hr.update(0.1f, mw, act);
    CHECK(pings == 1);
    auto rel = parse_rtc(parse_text_ini("[CharacterEncounter]\nLocation = b1, Bar\nrelocate_player = Deck2, b2\n"));
    CHECK(rel.relocate_player == "Deck2" && rel.relocate_base == "b2");
}

static void test_burst() {
    // Over 25 s at 60 fps a ship fires about 1 s in every 2.5 s, whatever the rounding.
    for (float start : {0.f, -1.5f, 1.f, -0.3f}) {
        float b = start;
        int on = 0;
        for (int i = 0; i < 1500; ++i) {
            game::step_burst(b, 1.f / 60);
            on += b > 0;
        }
        CHECK(on > 500 && on < 700);
    }
}

static void test_vibes() {
    CHECK(game::rep_value("REP_HOSTILE_MAXIMUM") < -0.8f && game::rep_value("REP_HOSTILE_THRESHOLD") <= -0.6f);
    CHECK(game::rep_value("REP_FRIEND_MAXIMUM") > 0.8f && game::rep_value("REP_NEUTRAL") == 0);
    CHECK(game::rep_value("REP_NEUTRAL_HOSTILE") < 0 && game::rep_value("REP_NEUTRAL_HOSTILE") > -0.6f);
    game::Vibes v;
    v.set("lbl_raiders", "convoy_lead", game::rep_value("REP_HOSTILE_MAXIMUM"));
    // One way only: the raiders hate the convoy leader; the leader has no say yet.
    auto r = v.between({"raider_1", "LBL_RAIDERS"}, {"Convoy_Lead", "lbl_convoy"});
    CHECK(r && *r < -0.8f);
    CHECK(!v.between({"convoy_lead"}, {"raider_1", "lbl_raiders"}));
    v.set("convoy_lead", "lbl_raiders", game::rep_value("REP_FRIEND_MAXIMUM"));
    CHECK(v.between({"convoy_lead"}, {"raider_1", "lbl_raiders"}) > 0.8f);
}

static void test_new_player_file() {
    std::string text = "[Player]\nsystem = Sys09 ; where it starts\npos = 100, -2.5, 3000\nrotate = 0, 90, 180\n"
                       "name = %%NAME%%\n[StoryInfo]\nMission = Mission_07b\n";
    // Plain text passes through; the scrambled form ("FLS1" + XOR with "Gene") decodes to it.
    std::vector<std::uint8_t> plain(text.begin(), text.end());
    CHECK(decode_fl_file(plain) == text);
    std::vector<std::uint8_t> packed{'F', 'L', 'S', '1'};
    const char key[4] = {'G', 'e', 'n', 'e'};
    for (std::size_t i = 0; i < text.size(); ++i)
        packed.push_back(static_cast<std::uint8_t>(text[i] ^ (((static_cast<unsigned>(key[i % 4]) + i) & 0xFFu) | 0x80u)));
    CHECK(decode_fl_file(packed) == text);
    auto st = read_new_player(decode_fl_file(packed));
    CHECK(st && st->system == "Sys09" && st->mission == "M07b");
    CHECK(st && st->pos[0] == 100 && st->pos[1] == -2.5 && st->pos[2] == 3000 && st->rotate[1] == 90 && st->rotate[2] == 180);
    CHECK(!read_new_player("[StoryInfo]\nMission = Mission_01\n"));
}

static void test_euler_degrees() {
    Vec3 in{20, -35, 70};
    auto m = render::Mat4::euler_deg(in);
    // Rebuild the quaternion from the matrix's axes and read the angles back.
    Vec3 fwd{-m.at(0, 2), -m.at(1, 2), -m.at(2, 2)}, up{m.at(0, 1), m.at(1, 1), m.at(2, 1)};
    auto q = game::Quat::look(fwd, up);
    Vec3 out = game::euler_degrees(q);
    CHECK(std::abs(out.x - in.x) < 0.05f && std::abs(out.y - in.y) < 0.05f && std::abs(out.z - in.z) < 0.05f);
    Vec3 zero = game::euler_degrees(game::Quat{});
    CHECK(std::abs(zero.x) + std::abs(zero.y) + std::abs(zero.z) < 1e-4f);
}

static void test_thn_attach() {
    // Self-written scene: a marker pinned to an object outside the scene, a probe riding
    // 10 m in front of it (in its frame), and a camera that keeps looking at the probe.
    ThnScript s;
    s.duration = 5;
    ThnEntity marker, probe, cam;
    marker.name = "Main";
    probe.name = "Probe";
    probe.pos = {0, 0, -10};
    cam.name = "Cam";
    cam.type = "CAMERA";
    cam.pos = {0, 50, 0};
    s.entities = {marker, probe, cam};
    ThnAttach ride;
    ride.child = "Probe";
    ride.parent = "Main";
    ride.position = ride.orientation = ride.relative = true;
    ThnAttach look;
    look.child = "Cam";
    look.parent = "Probe";
    look.look_at = true;
    s.attachments = {ride, look};
    s.cuts.push_back({0, "Cam"});
    game::ThnPlayer p(s);
    // The object stands at x = 100 turned 90 degrees left: its forward (-Z) is world -X.
    p.bind("Main", {100, 0, 0}, {0, 0, -1, 0, 1, 0, 1, 0, 0});  // rows: right, up, back
    p.seek(1);
    auto* pr = p.state("probe");
    CHECK(pr && std::abs(pr->pos.x - 90) < 1e-3f && std::abs(pr->pos.z) < 1e-3f);
    game::ThnPlayer::View v;
    CHECK(p.view(v));
    // From (0, 50, 0) towards (90, 0, 0).
    float len = std::sqrt(90.f * 90.f + 50.f * 50.f);
    CHECK(std::abs(v.forward.x - 90 / len) < 1e-3f && std::abs(v.forward.y + 50 / len) < 1e-3f);
}

static void test_voices() {
    // Self-written voice list: a voice, its lines, and a later section extending it.
    VoiceDb db;
    db.add(parse_text_ini("[Voice]\nnickname = Pilot_A\nscript = sc_talk_a\nscript = sc_talk_b\n"
                          "[Sound]\nmsg = Line_One\nattenuation = -3\nduration = 1.5\nPriority = 2\n[Sound]\nmsg = line_two\n"
                          "[Voice]\nextend = pilot_a\n[Sound]\nmsg = line_three\n"));
    const Voice* v = db.voice("PILOT_A");
    CHECK(db.size() == 1 && v && v->scripts.size() == 2 && v->lines.size() == 3);
    CHECK(v && v->lines.count("line_one") && v->lines.at("line_one").attenuation == -3 && v->lines.at("line_one").duration == 1.5f &&
          v->lines.at("line_one").priority == 2);
    // Node names: "0x" and eight upper-case hex digits of the nickname hash.
    std::string n = voice_node_name("line_one");
    char want[16];
    std::snprintf(want, sizeof want, "0x%08X", static_cast<unsigned>(fl_create_id("line_one")));
    CHECK(n == want && n.size() == 10);
}

static void test_population() {
    // A self-written system file with one populated zone and one without traffic.
    auto zones = parse_pop_zones(parse_text_ini(
        "[Zone]\nnickname = z_busy\npos = 1000, 0, 0\nrotate = 0, 90, 0\nshape = ELLIPSOID\nsize = 4000, 500, 1000\n"
        "toughness = 2\ndensity = 5\nrepop_time = 20\ndensity_restriction = 2, raiders\n"
        "encounter = enc_a, 2, 0.4\nfaction = grp_a, 0.75\nfaction = grp_b, 0.25\nencounter = enc_b, 1, 0.1\nfaction = grp_c, 1\n"
        "[Zone]\nnickname = z_empty\npos = 0, 0, 0\nshape = SPHERE\nsize = 100\n"));
    CHECK(zones.size() == 2 && zones[0].populated() && !zones[1].populated());
    CHECK(zones[0].encounters.size() == 2 && zones[0].encounters[0].factions.size() == 2 && zones[0].encounters[1].factions[0].first == "grp_c");
    CHECK(zones[0].density_restrictions.size() == 1 && zones[0].density_restrictions[0].ship_class == "raiders");
    // Turned 90 degrees about y, the long x axis now lies along z.
    CHECK(zones[0].contains(1000, 0, 3500) && !zones[0].contains(4500, 0, 0));
    CHECK(zones[1].contains(50, 50, 0) && !zones[1].contains(90, 90, 0));

    auto enc = parse_encounter(parse_text_ini(
        "[EncounterFormation]\nship_by_class = 1, 2, cls_big\npilot_job = job_a\nship_by_class = 0, 3, cls_small, -1\n"
        "pilot_job = job_b\nformation_by_class = shape_a\nbehavior = wander\narrival = all, -tradelane\n"
        "[Creation]\npermutation = 0, 3\n"));
    CHECK(enc.formations.size() == 1 && enc.formations[0].slots.size() == 2 && enc.formations[0].slots[1].level_offset == -1);
    CHECK(enc.formations[0].slots[1].pilot_job == "job_b" && enc.formations[0].arrival.size() == 2 && enc.permutations[0].second == 3.f);

    auto classes = parse_ship_classes(parse_text_ini("[ShipClass]\nnickname = cls_small\nmember = tag_small\n"));
    auto factions = parse_faction_ships(parse_text_ini("[FactionProps]\naffiliation = grp_a\nnpc_ship = s1\nnpc_ship = s2\nnpc_ship = s3\n"));
    auto archs = parse_npc_ship_archs(parse_text_ini(
        "[NPCShipArch]\nnickname = s1\nnpc_class = side_a, tag_small, d1, d2\n"
        "[NPCShipArch]\nnickname = s2\nnpc_class = side_a, tag_big, d2\n"
        "[NPCShipArch]\nnickname = s3\nnpc_class = side_a, tag_small, d3\n"));
    auto picks = ships_for(archs, factions, classes, "GRP_A", "cls_small", 2);
    CHECK(picks.size() == 1 && picks[0]->nickname == "s1");
    CHECK(ships_for(archs, factions, classes, "grp_a", "cls_small", 3).size() == 1);
    CHECK(ships_for(archs, factions, classes, "grp_x", "cls_small", 2).empty());

    // The planner: a zone near the player sends encounters until it holds its density.
    std::vector<PopZone> pz = parse_pop_zones(parse_text_ini(
        "[Zone]\nnickname = z\npos = 0, 0, 0\nshape = SPHERE\nsize = 10000\ndensity = 3\nrepop_time = 10\n"
        "density_restriction = 1, cls_small\nencounter = enc_a, 2, 1\nfaction = grp_a, 1\n"));
    EncounterDef one = parse_encounter(parse_text_ini("[EncounterFormation]\nship_by_class = 1, 1, cls_small\nbehavior = wander\n"));
    game::Population pop(pz, archs, factions, classes, [&](std::string_view n) { return n == "enc_a" ? &one : nullptr; }, 7);
    game::DVec3 player{0, 0, 0};
    auto first = pop.update(0.1f, player);
    CHECK(first.size() == 1 && first[0].ships.size() == 1 && first[0].ships[0]->nickname == "s1" && first[0].behavior == "wander");
    if (!first.empty()) {
        double d = first[0].pos.length();
        CHECK(d >= pop.spawn_min && d <= pop.spawn_max);
    }
    for (int i = 0; i < 300; ++i) CHECK(pop.update(0.1f, player).empty());  // the class limit of 1 holds
    pop.ship_gone(0, first.empty() ? nullptr : first[0].ships[0]);
    CHECK(pop.alive(0) == 0);
    pop.set_enabled(false);
    for (int i = 0; i < 300; ++i) CHECK(pop.update(0.1f, player).empty());  // RandomPop off
    pop.set_enabled(true);
    bool again = false;
    for (int i = 0; i < 300 && !again; ++i) again = !pop.update(0.1f, player).empty();
    CHECK(again);
    CHECK(game::Population(pz, archs, factions, classes, [&](std::string_view) { return &one; }).update(0.1f, game::DVec3{90000, 0, 0}).empty());
}

// A trigger bound to "ANY" system runs wherever the player is.
static void test_mission_any_system() {
    using namespace sterrenvaarder;
    using namespace sterrenvaarder::game;
    MissionScript m = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_any\nsystem = ANY\nInitState = ACTIVE\nCnd_True = no_params\nAct_Ping = a\n"
        "[Trigger]\nnickname = t_other\nsystem = Sys9\nInitState = ACTIVE\nCnd_Timer = 0\nAct_Ping = b\n"));
    struct World : MissionWorld {
        std::string system() const override { return "Sys1"; }
        bool in_space() const override { return true; }
        std::string base() const override { return {}; }
        std::optional<DVec3> position(std::string_view) const override { return std::nullopt; }
        float health(std::string_view) const override { return 1; }
    } w;
    MissionRunner r(m);
    r.start();
    r.update(0.1f, w, [](const MsnCommand&, const MsnTrigger&) {});
    CHECK(r.fired("t_any") && !r.fired("t_other"));

    // A repeatable trigger on a state (docked on a base) fires once per landing.
    MissionScript rep = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = t_land\nInitState = ACTIVE\nrepeatable = true\nCnd_BaseEnter = Base1\nAct_Ping = land\n"));
    struct Dock : MissionWorld {
        bool docked = true;
        std::string system() const override { return "Sys1"; }
        bool in_space() const override { return !docked; }
        std::string base() const override { return docked ? "Base1" : ""; }
        std::optional<DVec3> position(std::string_view) const override { return std::nullopt; }
        float health(std::string_view) const override { return 1; }
    } dock;
    MissionRunner rr(rep);
    int lands = 0;
    auto count = [&](const MsnCommand& c, const MsnTrigger&) { lands += c.name == "Ping"; };
    rr.start();
    for (int i = 0; i < 5; ++i) rr.update(0.1f, dock, count);
    CHECK(lands == 1);
    dock.docked = false;
    rr.update(0.1f, dock, count);
    dock.docked = true;
    rr.update(0.1f, dock, count);
    CHECK(lands == 2);

    // Loot: parsed, and taking it in (LootAcquired = loot, Player) fires.
    MissionScript l = parse_mission(parse_text_ini(
        "[MsnLoot]\nnickname = pod\narchetype = pod_arch\nrel_pos_obj = boss\nrel_pos_offset = 50, 0, 0\nequip_amount = 1\n"
        "[Trigger]\nnickname = t_pod\nInitState = ACTIVE\nCnd_LootAcquired = pod, Player\nAct_Ping = got\n"));
    CHECK(l.loot("pod") && l.loot("pod")->rel_obj == "boss" && l.loot("pod")->offset[0] == 50.f);
    MissionRunner lr(l);
    lr.start();
    lr.update(0.1f, w, [](const MsnCommand&, const MsnTrigger&) {});
    CHECK(!lr.fired("t_pod"));
    lr.event({MissionEvent::Type::LootAcquired, "pod", "Player", ""});
    lr.update(0.1f, w, [](const MsnCommand&, const MsnTrigger&) {});
    CHECK(lr.fired("t_pod"));
}

static void test_mission() {
    using namespace sterrenvaarder;
    using namespace sterrenvaarder::game;
    // A small self-written script: start, a timed call, an escort that must survive and
    // two raiders (one label) whose loss wins the mission.
    const char* text =
        "[Mission]\nmission_title = 10\nreward = 500\n"
        "[MsnShip]\nnickname = raider1\nlabel = raiders\nsystem = Sys1\n"
        "[MsnShip]\nnickname = raider2\nlabel = raiders\nsystem = Sys1\nrel_pos = 180, Player, 2000\n"
        "[Trigger]\nnickname = t_start\nInitState = ACTIVE\nCnd_True = no_params\nAct_ActTrig = t_wait\nAct_ActTrig = t_escort_dies\n"
        "[Trigger]\nnickname = t_wait\nsystem = Sys1\nCnd_Timer = 2\nAct_SendComm = a, Player, LINE_1\nAct_ActTrig = t_after_comm\n"
        "[Trigger]\nnickname = t_after_comm\nCnd_CommComplete = LINE_1\nAct_SpawnShip = raider1\nAct_SpawnShip = raider2\nAct_ActTrig = t_near\n"
        "[Trigger]\nnickname = t_near\nCnd_DistVec = inside, Player, 0, 0, -1000, 300\nAct_ActTrig = t_raiders_dead\n"
        "[Trigger]\nnickname = t_raiders_dead\nCnd_Destroyed = raiders, -1\nAct_ChangeState = SUCCEED, 77\n"
        "[Trigger]\nnickname = t_escort_dies\nCnd_Destroyed = escort, 1, EXPLODE\nAct_ChangeState = FAIL, 66\n"
        "[Trigger]\nnickname = t_watch\nInitState = ACTIVE\nrepeatable = true\nCnd_WatchTrigger = t_wait, OFF\nAct_Ping = x\n";
    MissionScript m = parse_mission(parse_text_ini(text));
    auto archs = parse_npc_ship_archs(parse_text_ini(
        "[NPCShipArch]\nnickname = arch_a\nloadout = lo_a\nship_archetype = ship_a\npilot = pilot_a\nnpc_class = lawful, FIGHTER\n"));
    CHECK(archs.size() == 1 && archs[0].ship_archetype == "ship_a" && archs[0].npc_class.size() == 2);
    auto forms = parse_formations(parse_text_ini("[Formation]\nnickname = vee\npos = 0, 0, 0\npos = -20, 0, 20\npos = 20, 0, 20\n"));
    CHECK(forms.size() == 1 && forms[0].slots.size() == 3 && forms[0].slots[1][0] == -20.f);
    auto rtc = parse_rtc(parse_text_ini(
        "[CharacterEncounter]\nLocation = base_a, bar\noffer = scene_offer.thn\naccept = scene_yes.thn\nmission_text_id = 77\n"
        "[Reserve]\nspot = spot/1\nspot = spot/2\n[Char]\nnpc = npc_a\nactor = actor_a\n"));
    CHECK(rtc.base == "base_a" && rtc.room == "bar" && rtc.is_offer() && rtc.mission_text_id == 77);
    CHECK(rtc.spots.size() == 2 && rtc.chars.size() == 1 && rtc.chars[0].actor == "actor_a");
    auto rtc2 = parse_rtc(parse_text_ini("[CharacterEncounter]\nLocation = base_b, Cityscape\naction = s.thn\nautoplay\n"));
    CHECK(!rtc2.is_offer() && rtc2.autoplay && rtc2.action == "s.thn");
    CHECK(m.title_ids == 10 && m.reward == 500 && m.triggers.size() == 7 && m.ships.size() == 2);
    CHECK(m.trigger("t_wait") && m.trigger("t_wait")->conditions[0].name == "Timer" && m.trigger("t_start")->initially_active);
    CHECK(m.ships[1].rel_pos.object == "Player" && m.ships[1].rel_pos.angle == 180.f && m.ships[1].rel_pos.distance == 2000.f);
    CHECK(m.ships[0].rel_pos.object.empty());

    struct World : MissionWorld {
        std::string sys = "Sys1";
        game::DVec3 player{0, 0, 0};
        std::string system() const override { return sys; }
        bool in_space() const override { return true; }
        std::string base() const override { return {}; }
        std::optional<DVec3> position(std::string_view o) const override {
            if (iequals(o, "Player")) return player;
            return std::nullopt;
        }
        float health(std::string_view) const override { return 1; }
    } w;
    MissionRunner r(m);
    std::vector<std::string> acts;
    auto act = [&](const MsnCommand& c, const MsnTrigger&) {
        acts.push_back(c.name + ":" + c.arg(0));
        if (c.name == "SpawnShip") r.ship_spawned(c.arg(0));
    };
    r.start();
    r.update(0.1f, w, act);  // t_start fires
    CHECK(r.fired("t_start") && r.active("t_wait") && !r.active("t_start"));
    int pings = 0;
    for (auto& a : acts) pings += a == "Ping:x";
    CHECK(pings == 1);  // t_wait was not on yet when this frame's checks were made
    r.update(0.1f, w, act);
    pings = 0;
    for (auto& a : acts) pings += a == "Ping:x";
    CHECK(pings == 1);  // now t_wait is on: the watch waits
    for (int i = 0; i < 25; ++i) r.update(0.1f, w, act);
    CHECK(r.fired("t_wait") && r.active("t_after_comm"));
    CHECK(std::find(acts.begin(), acts.end(), "SendComm:a") != acts.end());
    std::size_t before = acts.size();
    r.update(0.1f, w, act);
    CHECK(r.fired("t_after_comm") == false);  // the comm hasn't finished
    r.event({MissionEvent::Type::CommComplete, "LINE_1", "", ""});
    r.update(0.1f, w, act);
    CHECK(r.fired("t_after_comm") && acts.size() > before);
    r.update(0.1f, w, act);
    CHECK(!r.fired("t_near"));  // too far away
    w.player = {0, 0, -900};
    r.update(0.1f, w, act);
    CHECK(r.fired("t_near") && r.active("t_raiders_dead"));
    {
        // Saved progress comes back without running any actions.
        auto saved = r.progress();
        MissionRunner copy(m);
        copy.restore(saved);
        CHECK(copy.active("t_raiders_dead") && copy.fired("t_near") && !copy.active("t_start"));
        CHECK(copy.progress().active == saved.active && copy.progress().fired == saved.fired);
    }
    r.event({MissionEvent::Type::Destroyed, "raider1", "", ""});
    r.update(0.1f, w, act);
    CHECK(r.outcome() == MissionRunner::Outcome::Running);  // one of two
    r.event({MissionEvent::Type::Destroyed, "raider2", "", ""});
    r.update(0.1f, w, act);
    CHECK(r.outcome() == MissionRunner::Outcome::Succeeded && r.outcome_ids() == 77);
    pings = 0;
    for (auto& a : acts) pings += a == "Ping:x";
    CHECK(pings > 1);  // repeatable: fires on every check once t_wait is off

    // Wrong system: a system-bound trigger waits.
    MissionRunner r2(m);
    r2.start();
    w.sys = "Sys2";
    r2.update(0.1f, w, {});
    for (int i = 0; i < 30; ++i) r2.update(0.1f, w, {});
    CHECK(r2.active("t_wait") && !r2.fired("t_wait"));
    r2.event({MissionEvent::Type::Destroyed, "escort", "", ""});
    r2.update(0.1f, w, {});
    CHECK(r2.outcome() == MissionRunner::Outcome::Failed && r2.outcome_ids() == 66);

    // Rooms: a trigger switched on while the player is already in the room fires.
    MissionScript m3 = parse_mission(parse_text_ini(
        "[Trigger]\nnickname = a\nInitState = ACTIVE\nCnd_True = no_params\nAct_ActTrig = b\n"
        "[Trigger]\nnickname = b\nCnd_LocEnter = Bar, Base_1\nAct_ChangeState = SUCCEED, 1\n"));
    struct Docked : World {
        bool in_space() const override { return false; }
        std::string base() const override { return "base_1"; }
        std::string room() const override { return "bar"; }
    } d;
    MissionRunner r3(m3);
    r3.start();
    r3.update(0.1f, d, {});
    r3.update(0.1f, d, {});
    CHECK(r3.outcome() == MissionRunner::Outcome::Succeeded);
}

static void test_fonts() {
    using namespace sterrenvaarder;
    auto styles = parse_fonts_ini(parse_text_ini("[TrueType]\nnickname = HudSmall\nfont = Agency FB\nfixed_height = 0.03\n"));
    const FontStyle* hs = find_font_style(styles, "hudsmall");
    CHECK(hs && hs->font == "Agency FB" && std::fabs(hs->fixed_height - 0.03f) < 1e-6f);
    std::ifstream f(std::string(SV_SOURCE_DIR) + "/fonts/saira-condensed/SairaCondensed-Medium.ttf", std::ios::binary);
    std::vector<std::uint8_t> ttf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(!ttf.empty() && font_family(Bytes(ttf)) == "Saira Condensed Medium");
    auto atlas = bake_font_atlas(Bytes(ttf));
    CHECK(atlas && atlas->cap_height > 20 && atlas->glyphs['A'].present && atlas->glyphs[0xe9].present);
    CHECK(atlas && atlas->glyphs['W'].advance > atlas->glyphs['i'].advance && !atlas->glyphs[200 - 70].present);
    CHECK(!find_system_font("No Such Font Family 123").has_value());
}

static void test_postfx() {
    using namespace sterrenvaarder;
    PostFx off;
    CHECK(!off.enabled && off.saturation == 1 && off.bloom == 0);  // original look by default
    PostFx a;
    auto w = a.apply_ini("[PostFx]\nenabled = true\npreset = cinematic\nvignette = 0.5\n");
    CHECK(w.empty() && a.enabled && a.vignette == 0.5f && a.grain > 0);
    PostFx b;
    w = b.apply_ini("[Preset]\nnickname = mine\nsaturation = 0.5\n[PostFx]\npreset = Mine\nfoo = 1\n");
    CHECK(b.enabled && b.saturation == 0.5f && w.size() == 1);
    PostFx c;
    w = c.apply_ini("[PostFx]\npreset = vivid\nenabled = false\n");
    CHECK(!c.enabled && c.saturation > 1);
    CHECK(!PostFx::preset("nope", c) && c.saturation > 1);
}

int main() {
    test_mission();
    test_mission_any_system();
    test_new_player_file();
    test_burst();
    test_vibes();
    test_population();
    test_dfm_strips();
    test_skinning();
    test_mbases();
    test_thn_player();
    test_wav();
    test_mixer();
    test_sound_db();
    test_voices();
    test_radio();
    test_mission_start();
    test_thn_attach();
    test_euler_degrees();
    test_mission_music();
    test_music_mood();
    test_anm_frames();
    test_costumes();
    test_fonts();
    test_postfx();
    test_thn();
    test_flight();
    test_lanes();
    test_combat();
    test_resources();
    test_trade();
    test_dealers();
    test_factions();
    test_hash();
    test_text_ini();
    test_bini();
    test_utf();
    test_vfs_normalize();
    test_vfs_layers();
    test_png();
    test_pilots_and_ai();
    test_ale();
    std::printf(failures ? "%d test(s) failed\n" : "all unit tests passed\n", failures);
    return failures ? 1 : 0;
}
