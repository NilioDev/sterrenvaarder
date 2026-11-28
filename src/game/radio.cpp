// SPDX-License-Identifier: MPL-2.0
#include "game/radio.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

std::vector<std::string> dock_reply_lines(std::string_view base_nickname, bool granted) {
    std::vector<std::string> out;
    if (!base_nickname.empty()) {
        out.push_back("gcs_misc_thisis+");
        out.push_back("gcs_refer_base_" + to_lower(base_nickname) + "-");
    }
    out.push_back(granted ? "gcs_dockrequest_granted_01-" : "gcs_dockrequest_responsehostile_01-");
    return out;
}

DockKind dock_kind(std::string_view archetype) {
    std::string a = to_lower(archetype);
    if (a.find("ring") != std::string::npos) return DockKind::Ring;
    for (const char* cap : {"battleship", "dreadnought", "liner", "cruiser", "carrier"})
        if (a.find(cap) != std::string::npos) return DockKind::Moor;
    return DockKind::Berth;
}

std::vector<std::string> launch_clear_lines(DockKind kind) {
    switch (kind) {
        case DockKind::Ring: return {"gcs_docklaunch_clear_ring_01-"};
        case DockKind::Moor: return {"gcs_docklaunch_clear_moor_01-"};
        default: return {"gcs_docklaunch_clear_berth_01-"};
    }
}

}  // namespace sterrenvaarder::game
