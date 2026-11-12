// Parsing of rules / map "raid" blocks (RtsRaidData.h). Errors name the block and the key.
#include "RtsData.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

namespace {
[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error(where + ": " + what); }

Fixed seconds(const Json& j, const char* key, Fixed def, const std::string& where) {
    if (!j.has(key)) return def;
    if (!j[key].isNumber()) fail(where + "." + key, "number of seconds expected");
    Fixed v = j[key].fx();
    if (v.raw < 0 || v > Fixed::fromInt(86400)) fail(where + "." + key, "must be 0..86400 seconds");
    return v;
}
int itemRef(const Rules& r, const std::string& id, const std::string& where) {
    int i = r.mech.findItem(id);
    if (i < 0) fail(where, "unknown item '" + id + "' (rules \"items\")");
    return i;
}
uint16_t typeRef(const Rules& r, const std::string& id, const std::string& where) {
    int t = r.findType(id);
    if (t < 0) fail(where, "unknown type '" + id + "'");
    return uint16_t(t);
}
std::pair<int, int> range(const Json& j, int def, const std::string& where) {   // 2 or [1, 3]
    if (j.isNull()) return {def, def};
    int a, b;
    if (j.isArray()) { a = int(j[size_t(0)].integer(def)); b = int(j[size_t(1)].integer(a)); }
    else { a = b = int(j.integer(def)); }
    if (a < 0 || b < a || b > 1000) fail(where, "count must be 0..1000 (or [min, max])");
    return {a, b};
}
RaidLeave leaveOf(const Json& j, RaidLeave def, const std::string& where) {
    if (j.isNull()) return def;
    const std::string s = j.str();
    if (s == "reset") return RaidLeave::Reset;
    if (s == "pause") return RaidLeave::Pause;
    fail(where, "\"reset\" or \"pause\" expected");
}

RaidPointDef parsePoint(const Json& j, const RaidPointDef& base, const std::string& where) {
    if (!j.isObject()) fail(where, "object expected");
    RaidPointDef p = base;
    p.id = j.getStr("id", p.id);
    if (const Json& pos = j["pos"]; pos.isArray()) { p.x = pos[size_t(0)].fx(); p.z = pos[size_t(1)].fx(); }
    else if (!pos.isNull()) fail(where + ".pos", "[x, z] expected");
    if (j.has("radius")) { p.radius = j.getFx("radius"); if (p.radius.raw <= 0 || p.radius > Fixed::fromInt(64)) fail(where + ".radius", "must be 0..64"); }
    p.channel = seconds(j, "channel", p.channel, where);
    p.opensAt = seconds(j, "opensAt", p.opensAt, where);
    p.open = seconds(j, "open", p.open, where);
    p.sleep = seconds(j, "sleep", p.sleep, where);
    p.arrival = seconds(j, "arrival", p.arrival, where);
    p.departure = seconds(j, "departure", p.departure, where);
    if (j.has("requires")) { p.requires_.clear(); for (auto& r : j["requires"].items()) p.requires_.push_back(r.str()); }
    if (j.has("scope")) {
        const std::string s = j.getStr("scope");
        if (s != "player" && s != "point") fail(where + ".scope", "\"player\" or \"point\" expected");
        p.scope = s == "point" ? RaidScope::Point : RaidScope::Player;
    }
    if (j.has("contest")) {
        const std::string s = j.getStr("contest");
        if (s == "block") p.contest = RaidContest::Block;
        else if (s == "ignore") p.contest = RaidContest::Ignore;
        else if (s == "reset") p.contest = RaidContest::Reset;
        else fail(where + ".contest", "\"block\", \"ignore\" or \"reset\" expected");
    }
    p.leave = leaveOf(j["leave"], p.leave, where + ".leave");
    p.interruptOnDamage = j.getBool("interruptOnDamage", p.interruptOnDamage);
    p.capacity = int(j.getInt("capacity", p.capacity));
    if (p.capacity < 0 || p.capacity > 64) fail(where + ".capacity", "must be 0..64");
    p.call = j.getBool("call", p.call);
    p.follow = j.getBool("follow", p.follow);
    if (p.follow && !p.call) fail(where + ".follow", "needs \"call\": true (it opens around the caller)");
    p.team = int(j.getInt("team", p.team));
    p.fn = j.getStr("fn", p.fn);
    if (p.open.raw == 0 && p.sleep.raw > 0) fail(where + ".sleep", "needs \"open\" (seconds of the open window)");
    return p;
}

RaidObjectiveDef parseObjective(const Rules& r, const Json& j, const std::string& where) {
    if (!j.isObject()) fail(where, "object expected");
    RaidObjectiveDef o;
    o.id = j.getStr("id");
    if (o.id.empty()) fail(where, "\"id\" expected");
    o.text = j.getStr("text", o.id);
    const std::string k = j.getStr("kind", "script");
    if (k == "collect") { o.kind = RaidObjKind::Collect; o.item = int16_t(itemRef(r, j.getStr("item"), where + ".item")); }
    else if (k == "reach") {
        o.kind = RaidObjKind::Reach;
        const Json& pos = j["pos"];
        if (!pos.isArray()) fail(where + ".pos", "[x, z] expected");
        o.x = pos[size_t(0)].fx(); o.z = pos[size_t(1)].fx();
        o.radius = j.getFx("radius", o.radius);
    } else if (k == "kill") { o.kind = RaidObjKind::Kill; if (j.has("type")) o.type = typeRef(r, j.getStr("type"), where + ".type"); }
    else if (k == "interact") { o.kind = RaidObjKind::Interact; if (j.has("type")) o.type = typeRef(r, j.getStr("type"), where + ".type"); }
    else if (k == "script") o.kind = RaidObjKind::Script;
    else fail(where + ".kind", "collect, reach, kill, interact or script expected");
    o.count = int(j.getInt("count", 1));
    if (o.count < 1 || o.count > 100000) fail(where + ".count", "must be 1..100000");
    o.shared = j.getBool("shared", false);
    o.required = j.getBool("required", false);
    for (auto& [asset, n] : j["reward"].members()) {
        if (!n.isNumber() || n.integer() < 0) fail(where + ".reward." + asset, "non-negative integer expected");
        o.reward.push_back({asset, n.integer()});
    }
    return o;
}

void mergeMap(const Rules& r, RaidRules& out, const Json& m, const std::string& where) {
    out.duration = seconds(m, "duration", out.duration, where);
    out.startDelay = seconds(m, "startDelay", out.startDelay, where);
    if (m.has("mode")) {
        const std::string s = m.getStr("mode");
        if (s != "pve" && s != "pvp") fail(where + ".mode", "\"pve\" or \"pvp\" expected");
        out.pvp = s == "pvp";
    }
    out.endOnFirstExtract = m.getBool("endOnFirstExtract", out.endOnFirstExtract);
    for (size_t i = 0; i < m["points"].size(); ++i) {
        const Json& pj = m["points"][i];
        const std::string w = where + ".points[" + std::to_string(i) + "]";
        const std::string id = pj.getStr("id");
        if (id.empty()) fail(w, "\"id\" expected");
        const int k = out.findPoint(id);
        RaidPointDef p = parsePoint(pj, k >= 0 ? out.points[size_t(k)] : out.pointDefaults, w);
        if (k >= 0) out.points[size_t(k)] = p; else out.points.push_back(p);
    }
    for (size_t i = 0; i < m["objectives"].size(); ++i) {
        RaidObjectiveDef o = parseObjective(r, m["objectives"][i], where + ".objectives[" + std::to_string(i) + "]");
        const int k = out.findObjective(o.id);
        if (k >= 0) out.objectives[size_t(k)] = o; else out.objectives.push_back(o);
    }
}

void validate(const RaidRules& R, const std::string& where) {
    if (R.points.size() > 64) fail(where + ".points", "at most 64 extraction points");
    if (R.objectives.size() > 64) fail(where + ".objectives", "at most 64 objectives");
    for (auto& p : R.points)
        for (auto& q : p.requires_)
            if (R.findObjective(q) < 0) fail(where + " point '" + p.id + "' requires", "unknown objective '" + q + "'");
    for (size_t t = 0; t < R.interact.size(); ++t)
        if (R.interact[t].on && !R.interact[t].objective.empty() && R.findObjective(R.interact[t].objective) < 0)
            fail(where + " interact", "unknown objective '" + R.interact[t].objective + "'");
}
} // namespace

