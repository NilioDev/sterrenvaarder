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
#include "game/trade.hpp"
#include "sterrenvaarder/economy.hpp"
#include "sterrenvaarder/factions.hpp"
#include "sterrenvaarder/ale.hpp"
#include "sterrenvaarder/effects.hpp"
#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/lua32.hpp"
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
    SystemObject gate;
    gate.pos = {100, 0, 0};
    gate.rotate_deg = {0, 180, 0};
    auto sp = exit_point(gate, 400);
    CHECK(std::fabs(sp.pos.z - 400) < 0.01 && std::fabs(sp.pos.x - 100) < 0.01);
}

static void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) { b[o] = v & 0xFF; b[o + 1] = v >> 8; }
static void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) { put16(b, o, v & 0xFFFF); put16(b, o + 2, v >> 16); }

// A minimal PE32 with one string block (ids 16..31) in a .rsrc section.
static void test_resources() {
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
                                                 "[ForSaleShipPlacement]\nname = X/Pad/01\n[ForSaleShipPlacement]\nname = X/Pad/02\n";
    Vfs vfs;
    vfs.mount(root, "base");
    auto file = base_file_of(vfs, "x_base");
    auto rooms = BaseRooms::load(vfs, file);
    CHECK(rooms && rooms->start_room == "Deck" && rooms->rooms.size() == 1);
    auto deck = rooms ? rooms->room("deck") : nullptr;
    CHECK(deck && deck->camera == "Camera_0" && deck->scenes.size() == 1 && deck->hotspots.size() == 1 &&
          deck->hotspots[0].room_switch == "Bar");
    CHECK(deck && deck->ship_placements.size() == 2 && deck->ship_placements[1] == "X/Pad/02");
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
}

static void test_mission() {
    using namespace sterrenvaarder;
    using namespace sterrenvaarder::game;
    // A small self-written script: start, a timed call, an escort that must survive and
    // two raiders (one label) whose loss wins the mission.
    const char* text =
        "[Mission]\nmission_title = 10\nreward = 500\n"
        "[MsnShip]\nnickname = raider1\nlabel = raiders\nsystem = Sys1\n"
        "[MsnShip]\nnickname = raider2\nlabel = raiders\nsystem = Sys1\n"
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
    CHECK(m.title_ids == 10 && m.reward == 500 && m.triggers.size() == 7 && m.ships.size() == 2);
    CHECK(m.trigger("t_wait") && m.trigger("t_wait")->conditions[0].name == "Timer" && m.trigger("t_start")->initially_active);

    struct World : MissionWorld {
        std::string sys = "Sys1";
        DVec3 player{0, 0, 0};
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
