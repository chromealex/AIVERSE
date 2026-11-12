// Bunker Wars computer player: data (rules ai.bw, map "ai" block, map players' "bot"), built-in difficulty
// profiles (BW AI6ComplexityInfo_* / AI6SharedComplexityInfo_* / AIBishopComplexityInfo assets).
#include "RtsBwAIInternal.h"
#include "../../engine/data/Json.h"
#include <stdexcept>

namespace aiverse::game {

namespace {
[[noreturn]] void fail(const std::string& where, const std::string& msg) { throw std::runtime_error(where + ": " + msg); }

void allow(const Json& j, const std::string& where, std::initializer_list<const char*> keys) {
    if (!j.isObject()) fail(where, "expected object");
    for (auto& [k, v] : j.members()) {
        bool ok = false;
        for (const char* a : keys) if (k == a) ok = true;
        if (!ok) {
            std::string list;
            for (const char* a : keys) list += (list.empty() ? "" : ", ") + std::string(a);
            fail(where, "unknown key '" + k + "' (" + list + ")");
        }
    }
}

BwCmp cmpOf(const std::string& where, const std::string& s) {
    if (s == "<" || s == "less") return BwCmp::Less;
    if (s == "<=" || s == "lessOrEqual") return BwCmp::LessOrEqual;
    if (s == "==" || s == "=" || s == "equal") return BwCmp::Equal;
    if (s == ">=" || s == "greaterOrEqual") return BwCmp::GreaterOrEqual;
    if (s == ">" || s == "greater") return BwCmp::Greater;
    if (s == "!=" || s == "notEqual") return BwCmp::NotEqual;
    fail(where, "unknown comparison '" + s + "' (<, <=, ==, >=, >, !=)");
}

BwAlgo algoOf(const std::string& where, const std::string& s) {
    if (s == "ai6") return BwAlgo::AI6;
    if (s == "bishop") return BwAlgo::Bishop;
    if (s == "ashley") return BwAlgo::Ashley;
    if (s == "none") return BwAlgo::None;
    fail(where, "unknown algorithm '" + s + "' (ai6, bishop, ashley, none)");
}

BwRole roleOf(const std::string& where, const std::string& s) {
    if (s == "house" || s == "village") return BwRole::House;
    if (s == "tower") return BwRole::Tower;
    if (s == "smithy" || s == "forge") return BwRole::Smithy;
    fail(where, "unknown role '" + s + "' (house, tower, smithy)");
}

uint64_t tagsOf(const Rules& R, const std::string& where, const Json& j) {
    uint64_t bits = 0;
    auto one = [&](const std::string& t) {
        for (size_t i = 0; i < R.tagNames.size(); ++i) if (R.tagNames[i] == t) { bits |= uint64_t(1) << i; return; }
        // a tag no type carries: allowed (matches nothing), so profiles can be shared between games
    };
    if (j.isString()) one(j.str());
    else if (j.isArray()) for (auto& t : j.items()) { if (!t.isString()) fail(where, "expected tag names"); one(t.str()); }
    else fail(where, "expected a tag or a list of tags");
    return bits;
}

int pointIndex(const std::string& where, const Json& v) {
    if (!v.isNumber() || v.integer(-1) < 0) fail(where, "expected a point index (n-th garrison structure of the map, from 0)");
    return int(v.integer());
}
std::vector<int> pointList(const std::string& where, const Json& v) {
    std::vector<int> out;
    if (!v.isArray()) fail(where, "expected a list of point indexes");
    for (auto& x : v.items()) out.push_back(pointIndex(where, x));
    return out;
}

// ---- built-in profiles (values of the BW assets)
BwProfile builtin(const std::string& name) {
    BwProfile p; p.name = name;
    auto await = [&](int a, int b, int c, int d) { p.skillSlotAwait[0] = Fixed::fromInt(a); p.skillSlotAwait[1] = Fixed::fromInt(b); p.skillSlotAwait[2] = Fixed::fromInt(c); p.skillSlotAwait[3] = Fixed::fromInt(d); };
    if (name == "easy") {
        p.complexity = 1; await(90, 60, 30, 15);
        p.minDelay = Fixed::fromRatio(9, 2); p.maxDelay = Fixed::fromInt(12); p.attackLimitPerUpdate = 1; p.defenseLimitPerUpdate = 1;
        p.increaseMoraleCoefficient = Fixed::fromRatio(35, 100); p.decreaseMoraleCooldownCoefficient = Fixed::fromRatio(5, 2);
    } else if (name == "normal") {
        p.complexity = 2; await(70, 40, 20, 10);
    } else if (name == "hard") {
        p.complexity = 3; await(45, 30, 15, 7);
        p.minDelay = p.maxDelay = Fixed{}; p.ignoreAverageActionTime = true;
        p.attackDistanceSafety = Fixed::fromRatio(3, 4); p.reserveUnitsForCounterAttackFactor = Fixed::fromInt(1);
    } else if (name == "expert") {
        p.complexity = 4; await(0, 0, 0, 0);
        p.minDelay = p.maxDelay = Fixed{}; p.ignoreAverageActionTime = true;
        p.attackLimitPerUpdate = 0; p.limitOneUpgrade = false; p.structuredAttacks = true;
        p.attackDistanceSafety = Fixed::fromRatio(1, 2); p.reserveUnitsForCounterAttackFactor = Fixed::fromRatio(1, 2);
    } else if (name == "bishop") {
        p.complexity = 0; p.algorithm = BwAlgo::Bishop; p.useSkills = false; await(90, 60, 30, 15);
    } else if (name == "ashley") {
        p.complexity = 0; p.algorithm = BwAlgo::Ashley; await(90, 60, 30, 15);
    }
    return p;
}

void act(const Json& j, const std::string& w, BwProfile::Act& a) {
    allow(j, w, {"delay", "minPercent", "minCount", "maxBuildings"});
    a.delay = j.getFx("delay", a.delay); a.minPercent = j.getFx("minPercent", a.minPercent);
    a.minCount = int(j.getInt("minCount", a.minCount)); a.maxBuildings = int(j.getInt("maxBuildings", a.maxBuildings));
}

void profile(const Json& j, const std::string& w, BwProfile& p, const Rules& R) {
    allow(j, w, {"base", "complexity", "algorithm", "useSkills", "canMoveInBlizzard", "skillSlotAwait", "minDelay", "maxDelay",
                 "ignoreAverageActionTime", "attackLimitPerUpdate", "defenseLimitPerUpdate", "limitOneUpgrade", "structuredAttacks",
                 "attackDistanceSafety", "reserveUnitsForCounterAttackFactor", "increaseMoraleCoefficient",
                 "decreaseMoraleCooldownCoefficient", "minGroupSize", "attackTimeouts", "pointRankFactors",
                 "counterAttackScanRadiusFactor", "canDefendPointMinTimeFactor", "towerRankFactor", "smithyRankFactor",
                 "attackDistanceFactor", "minGroupSizeOverride", "upgradeTimeout", "allowConvert", "defeat", "custom", "freezeWave",
                 "testAttack", "analyzePeriod", "attack", "defense", "additionalAttackCount", "increaseUnitsCoefficient",
                 "maxAttackTargets", "upgradePeriod", "disablePointsConvertation", "errorPercentPerSecond", "firstActionDelay",
                 "deniedConvert", "awaitForPlayerAction"});
    p.complexity = int(j.getInt("complexity", p.complexity));
    if (j.has("algorithm")) p.algorithm = algoOf(w + ".algorithm", j.getStr("algorithm"));
    p.useSkills = j.getBool("useSkills", p.useSkills);
    p.canMoveInBlizzard = j.getBool("canMoveInBlizzard", p.canMoveInBlizzard);
    if (const Json& a = j["skillSlotAwait"]; !a.isNull()) {
        if (!a.isArray() || a.size() > 4) fail(w + ".skillSlotAwait", "expected up to 4 seconds (slot 1..4)");
        for (size_t i = 0; i < a.size(); ++i) p.skillSlotAwait[i] = a[i].fx();
    }
    p.minDelay = j.getFx("minDelay", p.minDelay); p.maxDelay = j.getFx("maxDelay", p.maxDelay);
    if (p.maxDelay < p.minDelay) fail(w, "maxDelay < minDelay");
    p.ignoreAverageActionTime = j.getBool("ignoreAverageActionTime", p.ignoreAverageActionTime);
    p.attackLimitPerUpdate = int(j.getInt("attackLimitPerUpdate", p.attackLimitPerUpdate));
    p.defenseLimitPerUpdate = int(j.getInt("defenseLimitPerUpdate", p.defenseLimitPerUpdate));
    p.limitOneUpgrade = j.getBool("limitOneUpgrade", p.limitOneUpgrade);
    p.structuredAttacks = j.getBool("structuredAttacks", p.structuredAttacks);
    p.attackDistanceSafety = j.getFx("attackDistanceSafety", p.attackDistanceSafety);
    if (p.attackDistanceSafety.raw < 0 || p.attackDistanceSafety > Fixed::fromInt(1)) fail(w + ".attackDistanceSafety", "expected 0..1");
    p.reserveUnitsForCounterAttackFactor = j.getFx("reserveUnitsForCounterAttackFactor", p.reserveUnitsForCounterAttackFactor);
    p.increaseMoraleCoefficient = j.getFx("increaseMoraleCoefficient", p.increaseMoraleCoefficient);
    p.decreaseMoraleCooldownCoefficient = j.getFx("decreaseMoraleCooldownCoefficient", p.decreaseMoraleCooldownCoefficient);
    p.minGroupSize = int(j.getInt("minGroupSize", p.minGroupSize));
    if (const Json& t = j["attackTimeouts"]; !t.isNull()) {
        if (!t.isObject()) fail(w + ".attackTimeouts", "expected {\"<point index>\": seconds}");
        p.attackTimeouts.clear();
        for (auto& [k, v] : t.members()) p.attackTimeouts.push_back({int(std::stol(k)), v.fx()});
    }
    if (const Json& t = j["pointRankFactors"]; !t.isNull()) {
        if (!t.isObject()) fail(w + ".pointRankFactors", "expected {\"<point index>\": [[seconds, factor], ...]}");
        p.pointRankFactors.clear();
        for (auto& [k, v] : t.members()) {
            BwCurve c;
            if (v.isNumber()) c.keys.push_back({Fixed{}, v.fx()});
            else if (v.isArray()) for (auto& kv : v.items()) {
                if (!kv.isArray() || kv.size() != 2) fail(w + ".pointRankFactors." + k, "expected [seconds, factor] pairs");
                c.keys.push_back({kv[0].fx(), kv[1].fx()});
            } else fail(w + ".pointRankFactors." + k, "expected a factor or [[seconds, factor], ...]");
            std::stable_sort(c.keys.begin(), c.keys.end(), [](auto& a, auto& b) { return a.first < b.first; });
            p.pointRankFactors.push_back({int(std::stol(k)), c});
        }
    }
    p.counterAttackScanRadiusFactor = j.getFx("counterAttackScanRadiusFactor", p.counterAttackScanRadiusFactor);
    p.canDefendPointMinTimeFactor = j.getFx("canDefendPointMinTimeFactor", p.canDefendPointMinTimeFactor);
    p.towerRankFactor = j.getFx("towerRankFactor", p.towerRankFactor);
    p.smithyRankFactor = j.getFx("smithyRankFactor", p.smithyRankFactor);
    p.attackDistanceFactor = j.getFx("attackDistanceFactor", p.attackDistanceFactor);
    p.minGroupSizeOverride = int(j.getInt("minGroupSizeOverride", p.minGroupSizeOverride));
    p.upgradeTimeout = j.getFx("upgradeTimeout", p.upgradeTimeout);
    p.allowConvert = j.getBool("allowConvert", p.allowConvert);
    if (const Json& d = j["defeat"]; !d.isNull()) {
        if (!d.isArray()) fail(w + ".defeat", "expected a list");
        p.defeat.clear();
        for (size_t i = 0; i < d.size(); ++i) {
            const std::string dw = w + ".defeat[" + std::to_string(i) + "]";
            allow(d[i], dw, {"timeToCheck", "chance", "conditions"});
            BwDefeat df;
            df.timeToCheck = d[i].getFx("timeToCheck", df.timeToCheck);
            df.chance = int(d[i].getInt("chance", 0));
            for (auto& cj : d[i]["conditions"].items()) {
                allow(cj, dw + ".conditions", {"type", "cmp", "delta"});
                BwDefeatCheck ck;
                const std::string t = cj.getStr("type");
                if (t == "points") ck.kind = BwDefeatCheck::Points;
                else if (t == "units") ck.kind = BwDefeatCheck::Units;
                else if (t == "morale") ck.kind = BwDefeatCheck::Morale;
                else if (t == "time") ck.kind = BwDefeatCheck::Time;
                else fail(dw + ".conditions", "unknown type '" + t + "' (points, units, morale, time)");
                ck.cmp = cmpOf(dw + ".conditions.cmp", cj.getStr("cmp", "<"));
                ck.delta = int(cj.getInt("delta", 0));
                df.checks.push_back(ck);
            }
            p.defeat.push_back(df);
        }
    }
    if (j.has("custom")) {
        p.custom = j.getStr("custom");
        if (p.custom != "" && p.custom != "map17005" && p.custom != "map11001") fail(w + ".custom", "unknown '" + p.custom + "' (map17005, map11001)");
    }
    if (const Json& f = j["freezeWave"]; !f.isNull()) {
        allow(f, w + ".freezeWave", {"ability", "interval", "attacks", "defenses", "refresh"});
        if (f.has("ability")) {
            p.freezeWaveAbility = R.findPlayerAbility(f.getStr("ability"));
            if (p.freezeWaveAbility < 0) fail(w + ".freezeWave.ability", "unknown player ability '" + f.getStr("ability") + "'");
        }
        p.freezeWaveInterval = f.getFx("interval", p.freezeWaveInterval);
        p.avoidFreezeWaveForAttacks = f.getBool("attacks", p.avoidFreezeWaveForAttacks);
        p.avoidFreezeWaveForDefenses = f.getBool("defenses", p.avoidFreezeWaveForDefenses);
        p.refreshTimeAfterFreezeWave = f.getFx("refresh", p.refreshTimeAfterFreezeWave);
    }
    if (const Json& t = j["testAttack"]; !t.isNull()) {
        allow(t, w + ".testAttack", {"target", "playerSide", "developTimeout"});
        if (t.has("target")) p.testAttackTarget = pointIndex(w + ".testAttack.target", t["target"]);
        if (t.has("playerSide")) p.playerSidePoints = pointList(w + ".testAttack.playerSide", t["playerSide"]);
        p.playerDevelopTimeout = t.getFx("developTimeout", p.playerDevelopTimeout);
    }
    p.analyzePeriod = j.getFx("analyzePeriod", p.analyzePeriod);
    if (j.has("attack")) act(j["attack"], w + ".attack", p.attack);
    if (j.has("defense")) act(j["defense"], w + ".defense", p.defense);
    p.additionalAttackCount = int(j.getInt("additionalAttackCount", p.additionalAttackCount));
    p.increaseUnitsCoefficient = j.getFx("increaseUnitsCoefficient", p.increaseUnitsCoefficient);
    p.maxAttackTargets = int(j.getInt("maxAttackTargets", p.maxAttackTargets));
    p.upgradePeriod = j.getFx("upgradePeriod", p.upgradePeriod);
    p.disablePointsConvertation = j.getBool("disablePointsConvertation", p.disablePointsConvertation);
    p.errorPercentPerSecond = j.getFx("errorPercentPerSecond", p.errorPercentPerSecond);
    p.firstActionDelay = j.getFx("firstActionDelay", p.firstActionDelay);
    if (const Json& d = j["deniedConvert"]; !d.isNull()) {
        p.deniedConvertRoles = 0;
        if (!d.isArray()) fail(w + ".deniedConvert", "expected a list of roles");
        for (auto& r : d.items()) p.deniedConvertRoles |= uint8_t(1u << unsigned(roleOf(w + ".deniedConvert", r.str())));
    }
    p.awaitForPlayerAction = j.getBool("awaitForPlayerAction", p.awaitForPlayerAction);
}

void profiles(const Json& j, const std::string& w, BwAIConfig& cfg, const Rules& R) {
    if (!j.isObject()) fail(w, "expected {\"<name>\": {...}}");
    for (auto& [name, pj] : j.members()) {
        if (!pj.isObject()) fail(w + "." + name, "expected object");
        BwProfile p;
        const std::string base = pj.getStr("base", "");
        if (!base.empty()) {
            auto it = cfg.profiles.find(base);
            if (it == cfg.profiles.end()) fail(w + "." + name + ".base", "unknown profile '" + base + "'");
            p = it->second;
        } else if (auto it = cfg.profiles.find(name); it != cfg.profiles.end()) p = it->second;   // overrides a built-in
        p.name = name;
        profile(pj, w + "." + name, p, R);
        cfg.profiles[name] = p;
    }
}

void slotProfiles(const Json& j, const std::string& w, BwAIConfig& cfg) {
    if (!j.isObject()) fail(w, "expected {\"<slot>\": \"<profile>\"}");
    for (auto& [k, v] : j.members()) {
        const int slot = int(std::stol(k));
        if (slot < 1 || slot > 254) fail(w + "." + k, "expected a player slot 1..254");
        if (!v.isString() || !cfg.profiles.count(v.str())) fail(w + "." + k, "unknown profile '" + v.str() + "'");
        cfg.slotProfile[slot] = v.str();
    }
}

BwMode modeOf(const std::string& w, const std::string& s, bool& autoMode) {
    autoMode = s == "auto";
    if (s == "auto" || s == "standard") return BwMode::Standard;
    if (s == "domination") return BwMode::Domination;
    if (s == "kingOfTheHill") return BwMode::KingOfTheHill;
    fail(w, "unknown mode '" + s + "' (auto, standard, domination, kingOfTheHill)");
}

void debuts(const Json& j, const std::string& w, BwAIConfig& cfg, const Rules& R) {
    if (!j.isArray()) fail(w, "expected a list");
    for (size_t i = 0; i < j.size(); ++i) {
        const Json& d = j[i];
        const std::string dw = w + "[" + std::to_string(i) + "]";
        allow(d, dw, {"difficulty", "slot", "mode", "actions", "states"});
        BwDebut db;
        if (d["difficulty"].isNumber()) db.complexity = int(d.getInt("difficulty"));
        else if (d.has("difficulty")) {
            auto it = cfg.profiles.find(d.getStr("difficulty"));
            if (it == cfg.profiles.end()) fail(dw + ".difficulty", "unknown profile '" + d.getStr("difficulty") + "'");
            db.complexity = it->second.complexity;
        }
        db.slot = int(d.getInt("slot", 0));
        if (d.has("mode")) { bool a = false; db.mode = modeOf(dw + ".mode", d.getStr("mode"), a); db.anyMode = a; }
        for (size_t k = 0; k < d["actions"].size(); ++k) {
            const Json& a = d["actions"][k];
            const std::string aw = dw + ".actions[" + std::to_string(k) + "]";
            allow(a, aw, {"tick", "send", "upgrade", "to", "skill", "at"});
            BwDebut::Act ac; ac.tick = uint32_t(a.getInt("tick", 0));
            if (a.has("send")) {
                const Json& s = a["send"];
                if (!s.isArray() || s.size() != 3) fail(aw + ".send", "expected [from point, to point, units]");
                ac.kind = 0; ac.from = pointIndex(aw + ".send", s[0]); ac.to = pointIndex(aw + ".send", s[1]); ac.count = int(s[2].integer());
            } else if (a.has("upgrade")) {
                ac.kind = 1; ac.from = pointIndex(aw + ".upgrade", a["upgrade"]);
                const int t = R.findType(a.getStr("to"));
                if (t < 0) fail(aw + ".to", "unknown type '" + a.getStr("to") + "'");
                ac.type = uint16_t(t);
            } else if (a.has("skill")) {
                ac.kind = 2; ac.ability = R.findPlayerAbility(a.getStr("skill"));
                if (ac.ability < 0) fail(aw + ".skill", "unknown player ability '" + a.getStr("skill") + "'");
                if (a["at"].isArray() && a["at"].size() == 2) ac.point = {a["at"][0].fx(), Fixed{}, a["at"][1].fx()};
            } else fail(aw, "expected send, upgrade or skill");
            db.actions.push_back(ac);
        }
        std::stable_sort(db.actions.begin(), db.actions.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
        for (size_t k = 0; k < d["states"].size(); ++k) {
            const Json& s = d["states"][k];
            const std::string sw = dw + ".states[" + std::to_string(k) + "]";
            allow(s, sw, {"tick", "point", "type"});
            BwDebut::State st; st.tick = uint32_t(s.getInt("tick", 0)); st.point = pointIndex(sw + ".point", s["point"]);
            const int t = R.findType(s.getStr("type"));
            if (t < 0) fail(sw + ".type", "unknown type '" + s.getStr("type") + "'");
            st.type = uint16_t(t);
            db.states.push_back(st);
        }
        cfg.debuts.push_back(db);
    }
}

uint8_t whoOf(const std::string& w, const std::string& s) {
    if (s == "any" || s.empty()) return 0;
    if (s == "me") return 1;
    if (s == "allies") return 2;
    if (s == "enemies") return 3;
    fail(w, "unknown players filter '" + s + "' (any, me, allies, enemies)");
}

void triggers(const Json& j, const std::string& w, BwAIConfig& cfg) {
    if (!j.isArray()) fail(w, "expected a list of trigger groups");
    for (size_t gi = 0; gi < j.size(); ++gi) {
        const Json& g = j[gi];
        const std::string gw = w + "[" + std::to_string(gi) + "]";
        allow(g, gw, {"name", "enabled", "elements"});
        BwTrigger::Group grp; grp.name = g.getStr("name"); grp.enabled = g.getBool("enabled", true);
        for (size_t ei = 0; ei < g["elements"].size(); ++ei) {
            const Json& e = g["elements"][ei];
            const std::string ew = gw + ".elements[" + std::to_string(ei) + "]";
            allow(e, ew, {"when", "conditions", "actions"});
            BwTrigger::Element el;
            const std::string when = e.getStr("when", "all");
            if (when != "all" && when != "any") fail(ew + ".when", "expected all or any");
            el.all = when == "all";
            for (auto& cj : e["conditions"].items()) {
                const std::string t = cj.getStr("type");
                BwTrigger::Cond c;
                const std::string cw = ew + ".conditions(" + t + ")";
                if (t == "instant") { allow(cj, cw, {"type"}); c.kind = 0; }
                else if (t == "timer") { allow(cj, cw, {"type", "name"}); c.kind = 1; c.name = cj.getStr("name"); }
                else if (t == "value") { allow(cj, cw, {"type", "name", "cmp", "value"}); c.kind = 2; c.name = cj.getStr("name"); c.cmp = cmpOf(cw, cj.getStr("cmp", ">=")); c.value = cj.getFx("value"); }
                else if (t == "pointCaptured") { allow(cj, cw, {"type", "point"}); c.kind = 3; c.point = pointIndex(cw + ".point", cj["point"]); }
                else if (t == "unitsMove") {
                    allow(cj, cw, {"type", "who", "from", "to", "minCount", "minSpawned", "progress"});
                    c.kind = 4; c.who = whoOf(cw + ".who", cj.getStr("who", "any"));
                    c.from = int(cj.getInt("from", -1)); c.to = int(cj.getInt("to", -1)); c.minCount = int(cj.getInt("minCount", -1));
                    c.point = int(cj.getInt("minSpawned", -1)); c.progress = cj.getFx("progress", Fixed::fromInt(-1));
                } else if (t == "time") { allow(cj, cw, {"type", "cmp", "value"}); c.kind = 5; c.cmp = cmpOf(cw, cj.getStr("cmp", ">=")); c.value = cj.getFx("value"); }
                else fail(cw, "unknown condition (instant, timer, value, pointCaptured, unitsMove, time); skill / morale conditions: use rules triggers");
                el.conds.push_back(c);
            }
            for (auto& aj : e["actions"].items()) {
                const std::string t = aj.getStr("type");
                BwTrigger::Action a;
                const std::string aw = ew + ".actions(" + t + ")";
                if (t == "setTimer") { allow(aj, aw, {"type", "name", "seconds"}); a.kind = 0; a.name = aj.getStr("name"); a.value = aj.getFx("seconds"); }
                else if (t == "setValue") { allow(aj, aw, {"type", "name", "value"}); a.kind = 1; a.name = aj.getStr("name"); a.value = aj.getFx("value"); }
                else if (t == "botAttack") {
                    allow(aj, aw, {"type", "sources", "targets", "count", "useAllUnits", "testing"});
                    a.kind = 2;
                    if (aj.has("sources")) { a.a = pointList(aw + ".sources", aj["sources"]); a.mask |= 1; }
                    if (aj.has("targets")) { a.b = pointList(aw + ".targets", aj["targets"]); a.mask |= 2; }
                    if (aj.has("count")) { a.count = int(aj.getInt("count")); a.mask |= 4; }
                    a.flag = aj.getBool("useAllUnits", false); a.flag2 = aj.getBool("testing", true);
                } else if (t == "botIgnore") { allow(aj, aw, {"type", "points"}); a.kind = 3; a.a = pointList(aw + ".points", aj["points"]); }
                else if (t == "botDisable") { allow(aj, aw, {"type", "disabled"}); a.kind = 4; a.flag = aj.getBool("disabled", true); }
                else if (t == "botReset") { allow(aj, aw, {"type"}); a.kind = 5; }
                else if (t == "botMoveUnits") { allow(aj, aw, {"type", "from", "to", "percent"}); a.kind = 6; a.from = pointIndex(aw + ".from", aj["from"]); a.to = pointIndex(aw + ".to", aj["to"]); a.value = aj.getFx("percent", Fixed::fromInt(100)); }
                else if (t == "groupSetState") {
                    allow(aj, aw, {"type", "groups", "enabled"}); a.kind = 7; a.flag = aj.getBool("enabled", true);
                    for (auto& n : aj["groups"].items()) a.name += n.str() + "\n";
                } else if (t == "botSet") {
                    allow(aj, aw, {"type", "attack", "relocation", "defense", "upgrade"}); a.kind = 8;
                    const char* keys[4] = {"attack", "relocation", "defense", "upgrade"};
                    for (int k = 0; k < 4; ++k) if (aj.has(keys[k])) { a.mask |= uint8_t(1 << k); if (aj.getBool(keys[k])) a.a.push_back(k); }
                } else fail(aw, "unknown action (setTimer, setValue, botAttack, botIgnore, botDisable, botReset, botMoveUnits, groupSetState, botSet); "
                                "morale / energy / production / time scale changes: use rules triggers");
                el.actions.push_back(a);
            }
            grp.elements.push_back(el);
        }
        cfg.triggers.groups.push_back(grp);
    }
}
} // namespace

Fixed BwCurve::eval(Fixed t) const {
    if (keys.empty()) return Fixed::fromInt(1);
    if (t <= keys.front().first) return keys.front().second;
    for (size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].first) {
            const Fixed span = keys[i].first - keys[i - 1].first;
            if (span.raw <= 0) return keys[i].second;
            return keys[i - 1].second + (keys[i].second - keys[i - 1].second) * ((t - keys[i - 1].first) / span);
        }
    return keys.back().second;
}

Fixed BwProfile::attackTimeout(int point) const {
    for (auto& [p, t] : attackTimeouts) if (p == point) return t;
    return Fixed{};
}
Fixed BwProfile::rankFactor(int point, Fixed time) const {
    for (auto& [p, c] : pointRankFactors) if (p == point) return c.eval(time);
    return Fixed::fromInt(1);
}

const BwProfile& BwAIConfig::profileFor(int slot) const {
    auto s = slotProfile.find(slot);
    auto it = profiles.find(s != slotProfile.end() ? s->second : defaultProfile);
    return it != profiles.end() ? it->second : profiles.begin()->second;
}

bool bwCompare(BwCmp c, Fixed a, Fixed b) {
    switch (c) {
    case BwCmp::Less: return a < b;
    case BwCmp::LessOrEqual: return a <= b;
    case BwCmp::Equal: return a == b;
    case BwCmp::GreaterOrEqual: return a >= b;
    case BwCmp::Greater: return a > b;
    case BwCmp::NotEqual: return a != b;
    }
    return false;
}

// Role of a garrison type: explicit tags first, else what it does (shoots: tower, player mods: smithy, breeds: house).
BwRole bwRoleOf(const Rules& R, const BwAIConfig& cfg, uint16_t type) {
    const UnitType& t = R.types[type];
    for (int r = 1; r < 4; ++r) if (cfg.roleTags[r] && (t.tags & cfg.roleTags[r])) return BwRole(r);
    if (!t.weapons.empty()) return BwRole::Tower;
    if (!t.playerMods.empty()) return BwRole::Smithy;
    if (t.spawnInterval.raw > 0) return BwRole::House;
    return BwRole::Other;
}

std::shared_ptr<const BwAIConfig> loadBwAIConfig(const Json& rulesAi, const Json& map, const Rules& R) {
    auto cfg = std::make_shared<BwAIConfig>();
    for (const char* n : {"easy", "normal", "hard", "expert", "bishop", "ashley"}) cfg->profiles[n] = builtin(n);
    const Json& b = rulesAi["bw"];
    const std::string w = "ai.bw";
    if (!b.isNull()) {
        allow(b, w, {"difficulty", "players", "bots", "mode", "roles", "objectives", "skills", "pxPerCell", "vars", "profiles"});
        if (b.has("profiles")) profiles(b["profiles"], w + ".profiles", *cfg, R);
        if (b.has("mode")) cfg->mode = modeOf(w + ".mode", b.getStr("mode"), cfg->modeAuto);
        if (const Json& r = b["roles"]; !r.isNull()) {
            allow(r, w + ".roles", {"house", "tower", "smithy"});
            cfg->roleTags[int(BwRole::House)] = r.has("house") ? tagsOf(R, w + ".roles.house", r["house"]) : 0;
            cfg->roleTags[int(BwRole::Tower)] = r.has("tower") ? tagsOf(R, w + ".roles.tower", r["tower"]) : 0;
            cfg->roleTags[int(BwRole::Smithy)] = r.has("smithy") ? tagsOf(R, w + ".roles.smithy", r["smithy"]) : 0;
        }
        if (const Json& o = b["objectives"]; !o.isNull()) {
            allow(o, w + ".objectives", {"domination", "kingOfTheHill"});
            if (o.has("domination")) cfg->dominationTags = tagsOf(R, w + ".objectives.domination", o["domination"]);
            if (o.has("kingOfTheHill")) cfg->kothTags = tagsOf(R, w + ".objectives.kingOfTheHill", o["kingOfTheHill"]);
        }
        if (const Json& s = b["skills"]; !s.isNull()) {
            if (!s.isObject()) fail(w + ".skills", "expected {\"<player ability>\": \"<bot skill>\"}");
            for (auto& [id, v] : s.members()) {
                const int k = R.findPlayerAbility(id);
                if (k < 0) fail(w + ".skills." + id, "unknown player ability '" + id + "'");
                const BwSkill sk = bwSkillByName(v.str());
                if (sk == BwSkill::Count) fail(w + ".skills." + id, "unknown bot skill '" + v.str() + "' (Freeze, Shield, Hypnosis, ... see docs)");
                cfg->skills.push_back({k, sk});
            }
        }
        cfg->pxPerCell = b.getFx("pxPerCell", cfg->pxPerCell);
        if (cfg->pxPerCell.raw <= 0) fail(w + ".pxPerCell", "must be > 0");
        if (const Json& v = b["vars"]; !v.isNull()) {
            allow(v, w + ".vars", {"blizzard", "kothScore", "kothMax"});
            cfg->blizzardVar = v.getStr("blizzard", cfg->blizzardVar);
            cfg->kothScoreVar = v.getStr("kothScore", cfg->kothScoreVar);
            cfg->kothMaxVar = v.getStr("kothMax", cfg->kothMaxVar);
        }
        if (b.has("difficulty")) {
            cfg->defaultProfile = b.getStr("difficulty");
            if (!cfg->profiles.count(cfg->defaultProfile)) fail(w + ".difficulty", "unknown profile '" + cfg->defaultProfile + "'");
        }
        if (b.has("players")) slotProfiles(b["players"], w + ".players", *cfg);
        if (const Json& bs = b["bots"]; !bs.isNull()) {
            if (!bs.isArray()) fail(w + ".bots", "expected a list of slots");
            for (auto& s : bs.items()) cfg->botSlots.push_back(int(s.integer()));
        }
    }
    // map: per-map profile overrides (BW Maps/*/Bot/*ComplexityInfo), debuts, bot triggers, players' "bot"
    const Json& ma = map["ai"];
    if (!ma.isNull()) {
        allow(ma, "map ai", {"profiles", "players", "debuts", "triggers", "difficulty"});
        if (ma.has("profiles")) profiles(ma["profiles"], "map ai.profiles", *cfg, R);
        if (ma.has("difficulty")) {
            cfg->defaultProfile = ma.getStr("difficulty");
            if (!cfg->profiles.count(cfg->defaultProfile)) fail("map ai.difficulty", "unknown profile '" + cfg->defaultProfile + "'");
        }
        if (ma.has("players")) slotProfiles(ma["players"], "map ai.players", *cfg);
        if (ma.has("debuts")) debuts(ma["debuts"], "map ai.debuts", *cfg, R);
        if (ma.has("triggers")) triggers(ma["triggers"], "map ai.triggers", *cfg);
    }
    const Json& mp = map["players"];
    for (size_t i = 0; i < mp.size(); ++i) {
        const Json& bot = mp[i]["bot"];
        if (bot.isNull()) continue;
        const std::string pw = "map player " + std::to_string(i + 1) + " bot";
        const std::string name = bot.isString() ? bot.str() : bot.getStr("difficulty", cfg->defaultProfile);
        if (bot.isObject()) allow(bot, pw, {"difficulty"});
        if (!cfg->profiles.count(name)) fail(pw, "unknown profile '" + name + "'");
        cfg->slotProfile[int(i) + 1] = name;
        cfg->botSlots.push_back(int(i) + 1);
    }
    return cfg;
}

} // namespace aiverse::game
