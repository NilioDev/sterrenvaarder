// SPDX-License-Identifier: MPL-2.0
#include "game/mission.hpp"

#include <algorithm>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder::game {

MissionRunner::MissionRunner(const MissionScript& script) : script_(script), states_(script.triggers.size()) {
    for (std::size_t i = 0; i < script.triggers.size(); ++i) index_.emplace(to_lower(script.triggers[i].nickname), i);
    for (auto& s : script.ships)
        for (auto& l : s.labels) ship_labels_[to_lower(s.nickname)].push_back(to_lower(l));
}

void MissionRunner::start() {
    for (auto& t : script_.triggers)
        if (t.initially_active) activate(t.nickname);
}

void MissionRunner::activate(std::string_view trigger) {
    auto it = index_.find(to_lower(trigger));
    if (it == index_.end()) return;
    auto& s = states_[it->second];
    s.active = true;
    s.since = clock_;
    s.events_from = events_.size();
}

MissionRunner::Progress MissionRunner::progress() const {
    Progress p;
    for (std::size_t i = 0; i < states_.size(); ++i) {
        auto& n = script_.triggers[i].nickname;
        if (states_[i].active) p.active.push_back(n);
        if (states_[i].fired) p.fired.emplace_back(n, states_[i].fired);
    }
    p.outcome = static_cast<int>(outcome_);
    return p;
}

void MissionRunner::restore(const Progress& p) {
    for (auto& s : states_) s = State{};
    events_.clear();
    for (auto& [n, count] : p.fired)
        if (auto it = index_.find(to_lower(n)); it != index_.end()) states_[it->second].fired = count;
    for (auto& n : p.active) activate(n);
    outcome_ = static_cast<Outcome>(p.outcome);
}

bool MissionRunner::active(std::string_view trigger) const {
    auto it = index_.find(to_lower(trigger));
    return it != index_.end() && states_[it->second].active;
}

bool MissionRunner::fired(std::string_view trigger) const {
    auto it = index_.find(to_lower(trigger));
    return it != index_.end() && states_[it->second].fired > 0;
}

void MissionRunner::event(MissionEvent e) {
    if (e.type == MissionEvent::Type::Destroyed) {
        std::string n = to_lower(e.a);
        ++destroyed_[n];
        if (auto it = ship_labels_.find(n); it != ship_labels_.end())
            for (auto& l : it->second) ++destroyed_["label:" + l];
    }
    events_.push_back(std::move(e));
}

void MissionRunner::ship_spawned(std::string_view nickname) {
    if (auto it = ship_labels_.find(to_lower(nickname)); it != ship_labels_.end())
        for (auto& l : it->second) ++spawned_labels_[l];
}

int MissionRunner::label_size(std::string_view label) const {
    auto it = spawned_labels_.find(to_lower(label));
    return it == spawned_labels_.end() ? 0 : it->second;
}

int MissionRunner::destroyed_count(std::string_view name) const {
    std::string n = to_lower(name);
    if (auto it = destroyed_.find(n); it != destroyed_.end()) return it->second;
    if (auto it = destroyed_.find("label:" + n); it != destroyed_.end()) return it->second;
    return 0;
}

bool MissionRunner::waiting_for(std::string_view condition) const {
    for (std::size_t i = 0; i < states_.size(); ++i)
        if (states_[i].active)
            for (auto& c : script_.triggers[i].conditions)
                if (iequals(c.name, condition)) return true;
    return false;
}

bool MissionRunner::knows_condition(std::string_view n) {
    static const char* known[] = {"True",       "Timer",        "CommComplete", "Destroyed",    "WatchTrigger",
                                  "DistShip",   "DistVec",      "HealthDec",    "SpaceEnter",   "SpaceExit",
                                  "BaseEnter",  "BaseExit",     "SystemEnter",  "SystemExit",   "PlayerLaunch",
                                  "LaunchComplete", "TLEntered", "TLExited",    "LocEnter",     "LocExit",
                                  "MsnResponse", "ProjHit",     "InSpace"};
    for (auto* k : known)
        if (iequals(n, k)) return true;
    return false;
}

