// SPDX-License-Identifier: MPL-2.0
#pragma once
// Player state (credits, cargo, ship, location), commodity trading and save files.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/economy.hpp"
#include "sterrenvaarder/math.hpp"

namespace sterrenvaarder::game {

// An item on the ship. Hardpoint empty: internal equipment (engine, power, scanner...).
struct Mounted {
    std::string item, hardpoint;
};

struct PlayerState {
    std::int64_t credits = 0;
    int rank = 0;  // level: market lines above it stay hidden
    std::string system;
    std::string base;  // docked base; empty while in space
    std::string ship, loadout;
    float hold_size = 0;
    std::map<std::string, int> cargo;  // good nickname (lower case) -> units
    std::vector<Mounted> equipment;
    std::map<std::string, float> rep;  // faction group (lower case) -> -1..1
    std::string base_faction;          // owner of the docked base (not saved; set on docking)

    float rep_with(std::string_view group) const;
    std::optional<Vec3> position;      // set when saved in space

    int units(std::string_view good) const;
};

float cargo_used(const Economy& econ, const PlayerState& p);

enum class TradeResult { Ok, NotDocked, NotSold, NotBought, NoMoney, NoRoom, NoneOwned, NoHardpoint, LevelTooLow, RepTooLow };
const char* trade_message(TradeResult r);

// Most units of a good the player can buy here right now (money and hold space).
int max_buy(const Economy& econ, const PlayerState& p, std::string_view good);
TradeResult buy(const Economy& econ, PlayerState& p, std::string_view good, int count);
TradeResult sell(const Economy& econ, PlayerState& p, std::string_view good, int count);

// Whether the base offers a market line to this player: sold here, level reached and
// enough standing with the base's owner.
bool offered(const MarketEntry& m, const PlayerState& p);

// Equipment dealer: buying mounts the item on the first free hardpoint that accepts it.
// Selling removes the mounted item at `index` in p.equipment.
std::string free_hardpoint(const Economy& econ, const PlayerState& p, std::string_view item);
TradeResult buy_equipment(const Economy& econ, PlayerState& p, std::string_view good);
TradeResult sell_equipment(const Economy& econ, PlayerState& p, std::size_t index);

// Ship dealer: the current ship and everything mounted on it are traded in at this
// base's prices; the new ship comes with its package contents.
std::int64_t trade_in_value(const Economy& econ, const PlayerState& p);
std::int64_t ship_price(const Economy& econ, const PlayerState& p, std::string_view package);
TradeResult buy_ship(const Economy& econ, PlayerState& p, std::string_view package);

// Our own text save format (INI). The original .fl saves can be imported later.
std::string write_save(const PlayerState& p);
std::optional<PlayerState> read_save(std::string_view text);

}  // namespace sterrenvaarder::game
