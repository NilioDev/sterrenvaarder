// SPDX-License-Identifier: MPL-2.0
#pragma once
// NPC pilot personalities from missions/pilots_population.ini and pilots_story.ini.
// A [Pilot] names one block of each kind ([GunBlock], [EvadeDodgeBlock], ...) and may
// inherit the blocks of another pilot. Only the values the AI uses are typed here.
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

// A "name, weight" list such as evade_dodge_style_weight = waggle, 1.0.
using Weights = std::vector<std::pair<std::string, float>>;

struct PilotGun {
    float fire_interval = 0.1f, fire_interval_variance = 0;      // between shots in a burst
    float burst_time = 1.f, burst_variance = 0;                   // length of a burst
    float no_burst_time = 1.f;                                    // pause between bursts
    float accuracy_cone_deg = 5.f;                                // largest aiming error
    float accuracy_power = 1.f, accuracy_power_npc = 1.f;         // higher: errors bunch near the centre
    float range_threshold = 1.f, range_threshold_variance = 0;    // fraction of weapon range
    float target_point_switch_time = 0.5f;
};

struct PilotEvadeDodge {
    Weights styles;  // waggle, slide, corkscrew, ...
    Weights directions;
    float cone_deg = 0;  // dodge when the target's nose points within this cone of us
    float cone_variance = 0;
    float interval = 3, interval_variance = 0;
    float time = 0;  // how long one dodge lasts
    float distance = 0, activate_range = 0;
    float turn_throttle = 1, slide_throttle = 0;
};

struct PilotEvadeBreak {
    Weights styles;  // sideways, outrun, reverse
    Weights directions;
    float time = 0, interval = 0;
    float turn_throttle = 1, roll_throttle = 0;
    float afterburner_delay = 0;
};

struct PilotBuzzHeadToward {
    Weights styles;  // straight_to, slide, waggle
    float min_distance = 500, min_distance_variance = 0;  // start the run from this far out
    float max_time_to_head_away = 5;
    float engine_throttle = 1, turn_throttle = 1;
    float dodge_cone_deg = 0, dodge_cone_variance = 0;
    float dodge_interval = 3, dodge_interval_variance = 0;
};

struct PilotBuzzPassBy {
    Weights styles;  // break_away, straight_by
    Weights directions;
    float distance = 200;  // start the pass this close
    float time = 2;        // and keep going this long
    float break_turn_throttle = 1;
};

struct PilotTrail {
    float lock_cone_deg = 30, break_time = 0.5f;
    float max_turn_throttle = 1, distance = 300;
};

struct PilotStrafe {
    float run_away_distance = 600, attack_throttle = 1;
};

struct PilotDamageReaction {
    // Fractions of max hull lost within the trigger time (or since the last reaction).
    float evade_break = 1, evade_dodge_more = 1;
    float afterburner = 1, afterburner_time = 1;
    float roll = 1, roll_time = 1;
};

struct PilotJob {
    float flee_when_hull_damaged = 0;  // flee below this fraction of hull lost (0 = never)
};

struct Pilot {
    std::string nickname;
    PilotGun gun;
    PilotEvadeDodge evade_dodge;
    PilotEvadeBreak evade_break;
    PilotBuzzHeadToward buzz_head_toward;
    PilotBuzzPassBy buzz_pass_by;
    PilotTrail trail;
    PilotStrafe strafe;
    PilotDamageReaction damage_reaction;
    PilotJob job;
    // Which blocks the data actually gave (an absent block switches that behaviour off).
    bool has_evade_dodge = false, has_evade_break = false, has_buzz = false, has_trail = false;
};

class PilotDb {
public:
    PilotDb() = default;
    // Reads every given file that exists (missing files are skipped).
    explicit PilotDb(const Vfs& vfs, const std::vector<std::string>& files = {"missions/pilots_population.ini",
                                                                              "missions/pilots_story.ini"});
    void add(const IniFile& ini);
    // Resolves inheritance; nullptr-like empty result when unknown.
    const Pilot* pilot(std::string_view nickname) const;
    std::size_t size() const { return pilot_ids_.size(); }

private:
    // block kind ("GunBlock") + nickname -> section
    std::unordered_map<std::string, IniSection> blocks_;
    std::unordered_map<std::string, IniSection> pilot_ids_;
    mutable std::unordered_map<std::string, Pilot> resolved_;
    const IniSection* block(std::string_view kind, std::string_view nickname) const;
};

}  // namespace sterrenvaarder
