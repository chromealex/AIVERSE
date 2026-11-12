// Boss encounters at run time (rules "encounters", RtsEncounters.h): parsing into the boss-encounters library
// definitions, unit discovery, boss brains, mob behaviour -> RTS orders, telegraph resolution -> damage + statuses,
// the encounter schedule, hashing and saving. Integer / Fixed maths only; loops in entity order or sorted vectors.
#include "RtsEncounters.h"
#include "RtsInternal.h"
#include "aiverselib/genre/EncounterPersistence.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;

namespace {

[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error(where + ": " + what); }

uint32_t ticksOf(Fixed s, uint32_t rate) { return s.raw <= 0 ? 0 : uint32_t(((s * int64_t(rate)) + kHalf).floorInt()); }
Fixed nonNeg(const Json& j, const char* key, Fixed def, const std::string& where) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0) fail(where + "." + key, "must not be negative");
    return v;
}
uint32_t secs(const Json& j, const char* key, uint32_t defTicks, uint32_t rate, const std::string& where, uint32_t maxTicks = 36000) {
    if (!j.has(key)) return defTicks;
    uint32_t t = ticksOf(nonNeg(j, key, Fixed{}, where), rate);
    if (t > maxTicks) fail(where + "." + key, "too long");
    return t;
}
// multiplier (1 = unchanged) -> milli
int64_t milliOf(const Json& j, const char* key, int64_t def, const std::string& where, int64_t high = 100000) {
    if (!j.has(key)) return def;
    int64_t v = (nonNeg(j, key, Fixed{}, where) * 1000 + kHalf).floorInt();
    if (v > high) fail(where + "." + key, "too large");
    return v;
}
int typeIdx(const Rules& r, const std::string& id, const std::string& where) {
    int t = r.findType(id);
    if (t < 0) fail(where, "unknown unit type '" + id + "'");
    return t;
}
int statusIdx(const Rules& r, const std::string& id, const std::string& where) {
    int s = r.findStatus(id);
    if (s < 0 || size_t(s) >= r.userStatuses) fail(where, "unknown status '" + id + "' (rules \"statuses\")");
    return s;
}
Vec2 forwardOf(Fixed turns) { return {fxSinTurns(turns), fxCosTurns(turns)}; }
Fixed yawOf(Vec3 facing) { return (facing.x.raw == 0 && facing.z.raw == 0) ? Fixed{} : fxAtan2Turns(facing.x, facing.z); }
int64_t idOf(Entity e) { return int64_t(e.packed()) + 1; }
Entity entityOf(int64_t id) { return id > 0 ? Entity::fromPacked(uint64_t(id - 1)) : kNullEntity; }
template <class T> T* findSorted(std::vector<T>& v, uint64_t e) {
    auto it = std::lower_bound(v.begin(), v.end(), e, [](const T& a, uint64_t k) { return a.entity < k; });
    return it != v.end() && it->entity == e ? &*it : nullptr;
}
template <class T> T& insertSorted(std::vector<T>& v, T x) {
    auto it = std::lower_bound(v.begin(), v.end(), x.entity, [](const T& a, uint64_t k) { return a.entity < k; });
    return *v.insert(it, std::move(x));
}
bool liveUnit(World& w, Entity e) {
    if (!w.alive(e)) return false;
    auto* u = w.tryGet<Unit>(e);
    return u && u->hp.raw > 0 && !(u->flags & kUnitOffMap);
}
Fixed areaExtent(const lg::TelegraphDef& d) {
    Fixed off = fxAbs(d.offset.x) + fxAbs(d.offset.y);
    return (d.area.shape == lg::AreaShape::Line ? d.area.length : d.area.radius) + off;
}

// ---- persistence of the store (save + hash use the same bytes)
void ioStore(Archive& a, EncounterStore& s) {
    using lg::io;
    uint32_t n = uint32_t(s.bosses.size());
    a(n);
    if (!a.writing()) s.bosses.resize(n);
    for (auto& b : s.bosses) { a(b.entity); io(a, b.brain); a(b.target, b.participant, b.window, b.engaged, b.defeated, b.damageMilli, b.speedMilli, b.goal); }
    n = uint32_t(s.adds.size());
    a(n);
    if (!a.writing()) s.adds.resize(n);
    for (auto& x : s.adds) a(x.entity, x.boss, x.phase, x.wave);
    n = uint32_t(s.mobs.size());
    a(n);
    if (!a.writing()) s.mobs.resize(n);
    for (auto& m : s.mobs) { a(m.entity); io(a, m.brain); a(m.priority, m.home, m.goal, m.order, m.readyAt); }
    n = uint32_t(s.casts.size());
    a(n);
    if (!a.writing()) s.casts.resize(n);
    for (auto& c : s.casts) { io(a, c.t); a(c.caster, c.owner, c.attack, c.damageMilli); }
    a(s.nextCast);
    io(a, s.schedule);
}

} // namespace

EncBossState* EncounterStore::boss(uint64_t e) { return findSorted(bosses, e); }
EncMobState* EncounterStore::mob(uint64_t e) { return findSorted(mobs, e); }

// ================================================================= data

