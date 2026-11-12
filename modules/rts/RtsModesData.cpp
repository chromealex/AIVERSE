// Data of the match modes (agent-bw-modes, RtsModes.h): rules "modes" / "matchModes" / "timedRules" / "matchStats",
// resource "levels". Every block is optional; games without them load and play exactly as before.
#include "RtsData.h"
#include <stdexcept>

namespace aiverse::game {

namespace {
[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error(where + ": " + what); }

uint16_t typeOf(const Rules& r, const std::string& where, const Json& j) {
    if (j.isNull()) return 0xFFFF;
    int k = r.findType(j.str());
    if (k < 0) fail(where, "unknown type '" + j.str() + "'");
    if (!r.types[size_t(k)].structure) fail(where, "type '" + j.str() + "' is not a structure");
    return uint16_t(k);
}
int16_t statusOf(const Rules& r, const std::string& where, const Json& j) {
    if (j.isNull()) return -1;
    int k = r.findStatus(j.str());
    if (k < 0) fail(where, "unknown status '" + j.str() + "'");
    return int16_t(k);
}
Fixed nonNeg(const std::string& where, const Json& o, const char* key, Fixed def) {
    Fixed v = o.getFx(key, def);
    if (v.raw < 0) fail(where, std::string(key) + " must not be negative");
    return v;
}
ModeRect rectOf(const std::string& where, const Json& j) {
    if (!j.isArray() || j.size() != 4) fail(where, "rect must be [x, z, w, h]");
    ModeRect m{j[0].fx(), j[1].fx(), j[2].fx(), j[3].fx()};
    if (m.w.raw <= 0 || m.h.raw <= 0) fail(where, "rect width and height must be positive");
    return m;
}
std::vector<ZoneStep> stepsOf(const std::string& where, const Json& arr) {
    std::vector<ZoneStep> out;
    for (auto& s : arr.items()) {
        const std::string w = where + "[" + std::to_string(out.size()) + "]";
        ZoneStep z;
        z.rect = rectOf(w, s["rect"]);
        z.stay = nonNeg(w, s, "stay", Fixed{});
        z.move = nonNeg(w, s, "move", Fixed{});
        z.next = int(s.getInt("next", -1));
        out.push_back(z);
    }
    return out;
}

int16_t levelsIndex(const Rules& r, int res) {
    for (size_t i = 0; i < r.modes.levels.size(); ++i) if (r.modes.levels[i].res == res) return int16_t(i);
    return -1;
}

std::vector<TimedRule> timedRulesOf(const Rules& r, const std::string& where, const Json& arr) {
    std::vector<TimedRule> out;
    if (!arr.isNull() && !arr.isArray()) fail(where, "expected a list of rules");
    for (auto& tj : arr.items()) {
        const std::string w = where + "[" + std::to_string(out.size()) + "]";
        TimedRule t;
        t.status = statusOf(r, w, tj["status"]);
        if (t.status < 0) fail(w, "missing status");
        if (tj.has("resource")) {
            int res = r.findResource(tj.getStr("resource"));
            if (res < 0) fail(w, "unknown resource '" + tj.getStr("resource") + "'");
            t.levelRes = levelsIndex(r, res);
            if (t.levelRes < 0) fail(w, "resource '" + tj.getStr("resource") + "' has no levels");
        }
        for (auto& sj : tj["when"].items()) {
            TimeSlot s;
            s.checkTime = sj.has("from") || sj.has("to");
            s.from = sj.getFx("from"); s.to = sj.getFx("to", Fixed::fromInt(1000000));
            s.inverse = sj.getBool("inverse", false);
            s.iterate = sj.has("every");
            s.every = sj.getFx("every"); s.duration = sj.getFx("duration");
            if (s.iterate && s.every.raw <= 0) fail(w, "every must be positive");
            if (sj.has("levelBehind")) {
                if (t.levelRes < 0) fail(w, "levelBehind needs \"resource\" with levels");
                s.levelBehind = true; s.behind = int(sj.getInt("levelBehind", 0));
            }
            s.everyAbilities = int(sj.getInt("everyAbilities", 0));
            t.slots.push_back(s);
        }
        out.push_back(std::move(t));
    }
    return out;
}
} // namespace

// Parses one effective "modes" object (rules + preset + map already merged).
ModesRules parseModes(const Rules& r, const Json& m) {
    ModesRules o;
    o.levels = r.modes.levels;
    o.timedRules = r.modes.timedRules;
    o.stats = r.modes.stats;
    if (!m.isObject()) return o;
    o.any = true;
    o.stats = true;
    const std::string pts = m.getStr("points", "garrison");
    if (pts != "garrison" && pts != "structures") fail("modes.points", "expected \"garrison\" or \"structures\"");
    o.garrisonPoints = pts == "garrison";
    if (const Json& d = m["domination"]; d.isObject()) {
        const std::string w = "modes.domination";
        o.domination = true;
        o.domHold = nonNeg(w, d, "hold", o.domHold);
        const std::string rule = d.getStr("rule", "auto");
        if (rule == "auto") o.domRule = DominationRule::Auto;
        else if (rule == "holdAny") o.domRule = DominationRule::HoldAny;
        else if (rule == "holdAll") o.domRule = DominationRule::HoldAll;
        else fail(w, "rule must be auto, holdAny or holdAll");
        o.domType = typeOf(r, w + ".type", d["type"]);
        o.domGarrison = int(d.getInt("garrison", -1));
    }
    if (const Json& k = m["kingOfTheHill"]; k.isObject()) {
        const std::string w = "modes.kingOfTheHill";
        o.koh = true;
        o.kohScore = nonNeg(w, k, "score", o.kohScore);
        o.kohTeamScore = nonNeg(w, k, "teamScore", o.kohScore);
        o.kohRate = nonNeg(w, k, "rate", o.kohRate);
        o.kohAlarm = nonNeg(w, k, "alarm", o.kohAlarm);
        o.kohType = typeOf(r, w + ".type", k["type"]);
        o.kohGarrison = int(k.getInt("garrison", -1));
    }
    if (const Json& t = m["timeLimit"]; t.isObject()) {
        const std::string w = "modes.timeLimit";
        o.timeLimit = true;
        o.timeSeconds = nonNeg(w, t, "seconds", o.timeSeconds);
        const std::string win = t.getStr("win", "maxPoints");
        if (win == "maxPoints") o.timeWin = TimeWin::MaxPoints;
        else if (win == "compare") o.timeWin = TimeWin::Compare;
        else fail(w, "win must be maxPoints or compare");
        o.timeCoef = t.getFx("coefficient", Fixed::fromInt(1));
        for (auto& s : t["speed"].items()) {
            Fixed sp = s.getFx("speed", Fixed::fromInt(1));
            if (sp.raw <= 0) fail(w + ".speed", "speed must be positive");
            o.timeSpeed.push_back({s.getFx("remaining"), sp});
        }
    }
    if (const Json& s = m["suddenDeath"]; s.isObject()) {
        const std::string w = "modes.suddenDeath";
        o.suddenDeath = true;
        o.sdAt = nonNeg(w, s, "at", Fixed::fromInt(300));
        o.sdPreview = nonNeg(w, s, "preview", o.sdPreview);
        o.sdPreviewSpeed = s.getFx("previewSpeed", o.sdPreviewSpeed);
        if (o.sdPreviewSpeed.raw <= 0) fail(w, "previewSpeed must be positive");
        o.sdEnd = nonNeg(w, s, "end", Fixed{});
        o.sdCounter = nonNeg(w, s, "counter", Fixed{});
        o.sdStatus = statusOf(r, w + ".status", s["status"]);
        for (auto& rs : s["ruleSets"].items())
            o.sdRuleSets.push_back(timedRulesOf(r, w + ".ruleSets[" + std::to_string(o.sdRuleSets.size()) + "]", rs));
    }
    if (const Json& z = m["zones"]; z.isObject()) {
        const std::string w = "modes.zones";
        o.zones = true;
        o.zoneStart = stepsOf(w + ".start", z["start"]);
        o.zoneSteps = stepsOf(w + ".steps", z["steps"]);
        if (o.zoneStart.empty()) fail(w, "needs at least one start rect");
        auto check = [&](const std::vector<ZoneStep>& v, const char* name) {
            for (auto& st : v) if (st.next < -1 || st.next >= int(o.zoneSteps.size())) fail(w + "." + name, "next out of range");
        };
        check(o.zoneStart, "start"); check(o.zoneSteps, "steps");
        o.zoneNeutralType = typeOf(r, w + ".neutralType", z["neutralType"]);
        o.zoneSdDelay = nonNeg(w, z, "suddenDeathDelay", Fixed{});
    }
    return o;
}

// Merges rules "modes", the chosen rules "matchModes" preset and map "modes" (a key in a later block replaces
// the earlier one; false switches it off). Null when no block has modes.
Json effectiveModes(const Json& rules, const Json& map, std::string* presetId) {
    Json out;
    auto merge = [&](const Json& src) {
        if (src.isNull()) return;
        if (src.type() == Json::Type::Bool && !src.boolean(true)) { out = Json(); return; }
        if (!src.isObject()) return;
        if (!out.isObject()) out = Json::object();
        for (auto& [k, v] : src.members()) {
            if (v.type() == Json::Type::Bool && !v.boolean(true)) out.erase(k);
            else out.set(k, v);
        }
    };
    merge(rules["modes"]);
    std::string id = map.getStr("matchMode", rules.getStr("matchMode"));
    if (!id.empty()) {
        const Json& p = rules["matchModes"][id];
        if (!p.isObject()) throw std::runtime_error("matchMode: unknown preset '" + id + "'");
        merge(p["modes"].isNull() ? Json::object() : p["modes"]);
    }
    merge(map["modes"]);
    if (presetId) *presetId = id;
    return out;
}

void loadModes(Rules& r, const Json& j) {
    // resource levels (morale stars)
    for (size_t k = 0; k < r.resources.size(); ++k) {
        const Json& lj = j["resources"][r.resources[k].id]["levels"];
        if (lj.isNull()) continue;
        const std::string where = "resource '" + r.resources[k].id + "' levels";
        if (!lj.isArray() || lj.size() == 0) fail(where, "expected a non-empty list");
        ResourceLevels rl; rl.res = int16_t(k);
        for (auto& e : lj.items()) {
            const std::string w = where + "[" + std::to_string(rl.levels.size()) + "]";
            LevelDef d;
            d.at = e.getFx("at");
            if (!rl.levels.empty() && d.at < rl.levels.back().at) fail(w, "levels must be sorted by \"at\"");
            for (auto& mj : e["mods"].items()) {
                uint64_t tags = 0;
                for (auto& t : mj["tags"].items()) {
                    uint64_t b = r.tagMask(t.str());
                    if (!b) fail(w, "unknown tag '" + t.str() + "' (no type has it)");
                    tags |= b;
                }
                bool ok = false;
                Stat s = parseStat(mj.getStr("stat", "damage"), &ok);
                if (!ok) fail(w, "unknown stat '" + mj.getStr("stat") + "'");
                d.modTags.push_back(tags); d.modStat.push_back(uint8_t(s));
                d.modAdd.push_back(mj.getFx("add")); d.modMul.push_back(mj.getFx("mul", Fixed::fromInt(1)));
            }
            if (const Json& dj = e["decay"]; dj.isObject()) {
                d.decayEvery = nonNeg(w, dj, "every", Fixed{});
                d.decayAmount = nonNeg(w, dj, "amount", Fixed{});
                d.decayAfter = nonNeg(w, dj, "after", Fixed{});
            }
            rl.levels.push_back(std::move(d));
        }
        r.modes.levels.push_back(std::move(rl));
    }
    r.modes.timedRules = timedRulesOf(r, "timedRules", j["timedRules"]);
    r.modes.stats = j.getBool("matchStats", false);
    // validate the rules block and every preset now (readable errors at load); setup parses the merged one
    ModesRules base = parseModes(r, j["modes"]);
    for (auto& [id, p] : j["matchModes"].members()) {
        if (!p.isObject()) fail("matchModes." + id, "expected an object");
        Json m = j["modes"].isObject() ? j["modes"] : Json::object();
        for (auto& [k, v] : p["modes"].members()) m.set(k, v);
        (void)parseModes(r, m);
        if (p.has("victory")) (void)loadVictory(p["victory"], r.victory);
    }
    // the modes system runs (and its state is hashed / saved) for any of these blocks; other games keep their checksums
    r.modes.any = base.any || j["matchModes"].isObject() || !r.modes.levels.empty() || !r.modes.timedRules.empty() || r.modes.stats;
    if (!r.modes.levels.empty()) r.originals = true;   // level mods go through playerModsStat
}

} // namespace aiverse::game
