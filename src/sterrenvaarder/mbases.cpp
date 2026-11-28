// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/mbases.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

std::string str(const IniEntry& e, std::size_t i) { return e.at(i) ? e.at(i)->as_string() : std::string(); }

}  // namespace

const BaseRoomCrowd* BaseCrowd::room(std::string_view n) const {
    for (auto& r : rooms)
        if (iequals(r.nickname, n)) return &r;
    return nullptr;
}

const BaseNpc* BaseCrowd::npc(std::string_view n) const {
    for (auto& c : npcs)
        if (iequals(c.nickname, n)) return &c;
    return nullptr;
}

std::vector<BaseCrowd> parse_mbases(const IniFile& ini) {
    std::vector<BaseCrowd> out;
    for (auto& sec : ini) {
        if (iequals(sec.name, "MBase")) {
            BaseCrowd b;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) b.nickname = str(e, 0);
                else if (iequals(e.key, "local_faction")) b.local_faction = str(e, 0);
            }
            out.push_back(std::move(b));
            continue;
        }
        if (out.empty()) continue;  // rooms and characters belong to the base before them
        auto& base = out.back();
        if (iequals(sec.name, "MRoom")) {
            BaseRoomCrowd r;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) r.nickname = str(e, 0);
                else if (iequals(e.key, "character_density")) r.density = e.at(0) ? static_cast<int>(e.at(0)->as_float()) : 0;
                else if (iequals(e.key, "fixture")) r.fixtures.push_back({str(e, 0), str(e, 1), str(e, 2), str(e, 3)});
            }
            base.rooms.push_back(std::move(r));
        } else if (iequals(sec.name, "GF_NPC")) {
            BaseNpc n;
            for (auto& e : sec.entries) {
                const auto& k = e.key;
                if (iequals(k, "nickname")) n.nickname = str(e, 0);
                else if (iequals(k, "body")) n.body = str(e, 0);
                else if (iequals(k, "head")) n.head = str(e, 0);
                else if (iequals(k, "lefthand")) n.left_hand = str(e, 0);
                else if (iequals(k, "righthand")) n.right_hand = str(e, 0);
                else if (iequals(k, "accessory")) n.accessories.push_back(str(e, 0));
                else if (iequals(k, "room")) n.room = str(e, 0);
                else if (iequals(k, "affiliation")) n.affiliation = str(e, 0);
                else if (iequals(k, "voice")) n.voice = str(e, 0);
                else if (iequals(k, "individual_name")) n.name_id = e.at(0) ? static_cast<int>(e.at(0)->as_float()) : 0;
            }
            base.npcs.push_back(std::move(n));
        }
    }
    return out;
}

}  // namespace sterrenvaarder
