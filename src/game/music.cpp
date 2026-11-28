// SPDX-License-Identifier: MPL-2.0
#include "game/music.hpp"

#include <cstdlib>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

MusicMood choose_mood(const MusicInput& in, MusicMood current, const MusicRules& r) {
    if (in.since_combat <= r.battle_hold) return MusicMood::Battle;
    // Leaving danger (or battle) needs the enemies a little further off than entering it.
    double reach = current == MusicMood::Space ? r.danger_enter : r.danger_leave;
    return in.nearest_hostile <= reach ? MusicMood::Danger : MusicMood::Space;
}

MissionMusic parse_play_music(const std::vector<std::string>& args) {
    MissionMusic m;
    auto at = [&](std::size_t i) -> std::string {
        if (i >= args.size() || iequals(args[i], "none") || iequals(args[i], "no_params")) return {};
        return args[i];
    };
    m.tracks.space = at(0);
    m.tracks.danger = at(1);
    m.tracks.battle = at(2);
    m.motif = at(3);
    if (args.size() > 4) {
        char* end = nullptr;
        float f = std::strtof(args[4].c_str(), &end);
        if (end != args[4].c_str() && f >= 0) m.fade = f;
    }
    if (args.size() > 5) m.once = iequals(args[5], "true") || args[5] == "1";
    return m;
}

std::string music_track(const MissionMusic& mission, const SystemMusic& system, MusicMood mood) {
    if (!mission.motif.empty()) return mission.motif;
    auto pick = [&](const SystemMusic& t) -> std::string {
        if (mood == MusicMood::Battle && !t.battle.empty()) return t.battle;
        if (mood == MusicMood::Danger && !t.danger.empty()) return t.danger;
        return t.space;
    };
    std::string m = pick(mission.tracks);
    return m.empty() ? pick(system) : m;
}

}  // namespace sterrenvaarder::game
