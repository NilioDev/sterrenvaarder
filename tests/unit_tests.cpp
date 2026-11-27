// SPDX-License-Identifier: MPL-2.0
// Unit tests that need no game data. Run: sterrenvaarder_tests
#include <cstdio>
#include <cstring>
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

static void test_png() {
    Image img;
    img.width = 2;
    img.height = 2;
    img.rgba.assign(16, 200);
    auto png = encode_png(img);
    CHECK(png.size() > 33 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
}

int main() {
    test_hash();
    test_text_ini();
    test_bini();
    test_utf();
    test_vfs_normalize();
    test_png();
    std::printf(failures ? "%d test(s) failed\n" : "all unit tests passed\n", failures);
    return failures ? 1 : 0;
}