int RaidRules::findLoot(const std::string& id) const {
    for (size_t i = 0; i < loot.size(); ++i) if (loot[i].id == id) return int(i);
    return -1;
}
int RaidRules::findObjective(const std::string& id) const {
    for (size_t i = 0; i < objectives.size(); ++i) if (objectives[i].id == id) return int(i);
    return -1;
}
int RaidRules::findPoint(const std::string& id) const {
    for (size_t i = 0; i < points.size(); ++i) if (points[i].id == id) return int(i);
    return -1;
}

void loadRaidRules(Rules& r, const Json& j) {
    const Json& rj = j["raid"];
    if (rj.isNull()) return;
    const std::string where = "raid";
    if (!rj.isObject()) fail(where, "object expected");
    RaidRules& R = r.raid;
    R = RaidRules{};
    R.enabled = true;
    for (auto& t : rj["raiders"].items()) {
        const std::string s = t.str();
        if (s.rfind("tag:", 0) == 0) {
            const uint64_t b = r.tagMask(s.substr(4));
            if (!b) fail(where + ".raiders", "unknown tag '" + s.substr(4) + "'");
            R.raiderTags |= b;
        } else R.raiderTypes.push_back(typeRef(r, s, where + ".raiders"));
    }
    if (R.raiderTypes.empty() && !R.raiderTags) {
        bool any = false;
        for (auto& t : r.types) any |= t.mech.inventorySlots > 0;
        if (!any) fail(where + ".raiders", "name the raider types (or give a type an \"inventory\")");
    }
    if (rj.has("loseOnDeath")) {
        const std::string s = rj.getStr("loseOnDeath");
        if (s != "drop" && s != "destroy") fail(where + ".loseOnDeath", "\"drop\" or \"destroy\" expected");
        R.loss = s == "destroy" ? RaidLoss::Destroy : RaidLoss::Drop;
    }
    R.pickupRadius = rj.getFx("pickupRadius", R.pickupRadius);
    if (R.pickupRadius.raw < 0 || R.pickupRadius > Fixed::fromInt(16)) fail(where + ".pickupRadius", "must be 0..16");
    R.removeAll = rj.getStr("removeOnExtract", "all") != "raiders";
    // item values (receipts) and meta asset ids
    R.itemValue.assign(r.mech.items.size(), 0);
    R.itemAsset.resize(r.mech.items.size());
    for (size_t i = 0; i < r.mech.items.size(); ++i) {
        Fixed v{};
        for (auto& a : r.mech.items[i].cost) v += a.value;
        R.itemValue[i] = v.toInt();
        R.itemAsset[i] = r.mech.items[i].id;
    }
    for (auto& [id, ij] : rj["items"].members()) {
        const int i = itemRef(r, id, where + ".items");
        R.itemValue[size_t(i)] = ij.getInt("value", R.itemValue[size_t(i)]);
        R.itemAsset[size_t(i)] = ij.getStr("asset", R.itemAsset[size_t(i)]);
    }
    // loot tables
    for (auto& [id, tj] : rj["loot"].members()) {
        const std::string w = where + ".loot." + id;
        RaidLootTable t;
        t.id = id;
        const Json& entries = tj.isArray() ? tj : tj["entries"];
        auto rolls = range(tj.isObject() ? tj["rolls"] : Json(), 1, w + ".rolls");
        t.rollsMin = rolls.first; t.rollsMax = rolls.second;
        for (size_t i = 0; i < entries.size(); ++i) {
            const Json& e = entries[i];
            const std::string we = w + "[" + std::to_string(i) + "]";
            RaidLootEntry le;
            if (e.has("item")) le.item = int16_t(itemRef(r, e.getStr("item"), we + ".item"));
            else if (e.has("resource")) {
                const int res = r.findResource(e.getStr("resource"));
                if (res < 0) fail(we + ".resource", "unknown resource '" + e.getStr("resource") + "'");
                le.res = int16_t(res);
                le.amount = e.getFx("amount", Fixed::fromInt(1));
            } else if (!e.getBool("none")) fail(we, "\"item\", \"resource\" or \"none\": true expected");
            auto c = range(e["count"], 1, we + ".count");
            le.countMin = c.first; le.countMax = c.second;
            const int64_t wt = e.getInt("weight", 1);
            if (wt < 0 || wt > 1000000) fail(we + ".weight", "must be 0..1000000");
            le.weight = uint32_t(wt);
            t.total += le.weight;
            t.entries.push_back(le);
        }
        if (t.entries.empty()) fail(w, "no entries");
        R.loot.push_back(t);
    }
    // extraction defaults, rules points and objectives (maps add more)
    R.pointDefaults = parsePoint(rj["extraction"].isObject() ? rj["extraction"] : Json::object(), RaidPointDef{}, where + ".extraction");
    R.duration = seconds(rj, "duration", R.duration, where);
    R.startDelay = seconds(rj, "startDelay", R.startDelay, where);
    // interactables per type
    R.interact.assign(r.types.size(), {});
    for (auto& [tid, ij] : rj["interact"].members()) {
        const std::string w = where + ".interact." + tid;
        const uint16_t t = typeRef(r, tid, w);
        RaidInteractDef d;
        d.on = true;
        d.hold = seconds(ij, "hold", d.hold, w);
        d.range = ij.getFx("range", d.range);
        if (d.range.raw <= 0 || d.range > Fixed::fromInt(32)) fail(w + ".range", "must be 0..32");
        if (ij.has("loot")) { d.loot = int16_t(R.findLoot(ij.getStr("loot"))); if (d.loot < 0) fail(w + ".loot", "unknown loot table '" + ij.getStr("loot") + "'"); }
        d.objective = ij.getStr("objective");
        d.fn = ij.getStr("fn");
        d.uses = int(ij.getInt("uses", d.uses));
        if (d.uses < 0 || d.uses > 1000) fail(w + ".uses", "must be 0..1000");
        d.cooldown = seconds(ij, "cooldown", d.cooldown, w);
        d.consume = ij.getBool("consume", d.uses > 0);
        d.single = ij.getBool("single", d.single);
        d.interruptOnDamage = ij.getBool("interruptOnDamage", d.interruptOnDamage);
        d.leave = leaveOf(ij["leave"], d.leave, w + ".leave");
        R.interact[t] = d;
        R.anyInteract = true;
    }
    mergeMap(r, R, rj, where);   // mode, points, objectives
    validate(R, where);
}

RaidRules raidRulesForMap(const Rules& r, const Json& map) {
    RaidRules R = r.raid;
    const Json& m = map["raid"];
    if (m.isNull() || !R.enabled) {
        if (!m.isNull() && !R.enabled) fail("map raid", "the rules have no \"raid\" block");
        return R;
    }
    if (!m.isObject()) fail("map raid", "object expected");
    mergeMap(r, R, m, "map raid");
    validate(R, "map raid");
    return R;
}

} // namespace aiverse::game