EncounterRules Encounters::parse(const Json& j, const Rules& rules, uint32_t rate) {
    EncounterRules R;
    if (!j.isObject()) return R;
    R.enabled = true;
    if (rate == 0) rate = 20;
    const size_t ntypes = rules.types.size();
    R.bossOfType.assign(ntypes, -1);
    R.mobOfType.assign(ntypes, -1);

    // ---- attacks
    for (auto& [id, a] : j["attacks"].members()) {
        const std::string w = "encounters.attacks." + id;
        if (!a.isObject()) fail(w, "expected an object");
        EncAttack ea; ea.id = id;
        lg::TelegraphDef d; d.id = int64_t(R.attacks.size()) + 1;
        const std::string shape = a.getStr("shape", "");
        if (shape == "circle") d.area.shape = lg::AreaShape::Circle;
        else if (shape == "cone") d.area.shape = lg::AreaShape::Cone;
        else if (shape == "line") d.area.shape = lg::AreaShape::Line;
        else if (shape == "ring") d.area.shape = lg::AreaShape::Ring;
        else fail(w + ".shape", "circle, cone, line or ring");
        if (d.area.shape == lg::AreaShape::Line) {
            d.area.length = nonNeg(a, "length", Fixed::fromInt(6), w);
            d.area.halfWidth = nonNeg(a, "width", Fixed::fromInt(2), w) / 2;
        } else {
            d.area.radius = nonNeg(a, "radius", Fixed::fromInt(3), w);
        }
        if (d.area.shape == lg::AreaShape::Ring) d.area.inner = nonNeg(a, "inner", d.area.radius / 2, w);
        if (d.area.shape == lg::AreaShape::Cone) {
            Fixed deg = nonNeg(a, "angle", Fixed::fromInt(90), w);
            if (deg.raw <= 0 || deg > Fixed::fromInt(360)) fail(w + ".angle", "0 < angle <= 360 degrees (full opening)");
            d.area.halfAngle = deg / 720;
        }
        const std::string anchor = a.getStr("anchor", "source");
        if (anchor == "aim") d.anchor = lg::AreaAnchor::Aim;
        else if (anchor != "source") fail(w + ".anchor", "'source' (at the caster, facing the aim) or 'aim' (centred on the aimed point)");
        if (const Json& o = a["offset"]; o.isArray()) {
            if (o.size() != 2) fail(w + ".offset", "expected [right, forward]");
            d.offset = {o[0].fx(), o[1].fx()};
        }
        d.windup = secs(a, "windup", ticksOf(Fixed::fromRatio(3, 4), rate), rate, w);
        d.active = std::max<uint32_t>(1, secs(a, "active", 1, rate, w));
        d.recovery = secs(a, "recovery", ticksOf(Fixed::fromRatio(1, 2), rate), rate, w);
        d.interval = secs(a, "interval", 0, rate, w);
        d.cooldown = secs(a, "cooldown", 0, rate, w);
        const Fixed dmg = nonNeg(a, "damage", Fixed{}, w);
        d.damage = (dmg * 1000).floorInt();                      // milli damage: hits keep fractions
        d.damageMin = (nonNeg(a, "damageMin", dmg, w) * 1000).floorInt();
        if (d.damageMin > d.damage) fail(w + ".damageMin", "must not exceed damage");
        d.falloff = a.getBool("falloff", false);
        d.oncePerTarget = a.getBool("oncePerTarget", true);
        d.trackAim = a.getBool("trackAim", false);
        d.sweep = a.getBool("sweep", false);
        d.cancelOnSourceDeath = a.getBool("cancelOnDeath", true);
        d.friendlyFire = a.getBool("friendlyFire", false);
        d.minRange = nonNeg(a, "minRange", Fixed{}, w);
        d.maxRange = nonNeg(a, "maxRange", Fixed{}, w);
        if (d.interval > d.active) fail(w + ".interval", "must not exceed active");
        if (d.sweep && d.area.shape != lg::AreaShape::Line) fail(w + ".sweep", "only line attacks sweep");
        if (d.maxRange.raw > 0 && d.minRange > d.maxRange) fail(w + ".minRange", "above maxRange");
        const Fixed statusDur = nonNeg(a, "statusDuration", Fixed::fromInt(2), w);
        for (auto& s : a["statuses"].items()) {
            EncStatusHit h;
            if (s.isString()) { h.status = int16_t(statusIdx(rules, s.str(), w + ".statuses")); h.duration = statusDur; }
            else if (s.isObject()) { h.status = int16_t(statusIdx(rules, s.getStr("status", ""), w + ".statuses")); h.duration = nonNeg(s, "duration", statusDur, w + ".statuses"); }
            else fail(w + ".statuses", "status id or {\"status\": id, \"duration\": seconds}");
            ea.statuses.push_back(h);
        }
        if (ea.statuses.size() > lg::kTelegraphMaxStatuses) fail(w + ".statuses", "too many");
        for (size_t k = 0; k < ea.statuses.size(); ++k) d.statuses.push_back(int64_t(k) + 1);   // per-attack slot ids (the hit reports them)
        if (a.has("damageType")) {
            const std::string dt = a.getStr("damageType", "");
            auto it = std::find(rules.damageTypes.begin(), rules.damageTypes.end(), dt);
            if (it == rules.damageTypes.end()) fail(w + ".damageType", "unknown damage type '" + dt + "'");
            ea.damageType = uint16_t(it - rules.damageTypes.begin());
        }
        ea.ability = a.getStr("ability", "");
        ea.reach = d.maxRange.raw > 0 ? d.maxRange : areaExtent(d);
        if (!lg::valid(d)) fail(w, "invalid attack (sizes, ranges or times out of bounds)");
        R.telegraphs[d.id] = d;
        R.attacks.push_back(std::move(ea));
    }
    auto attackRef = [&](const std::string& id, const std::string& where) {
        int k = R.findAttack(id);
        if (k < 0) fail(where, "unknown attack '" + id + "' (encounters.attacks)");
        return int64_t(k) + 1;
    };

    // ---- bosses (by unit type)
    for (auto& [tid, b] : j["bosses"].members()) {
        const std::string w = "encounters.bosses." + tid;
        if (!b.isObject()) fail(w, "expected an object");
        if (R.attacks.empty()) fail(w, "bosses need encounters.attacks");
        EncBossDef ed; ed.type = uint16_t(typeIdx(rules, tid, w));
        if (rules.types[ed.type].structure) fail(w, "a boss must be a mobile unit");
        ed.engage = nonNeg(b, "engageRange", ed.engage, w);
        ed.lose = fxMax(ed.engage, nonNeg(b, "loseRange", fxMax(ed.lose, ed.engage), w));
        ed.approach = nonNeg(b, "approach", ed.approach, w);
        for (auto& s : b["immune"].items()) ed.immune.push_back(int16_t(statusIdx(rules, s.str(), w + ".immune")));
        std::sort(ed.immune.begin(), ed.immune.end());
        lg::BossDef bd; bd.id = int64_t(ed.type) + 1;
        bd.archetype = uint8_t(std::clamp<int64_t>(b.getInt("archetype", 0), 0, 8));
        const Json& phases = b["phases"];
        if (!phases.isArray() || phases.size() == 0) fail(w + ".phases", "list at least one phase");
        if (phases.size() > lg::kBossMaxPhases) fail(w + ".phases", "at most 16 phases");
        for (size_t i = 0; i < phases.size(); ++i) {
            const Json& p = phases[i];
            const std::string pw = w + ".phases[" + std::to_string(i) + "]";
            lg::BossPhaseDef pd; pd.id = int64_t(i) + 1;
            pd.hpBelowMilli = milliOf(p, "hpBelow", 1000, pw, 1000);
            pd.afterTicks = secs(p, "after", 0, rate, pw, 3600000);
            if (i > 0 && pd.hpBelowMilli >= 1000 && pd.afterTicks == 0) fail(pw, "needs \"hpBelow\" < 1 or \"after\" seconds");
            pd.damageMilli = milliOf(p, "damage", 1000, pw);
            pd.speedMilli = milliOf(p, "speed", 1000, pw);
            pd.cooldownMilli = milliOf(p, "cooldown", 1000, pw);
            if (pd.speedMilli != 1000) R.speedMods = true;
            pd.globalCooldown = secs(p, "globalCooldown", 0, rate, pw);
            pd.transitionTicks = secs(p, "transition", 0, rate, pw);
            const std::string mode = p.getStr("mode", "byRange");
            if (mode == "byRange") pd.mode = lg::PatternMode::ByRange;
            else if (mode == "sequence") pd.mode = lg::PatternMode::Sequence;
            else if (mode == "weighted") pd.mode = lg::PatternMode::Weighted;
            else fail(pw + ".mode", "byRange, sequence or weighted");
            for (auto& e : p["pattern"].items()) {
                lg::BossPatternEntry pe;
                if (e.isString()) pe.attack = attackRef(e.str(), pw + ".pattern");
                else {
                    pe.attack = attackRef(e.getStr("attack", ""), pw + ".pattern");
                    if (e.has("alternate")) pe.alternate = attackRef(e.getStr("alternate", ""), pw + ".pattern");
                    pe.minRange = nonNeg(e, "minRange", Fixed{}, pw + ".pattern");
                    pe.maxRange = nonNeg(e, "maxRange", Fixed{}, pw + ".pattern");
                    pe.weight = uint32_t(std::clamp<int64_t>(e.getInt("weight", 1), 1, 1000000));
                }
                pd.pattern.push_back(pe);
            }
            if (pd.pattern.size() > lg::kBossMaxPattern) fail(pw + ".pattern", "at most 32 entries");
            for (auto& a : p["adds"].items()) {
                const std::string aw = pw + ".adds";
                lg::AddWaveDef wd;
                wd.unit = int64_t(typeIdx(rules, a.getStr("unit", ""), aw)) + 1;
                wd.count = uint32_t(std::clamp<int64_t>(a.getInt("count", 1), 1, 256));
                wd.maxAlive = uint32_t(std::clamp<int64_t>(a.getInt("maxAlive", 0), 0, 4096));
                wd.every = secs(a, "every", 0, rate, aw, 3600000);
                wd.minRadius = nonNeg(a, "minRadius", wd.minRadius, aw);
                wd.maxRadius = fxMax(wd.minRadius, nonNeg(a, "maxRadius", fxMax(wd.maxRadius, wd.minRadius), aw));
                pd.adds.push_back(wd);
            }
            if (pd.adds.size() > lg::kBossMaxAdds) fail(pw + ".adds", "at most 8 waves");
            bd.phases.push_back(std::move(pd));
        }
        if (const Json& e = b["enrage"]; e.isObject()) {
            const std::string ew = w + ".enrage";
            bd.enrage.afterTicks = secs(e, "after", 0, rate, ew, 3600000);
            bd.enrage.hpBelowMilli = milliOf(e, "hpBelow", 0, ew, 1000);
            bd.enrage.damageMilli = milliOf(e, "damage", 1000, ew);
            bd.enrage.speedMilli = milliOf(e, "speed", 1000, ew);
            bd.enrage.cooldownMilli = milliOf(e, "cooldown", 1000, ew);
            if (bd.enrage.speedMilli != 1000) R.speedMods = true;
        }
        if (!lg::valid(bd, R.telegraphs)) fail(w, "invalid boss (phases, ranges or multipliers out of bounds)");
        R.bossOfType[ed.type] = int16_t(R.bosses.size());
        R.bossSet[bd.id] = std::move(bd);
        R.bosses.push_back(std::move(ed));
    }

    // ---- mobs (by unit type)
    for (auto& [tid, m] : j["mobs"].members()) {
        const std::string w = "encounters.mobs." + tid;
        if (!m.isObject()) fail(w, "expected an object");
        EncMobDef md; md.type = uint16_t(typeIdx(rules, tid, w));
        if (rules.types[md.type].structure) fail(w, "a mob must be a mobile unit");
        auto& c = md.cfg;
        c.aggroRange = nonNeg(m, "aggroRange", c.aggroRange, w);
        c.loseRange = nonNeg(m, "loseRange", fxMax(c.loseRange, c.aggroRange), w);
        if (c.loseRange < c.aggroRange) fail(w + ".loseRange", "must be at least aggroRange");
        c.stopDistance = nonNeg(m, "stopDistance", c.stopDistance, w);
        c.switchMargin = nonNeg(m, "switchMargin", c.switchMargin, w);
        c.wanderMin = nonNeg(m, "wanderMin", c.wanderMin, w);
        c.wanderMax = nonNeg(m, "wanderMax", fxMax(c.wanderMax, c.wanderMin), w);
        if (c.wanderMax < c.wanderMin) fail(w + ".wanderMax", "must be at least wanderMin");
        c.arriveRadius = nonNeg(m, "arriveRadius", c.arriveRadius, w);
        if (c.arriveRadius.raw <= 0) fail(w + ".arriveRadius", "must be positive");
        c.leashRadius = nonNeg(m, "leash", Fixed{}, w);
        c.wanderDelay = secs(m, "wanderDelay", ticksOf(Fixed::fromInt(1), rate), rate, w);
        c.wanderLegTicks = std::max<uint32_t>(1, secs(m, "wanderLeg", ticksOf(Fixed::fromInt(5), rate), rate, w));
        c.wanderAttempts = uint32_t(std::clamp<int64_t>(m.getInt("wanderAttempts", 10), 1, 32));
        c.fleeBelowMilli = milliOf(m, "fleeBelow", 0, w, 1000);
        c.fleeRecoverMilli = milliOf(m, "fleeRecover", c.fleeBelowMilli, w, 1000);
        if (c.fleeRecoverMilli < c.fleeBelowMilli) fail(w + ".fleeRecover", "must be at least fleeBelow");
        c.fleeRange = nonNeg(m, "fleeRange", c.fleeRange, w);
        c.fleeTicks = secs(m, "fleeTime", ticksOf(Fixed::fromInt(3), rate), rate, w);
        c.wander = m.getBool("wander", true);
        c.spacingMilli = std::max<int64_t>(1, milliOf(m, "spacing", c.spacingMilli, w, 10000));
        c.correctionMilli = milliOf(m, "correction", c.correctionMilli, w, 1000);
        c.lookAhead = nonNeg(m, "lookAhead", c.lookAhead, w);
        c.maxNeighbours = uint32_t(std::clamp<int64_t>(m.getInt("neighbours", c.maxNeighbours), 0, int64_t(lg::kMobMaxNeighbours)));
        if (m.has("attack")) md.attack = int16_t(attackRef(m.getStr("attack", ""), w + ".attack") - 1);
        md.think = std::max<uint32_t>(1, secs(m, "think", ticksOf(Fixed::fromRatio(1, 5), rate), rate, w));
        md.flowDistance = nonNeg(m, "flowDistance", md.flowDistance, w);
        if (!lg::valid(c)) fail(w, "invalid behaviour (ranges or times out of bounds)");
        R.mobOfType[md.type] = int16_t(R.mobs.size());
        R.mobs.push_back(md);
    }

    // ---- schedule
    if (const Json& s = j["schedule"]; s.isObject()) {
        const std::string w = "encounters.schedule";
        auto& d = R.schedule;
        d.personal = s.getBool("personal", true);
        d.rotation = s.getBool("rotation", false);
        d.archetypes = uint8_t(std::clamp<int64_t>(s.getInt("archetypes", 0), 0, 8));
        if (d.rotation && d.archetypes == 0) fail(w + ".archetypes", "rotation needs 1..8 archetypes");
        if (const Json& sd = s["spawnDistance"]; sd.isArray()) {
            if (sd.size() != 2) fail(w + ".spawnDistance", "expected [min, max]");
            R.spawnMin = sd[0].fx(); R.spawnMax = sd[1].fx();
            if (R.spawnMin.raw < 0 || R.spawnMax < R.spawnMin) fail(w + ".spawnDistance", "0 <= min <= max");
        }
        const Json& wins = s["windows"];
        if (wins.size() > lg::kScheduleMaxWindows) fail(w + ".windows", "at most 64 windows");
        for (size_t i = 0; i < wins.size(); ++i) {
            const Json& x = wins[i];
            const std::string ww = w + ".windows[" + std::to_string(i) + "]";
            lg::EncounterWindowDef wd; wd.id = int64_t(i) + 1;
            wd.start = secs(x, "start", 0, rate, ww, 3600000);
            wd.end = secs(x, "end", 0, rate, ww, 3600000);
            if (wd.end <= wd.start) fail(ww + ".end", "must be after start");
            if (!d.windows.empty() && wd.start < d.windows.back().start) fail(ww + ".start", "windows must be in start order");
            wd.breather = secs(x, "breather", 0, rate, ww, 3600000);
            wd.excludePrevious = x.getBool("excludePrevious", false);
            wd.pauseWaves = x.getBool("pauseWaves", true);
            for (auto& v : x["variants"].items()) {
                lg::BossVariant bv;
                const std::string bid = v.isString() ? v.str() : v.getStr("boss", "");
                const int t = typeIdx(R.bosses.empty() ? rules : rules, bid, ww + ".variants");
                if (R.bossOfType[size_t(t)] < 0) fail(ww + ".variants", "'" + bid + "' is not in encounters.bosses");
                bv.boss = int64_t(t) + 1;
                bv.archetype = R.bossSet.at(bv.boss).archetype;
                if (v.isObject()) {
                    bv.weight = uint32_t(std::clamp<int64_t>(v.getInt("weight", 1), 1, 1000000));
                    bv.archetype = uint8_t(std::clamp<int64_t>(v.getInt("archetype", bv.archetype), 0, 8));
                }
                if (d.rotation && bv.archetype > d.archetypes) fail(ww + ".variants", "archetype above schedule.archetypes");
                wd.variants.push_back(bv);
            }
            if (wd.variants.empty()) fail(ww + ".variants", "list at least one boss");
            d.windows.push_back(std::move(wd));
        }
        if (!lg::valid(d)) fail(w, "invalid schedule");
    }
    for (auto& a : R.attacks)
        if (!a.ability.empty()) {
            bool any = false;
            for (auto& t : rules.types) for (auto& ab : t.abilities) if (ab.id == a.ability) any = true;
            if (!any) fail("encounters.attacks." + a.id + ".ability", "no unit type has an ability '" + a.ability + "'");
        }
    return R;
}

