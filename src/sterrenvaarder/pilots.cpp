// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/pilots.hpp"

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

namespace {

const char* const kBlockKinds[][2] = {
    {"GunBlock", "gun_id"},
    {"EvadeDodgeBlock", "evade_dodge_id"},
    {"EvadeBreakBlock", "evade_break_id"},
    {"BuzzHeadTowardBlock", "buzz_head_toward_id"},
    {"BuzzPassByBlock", "buzz_pass_by_id"},
    {"TrailBlock", "trail_id"},
    {"StrafeBlock", "strafe_id"},
    {"DamageReactionBlock", "damage_reaction_id"},
    {"JobBlock", "job_id"},
};

std::string first_string(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

void get(const IniSection* s, std::string_view key, float& out) {
    if (!s) return;
    if (auto e = s->find(key); e && !e->values.empty()) out = e->values[0].as_float();
}

void get_deg(const IniSection* s, std::string_view key, float& out) { get(s, key, out); }

void get_weights(const IniSection* s, std::string_view key, Weights& out) {
    if (!s) return;
    auto all = s->find_all(key);
    if (all.empty()) return;
    out.clear();
    for (auto* e : all)
        if (e->values.size() >= 2) out.emplace_back(to_lower(e->values[0].as_string()), e->values[1].as_float());
}

std::string key_of(std::string_view kind, std::string_view nickname) {
    return to_lower(kind) + "/" + to_lower(nickname);
}

}  // namespace

PilotDb::PilotDb(const Vfs& vfs, const std::vector<std::string>& files) {
    for (auto& f : files)
        if (vfs.exists(f)) add(parse_ini(vfs.read(f)));
}

void PilotDb::add(const IniFile& ini) {
    resolved_.clear();
    for (auto& sec : ini) {
        std::string nick = first_string(sec, "nickname");
        if (nick.empty()) continue;
        if (iequals(sec.name, "Pilot"))
            pilot_ids_[to_lower(nick)] = sec;
        else
            blocks_[key_of(sec.name, nick)] = sec;
    }
}

const IniSection* PilotDb::block(std::string_view kind, std::string_view nickname) const {
    if (nickname.empty()) return nullptr;
    auto it = blocks_.find(key_of(kind, nickname));
    return it == blocks_.end() ? nullptr : &it->second;
}

const Pilot* PilotDb::pilot(std::string_view nickname) const {
    std::string key = to_lower(nickname);
    if (auto it = resolved_.find(key); it != resolved_.end()) return &it->second;
    if (!pilot_ids_.count(key)) return nullptr;

    // Block ids along the inheritance chain: the pilot's own win over its parents'.
    std::unordered_map<std::string, std::string> ids;
    std::string cur = key;
    for (int depth = 0; depth < 16 && !cur.empty(); ++depth) {
        auto it = pilot_ids_.find(cur);
        if (it == pilot_ids_.end()) break;
        for (auto& kv : kBlockKinds) {
            std::string id = first_string(it->second, kv[1]);
            if (!id.empty() && !ids.count(kv[0])) ids[kv[0]] = id;
        }
        cur = to_lower(first_string(it->second, "inherit"));
    }
    auto blk = [&](const char* kind) { return ids.count(kind) ? block(kind, ids[kind]) : nullptr; };

    Pilot p;
    p.nickname = pilot_ids_.at(key).find("nickname")->values[0].as_string();

    if (auto* g = blk("GunBlock")) {
        auto& o = p.gun;
        get(g, "gun_fire_interval_time", o.fire_interval);
        get(g, "gun_fire_interval_variance_percent", o.fire_interval_variance);
        get(g, "gun_fire_burst_interval_time", o.burst_time);
        get(g, "gun_fire_burst_interval_variance_percent", o.burst_variance);
        get(g, "gun_fire_no_burst_interval_time", o.no_burst_time);
        get_deg(g, "gun_fire_accuracy_cone_angle", o.accuracy_cone_deg);
        get(g, "gun_fire_accuracy_power", o.accuracy_power);
        o.accuracy_power_npc = o.accuracy_power;
        get(g, "gun_fire_accuracy_power_npc", o.accuracy_power_npc);
        get(g, "gun_range_threshold", o.range_threshold);
        get(g, "gun_range_threshold_variance_percent", o.range_threshold_variance);
        get(g, "gun_target_point_switch_time", o.target_point_switch_time);
    }
    if (auto* d = blk("EvadeDodgeBlock")) {
        p.has_evade_dodge = true;
        auto& o = p.evade_dodge;
        get_weights(d, "evade_dodge_style_weight", o.styles);
        get_weights(d, "evade_dodge_direction_weight", o.directions);
        get_deg(d, "evade_dodge_cone_angle", o.cone_deg);
        get(d, "evade_dodge_cone_angle_variance_percent", o.cone_variance);
        get(d, "evade_dodge_interval_time", o.interval);
        get(d, "evade_dodge_interval_time_variance_percent", o.interval_variance);
        get(d, "evade_dodge_time", o.time);
        get(d, "evade_dodge_distance", o.distance);
        get(d, "evade_activate_range", o.activate_range);
        get(d, "evade_dodge_turn_throttle", o.turn_throttle);
        get(d, "evade_dodge_slide_throttle", o.slide_throttle);
    }
    if (auto* b = blk("EvadeBreakBlock")) {
        p.has_evade_break = true;
        auto& o = p.evade_break;
        get_weights(b, "evade_break_style_weight", o.styles);
        get_weights(b, "evade_break_direction_weight", o.directions);
        get(b, "evade_break_time", o.time);
        get(b, "evade_break_interval_time", o.interval);
        get(b, "evade_break_turn_throttle", o.turn_throttle);
        get(b, "evade_break_roll_throttle", o.roll_throttle);
        get(b, "evade_break_afterburner_delay", o.afterburner_delay);
    }
    if (auto* h = blk("BuzzHeadTowardBlock")) {
        p.has_buzz = true;
        auto& o = p.buzz_head_toward;
        get_weights(h, "buzz_head_toward_style_weight", o.styles);
        get(h, "buzz_min_distance_to_head_toward", o.min_distance);
        get(h, "buzz_min_distance_to_head_toward_variance_percent", o.min_distance_variance);
        get(h, "buzz_max_time_to_head_away", o.max_time_to_head_away);
        get(h, "buzz_head_toward_engine_throttle", o.engine_throttle);
        get(h, "buzz_head_toward_turn_throttle", o.turn_throttle);
        get_deg(h, "buzz_dodge_cone_angle", o.dodge_cone_deg);
        get(h, "buzz_dodge_cone_angle_variance_percent", o.dodge_cone_variance);
        get(h, "buzz_dodge_interval_time", o.dodge_interval);
        get(h, "buzz_dodge_interval_time_variance_percent", o.dodge_interval_variance);
    }
    if (auto* b = blk("BuzzPassByBlock")) {
        auto& o = p.buzz_pass_by;
        get_weights(b, "buzz_pass_by_style_weight", o.styles);
        get_weights(b, "buzz_break_direction_weight", o.directions);
        get(b, "buzz_distance_to_pass_by", o.distance);
        get(b, "buzz_pass_by_time", o.time);
        get(b, "buzz_break_turn_throttle", o.break_turn_throttle);
    }
    if (auto* t = blk("TrailBlock")) {
        p.has_trail = true;
        auto& o = p.trail;
        get_deg(t, "trail_lock_cone_angle", o.lock_cone_deg);
        get(t, "trail_break_time", o.break_time);
        get(t, "trail_max_turn_throttle", o.max_turn_throttle);
        get(t, "trail_distance", o.distance);
    }
    if (auto* s = blk("StrafeBlock")) {
        get(s, "strafe_run_away_distance", p.strafe.run_away_distance);
        get(s, "strafe_attack_throttle", p.strafe.attack_throttle);
    }
    if (auto* r = blk("DamageReactionBlock")) {
        auto& o = p.damage_reaction;
        get(r, "evade_break_damage_trigger_percent", o.evade_break);
        get(r, "evade_dodge_more_damage_trigger_percent", o.evade_dodge_more);
        get(r, "afterburner_damage_trigger_percent", o.afterburner);
        get(r, "afterburner_damage_trigger_time", o.afterburner_time);
        get(r, "roll_damage_trigger_percent", o.roll);
        get(r, "roll_damage_trigger_time", o.roll_time);
    }
    if (auto* j = blk("JobBlock")) get(j, "flee_when_hull_damaged_percent", p.job.flee_when_hull_damaged);

    return &(resolved_[key] = std::move(p));
}

}  // namespace sterrenvaarder
