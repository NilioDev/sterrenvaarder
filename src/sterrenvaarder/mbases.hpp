// SPDX-License-Identifier: MPL-2.0
#pragma once
// Who is on a base (missions/mbases.ini): each [MBase] is followed by its rooms ([MRoom]:
// how many characters stand around, and the fixed ones such as the bartender, each at a
// spot of the room's scene with its idle script) and its characters ([GF_NPC]: the parts
// they wear and the room they are in).
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

struct BaseNpc {
    std::string nickname, body, head, left_hand, right_hand, room, affiliation, voice;
    std::vector<std::string> accessories;
    int name_id = 0;  // individual_name (string resource)
};

struct BaseRoomCrowd {
    struct Fixture {
        std::string npc, spot, fidget, role;  // spot: a marker path in the room's scene
    };
    std::string nickname;
    int density = 0;  // character_density: characters standing around
    std::vector<Fixture> fixtures;
};

struct BaseCrowd {
    std::string nickname, local_faction;
    std::vector<BaseRoomCrowd> rooms;
    std::vector<BaseNpc> npcs;
    const BaseRoomCrowd* room(std::string_view nickname) const;
    const BaseNpc* npc(std::string_view nickname) const;
};

std::vector<BaseCrowd> parse_mbases(const IniFile& ini);

}  // namespace sterrenvaarder
