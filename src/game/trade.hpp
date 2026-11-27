// SPDX-License-Identifier: MPL-2.0
#pragma once
// Player state (credits, cargo, ship, location), commodity trading and save files.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "sterrenvaarder/economy.hpp"
#include "sterrenvaarder/math.hpp"

namespace sterrenvaarder::game {

struct PlayerState {
    std::int64_t credits = 0;
    std::string system;
    std::string base;  // docked base; empty while in space
    std::string ship, loadout;
    float hold_size = 0;
    std::map<std::string, int> cargo;  // good nickname (lower case) -> units
    std::optional<Vec3> position;      // set when saved in space

    int units(std::string_view good) const;
};

float cargo_used(const Economy& econ, const PlayerState& p);

enum class TradeResult { Ok, NotDocked, NotSold, NotBought, NoMoney, NoRoom, NoneOwned };
const char* trade_message(TradeResult r);

// Most units of a good the player can buy here right now (money and hold space).
int max_buy(const Economy& econ, const PlayerState& p, std::string_view good);
TradeResult buy(const Economy& econ, PlayerState& p, std::string_view good, int count);
TradeResult sell(const Economy& econ, PlayerState& p, std::string_view good, int count);

// Our own text save format (INI). The original .fl saves can be imported later.
std::string write_save(const PlayerState& p);
std::optional<PlayerState> read_save(std::string_view text);

}  // namespace sterrenvaarder::game
