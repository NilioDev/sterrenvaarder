// SPDX-License-Identifier: MPL-2.0
#pragma once
// Goods, markets and bases from the player's data (goods.ini, market_*.ini, universe.ini).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ships.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

enum class GoodCategory { Commodity, Equipment, Ship, ShipHull, Other };

struct Good {
    std::string nickname;
    GoodCategory category = GoodCategory::Other;
    float price = 0;
    std::string equipment;  // commodity/equipment: the equipment item
    std::string ship;       // ship hull: ship archetype
    std::string hull;       // ship package: the hull good
    std::uint32_t ids_name = 0;
    bool combinable = false;
    struct Addon { std::string item, hardpoint; int count = 1; };
    std::vector<Addon> addons;  // ship package contents
};

struct EquipmentItem {
    std::string nickname;
    std::string kind;  // INI section name: Commodity, Gun, Shield, ...
    std::uint32_t ids_name = 0, ids_info = 0;
    float volume = 0;
    std::string hp_class;  // hardpoint class it mounts on (hp_gun_special_1, hp_thruster, ...)
};

struct ShipInfo {
    std::string nickname;
    std::uint32_t ids_name = 0;
    float hold_size = 0;
    // Hardpoint classes and the hardpoints that accept them (shiparch hp_type lines).
    std::vector<std::pair<std::string, std::vector<std::string>>> hp_types;
};

// One MarketGood line: good, level required, reputation required, min stock, max stock,
// "base does not sell it" flag, price multiplier.
struct MarketEntry {
    std::string good;
    int level_required = 0;
    float rep_required = -1;
    int min_stock = 0, max_stock = 0;
    bool base_sells = true;
    float multiplier = 1;
};

struct BaseInfo {
    std::string nickname;
    std::string system;
    std::uint32_t ids_name = 0;
};

class Economy {
public:
    Economy(const Vfs& vfs, const DataFiles& files = {});

    const Good* good(std::string_view nickname) const;
    const EquipmentItem* equipment(std::string_view nickname) const;
    const BaseInfo* base(std::string_view nickname) const;
    // Market lines of a base in data order (empty when the base has no market).
    const std::vector<MarketEntry>& market(std::string_view base) const;
    const MarketEntry* market_entry(std::string_view base, std::string_view good) const;

    // Price of a good at a base (the same for buying and selling, as in the original),
    // or the good's base price when the base does not list it.
    float price(std::string_view base, std::string_view good) const;
    // Cargo space one unit takes; 0 for goods that do not go in the hold.
    float volume(std::string_view good) const;
    // String id for the good's display name (good, then equipment, then ship).
    std::uint32_t name_id(std::string_view good) const;
    // Hull good of a ship archetype (for its trade-in value), if any.
    const Good* hull_of_ship(std::string_view ship) const;
    std::uint32_t ship_name_id(std::string_view ship) const;
    const ShipInfo* ship(std::string_view nickname) const;
    // Hardpoints of a ship that accept an equipment item, in data order.
    std::vector<std::string> hardpoints_for(std::string_view ship, std::string_view item) const;

    std::size_t good_count() const { return goods_.size(); }
    std::size_t market_count() const { return markets_.size(); }

private:
    std::unordered_map<std::string, Good> goods_;
    std::unordered_map<std::string, EquipmentItem> equipment_;
    std::unordered_map<std::string, BaseInfo> bases_;
    std::unordered_map<std::string, std::vector<MarketEntry>> markets_;
    std::unordered_map<std::string, std::string> hull_by_ship_;
    std::unordered_map<std::string, ShipInfo> ships_;
};

// Start of a new game from Characters/NewCharacter.ini ([Faction] + [Package]).
struct NewGameProfile {
    std::string base;
    std::string ship, loadout;
    std::int64_t money = 0;
};
std::optional<NewGameProfile> read_new_character(const Vfs& vfs);

}  // namespace sterrenvaarder