// ================================================================= setup / state

EncounterStore* Encounters::store(Simulation& sim) const { return active() ? sim.world().resource<EncounterStore>() : nullptr; }

bool Encounters::monsterPlayer(uint8_t slot) const { return std::binary_search(monsters_.begin(), monsters_.end(), slot); }

void Encounters::setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map) {
    m_ = &m;
    R_ = parse(rulesJson["encounters"], m.rules(), sim.tickRate());
    if (!active()) return;
    if (m.state(sim).surface) throw std::runtime_error("encounters: planet maps are not supported (flat maps only)");
    const Rules& r = m.rules();
    statusKinds_.assign(r.statuses.size(), EncStatusKind::Other);
    for (size_t i = 0; i < r.statuses.size(); ++i) {
        const StatusDef& s = r.statuses[i];
        EncStatusKind k = EncStatusKind::Other;
        if (s.flags & kStStun) k = EncStatusKind::Stun;
        else if (s.flags & kStRoot) k = EncStatusKind::Root;
        else if (s.tickAmount.raw > 0) k = EncStatusKind::Burn;
        else for (auto& md : s.mods) if (md.stat == Stat::Speed && (md.mul < kOne || md.add.raw < 0)) k = EncStatusKind::Slow;
        statusKinds_[i] = k;
    }
    monsters_.clear();
    const Json& players = map["players"];
    for (size_t p = 0; p < players.size() && p < 255; ++p)
        if (players[p].getBool("monsters", false)) monsters_.push_back(uint8_t(p + 1));
    sim.world().setResource<EncounterStore>();
}

