// SPDX-License-Identifier: MPL-2.0
#pragma once
// Radio calls put together from spoken fragments of a voice (gcs_* lines). The data holds the
// fragments but not the order in which the original strings them together; the orders here
// are our own reading of the fragment names ("+" asks for a follow-up, "-" ends a phrase).
#include <string>
#include <string_view>
#include <vector>

namespace sterrenvaarder::game {

// A base answering the player's docking request: "this is <base>", then granted, or the
// answer to a hostile ship.
std::vector<std::string> dock_reply_lines(std::string_view base_nickname, bool granted);

// A base clearing the player to leave: "clear" for a berth (stations), a ring (planets, through
// their dock ring) or a mooring (capital ships and liners), chosen from the dock's archetype.
enum class DockKind { Berth, Ring, Moor };
DockKind dock_kind(std::string_view archetype);
std::vector<std::string> launch_clear_lines(DockKind kind);

}  // namespace sterrenvaarder::game
