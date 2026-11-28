// SPDX-License-Identifier: MPL-2.0
#pragma once
// Which of a system's three tracks fits the moment: quiet space, danger (enemies near) or
// battle (shots exchanged). The distances and times are our own estimates of the original's
// feel and can be tuned.
#include <limits>

namespace sterrenvaarder::game {

enum class MusicMood { Space, Danger, Battle };

struct MusicInput {
    double nearest_hostile = std::numeric_limits<double>::infinity();  // metres
    float since_combat = std::numeric_limits<float>::infinity();       // seconds since a shot hit or was fired at a foe
};

struct MusicRules {
    double danger_enter = 5000, danger_leave = 6500;  // hostile distance, with some hysteresis
    float battle_hold = 10;                           // battle music lasts this long after the last shot
};

MusicMood choose_mood(const MusicInput& in, MusicMood current, const MusicRules& rules = {});

}  // namespace sterrenvaarder::game
