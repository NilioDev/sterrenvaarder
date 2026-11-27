// SPDX-License-Identifier: MPL-2.0
#include "game/trade.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder::game {

int PlayerState::units(std::string_view good) const {
    auto it = cargo.find(to_lower(good));
    return it == cargo.end() ? 0 : it->second;
}

float cargo_used(const Economy& econ, const PlayerState& p) {
    float used = 0;
    for (auto& [good, n] : p.cargo) used += econ.volume(good) * n;
    return used;
}

const char* trade_message(TradeResult r) {
    switch (r) {
        case TradeResult::Ok: return "OK";
        case TradeResult::NotDocked: return "NOT DOCKED";
        case TradeResult::NotSold: return "NOT FOR SALE HERE";
        case TradeResult::NotBought: return "NOT BOUGHT HERE";
        case TradeResult::NoMoney: return "NOT ENOUGH CREDITS";
        case TradeResult::NoRoom: return "CARGO HOLD FULL";
        case TradeResult::NoneOwned: return "NONE IN CARGO";
    }
    return "";
}

namespace {

std::int64_t unit_price(const Economy& econ, const PlayerState& p, std::string_view good) {
    return static_cast<std::int64_t>(std::lround(econ.price(p.base, good)));
}

}  // namespace

int max_buy(const Economy& econ, const PlayerState& p, std::string_view good) {
    auto m = econ.market_entry(p.base, good);
    if (p.base.empty() || !m || !m->base_sells) return 0;
    std::int64_t price = unit_price(econ, p, good);
    std::int64_t by_money = price > 0 ? p.credits / price : 1 << 20;
    float vol = econ.volume(good);
    float free = p.hold_size - cargo_used(econ, p);
    std::int64_t by_room = vol > 0 ? static_cast<std::int64_t>(std::floor(free / vol + 1e-4f)) : 1 << 20;
    return static_cast<int>(std::max<std::int64_t>(0, std::min(by_money, by_room)));
}

TradeResult buy(const Economy& econ, PlayerState& p, std::string_view good, int count) {
    if (p.base.empty()) return TradeResult::NotDocked;
    auto m = econ.market_entry(p.base, good);
    auto g = econ.good(good);
    if (!m || !m->base_sells || !g || g->category != GoodCategory::Commodity) return TradeResult::NotSold;
    std::int64_t price = unit_price(econ, p, good);
    if (price * count > p.credits) return TradeResult::NoMoney;
    if (cargo_used(econ, p) + econ.volume(good) * count > p.hold_size + 1e-4f) return TradeResult::NoRoom;
    p.credits -= price * count;
    p.cargo[to_lower(good)] += count;
    return TradeResult::Ok;
}

TradeResult sell(const Economy& econ, PlayerState& p, std::string_view good, int count) {
    if (p.base.empty()) return TradeResult::NotDocked;
    auto it = p.cargo.find(to_lower(good));
    if (it == p.cargo.end() || it->second < count) return TradeResult::NoneOwned;
    if (!econ.market_entry(p.base, good)) return TradeResult::NotBought;
    p.credits += unit_price(econ, p, good) * count;
    if ((it->second -= count) == 0) p.cargo.erase(it);
    return TradeResult::Ok;
}

std::string write_save(const PlayerState& p) {
    std::string out = "; Sterrenvaarder save\n[Player]\nversion = 1\n";
    out += "money = " + std::to_string(p.credits) + "\n";
    out += "system = " + p.system + "\n";
    if (!p.base.empty()) out += "base = " + p.base + "\n";
    if (p.position && p.base.empty()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "pos = %.1f, %.1f, %.1f\n", p.position->x, p.position->y, p.position->z);
        out += buf;
    }
    out += "ship = " + p.ship + "\n";
    if (!p.loadout.empty()) out += "loadout = " + p.loadout + "\n";
    char hold[48];
    std::snprintf(hold, sizeof hold, "hold_size = %g\n", p.hold_size);
    out += hold;
    for (auto& [good, n] : p.cargo) out += "cargo = " + good + ", " + std::to_string(n) + "\n";
    return out;
}

std::optional<PlayerState> read_save(std::string_view text) {
    for (auto& sec : parse_text_ini(text)) {
        if (!iequals(sec.name, "Player")) continue;
        PlayerState p;
        auto s = [&](std::string_view k) {
            auto e = sec.find(k);
            return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
        };
        auto money = sec.find("money");
        if (!money || money->values.empty()) return std::nullopt;
        p.credits = std::strtoll(money->values[0].s.c_str(), nullptr, 10);
        p.system = s("system");
        p.base = s("base");
        p.ship = s("ship");
        p.loadout = s("loadout");
        if (auto h = sec.find("hold_size"); h && !h->values.empty()) p.hold_size = h->values[0].as_float();
        if (auto pos = sec.find("pos"); pos && pos->values.size() >= 3)
            p.position = Vec3{pos->values[0].as_float(), pos->values[1].as_float(), pos->values[2].as_float()};
        for (auto* c : sec.find_all("cargo"))
            if (c->values.size() >= 2) {
                int n = static_cast<int>(c->values[1].as_float());
                if (n > 0) p.cargo[to_lower(c->values[0].as_string())] += n;
            }
        if (p.system.empty() || p.ship.empty()) return std::nullopt;
        return p;
    }
    return std::nullopt;
}

}  // namespace sterrenvaarder::game
