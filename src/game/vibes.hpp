// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sterrenvaarder::game {

// Standing for a mission's REP_* names (SetVibe and friends). The values are our own
// estimates on the -1..1 scale the factions use.
float rep_value(std::string_view rep_name);

// How mission ships feel about each other, set by SetVibe actions. A ship is known by its
// nickname and its labels; a vibe names either.
class Vibes {
public:
    void set(std::string_view from, std::string_view to, float rep);
    // What a ship called by any of `a` feels about one called by any of `b`, if a mission said.
    std::optional<float> between(const std::vector<std::string>& a, const std::vector<std::string>& b) const;
    void clear() { vibes_.clear(); }

private:
    std::map<std::pair<std::string, std::string>, float> vibes_;  // lower-case names
};

}  // namespace sterrenvaarder::game
