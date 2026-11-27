// SPDX-License-Identifier: MPL-2.0
#pragma once
// Factions ("groups") and how they feel about each other, from InitialWorld.ini.
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

struct Faction {
    std::string nickname;
    std::uint32_t ids_name = 0, ids_short_name = 0;
    std::map<std::string, float> rep;  // other group (lower case) -> -1..1
};

enum class Attitude { Hostile = -1, Neutral = 0, Friendly = 1 };

// Our thresholds (the game's own are not in its data files): at or below -0.6 is
// hostile, at or above 0.6 friendly.
constexpr float kHostileRep = -0.6f, kFriendlyRep = 0.6f;
inline Attitude attitude_of(float rep) {
    return rep <= kHostileRep ? Attitude::Hostile : rep >= kFriendlyRep ? Attitude::Friendly : Attitude::Neutral;
}

class Factions {
public:
    explicit Factions(const Vfs& vfs, std::string_view initial_world = "initialworld.ini");
    const Faction* faction(std::string_view nickname) const;
    float rep(std::string_view from, std::string_view to) const;  // 0 when unknown
    std::size_t size() const { return factions_.size(); }

private:
    std::unordered_map<std::string, Faction> factions_;
};

// "house = 0.91, li_n_grp" lines of a character file such as EXE/MPNewCharacter.FL.
std::map<std::string, float> read_house_lines(std::string_view text);

}  // namespace sterrenvaarder