bool MissionRunner::holds(const MsnTrigger&, const MsnCommand& c, const State& s, const MissionWorld& w) const {
    using T = MissionEvent::Type;
    auto any_event = [&](T type, auto&& match) {
        for (std::size_t i = s.events_from; i < events_.size(); ++i)
            if (events_[i].type == type && match(events_[i])) return true;
        return false;
    };
    auto is = [](const std::string& a, const std::string& b) { return b.empty() || iequals(a, b) || iequals(b, "ANY"); };
    const std::string& n = c.name;
    if (iequals(n, "True")) return true;
    if (iequals(n, "Timer")) return clock_ - s.since >= c.num(0);
    if (iequals(n, "CommComplete"))
        return any_event(T::CommComplete, [&](const MissionEvent& e) { return iequals(e.a, c.arg(0)); });
    if (iequals(n, "Destroyed")) {
        // count > 0: that many destroyed; -1 (or none): every ship of the label (or the ship).
        int want = c.args.size() > 1 ? static_cast<int>(c.num(1)) : -1;
        int dead = destroyed_count(c.arg(0));
        if (want > 0) return dead >= want;
        int size = label_size(c.arg(0));
        return dead > 0 && dead >= std::max(1, size);
    }
    if (iequals(n, "WatchTrigger")) {
        bool on = active(c.arg(0));
        return iequals(c.arg(1), "ON") ? on : !on;
    }
    if (iequals(n, "DistShip") || iequals(n, "DistVec")) {
        bool inside = iequals(c.arg(0), "inside");
        auto a = w.position(c.arg(1));
        if (!a) return false;
        DVec3 b;
        float r;
        if (iequals(n, "DistShip")) {
            auto p = w.position(c.arg(2));
            if (!p) return false;
            b = *p;
            r = c.num(3);
        } else {
            b = {c.num(2), c.num(3), c.num(4)};
            r = c.num(5);
        }
        bool in = (*a - b).length() <= r;
        return inside ? in : !in;
    }
    if (iequals(n, "HealthDec")) return w.position(c.arg(0)) && w.health(c.arg(0)) <= c.num(1);
    if (iequals(n, "SpaceEnter") || iequals(n, "InSpace")) return w.in_space();
    if (iequals(n, "BaseEnter")) return !w.in_space() && iequals(w.base(), c.arg(0));
    if (iequals(n, "SystemEnter")) {
        if (!w.in_space()) return false;
        if (c.args.empty()) return true;
        for (auto& v : c.args)
            if (iequals(v.as_string(), w.system()) || iequals(v.as_string(), "ANY")) return true;
        return false;
    }
    if (iequals(n, "SpaceExit")) return any_event(T::SpaceExit, [](const MissionEvent&) { return true; });
    if (iequals(n, "BaseExit")) return any_event(T::BaseExit, [&](const MissionEvent& e) { return is(e.a, c.arg(0)); });
    if (iequals(n, "SystemExit")) return any_event(T::SystemExit, [&](const MissionEvent& e) { return is(e.a, c.arg(0)); });
    if (iequals(n, "PlayerLaunch")) return any_event(T::PlayerLaunch, [](const MissionEvent&) { return true; });
    if (iequals(n, "LaunchComplete"))
        return any_event(T::LaunchComplete, [&](const MissionEvent& e) { return is(e.a, c.arg(0)); });
    if (iequals(n, "TLEntered"))
        return any_event(T::TLEntered, [&](const MissionEvent& e) { return is(e.a, c.arg(0)) && is(e.b, c.arg(1)); });
    if (iequals(n, "TLExited"))
        return any_event(T::TLExited, [&](const MissionEvent& e) { return is(e.a, c.arg(0)) && is(e.b, c.arg(1)); });
    if (iequals(n, "LocEnter")) {
        // Being in the room counts, as with BaseEnter: a trigger switched on while the
        // player already stands there fires too.
        if (!w.in_space() && is(w.room(), c.arg(0)) && is(w.base(), c.arg(1)) && !w.room().empty()) return true;
        return any_event(T::LocEnter, [&](const MissionEvent& e) { return is(e.a, c.arg(0)) && is(e.b, c.arg(1)); });
    }
    if (iequals(n, "LocExit"))
        return any_event(T::LocExit, [&](const MissionEvent& e) { return is(e.a, c.arg(0)) && is(e.b, c.arg(1)); });
    if (iequals(n, "MsnResponse"))
        return any_event(T::MsnResponse, [&](const MissionEvent& e) { return iequals(e.a, c.arg(0)); });
    if (iequals(n, "ProjHit")) {
        // ship, hits needed, optional shooter
        int need = std::max(1, static_cast<int>(c.num(1, 1))), hits = 0;
        for (std::size_t i = s.events_from; i < events_.size(); ++i)
            if (events_[i].type == T::ProjHit && iequals(events_[i].a, c.arg(0)) &&
                (c.args.size() < 3 || iequals(events_[i].b, c.arg(2))))
                ++hits;
        return hits >= need;
    }
    return false;  // not supported yet: waits
}

