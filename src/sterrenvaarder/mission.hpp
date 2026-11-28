// SPDX-License-Identifier: MPL-2.0
#pragma once
// Story mission scripts (DATA/MISSIONS/Mxx/mxx.ini): the mission header, its ships, NPCs,
// objective lists, dialogs and the triggers that drive it. A trigger waits for its one
// condition (Cnd_*) while active, then runs its actions (Act_*). Commands keep their raw
// arguments; the mission runner (game/mission.hpp) gives them meaning.
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

struct MsnCommand {
    std::string name;  // without the Cnd_/Act_ prefix, e.g. "Timer", "ActTrig"
    std::vector<IniValue> args;
    std::string arg(std::size_t i) const { return i < args.size() ? args[i].as_string() : std::string(); }
    float num(std::size_t i, float fallback = 0) const { return i < args.size() ? args[i].as_float() : fallback; }
};

struct MsnTrigger {
    std::string nickname;
    std::string system;       // empty: any system
    bool initially_active = false;
    bool repeatable = false;
    std::vector<MsnCommand> conditions;  // the original uses exactly one
    std::vector<MsnCommand> actions;
};

struct MsnShipDef {
    std::string nickname, system, npc;
    std::vector<std::string> labels;
    float position[3] = {0, 0, 0};
    float orientation[4] = {1, 0, 0, 0};  // w, x, y, z
    std::string relative_to;   // relative_pos = object, min, max (spawns near an object)
    bool random_name = false, jumper = false;
    float radius = 0;
    std::string arrival_obj;
    std::vector<std::string> init_objectives;
};

struct MsnNpcDef {
    std::string nickname, ship_arch, affiliation, voice;
    std::vector<std::string> costume;
    int individual_name = 0;
};

struct MsnSolarDef {
    std::string nickname, system, faction, archetype, loadout, base;
    std::vector<std::string> labels;
    float position[3] = {0, 0, 0};
    float orientation[4] = {1, 0, 0, 0};
    int string_id = 0;
};

struct MsnFormationDef {
    std::string nickname, formation;
    std::vector<std::string> ships;
    float position[3] = {0, 0, 0};
    float orientation[4] = {1, 0, 0, 0};
};

struct MsnObjList {
    std::string nickname, system;
    std::vector<MsnCommand> steps;  // GotoShip, GotoVec, BreakFormation, Delay, ...
};

struct MsnDialog {
    struct Line { std::string source, target, line; };
    std::string nickname, system;
    std::vector<Line> lines;
};

struct MsnObjective {
    std::string nickname;
    std::string state;          // ACTIVE / HIDDEN ...
    MsnCommand type;            // type = ids, ... | navmarker, ... | rep_inst, ...
};

struct MissionScript {
    int title_ids = 0, offer_ids = 0;
    int reward = 0;
    std::string npc_ship_file;
    std::vector<MsnTrigger> triggers;
    std::vector<MsnShipDef> ships;
    std::vector<MsnNpcDef> npcs;
    std::vector<MsnSolarDef> solars;
    std::vector<MsnFormationDef> formations;
    std::vector<MsnObjList> obj_lists;
    std::vector<MsnDialog> dialogs;
    std::vector<MsnObjective> objectives;

    const MsnTrigger* trigger(std::string_view nickname) const;
    const MsnShipDef* ship(std::string_view nickname) const;
    const MsnNpcDef* npc(std::string_view nickname) const;
    const MsnObjList* obj_list(std::string_view nickname) const;
    const MsnDialog* dialog(std::string_view nickname) const;
};

MissionScript parse_mission(const IniFile& ini);

// [NPCShipArch] from npcships.ini (the mission's own npc_ship_file, or missions/npcships.ini):
// which ship, loadout and pilot an NPC flies.
struct NpcShipArch {
    std::string nickname, loadout, level, ship_archetype, pilot, state_graph;
    std::vector<std::string> npc_class;
};
std::vector<NpcShipArch> parse_npc_ship_archs(const IniFile& ini);

// [Formation] from missions/formations.ini: each ship's slot relative to the leader.
struct FormationShape {
    std::string nickname;
    std::vector<std::array<float, 3>> slots;  // pos entries, leader first
};
std::vector<FormationShape> parse_formations(const IniFile& ini);

// A room cutscene a mission queues with AddRTC ([CharacterEncounter]): it plays when the
// player enters its room. Offer scenes carry the offer text and the scenes for the offer,
// the decision, accepting and declining; others play one `action` scene.
struct MsnRtc {
    std::string base, room;  // Location
    std::string offer, decision, accept, reject, action;  // .thn scene paths
    int mission_text_id = 0, nooffer_text_id = 0;
    bool autoplay = false;
    std::string relocate_player;  // room the player is moved to
    std::string relocate_base;    // and its base (empty: this one)
    struct Char { std::string npc, actor, fidget; };
    std::vector<Char> chars;
    std::vector<std::string> spots;  // reserved standing spots
    bool is_offer() const { return !offer.empty(); }
};
MsnRtc parse_rtc(const IniFile& ini);

}  // namespace sterrenvaarder