void Encounters::hash(Simulation& sim, Hasher& h) const {
    EncounterStore* st = store(sim);
    if (!st) return;
    std::string bytes;
    Archive a(&bytes);
    ioStore(a, *st);   // writing never changes the store
    h.str(bytes);
}

void Encounters::io(Simulation& sim, Archive& a) {
    EncounterStore* st = store(sim);
    if (!st) return;
    ioStore(a, *st);
}

Fixed Encounters::speedMul(Simulation& sim, Entity e) const {
    EncounterStore* st = store(sim);
    if (!st) return kOne;
    const EncBossState* b = st->boss(e.packed());
    return b && b->speedMilli != 1000 ? Fixed::fromRatio(b->speedMilli, 1000) : kOne;
}

bool Encounters::wavesAllowed(Simulation& sim, uint8_t player) const {
    EncounterStore* st = store(sim);
    return !st || lg::ordinaryWavesAllowed(R_.schedule, st->schedule, sim.tick(), player);
}

lg::TelegraphView Encounters::view(const EncCast& c, uint32_t tick) const { return lg::telegraphView(R_.telegraphs, c.t, tick); }

// ================================================================= queries

Entity Encounters::anchorOf(Simulation& sim, uint8_t player) const {
    if (player == 0) return kNullEntity;
    if (auto* td = m_->topDown(); td && td->active()) {
        Entity h = td->heroOf(sim, player);
        if (liveUnit(sim.world(), h)) return h;
    }
    World& w = sim.world();
    Entity found;
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (found.valid() || o.player != player || u.hp.raw <= 0 || (u.flags & kUnitOffMap) || !w.has<Mobile>(e)) return;
        found = e;
    });
    return found;
}

Entity Encounters::nearestHostile(Simulation& sim, Entity self, Fixed range) const {
    World& w = sim.world();
    const Vec3 p = w.get<Transform>(self).pos;
    const uint8_t owner = w.get<Owner>(self).player;
    Entity best; Fixed bestD{};
    m_->state(sim).hash.query(p, range, [&](Entity c) {
        if (c == self || !liveUnit(w, c) || !m_->hostile(sim, owner, w.get<Owner>(c).player)) return;
        if (m_->rules().types[w.get<Unit>(c).type].invulnerable) return;
        const Fixed d = dist2D(w.get<Transform>(c).pos, p);
        if (d > range) return;
        if (!best.valid() || d < bestD || (d == bestD && c.packed() < best.packed())) { best = c; bestD = d; }
    });
    return best;
}

bool Encounters::castBusy(const EncounterStore& st, uint64_t caster, uint32_t tick, bool* locked, uint64_t* target) const {
    bool busy = false;
    for (auto& c : st.casts) {
        if (c.caster != caster || !c.t.live) continue;
        busy = true;
        if (locked && lg::telegraphLocksMovement(R_.telegraphs, c.t, tick)) *locked = true;
        if (target && c.t.target > 0) *target = uint64_t(c.t.target - 1);
    }
    return busy;
}

