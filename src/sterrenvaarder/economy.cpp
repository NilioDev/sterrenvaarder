// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/economy.hpp"

#include <algorithm>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

namespace {

std::string str(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

float num(const IniSection& s, std::string_view key, float fallback) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_float() : fallback;
}

std::uint32_t id(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? static_cast<std::uint32_t>(e->values[0].as_int()) : 0;
}

GoodCategory category(std::string_view c) {
    if (iequals(c, "commodity")) return GoodCategory::Commodity;
    if (iequals(c, "equipment")) return GoodCategory::Equipment;
    if (iequals(c, "ship")) return GoodCategory::Ship;
    if (iequals(c, "shiphull")) return GoodCategory::ShipHull;
    return GoodCategory::Other;
}

template <class F>
void each_section(const Vfs& vfs, const std::vector<std::string>& files, F f) {
    for (auto& path : files) {
        if (!vfs.exists(path)) continue;
        for (auto& sec : parse_ini(vfs.read(path))) f(sec);
    }
}

const std::vector<MarketEntry> kNoMarket;

}  // namespace

Economy::Economy(const Vfs& vfs, const DataFiles& files) {
    each_section(vfs, files.goods, [&](const IniSection& sec) {
        if (!iequals(sec.name, "Good")) return;
        Good g;
        g.nickname = str(sec, "nickname");
        if (g.nickname.empty()) return;
        g.category = category(str(sec, "category"));
        g.price = num(sec, "price", 0);
        g.equipment = str(sec, "equipment");
        g.ship = str(sec, "ship");
        g.hull = str(sec, "hull");
        g.ids_name = id(sec, "ids_name");
        g.combinable = iequals(str(sec, "combinable"), "true");
        for (auto* a : sec.find_all("addon")) {
            if (a->values.empty()) continue;
            Good::Addon ad;
            ad.item = a->values[0].as_string();
            if (a->values.size() > 1) ad.hardpoint = a->values[1].as_string();
            if (a->values.size() > 2) ad.count = static_cast<int>(a->values[2].as_float());
            g.addons.push_back(std::move(ad));
        }
        if (g.category == GoodCategory::ShipHull && !g.ship.empty()) hull_by_ship_[to_lower(g.ship)] = to_lower(g.nickname);
        goods_[to_lower(g.nickname)] = std::move(g);
    });

    each_section(vfs, files.equipment, [&](const IniSection& sec) {
        auto nick = str(sec, "nickname");
        if (nick.empty()) return;
        EquipmentItem e;
        e.nickname = nick;
        e.kind = sec.name;
        e.ids_name = id(sec, "ids_name");
        e.ids_info = id(sec, "ids_info");
        e.volume = num(sec, "volume", 0);
        e.hp_class = str(sec, "hp_gun_type");
        if (e.hp_class.empty()) e.hp_class = str(sec, "hp_type");
        if (e.hp_class.empty()) {
            if (iequals(sec.name, "Thruster")) e.hp_class = "hp_thruster";
            else if (iequals(sec.name, "MineDropper")) e.hp_class = "hp_mine_dropper";
            else if (iequals(sec.name, "CounterMeasureDropper")) e.hp_class = "hp_countermeasure_dropper";
        }
        equipment_[to_lower(nick)] = std::move(e);
    });

    each_section(vfs, files.ships, [&](const IniSection& sec) {
        if (!iequals(sec.name, "Ship")) return;
        ShipInfo si;
        si.nickname = str(sec, "nickname");
        if (si.nickname.empty()) return;
        si.ids_name = id(sec, "ids_name");
        si.hold_size = num(sec, "hold_size", 0);
        for (auto* h : sec.find_all("hp_type")) {
            if (h->values.size() < 2) continue;
            std::vector<std::string> hps;
            for (std::size_t i = 1; i < h->values.size(); ++i) hps.push_back(h->values[i].as_string());
            si.hp_types.emplace_back(h->values[0].as_string(), std::move(hps));
        }
        ships_[to_lower(si.nickname)] = std::move(si);
    });

    each_section(vfs, files.markets, [&](const IniSection& sec) {
        if (!iequals(sec.name, "BaseGood")) return;
        auto base = to_lower(str(sec, "base"));
        if (base.empty()) return;
        auto& list = markets_[base];
        for (auto& e : sec.entries) {
            if (!iequals(e.key, "MarketGood") || e.values.size() < 7) continue;
            MarketEntry m;
            m.good = e.values[0].as_string();
            m.level_required = static_cast<int>(e.values[1].as_float());
            m.rep_required = e.values[2].as_float();
            m.min_stock = static_cast<int>(e.values[3].as_float());
            m.max_stock = static_cast<int>(e.values[4].as_float());
            m.base_sells = e.values[5].as_float() == 0;
            m.multiplier = e.values[6].as_float();
            // A later line for the same good replaces the earlier one (mods append).
            bool replaced = false;
            for (auto& old : list)
                if (iequals(old.good, m.good)) { old = m; replaced = true; }
            if (!replaced) list.push_back(std::move(m));
        }
    });

    if (vfs.exists("universe/universe.ini"))
        for (auto& sec : parse_ini(vfs.read("universe/universe.ini"))) {
            if (!iequals(sec.name, "Base")) continue;
            BaseInfo b;
            b.nickname = str(sec, "nickname");
            if (b.nickname.empty()) continue;
            b.system = str(sec, "system");
            b.ids_name = id(sec, "strid_name");
            bases_[to_lower(b.nickname)] = std::move(b);
        }
}

