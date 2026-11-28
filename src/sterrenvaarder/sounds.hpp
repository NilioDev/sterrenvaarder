// SPDX-License-Identifier: MPL-2.0
#pragma once
// The sound lists of the player's data (audio/*.ini, [Sound] sections): a nickname with its
// file, volume and kind. Systems name their music by nickname ([Music]).
#include <string>
#include <string_view>
#include <unordered_map>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

struct SoundDef {
    std::string nickname, file, type;
    float attenuation = 0;  // dB, 0 = full volume (values are negative)
    bool streamer = false;
    float gain() const;     // attenuation as a linear factor
};

class SoundDb {
public:
    void add(const IniFile& ini);
    const SoundDef* find(std::string_view nickname) const;
    std::size_t size() const { return sounds_.size(); }
    const std::unordered_map<std::string, SoundDef>& all() const { return sounds_; }

private:
    std::unordered_map<std::string, SoundDef> sounds_;  // lower-case nickname
};

// A system's [Music]: the tracks for quiet space, danger and battle (sound nicknames).
struct SystemMusic {
    std::string space, danger, battle;
};
SystemMusic parse_system_music(const IniFile& system_ini);

}  // namespace sterrenvaarder