// ================================================================= casting

bool Encounters::cast(Simulation& sim, Entity caster, int attack, Vec3 aim, Entity target, int64_t damageMilli) {
    EncounterStore* st = store(sim);
    World& w = sim.world();
    if (!st || attack < 0 || size_t(attack) >= R_.attacks.size() || !liveUnit(w, caster)) return false;
    Unit& u = w.get<Unit>(caster);
    const Vec3 p = w.get<Transform>(caster).pos;
    const uint8_t owner = w.get<Owner>(caster).player;
    lg::CasterPose pose{p.xz(), yawOf(u.facing), true};
    const int64_t team = owner == 0 ? 0 : int64_t(m_->teamOf(sim, owner));
    auto inst = lg::beginTelegraph(R_.telegraphs, int64_t(attack) + 1, st->nextCast, sim.tick(), idOf(caster), team, pose, aim.xz(),
                                   w.alive(target) ? idOf(target) : 0);
    if (!inst) return false;
    ++st->nextCast;
    EncCast c; c.t = std::move(*inst); c.caster = caster.packed(); c.owner = owner; c.attack = uint16_t(attack);
    c.damageMilli = std::clamp<int64_t>(damageMilli, 0, 100000);
    st->casts.push_back(std::move(c));
    const Vec3 d = aim - p;
    if (d.x.raw != 0 || d.z.raw != 0) { Vec2 f = forwardOf(yawOf(Vec3{d.x, Fixed{}, d.z})); u.facing = {f.x, Fixed{}, f.y}; }
    return true;
}

void Encounters::onAbility(Simulation& sim, Entity caster, int index, Entity target, Vec3 point) {
    if (!active() || !sim.world().alive(caster)) return;
    const UnitType& t = m_->rules().types[sim.world().get<Unit>(caster).type];
    if (index < 0 || size_t(index) >= t.abilities.size()) return;
    const std::string& id = t.abilities[size_t(index)].id;
    for (size_t a = 0; a < R_.attacks.size(); ++a)
        if (R_.attacks[a].ability == id) cast(sim, caster, int(a), point, target == caster ? kNullEntity : target);
}

// ================================================================= the system

void Encounters::tick(Simulation& sim) {
    EncounterStore* st = store(sim);
    if (!st) return;
    discover(sim, *st);
    tickSchedule(sim, *st);
    for (size_t i = 0; i < st->bosses.size(); ++i) tickBoss(sim, *st, st->bosses[i]);
    const uint32_t tick = sim.tick();
    for (size_t i = 0; i < st->mobs.size(); ++i) {
        EncMobState& ms = st->mobs[i];
        const Entity e = Entity::fromPacked(ms.entity);
        if (!liveUnit(sim.world(), e)) continue;
        const int16_t k = R_.mobOfType[sim.world().get<Unit>(e).type];
        if (k < 0) continue;
        const EncMobDef& d = R_.mobs[size_t(k)];
        if ((tick + e.index) % d.think != 0) continue;
        tickMob(sim, *st, ms, d);
    }
    tickCasts(sim, *st);
    // forget the dead (adds: also those that left), keep the order
    World& w = sim.world();
    std::erase_if(st->bosses, [&](const EncBossState& b) { return !w.alive(Entity::fromPacked(b.entity)) || (b.defeated && !liveUnit(w, Entity::fromPacked(b.entity))); });
    std::erase_if(st->mobs, [&](const EncMobState& m) { return !liveUnit(w, Entity::fromPacked(m.entity)); });
    std::erase_if(st->adds, [&](const EncAdd& a) { return !liveUnit(w, Entity::fromPacked(a.entity)); });
}

void Encounters::discover(Simulation& sim, EncounterStore& st) {
    if (monsters_.empty() || (R_.bosses.empty() && R_.mobs.empty())) return;
    World& w = sim.world();
    std::vector<Entity> found;
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (u.hp.raw <= 0 || (u.flags & kUnitOffMap) || !monsterPlayer(o.player)) return;
        if (R_.bossOfType[u.type] >= 0 || R_.mobOfType[u.type] >= 0) found.push_back(e);
    });
    for (Entity e : found) {
        const uint16_t type = w.get<Unit>(e).type;
        if (R_.bossOfType[type] >= 0) {
            if (st.boss(e.packed())) continue;
            EncBossState b; b.entity = e.packed(); b.brain.boss = int64_t(type) + 1;
            insertSorted(st.bosses, std::move(b));
        } else if (!st.mob(e.packed())) {
            EncMobState m; m.entity = e.packed(); m.home = w.get<Transform>(e).pos;
            insertSorted(st.mobs, std::move(m));
        }
    }
}

void Encounters::tickSchedule(Simulation& sim, EncounterStore& st) {
    if (R_.schedule.windows.empty() || monsters_.empty()) return;
    World& w = sim.world();
    std::vector<int64_t> participants;
    for (auto& p : sim.players())
        if (p.alive && !monsterPlayer(p.slot) && anchorOf(sim, p.slot).valid()) participants.push_back(p.slot);
    std::sort(participants.begin(), participants.end());
    auto evs = lg::stepSchedule(R_.schedule, st.schedule, sim.tick(), participants, sim.rng());
    if (!evs) return;
    for (auto& ev : *evs) {
        const int64_t wi = ev.window - 1;
        if (ev.kind == lg::ScheduleEventKind::WindowOpened) {
            sim.emit({kEvBossWindow, 0, kNullEntity, kNullEntity, wi, {}});
            continue;
        }
        if (ev.kind == lg::ScheduleEventKind::Expired) {
            sim.emit({kEvBossWindow, uint8_t(ev.participant), kNullEntity, kNullEntity, wi | int64_t(2) << 16, {}});
            continue;
        }
        // Pending: try to place the boss on a free cell of the ring around the participant (shared: the first one)
        const uint8_t who = uint8_t(ev.participant ? ev.participant : (participants.empty() ? 0 : participants[0]));
        const Entity anchor = anchorOf(sim, who);
        if (!anchor.valid()) continue;
        const uint16_t type = uint16_t(ev.boss - 1);
        const UnitType& t = m_->rules().types[type];
        const MapGrid& g = m_->state(sim).grid;
        const Vec3 at = w.get<Transform>(anchor).pos;
        Vec3 spot; bool ok = false;
        for (int attempt = 0; attempt < 8 && !ok; ++attempt) {
            const Vec2 f = forwardOf(sim.rng().unit());
            const Fixed d = R_.spawnMin + (R_.spawnMax - R_.spawnMin) * sim.rng().unit();
            spot = {at.x + f.x * d, Fixed{}, at.z + f.y * d};
            const int cx = g.cellX(spot.x), cy = g.cellY(spot.z);
            ok = g.inside(cx, cy) && g.passable(cx, cy, t.pathLayer);
        }
        if (!ok) continue;   // try again next tick
        const Entity e = m_->spawn(sim, type, monsters_[0], spot);
        if (!e.valid()) continue;
        lg::markBossSpawned(st.schedule, ev.window, ev.participant, idOf(e), sim.tick());
        EncBossState b; b.entity = e.packed(); b.brain.boss = ev.boss; b.participant = who; b.window = ev.window;
        insertSorted(st.bosses, std::move(b));
        sim.emit({kEvBossWindow, who, e, anchor, wi | int64_t(1) << 16, spot});
    }
}