const Good* Economy::good(std::string_view n) const {
    auto it = goods_.find(to_lower(n));
    return it == goods_.end() ? nullptr : &it->second;
}

const EquipmentItem* Economy::equipment(std::string_view n) const {
    auto it = equipment_.find(to_lower(n));
    return it == equipment_.end() ? nullptr : &it->second;
}

const BaseInfo* Economy::base(std::string_view n) const {
    auto it = bases_.find(to_lower(n));
    return it == bases_.end() ? nullptr : &it->second;
}

const std::vector<MarketEntry>& Economy::market(std::string_view base) const {
    auto it = markets_.find(to_lower(base));
    return it == markets_.end() ? kNoMarket : it->second;
}

const MarketEntry* Economy::market_entry(std::string_view base, std::string_view good) const {
    for (auto& m : market(base))
        if (iequals(m.good, good)) return &m;
    return nullptr;
}

float Economy::price(std::string_view base, std::string_view good_name) const {
    auto g = good(good_name);
    if (!g) return 0;
    auto m = market_entry(base, good_name);
    return g->price * (m ? m->multiplier : 1.0f);
}

float Economy::volume(std::string_view good_name) const {
    auto g = good(good_name);
    if (!g || g->category != GoodCategory::Commodity) return 0;
    auto e = equipment(g->equipment.empty() ? g->nickname : g->equipment);
    return e ? e->volume : 1.0f;
}

std::uint32_t Economy::name_id(std::string_view good_name) const {
    auto g = good(good_name);
    if (!g) return 0;
    if (g->category == GoodCategory::Ship) {
        auto h = good(g->hull);
        if (h) return ship_name_id(h->ship);
    }
    if (g->category == GoodCategory::ShipHull) return ship_name_id(g->ship);
    if (g->ids_name) return g->ids_name;
    if (auto e = equipment(g->equipment.empty() ? g->nickname : g->equipment)) return e->ids_name;
    return 0;
}

const Good* Economy::hull_of_ship(std::string_view ship) const {
    auto it = hull_by_ship_.find(to_lower(ship));
    return it == hull_by_ship_.end() ? nullptr : good(it->second);
}

std::uint32_t Economy::ship_name_id(std::string_view ship) const {
    auto s = this->ship(ship);
    return s ? s->ids_name : 0;
}

const ShipInfo* Economy::ship(std::string_view n) const {
    auto it = ships_.find(to_lower(n));
    return it == ships_.end() ? nullptr : &it->second;
}

std::vector<std::string> Economy::hardpoints_for(std::string_view ship_name, std::string_view item) const {
    std::vector<std::string> out;
    auto s = ship(ship_name);
    auto e = equipment(item);
    if (!s || !e || e->hp_class.empty()) return out;
    for (auto& [cls, hps] : s->hp_types)
        if (iequals(cls, e->hp_class))
            for (auto& hp : hps)
                if (std::find_if(out.begin(), out.end(), [&](auto& o) { return iequals(o, hp); }) == out.end()) out.push_back(hp);
    return out;
}

std::optional<NewGameProfile> read_new_character(const Vfs& vfs) {
    if (!vfs.exists("characters/newcharacter.ini")) return std::nullopt;
    auto ini = parse_ini(vfs.read("characters/newcharacter.ini"));
    const IniSection* faction = nullptr;
    for (auto& sec : ini)
        if (iequals(sec.name, "Faction")) { faction = &sec; break; }
    if (!faction) return std::nullopt;
    NewGameProfile p;
    p.base = str(*faction, "base");
    p.rep_group = str(*faction, "rep_group");
    auto package = str(*faction, "Package");
    for (auto& sec : ini)
        if (iequals(sec.name, "Package") && iequals(str(sec, "nickname"), package)) {
            p.ship = str(sec, "ship");
            p.loadout = str(sec, "loadout");
            p.money = static_cast<std::int64_t>(num(sec, "money", 0));
        }
    if (p.base.empty() || p.ship.empty()) return std::nullopt;
    return p;
}

}  // namespace sterrenvaarder
