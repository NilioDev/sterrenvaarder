// SPDX-License-Identifier: MPL-2.0
#include "game/music.hpp"

namespace sterrenvaarder::game {

MusicMood choose_mood(const MusicInput& in, MusicMood current, const MusicRules& r) {
    if (in.since_combat <= r.battle_hold) return MusicMood::Battle;
    // Leaving danger (or battle) needs the enemies a little further off than entering it.
    double reach = current == MusicMood::Space ? r.danger_enter : r.danger_leave;
    return in.nearest_hostile <= reach ? MusicMood::Danger : MusicMood::Space;
}

}  // namespace sterrenvaarder::game