void Encounters::spawnAdds(Simulation& sim, EncounterStore& st, EncBossState& b, const lg::BossEvent& ev) {
    World& w = sim.world();
    const Entity boss = Entity::fromPacked(b.entity);
    const Vec3 at = w.get<Transform>(boss).pos;
    const uint8_t owner = w.get<Owner>(boss).player;
    const uint16_t type = uint16_t(ev.unit - 1);
    if (type >= m_->rules().types.size()) return;
    const MapGrid& g = m_->state(sim).grid;
    const uint8_t layer = m_->rules().types[type].pathLayer;
    int made = 0;
    for (uint32_t i = 0; i < ev.count; ++i) {
        const Vec2 f = forwardOf(sim.rng().unit());
        const Fixed d = ev.minRadius + (ev.maxRadius - ev.minRadius) * sim.rng().unit();
        Vec3 p{at.x + f.x * d, Fixed{}, at.z + f.y * d};
        int cx = g.cellX(p.x), cy = g.cellY(p.z);
        if (!g.inside(cx, cy) || !g.passable(cx, cy, layer)) {
            cx = std::clamp(cx, 0, g.w - 1); cy = std::clamp(cy, 0, g.h - 1);
            if (!g.nearestPassable(cx, cy, layer, 6)) continue;
            p = g.cellCenter(cx, cy);
        }
        const Entity e = m_->spawn(sim, type, owner, p);
        if (!e.valid()) continue;
        ++made;
        EncAdd a; a.entity = e.packed(); a.boss = b.entity; a.phase = b.brain.phase; a.wave = uint32_t(ev.value);
        insertSorted(st.adds, a);
        if (R_.mobOfType[type] >= 0 && monsterPlayer(owner) && !st.mob(e.packed())) {
            EncMobState m; m.entity = e.packed(); m.home = p;
            insertSorted(st.mobs, std::move(m));
        }
    }
    if (made) sim.emit({kEvBossAdds, owner, boss, kNullEntity, int64_t(type) | int64_t(made) << 16, at});
}

void Encounters::tickBoss(Simulation& sim, EncounterStore& st, EncBossState& b) {
    World& w = sim.world();
    const Entity e = Entity::fromPacked(b.entity);
    if (b.defeated || !w.alive(e) || !w.has<Unit>(e)) return;
    Unit& u = w.get<Unit>(e);
    const int16_t bi = R_.bossOfType[u.type];
    if (bi < 0) return;
    const EncBossDef& d = R_.bosses[size_t(bi)];
    const Vec3 pos = w.get<Transform>(e).pos;
    const uint8_t owner = w.get<Owner>(e).player;
    const uint32_t tick = sim.tick();
    // target: the participant's hero (personal boss) > the current one in reach > the nearest hostile
    Entity tgt;
    if (b.participant) { Entity a = anchorOf(sim, b.participant); if (a.valid()) tgt = a; }
    if (!tgt.valid()) {
        const Entity cur = b.target ? Entity::fromPacked(b.target) : kNullEntity;
        if (b.engaged && liveUnit(w, cur) && m_->hostile(sim, owner, w.get<Owner>(cur).player) && dist2D(w.get<Transform>(cur).pos, pos) <= d.lose) tgt = cur;
        else tgt = nearestHostile(sim, e, b.engaged ? d.lose : d.engage);
    }
    b.target = tgt.valid() ? tgt.packed() : 0;
    if (!b.engaged) {
        if (u.hp.raw <= 0 || !tgt.valid()) return;
        b.engaged = true;
    }
    bool locked = false;
    uint64_t castTarget = 0;
    const uint32_t flags = m_->statusFlags(sim, e);
    const bool stunned = (flags & kStStun) != 0;
    const bool busy = castBusy(st, b.entity, tick, &locked, &castTarget) || stunned;
    const lg::BossDef& bd = R_.bossSet.at(b.brain.boss);
    std::vector<uint32_t> alive;
    if (b.brain.initialized && b.brain.phase < bd.phases.size()) {
        alive.assign(bd.phases[b.brain.phase].adds.size(), 0);
        for (auto& a : st.adds) if (a.boss == b.entity && a.phase == b.brain.phase && a.wave < alive.size() && liveUnit(w, Entity::fromPacked(a.entity))) ++alive[a.wave];
    }
    lg::BossInput in;
    in.maxHp = std::max<int64_t>(1, m_->stat(sim, e, Stat::MaxHp).raw);
    in.hp = std::clamp<int64_t>(u.hp.raw, 0, in.maxHp);
    const Vec3 tpos = tgt.valid() ? w.get<Transform>(tgt).pos : pos;
    if (tgt.valid()) in.targetDistance = fxMin(dist2D(tpos, pos), lg::kCombatCoordLimit);
    in.busy = busy;
    in.aliveAdds = alive;
    auto step = lg::stepBoss(R_.bossSet, R_.telegraphs, b.brain, tick, in, sim.rng());
    if (!step) return;
    b.damageMilli = step->damageMilli;
    b.speedMilli = step->speedMilli;
    for (auto& ev : step->events) {
        switch (ev.kind) {
        case lg::BossEventKind::Engaged: sim.emit({kEvBossEngaged, owner, e, tgt, int64_t(u.type), pos}); break;
        case lg::BossEventKind::PhaseEntered: sim.emit({kEvBossPhase, owner, e, tgt, ev.value - 1, pos}); break;
        case lg::BossEventKind::Enraged: sim.emit({kEvBossEnraged, owner, e, tgt, int64_t(u.type), pos}); break;
        case lg::BossEventKind::Attack: cast(sim, e, int(ev.value - 1), tpos, tgt, step->damageMilli); break;
        case lg::BossEventKind::SpawnAdds: spawnAdds(sim, st, b, ev); break;
        case lg::BossEventKind::Defeated: {
            b.defeated = true;
            if (b.window) lg::markBossDefeated(R_.schedule, st.schedule, idOf(e), tick);
            sim.emit({kEvBossDefeated, u.lastAttacker, e, kNullEntity, int64_t(u.type), pos});
            break;
        }
        }
    }
    if (b.defeated || u.hp.raw <= 0) return;
    // movement: hold still while an attack winds up / recovers, during a phase transition and while stunned
    auto* mob = w.tryGet<Mobile>(e);
    if (!mob) return;
    if (locked || stunned || step->transitioning || busy) { if (mob->moving) m_->stopMoving(sim, e); return; }
    if (!tgt.valid()) return;
    const Fixed dist = dist2D(tpos, pos);
    if (dist > d.approach) {
        if (!mob->moving || dist2D(b.goal, tpos) > kOne) { m_->moveTo(sim, e, tpos, d.approach); b.goal = tpos; }
    } else {
        if (mob->moving) m_->stopMoving(sim, e);
        const Vec3 dir = tpos - pos;
        if (dir.x.raw != 0 || dir.z.raw != 0) { Vec2 f = forwardOf(yawOf(dir)); u.facing = {f.x, Fixed{}, f.y}; }
    }
}

