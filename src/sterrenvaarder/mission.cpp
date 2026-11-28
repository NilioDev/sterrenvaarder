// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/mission.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

std::string first(const IniEntry& e) { return e.values.empty() ? std::string() : e.values[0].as_string(); }

void read_vec(const IniEntry& e, float* out, std::size_t n) {
    for (std::size_t i = 0; i < n && i < e.values.size(); ++i) out[i] = e.values[i].as_float();
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

MsnCommand command(const IniEntry& e, std::size_t strip) {
    MsnCommand c;
    c.name = e.key.substr(strip);
    c.args = e.values;
    // "no_params" stands for no arguments.
    if (c.args.size() == 1 && iequals(c.args[0].as_string(), "no_params")) c.args.clear();
    return c;
}

template <class T>
const T* find_nick(const std::vector<T>& v, std::string_view n) {
    for (auto& x : v)
        if (iequals(x.nickname, n)) return &x;
    return nullptr;
}

}  // namespace

const MsnTrigger* MissionScript::trigger(std::string_view n) const { return find_nick(triggers, n); }
const MsnShipDef* MissionScript::ship(std::string_view n) const { return find_nick(ships, n); }
const MsnNpcDef* MissionScript::npc(std::string_view n) const { return find_nick(npcs, n); }
const MsnObjList* MissionScript::obj_list(std::string_view n) const { return find_nick(obj_lists, n); }
const MsnDialog* MissionScript::dialog(std::string_view n) const { return find_nick(dialogs, n); }

MissionScript parse_mission(const IniFile& ini) {
    MissionScript m;
    for (auto& sec : ini) {
        if (iequals(sec.name, "Mission")) {
            for (auto& e : sec.entries) {
                if (e.values.empty()) continue;
                if (iequals(e.key, "mission_title")) m.title_ids = e.values[0].as_int();
                else if (iequals(e.key, "mission_offer")) m.offer_ids = e.values[0].as_int();
                else if (iequals(e.key, "reward")) m.reward = e.values[0].as_int();
                else if (iequals(e.key, "npc_ship_file")) m.npc_ship_file = first(e);
            }
        } else if (iequals(sec.name, "Trigger")) {
            MsnTrigger t;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) t.nickname = first(e);
                else if (iequals(e.key, "system")) t.system = first(e);
                else if (iequals(e.key, "InitState")) t.initially_active = iequals(first(e), "ACTIVE");
                else if (iequals(e.key, "repeatable")) t.repeatable = !e.values.empty() && e.values[0].as_bool();
                else if (starts_with_ci(e.key, "Cnd_")) t.conditions.push_back(command(e, 4));
                else if (starts_with_ci(e.key, "Act_")) t.actions.push_back(command(e, 4));
            }
            m.triggers.push_back(std::move(t));
        } else if (iequals(sec.name, "MsnShip")) {
            MsnShipDef s;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) s.nickname = first(e);
                else if (iequals(e.key, "system")) s.system = first(e);
                else if (iequals(e.key, "NPC")) s.npc = first(e);
                else if (iequals(e.key, "label")) s.labels.push_back(first(e));
                else if (iequals(e.key, "position")) read_vec(e, s.position, 3);
                else if (iequals(e.key, "orientation")) read_vec(e, s.orientation, 4);
                else if (iequals(e.key, "relative_pos")) s.relative_to = first(e);
                else if (iequals(e.key, "random_name")) s.random_name = !e.values.empty() && e.values[0].as_bool();
                else if (iequals(e.key, "jumper")) s.jumper = !e.values.empty() && e.values[0].as_bool();
                else if (iequals(e.key, "radius") && !e.values.empty()) s.radius = e.values[0].as_float();
                else if (iequals(e.key, "arrival_obj")) s.arrival_obj = first(e);
                else if (iequals(e.key, "init_objectives")) s.init_objectives.push_back(first(e));
            }
            m.ships.push_back(std::move(s));
        } else if (iequals(sec.name, "NPC")) {
            MsnNpcDef n;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) n.nickname = first(e);
                else if (iequals(e.key, "npc_ship_arch")) n.ship_arch = first(e);
                else if (iequals(e.key, "affiliation")) n.affiliation = first(e);
                else if (iequals(e.key, "voice")) n.voice = first(e);
                else if (iequals(e.key, "individual_name") && !e.values.empty()) n.individual_name = e.values[0].as_int();
                else if (iequals(e.key, "space_costume"))
                    for (auto& v : e.values) n.costume.push_back(v.as_string());
            }
            m.npcs.push_back(std::move(n));
        } else if (iequals(sec.name, "MsnSolar")) {
            MsnSolarDef s;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) s.nickname = first(e);
                else if (iequals(e.key, "system")) s.system = first(e);
                else if (iequals(e.key, "faction")) s.faction = first(e);
                else if (iequals(e.key, "archetype")) s.archetype = first(e);
                else if (iequals(e.key, "loadout")) s.loadout = first(e);
                else if (iequals(e.key, "base")) s.base = first(e);
                else if (iequals(e.key, "label")) s.labels.push_back(first(e));
                else if (iequals(e.key, "position")) read_vec(e, s.position, 3);
                else if (iequals(e.key, "orientation")) read_vec(e, s.orientation, 4);
                else if (iequals(e.key, "string_id") && !e.values.empty()) s.string_id = e.values[0].as_int();
            }
            m.solars.push_back(std::move(s));
        } else if (iequals(sec.name, "MsnFormation")) {
            MsnFormationDef f;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) f.nickname = first(e);
                else if (iequals(e.key, "formation")) f.formation = first(e);
                else if (iequals(e.key, "ship")) f.ships.push_back(first(e));
                else if (iequals(e.key, "position")) read_vec(e, f.position, 3);
                else if (iequals(e.key, "orientation")) read_vec(e, f.orientation, 4);
            }
            m.formations.push_back(std::move(f));
        } else if (iequals(sec.name, "ObjList")) {
            MsnObjList o;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) o.nickname = first(e);
                else if (iequals(e.key, "system")) o.system = first(e);
                else o.steps.push_back(command(e, 0));
            }
            m.obj_lists.push_back(std::move(o));
        } else if (iequals(sec.name, "Dialog")) {
            MsnDialog d;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) d.nickname = first(e);
                else if (iequals(e.key, "system")) d.system = first(e);
                else if (iequals(e.key, "line") && e.values.size() >= 3)
                    d.lines.push_back({e.values[0].as_string(), e.values[1].as_string(), e.values[2].as_string()});
            }
            m.dialogs.push_back(std::move(d));
        } else if (iequals(sec.name, "NNObjective")) {
            MsnObjective o;
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname")) o.nickname = first(e);
                else if (iequals(e.key, "state")) o.state = first(e);
                else if (iequals(e.key, "type")) o.type = command(e, 0);
            }
            m.objectives.push_back(std::move(o));
        }
    }
    return m;
}

