// Bunker Wars meta (menu) tutorials: the UI.Windows TutorialSystem of the Unity client running the
// _TutorialMeta TutorialData exported to ui/bw_tutorials.json (tools/meta/bw_tutorials.py).
//  - window events (OnInitialize, OnShowBegin, OnShowEnd, OnWindowActive, OnWindowInactive, ManualRun)
//    start the tutorials whose forObject is that window and whose conditions hold;
//  - conditions are evaluated on the BW profile (ProfileResponseTO = meta view "raw"), the profile's
//    binData keys (HasKey / ExistKey / HasKeyTimer, SetKey), tutorial counters and the client state;
//    the few that need client-only knowledge (campaign progress, craft screen selection, ...) go to
//    TutorialHost::condition (default: false, listed in unsupported());
//  - actions run in order; Wait / WaitForClick / WaitButtonClick / SetKey(save) suspend a tutorial
//    until update() / click() / buttonClick() / the save completes; UI actions go to the host.
// Host/meta code, deterministic for a given call sequence (SetKeyRandom uses a seeded PCG).
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "engine/data/Json.h"

namespace aiverse::meta::bw {

enum class TutorialEvent { OnInitialize = 0, OnShowBegin, OnShowEnd, OnWindowActive, OnWindowInactive, ManualRun };

struct TutorialHost {
    std::function<void(const std::string& tag, bool show, const Json& action)> showHide;   // ShowHideComponents
    std::function<void(const std::string& tag, const Json& action)> lockElement;          // LockElement: highlight, block the rest
    std::function<void(const Json& action, const std::string& window)> action;            // every other UI / service action
    std::function<bool(const Json& condition, bool& result)> condition;                   // true = handled (result set)
    // binData key changed by SetKey / SetKeyRandom; save = BW SetKey.save (persist now; call
    // keySaved() when the backend confirmed to resume the tutorial).
    std::function<void(const std::string& key, int64_t value, bool save)> keyChanged;
};

class MetaTutorials {
public:
    bool load(const Json& doc, std::string* error = nullptr);   // {"tutorials": [...]} as written by bw_tutorials.py
    size_t size() const { return defs_.size(); }
    TutorialHost host;

    // ---- state the conditions read
    void setProfile(const Json& rawProfile) { profile_ = rawProfile; }      // v/profile/get data (view "raw")
    void setKeys(const Json& binData);                                     // BW binDataRaw (object of numbers)
    const std::map<std::string, int64_t>& keys() const { return keys_; }
    void setKey(const std::string& key, int64_t v) { keys_[key] = v; }
    void setCounter(int id, int64_t v) { counters_[id] = v; }
    void setNow(int64_t unixSeconds) { now_ = unixSeconds; }
    void setOffline(bool offline) { offline_ = offline; }
    void setClientVersion(std::string v) { version_ = std::move(v); }
    void setCountry(std::string c) { country_ = std::move(c); }
    void setSeed(uint64_t s) { rng_ = s ? s : 1; }

    // ---- events
    int windowEvent(const std::string& window, TutorialEvent ev);          // window: BW class name, short name or engine screen id
    bool run(const std::string& nameOrGuid, const std::string& window = {});   // ManualRun / "tutorial://name" (checks conditions)
    void click();                                                          // WaitForClick / pointer up
    void buttonClick(const std::string& tutorialTag);                      // WaitButtonClick
    void keySaved();                                                       // SetKey(save) completed
    void update(double seconds);                                           // Wait timers

    bool evaluate(const Json& condition, const std::string& window = {});
    bool conditionsHold(size_t def, const std::string& window = {});
    std::vector<std::string> runningNames() const;
    const std::set<std::string>& unsupported() const { return unsupported_; }
    static bool windowMatches(const std::string& window, const std::string& forObject);

private:
    enum class Wait { None, Time, Click, Button, Save };
    struct Def { std::string name, guid, forObject; int startEvent = 3; Json conditions, actions; };
    struct Run { size_t def = 0; size_t index = 0; Wait wait = Wait::None; double timer = 0; std::string tag, window; bool done = false; };
    void start(size_t def, const std::string& window);
    void advance(Run& r);
    void sweep();
    int64_t key(const std::string& k) const { auto it = keys_.find(k); return it == keys_.end() ? 0 : it->second; }
    uint32_t random();

    std::vector<Def> defs_;
    std::deque<Run> runs_;   // deque: a nested RunTutorial appends without moving running ones
    Json profile_;
    std::map<std::string, int64_t> keys_;
    std::map<int, int64_t> counters_;
    int64_t now_ = 0;
    bool offline_ = false;
    std::string version_, country_;
    uint64_t rng_ = 0x853c49e6748fea9bull;
    std::set<std::string> unsupported_;
    int depth_ = 0;
};

// ConditionValueChecker: op EqualsTo, NotEqualsTo, LessThan, GreaterThan, LessOrEqualsThan, GreaterOrEqualsThan
// applied as "runtime <op> configured".
bool bwCompare(int op, double configured, double runtime);

} // namespace aiverse::meta::bw