void Encounters::order(Simulation& sim, Entity e, EncMobState& ms, uint8_t kind, Vec3 goal, Entity target, bool flow) {
    Order o;
    o.type = kind == 3 ? OrderType::Attack : kind == 2 ? OrderType::AttackMove : OrderType::Move;
    o.point = goal;
    o.target = kind == 3 ? target : kNullEntity;
    o.flow = flow;
    m_->issue(sim, e, o, false);
    ms.order = kind;
    ms.goal = goal;
}

void Encounters::tickMob(Simulation& sim, EncounterStore& st, EncMobState& ms, const EncMobDef& d) {
    World& w = sim.world();
    const Entity e = Entity::fromPacked(ms.entity);
    Unit& u = w.get<Unit>(e);
    const UnitType& t = m_->rules().types[u.type];
    const Vec3 pos = w.get<Transform>(e).pos;
    const uint8_t owner = w.get<Owner>(e).player;
    const uint32_t tick = sim.tick();
    auto& gs = m_->state(sim);
    bool locked = false;
    uint64_t committed = 0;
    const bool busy = castBusy(st, ms.entity, tick, &locked, &committed);
    const uint32_t flags = m_->statusFlags(sim, e);
    // priority: the participant hero of the personal boss whose add this is
    ms.priority = 0;
    for (auto& a : st.adds)
        if (a.entity == ms.entity) {
            if (const EncBossState* b = st.boss(a.boss); b && b->participant) { Entity h = anchorOf(sim, b->participant); if (h.valid()) ms.priority = h.packed(); }
            break;
        }
    lg::MobSelf self;
    self.id = idOf(e);
    self.position = pos.xz();
    self.home = ms.home.xz();
    self.anchor = ms.home.xz();
    self.radius = fxMin(t.radius, lg::kCombatSizeLimit);
    const Fixed maxHp = m_->stat(sim, e, Stat::MaxHp);
    self.hpMilli = maxHp.raw > 0 ? std::clamp<int64_t>(u.hp.raw * 1000 / maxHp.raw, 0, 1000) : 1000;
    self.committed = committed ? int64_t(committed) + 1 : 0;
    self.priority = ms.priority ? int64_t(ms.priority) + 1 : 0;
    self.movementLocked = locked || (flags & (kStStun | kStRoot));
    std::vector<lg::MobCandidate> cands;
    std::vector<lg::MobNeighbour> neigh;
    const Fixed look = fxMax(fxMax(d.cfg.loseRange, d.cfg.fleeRange), d.cfg.aggroRange);
    gs.hash.query(pos, look, [&](Entity c) {
        if (c == e || !liveUnit(w, c) || cands.size() >= lg::kMobMaxCandidates) return;
        if (!m_->hostile(sim, owner, w.get<Owner>(c).player)) return;
        const Vec3 cp = w.get<Transform>(c).pos;
        if (dist2D(cp, pos) > look) return;
        cands.push_back({idOf(c), cp.xz(), true, !m_->rules().types[w.get<Unit>(c).type].invulnerable});
    });
    // priority / committed targets count even beyond the query radius
    for (int64_t id : {self.priority, self.committed}) {
        const Entity c = entityOf(id);
        if (!id || !liveUnit(w, c) || std::any_of(cands.begin(), cands.end(), [&](const lg::MobCandidate& x) { return x.id == id; })) continue;
        const Vec3 cp = w.get<Transform>(c).pos;
        if (dist2D(cp, pos) < lg::kCombatCoordLimit) cands.push_back({id, cp.xz(), m_->hostile(sim, owner, w.get<Owner>(c).player), true});
    }
    std::sort(cands.begin(), cands.end(), [](auto& a, auto& b) { return a.id < b.id; });
    const Fixed near = (self.radius * 2 + kOne) * Fixed::fromRatio(d.cfg.spacingMilli, 1000) + d.cfg.lookAhead;
    gs.hash.query(pos, near, [&](Entity c) {
        if (c == e || neigh.size() >= lg::kMobMaxNeighbours || !liveUnit(w, c) || w.get<Owner>(c).player != owner || !w.has<Mobile>(c)) return;
        const Vec3 cp = w.get<Transform>(c).pos;
        if (dist2D(cp, pos) > near) return;
        neigh.push_back({idOf(c), cp.xz(), fxMin(m_->rules().types[w.get<Unit>(c).type].radius, lg::kCombatSizeLimit)});
    });
    std::sort(neigh.begin(), neigh.end(), [](auto& a, auto& b) { return a.id < b.id; });
    const MapGrid& g = gs.grid;
    auto walkable = [&](Vec2 from, Vec2 to) {
        const int x1 = g.cellX(to.x), y1 = g.cellY(to.y);
        return g.inside(x1, y1) && g.passable(x1, y1, t.pathLayer) && g.lineWalkable(g.cellX(from.x), g.cellY(from.y), x1, y1, t.pathLayer);
    };
    auto dec = lg::stepMob(d.cfg, ms.brain, tick, self, cands, neigh, sim.rng(), walkable);
    if (!dec || self.movementLocked) return;
    const Entity tgt = entityOf(dec->target);
    const bool tgtOk = liveUnit(w, tgt);
    const Vec3 tpos = tgtOk ? w.get<Transform>(tgt).pos : pos;
    const bool flowOn = m_->rules().movement.flow.enabled() && !(t.layer & kLayerAir);
    auto* ord = w.tryGet<Orders>(e);
    auto stop = [&]() { if (ord && !ord->queue.empty()) ord->queue.clear(); if (auto* mb = w.tryGet<Mobile>(e); mb && mb->moving) m_->stopMoving(sim, e); ms.order = 0; };
    switch (dec->mode) {
    case lg::MobMode::Hunt: {
        if (!tgtOk) break;
        const Fixed edge = dist2D(tpos, pos) - m_->rules().types[w.get<Unit>(tgt).type].radius;
        if (d.attack >= 0 && !busy && tick >= ms.readyAt && edge <= R_.attacks[size_t(d.attack)].reach) {
            if (cast(sim, e, d.attack, tpos, tgt)) {
                const auto& td = R_.telegraphs.at(int64_t(d.attack) + 1);
                ms.readyAt = tick + lg::telegraphLength(td) + td.cooldown;
                stop();
                break;
            }
        }
        const bool armed = !t.weapons.empty() && w.has<Combat>(e);
        if (!dec->approach) { if (!armed) stop(); else if (ms.order != 3 || !ord || ord->queue.empty() || ord->queue.front().target != tgt) order(sim, e, ms, 3, tpos, tgt, false); break; }
        Vec3 goal = tpos;
        if (dec->correction.x.raw != 0 || dec->correction.y.raw != 0) {   // swarm pressure: shift the goal around the crowd
            const Fixed s = t.radius * 2 + kOne;
            goal = {goal.x + dec->correction.x * s, goal.y, goal.z + dec->correction.y * s};
        }
        const Fixed dist = dist2D(tpos, pos);
        if (flowOn && dist > d.flowDistance) {
            if (ms.order != 2 || !ord || ord->queue.empty() || dist2D(ms.goal, goal) > Fixed::fromInt(2)) order(sim, e, ms, 2, goal, kNullEntity, true);
        } else if (armed) {
            if (ms.order != 3 || !ord || ord->queue.empty() || ord->queue.front().target != tgt) order(sim, e, ms, 3, tpos, tgt, false);
        } else if (ms.order != 1 || !ord || ord->queue.empty() || dist2D(ms.goal, goal) > kHalf) {
            order(sim, e, ms, 1, goal, kNullEntity, false);
        }
        break;
    }
    case lg::MobMode::Wander:
    case lg::MobMode::Return:
    case lg::MobMode::Flee: {
        const Vec3 goal{dec->goal.x, Fixed{}, dec->goal.y};
        if (!dec->approach) { stop(); break; }
        if (ms.order != 1 || !ord || ord->queue.empty() || dist2D(ms.goal, goal) > kHalf) order(sim, e, ms, 1, goal, kNullEntity, false);
        break;
    }
    case lg::MobMode::Idle:
        if (ms.order == 1 || ms.order == 2) stop();
        break;
    }
}

