// SPDX-License-Identifier: MPL-2.0
#pragma once
// Runs a story mission's triggers. Each active trigger waits for its condition; when it
// holds, the trigger runs its actions and (unless repeatable) switches off. Trigger
// bookkeeping (ActTrig, DeactTrig, ChangeState) happens here; every other action is
// handed to the game. Conditions read the world through MissionWorld and through events
// the game reports (a comm finished, a ship died, the player docked, ...). Headless and
// unit-tested.
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/quat.hpp"
#include "sterrenvaarder/mission.hpp"

namespace sterrenvaarder::game {

// What the game reports as it happens. Names follow the script's own conditions.
struct MissionEvent {
    enum class Type {
        CommComplete,   // a = line
        Destroyed,      // a = ship nickname
        SpaceExit,      // player docked or jumped out
        BaseExit,       // a = base
        SystemExit,     // a = system
        PlayerLaunch,   // undocked
        LaunchComplete, // a = ship ("Player")
        TLEntered,      // a = ship, b = ring, c = next ring
        TLExited,       // a = ship, b = ring
        LocEnter,       // a = room, b = base
        LocExit,        // a = room, b = base
        MsnResponse,    // a = accept / reject
        ProjHit,        // a = ship hit, b = shooter
    } type;
    std::string a, b, c;
};

// The game state conditions look at.
class MissionWorld {
public:
    virtual ~MissionWorld() = default;
    virtual std::string system() const = 0;   // current system nickname
    virtual bool in_space() const = 0;
    virtual std::string base() const = 0;     // docked base, empty in space
    // A ship, solar or "Player" by nickname; nothing when it isn't there.
    virtual std::optional<DVec3> position(std::string_view object) const = 0;
    virtual float health(std::string_view object) const = 0;  // fraction of hull left
    virtual std::string room() const { return {}; }  // room part ("Bar") while docked
};

class MissionRunner {
public:
    enum class Outcome { Running, Succeeded, Failed };
    using ActionFn = std::function<void(const MsnCommand&, const MsnTrigger&)>;

    explicit MissionRunner(const MissionScript& script);

    void start();                       // activates the InitState = ACTIVE triggers
    void event(MissionEvent e);
    void ship_spawned(std::string_view nickname);  // counts it in its labels
    // Checks every active trigger once and runs the actions of those that fire.
    void update(float dt, const MissionWorld& world, const ActionFn& act);

    bool active(std::string_view trigger) const;
    bool fired(std::string_view trigger) const;  // has fired at least once
    Outcome outcome() const { return outcome_; }
    int outcome_ids() const { return outcome_ids_; }
    // Conditions this runner can't evaluate yet (they never hold); for reports.
    static bool knows_condition(std::string_view name);
    // True while an active trigger waits for a condition of this name (e.g. "MsnResponse").
    bool waiting_for(std::string_view condition) const;

private:
    struct State {
        bool active = false;
        int fired = 0;
        double since = 0;          // clock when it was activated
        std::size_t events_from = 0;  // first event it may react to
    };
    bool holds(const MsnTrigger& t, const MsnCommand& c, const State& s, const MissionWorld& w) const;
    void activate(std::string_view trigger);
    int destroyed_count(std::string_view name_or_label) const;
    int label_size(std::string_view label) const;

    const MissionScript& script_;
    std::vector<State> states_;
    std::unordered_map<std::string, std::size_t> index_;  // lower-case nickname -> trigger
    std::vector<MissionEvent> events_;
    std::unordered_map<std::string, int> destroyed_;      // lower-case ship nickname
    std::unordered_map<std::string, int> spawned_labels_; // lower-case label -> ships spawned
    std::unordered_map<std::string, std::vector<std::string>> ship_labels_;
    double clock_ = 0;
    Outcome outcome_ = Outcome::Running;
    int outcome_ids_ = 0;
};

}  // namespace sterrenvaarder::game