void MissionRunner::update(float dt, const MissionWorld& w, const ActionFn& act) {
    clock_ += dt;
    if (outcome_ != Outcome::Running) return;
    std::string sys = w.system();
    // Triggers activated by actions this frame are checked from the next frame on, as a
    // frame-by-frame script would; a fixed snapshot keeps the order independent of layout.
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < states_.size(); ++i) {
        const auto& t = script_.triggers[i];
        const auto& s = states_[i];
        if (!s.active) continue;
        if (!t.system.empty() && !iequals(t.system, sys)) continue;
        bool ok = !t.conditions.empty();
        for (auto& c : t.conditions) ok = ok && holds(t, c, s, w);
        if (ok) ready.push_back(i);
    }
    for (std::size_t i : ready) {
        auto& s = states_[i];
        if (!s.active || outcome_ != Outcome::Running) continue;  // switched off by an earlier one
        const auto& t = script_.triggers[i];
        ++s.fired;
        if (t.repeatable) {
            s.since = clock_;
            s.events_from = events_.size();
        } else {
            s.active = false;
        }
        for (auto& a : t.actions) {
            if (iequals(a.name, "ActTrig")) activate(a.arg(0));
            else if (iequals(a.name, "DeactTrig")) {
                if (auto it = index_.find(to_lower(a.arg(0))); it != index_.end()) states_[it->second].active = false;
            } else if (iequals(a.name, "ChangeState")) {
                outcome_ = iequals(a.arg(0), "SUCCEED") ? Outcome::Succeeded : Outcome::Failed;
                outcome_ids_ = static_cast<int>(a.num(1));
                if (act) act(a, t);
            } else if (act) {
                act(a, t);
            }
        }
    }
}

MissionStart mission_start(const MissionScript& script) {
    MissionStart out;
    std::vector<const MsnTrigger*> frontier;
    std::vector<std::string> seen;
    auto visit = [&](const MsnTrigger* t) {
        if (!t || std::find(seen.begin(), seen.end(), to_lower(t->nickname)) != seen.end()) return;
        seen.push_back(to_lower(t->nickname));
        frontier.push_back(t);
    };
    for (auto& t : script.triggers)
        if (t.initially_active) visit(&t);
    // Two steps past the starting triggers covers "start -> bar scene -> enter bar".
    for (int depth = 0; depth < 3 && !frontier.empty(); ++depth) {
        std::vector<const MsnTrigger*> now;
        now.swap(frontier);
        for (auto* t : now) {
            if (out.system.empty() && !t->system.empty() && !iequals(t->system, "ALL") && !iequals(t->system, "ANY"))
                out.system = t->system;
            for (auto& c : t->conditions) {
                bool loc = iequals(c.name, "LocEnter");
                std::string base = iequals(c.name, "BaseEnter") ? c.arg(0) : loc ? c.arg(1) : "";
                if (base.empty()) continue;
                std::string room = loc ? c.arg(0) : "";
                auto same = [&](const MissionStart::Place& p) { return iequals(p.base, base); };
                auto it = std::find_if(out.places.begin(), out.places.end(), same);
                if (it == out.places.end()) out.places.push_back({base, room});
                else if (it->room.empty()) it->room = room;
            }
        }
        for (auto* t : now)
            for (auto& a : t->actions)
                if (iequals(a.name, "ActTrig")) visit(script.trigger(a.arg(0)));
    }
    return out;
}

const std::vector<std::string>& story_missions() {
    static const std::vector<std::string> list{"M01a", "M01b", "M02", "M03", "M04", "M05", "M06",
                                               "M07",  "M08",  "M09", "M10", "M11", "M12", "M13"};
    return list;
}

std::string next_story_mission(std::string_view mission) {
    auto& l = story_missions();
    for (std::size_t i = 0; i + 1 < l.size(); ++i)
        if (iequals(l[i], mission)) return l[i + 1];
    return {};
}

}  // namespace sterrenvaarder::game