void Encounters::tickCasts(Simulation& sim, EncounterStore& st) {
    World& w = sim.world();
    auto& gs = m_->state(sim);
    const uint32_t tick = sim.tick();
    const Rules& rules = m_->rules();
    for (auto& c : st.casts) {
        if (!c.t.live) continue;
        const lg::TelegraphDef& def = R_.telegraphs.at(int64_t(c.attack) + 1);
        const Entity ce = Entity::fromPacked(c.caster);
        const bool alive = liveUnit(w, ce);
        lg::CasterPose pose{c.t.caster, c.t.pose.yaw, alive};
        if (alive) { pose.position = w.get<Transform>(ce).pos.xz(); pose.yaw = yawOf(w.get<Unit>(ce).facing); }
        std::optional<Vec2> aim;
        if (def.trackAim) { const Entity te = entityOf(c.t.target); if (liveUnit(w, te)) aim = w.get<Transform>(te).pos.xz(); }
        std::vector<lg::TelegraphTarget> targets;
        const Vec3 origin{c.t.pose.origin.x, Fixed{}, c.t.pose.origin.y};
        gs.hash.query(origin, areaExtent(def) + Fixed::fromInt(2), [&](Entity x) {
            if (!w.alive(x) || !w.has<Unit>(x) || targets.size() >= lg::kTelegraphMaxTargets) return;
            const Unit& xu = w.get<Unit>(x);
            if (xu.flags & kUnitOffMap) return;
            const UnitType& xt = rules.types[xu.type];
            const uint8_t xo = w.get<Owner>(x).player;
            lg::TelegraphTarget tt;
            tt.id = idOf(x); tt.team = xo == 0 ? 0 : int64_t(m_->teamOf(sim, xo)); tt.position = w.get<Transform>(x).pos.xz();
            tt.radius = fxMin(xt.radius, lg::kCombatSizeLimit); tt.targetable = xu.hp.raw > 0 && !xt.invulnerable;
            targets.push_back(tt);
        });
        std::sort(targets.begin(), targets.end(), [](auto& a, auto& b) { return a.id < b.id; });
        auto step = lg::stepTelegraph(R_.telegraphs, c.t, tick, pose, aim, targets);
        if (!step) { c.t.live = false; continue; }
        const Entity aimTarget = entityOf(c.t.target);
        for (auto ev : step->events) {
            if (ev == lg::TelegraphEvent::Started) sim.emit({kEvTelegraphStart, c.owner, ce, aimTarget, int64_t(c.attack), origin});
            else if (ev == lg::TelegraphEvent::Ended || ev == lg::TelegraphEvent::Cancelled)
                sim.emit({kEvTelegraphEnd, c.owner, ce, aimTarget, int64_t(c.attack) | int64_t(ev == lg::TelegraphEvent::Cancelled) << 16, origin});
        }
        const EncAttack& at = R_.attacks[c.attack];
        for (auto& h : step->hits) {
            const Entity te = entityOf(h.target);
            if (!liveUnit(w, te)) continue;
            const Fixed dmg = Fixed::fromRatio(h.damage * c.damageMilli / 1000, 1000);
            const Vec3 tp = w.get<Transform>(te).pos;
            if (dmg.raw > 0) m_->dealDamage(sim, te, dmg, at.damageType, alive ? ce : kNullEntity, c.owner);
            sim.emit({kEvTelegraphStrike, c.owner, ce, te, int64_t(c.attack) | (dmg.floorInt() << 16), tp});
            if (!w.alive(te) || at.statuses.empty()) continue;
            const EncBossState* tb = st.boss(te.packed());
            const EncBossDef* tdef = nullptr;
            if (tb) if (int16_t k = R_.bossOfType[w.get<Unit>(te).type]; k >= 0) tdef = &R_.bosses[size_t(k)];
            EffectCtx ctx; ctx.caster = alive ? ce : kNullEntity; ctx.owner = c.owner; ctx.target = te; ctx.point = tp; ctx.origin = origin;
            if (alive) ctx.sourceType = w.get<Unit>(ce).type;
            for (int64_t slot : h.statuses) {
                const EncStatusHit& sh = at.statuses[size_t(slot - 1)];
                if (tdef && std::binary_search(tdef->immune.begin(), tdef->immune.end(), sh.status)) continue;
                m_->addStatus(sim, te, sh.status, sh.duration, ctx);
            }
        }
        if (alive) {
            // charges: the caster rides the sweeping line; otherwise windup / recovery pin it
            if (def.sweep && c.t.released && c.t.live) {
                const Fixed reach = lg::sweepReach(R_.telegraphs, c.t, tick);
                const Vec2 f = forwardOf(c.t.pose.yaw);
                const Vec3 to{c.t.pose.origin.x + f.x * reach, w.get<Transform>(ce).pos.y, c.t.pose.origin.y + f.y * reach};
                const int cx = gs.grid.cellX(to.x), cy = gs.grid.cellY(to.z);
                if (gs.grid.inside(cx, cy) && gs.grid.passable(cx, cy, rules.types[w.get<Unit>(ce).type].pathLayer)) w.get<Transform>(ce).pos = to;
            }
            if (lg::telegraphLocksMovement(R_.telegraphs, c.t, tick)) if (auto* mb = w.tryGet<Mobile>(ce); mb && mb->moving) m_->stopMoving(sim, ce);
        }
    }
    std::erase_if(st.casts, [](const EncCast& c) { return !c.t.live; });
}

} // namespace aiverse::game
