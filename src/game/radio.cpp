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

}  // namespace sterrenvaarder::game
