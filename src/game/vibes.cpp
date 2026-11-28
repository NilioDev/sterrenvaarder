// SPDX-License-Identifier: MPL-2.0
#include "game/vibes.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

float rep_value(std::string_view n) {
    std::string s = to_lower(n);
    bool max = s.find("maximum") != std::string::npos;
    // REP_NEUTRAL_HOSTILE / REP_NEUTRAL_FRIENDLY lean one way without crossing a threshold.
    if (s.find("neutral") != std::string::npos) {
        if (s.find("hostile") != std::string::npos) return -0.3f;
        if (s.find("friend") != std::string::npos) return 0.3f;
        return 0.f;
    }
    if (s.find("hostile") != std::string::npos) return max ? -0.9f : -0.65f;
    if (s.find("friend") != std::string::npos) return max ? 0.9f : 0.65f;
    return 0.f;
}

void Vibes::set(std::string_view from, std::string_view to, float rep) { vibes_[{to_lower(from), to_lower(to)}] = rep; }

std::optional<float> Vibes::between(const std::vector<std::string>& a, const std::vector<std::string>& b) const {
    for (auto& x : a)
        for (auto& y : b)
            if (auto it = vibes_.find({to_lower(x), to_lower(y)}); it != vibes_.end()) return it->second;
    return std::nullopt;
}

}  // namespace sterrenvaarder::game