std::vector<FormationShape> parse_formations(const IniFile& ini) {
    std::vector<FormationShape> out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "Formation")) continue;
        FormationShape f;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) f.nickname = first(e);
            else if (iequals(e.key, "pos")) {
                std::array<float, 3> p{0, 0, 0};
                read_vec(e, p.data(), 3);
                f.slots.push_back(p);
            }
        }
        if (!f.nickname.empty()) out.push_back(std::move(f));
    }
    return out;
}

MsnRtc parse_rtc(const IniFile& ini) {
    MsnRtc r;
    for (auto& sec : ini) {
        if (iequals(sec.name, "CharacterEncounter")) {
            for (auto& e : sec.entries) {
                auto v = [&](std::size_t i) { return e.at(i) ? e.at(i)->as_string() : std::string(); };
                if (iequals(e.key, "Location")) {
                    r.base = v(0);
                    r.room = v(1);
                } else if (iequals(e.key, "offer")) r.offer = v(0);
                else if (iequals(e.key, "decision")) r.decision = v(0);
                else if (iequals(e.key, "accept")) r.accept = v(0);
                else if (iequals(e.key, "reject")) r.reject = v(0);
                else if (iequals(e.key, "action")) r.action = v(0);
                else if (iequals(e.key, "mission_text_id")) r.mission_text_id = e.at(0) ? e.at(0)->as_int() : 0;
                else if (iequals(e.key, "nooffer_text_id")) r.nooffer_text_id = e.at(0) ? e.at(0)->as_int() : 0;
                else if (iequals(e.key, "autoplay")) r.autoplay = true;
                else if (iequals(e.key, "relocate_player")) r.relocate_player = v(0);
            }
        } else if (iequals(sec.name, "Reserve")) {
            for (auto* e : sec.find_all("spot"))
                if (e->at(0)) r.spots.push_back(e->at(0)->as_string());
        } else if (iequals(sec.name, "Char")) {
            MsnRtc::Char c;
            for (auto& e : sec.entries) {
                std::string v = e.at(0) ? e.at(0)->as_string() : std::string();
                if (iequals(e.key, "npc")) c.npc = v;
                else if (iequals(e.key, "actor")) c.actor = v;
                else if (iequals(e.key, "fidget")) c.fidget = v;
            }
            r.chars.push_back(std::move(c));
        }
    }
    return r;
}

std::vector<NpcShipArch> parse_npc_ship_archs(const IniFile& ini) {
    std::vector<NpcShipArch> out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "NPCShipArch")) continue;
        NpcShipArch a;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) a.nickname = first(e);
            else if (iequals(e.key, "loadout")) a.loadout = first(e);
            else if (iequals(e.key, "level")) a.level = first(e);
            else if (iequals(e.key, "ship_archetype")) a.ship_archetype = first(e);
            else if (iequals(e.key, "pilot")) a.pilot = first(e);
            else if (iequals(e.key, "state_graph")) a.state_graph = first(e);
            else if (iequals(e.key, "npc_class"))
                for (auto& v : e.values) a.npc_class.push_back(v.as_string());
        }
        if (!a.nickname.empty()) out.push_back(std::move(a));
    }
    return out;
}

}  // namespace sterrenvaarder
