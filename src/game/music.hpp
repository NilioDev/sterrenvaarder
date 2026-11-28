// SPDX-License-Identifier: MPL-2.0
#pragma once
// Which of a system's three tracks fits the moment: quiet space, danger (enemies near) or
// battle (shots exchanged). The distances and times are our own estimates of the original's
// feel and can be tuned.
#include <limits>
#include <string>
#include <vector>

#include "sterrenvaarder/sounds.hpp"

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

// A mission's PlayMusic = space, danger, battle, motif [, fade seconds [, once]]: tracks that
// stand in for the system's ("none" keeps the system's), and a motif that plays over the mood
// while set, once or looping. PlayMusic = no_params (or all "none") gives the music back.
// This reading comes from the shapes of the values in the data.
struct MissionMusic {
    SystemMusic tracks;  // empty: the system's own
    std::string motif;
    float fade = 2;      // cross-fade seconds
    bool once = false;   // the motif plays once, then the mood track returns
};
MissionMusic parse_play_music(const std::vector<std::string>& args);
// The track for the moment: the motif, else the mission's track for the mood, else the
// system's (danger or battle falls back to space when a list has none, as for systems).
std::string music_track(const MissionMusic& mission, const SystemMusic& system, MusicMood mood);

}  // namespace sterrenvaarder::game
