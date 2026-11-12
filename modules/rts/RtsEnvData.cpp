// Environment data (RtsEnv.h): rules "environment" + map "environment" (map keys win), readable errors.
#include "RtsEnv.h"
#include "RtsInternal.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

using namespace detail;

namespace {

struct Src {
    const Json& rules;
    const Json& map;
    const Json& operator[](const std::string& k) const { return map.has(k) ? map[k] : rules[k]; }
    bool has(const std::string& k) const { return map.has(k) || rules.has(k); }
};

[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error("environment." + where + ": " + what); }

Vec3 pos(const Json& j) { return {j[0].fx(), Fixed{}, j[j.size() >= 3 ? 2 : 1].fx()}; }

std::pair<Fixed, Fixed> interval(const Json& j) {
    if (j.isArray()) return {j[0].fx(), j[1].fx(j[0].fx())};
    if (j.isObject()) return {j.getFx("min"), j.getFx("max", j.getFx("min"))};
    return {j.fx(), j.fx()};
}
std::vector<std::pair<Fixed, Fixed>> intervals(const Json& j) {
    std::vector<std::pair<Fixed, Fixed>> v;
    for (auto& x : j.items()) v.push_back(interval(x));
    return v;
}

} // namespace

// Point references: an index into the map's "entities" or the "id" of one of them.
static int pointRef(const Json& map, const Json& j, const std::string& where) {
    if (j.isNumber()) {
        int i = int(j.integer());
        if (i < 0 || size_t(i) >= map["entities"].size()) fail(where, "point index " + std::to_string(i) + " is not a map entity");
        return i;
    }
    const auto& ents = map["entities"].items();
    for (size_t i = 0; i < ents.size(); ++i) if (ents[i].getStr("id") == j.str()) return int(i);
    fail(where, "unknown map entity id '" + j.str() + "'");
}
static std::vector<int> pointRefs(const Json& map, const Json& j, const std::string& where) {
    std::vector<int> v;
    for (auto& x : j.items()) v.push_back(pointRef(map, x, where));
    return v;
}

void loadEnvironment(EnvDef& d, const Rules& r, const Json& rulesJ, const Json& mapJ) {
    const Json& rj = rulesJ["environment"];
    const Json& mj = mapJ["environment"];
    if (!rj.isObject() && !mj.isObject()) return;
    Src s{rj, mj};
    d.active = true;
    d.scale = s["scale"].fx(Fixed::fromInt(1));
    if (d.scale.raw <= 0) fail("scale", "must be > 0");
    d.complexity = mapJ.getStr("complexity", s["complexity"].str());
    d.screenY = s["screenYAxis"].isNumber() ? (s["screenYAxis"].integer() >= 0 ? 1 : -1) : -1;
    if (const Json& fg = s["fog"]; fg.isObject()) {   // BW FogOfWar (presentation in BW, sim vision here)
        d.fog.memory = fg.getBool("memory", true);
        d.fog.startSize = fg.getFx("startFowSize") / d.scale;
        d.fog.startTime = fg.getFx("startDuration", d.fog.startSize.raw > 0 ? Fixed::fromInt(3) : Fixed{});
    }
    auto D = [&](Fixed v) { return v / d.scale; };   // BW distance -> world
    auto skillRef = [&](const Json& j, const std::string& where) -> int16_t {
        if (j.isNull()) return -1;
        int k = r.findPlayerAbility(j.str());
        if (k < 0) fail(where, "unknown player ability '" + j.str() + "' (rules playerAbilities)");
        return int16_t(k);
    };
    auto typeRef = [&](const Json& j, const std::string& where) -> uint16_t {
        if (j.isNull()) return 0xFFFF;
        int k = r.findType(j.str());
        if (k < 0) fail(where, "unknown type '" + j.str() + "'");
        return uint16_t(k);
    };
    auto statusRef = [&](const Json& j, const std::string& where) -> int16_t {
        if (j.isNull()) return -1;
        int k = r.findStatus(j.str());
        if (k < 0) fail(where, "unknown status '" + j.str() + "'");
        return int16_t(k);
    };

    // ---- weather: "weather": ["rain", "snow", "lightning", "blizzard"] + "season": "winter" + tuning
    const Json& wj = s["weather"];
    auto flag = [&](const char* n) {
        if (wj.isArray()) { for (auto& x : wj.items()) if (x.str() == n) return true; return false; }
        return wj.getBool(n, false);
    };
    EnvWeatherDef& W = d.weather;
    W.rain = flag("rain"); W.snow = flag("snow"); W.lightning = flag("lightning"); W.blizzard = flag("blizzard");
    W.winter = s["season"].str() == "winter";
    const Json& wt = s["weatherParams"];
    W.puddlesFill = wt.getFx("puddlesFillSpeed", W.puddlesFill); W.puddlesDry = wt.getFx("puddlesDrySpeed", W.puddlesDry);
    W.rainWait = wt.getFx("rainWaitTime", W.rainWait); W.noRainWait = wt.getFx("noRainWaitTime", W.noRainWait);
    W.snowRestore = wt.getFx("snowRestoringSpeed", W.snowRestore); W.snowWait = wt.getFx("snowWaitTime", W.snowWait);
    W.snowFillChance = wt.getFx("snowFillRandomFactor", W.snowFillChance);
    if (wt.has("snowFillTimeRange")) { auto iv = interval(wt["snowFillTimeRange"]); W.snowFillMin = iv.first; W.snowFillMax = iv.second; }
    W.blizzardWait = wt.getFx("blizzardWaitTime", W.blizzardWait); W.blizzardFill = wt.getFx("blizzardFillTime", W.blizzardFill);
    W.blizzardKillSpeed = wt.getFx("blizzardUnitKillSpeed", W.blizzardKillSpeed); W.blizzardSpeed = wt.getFx("blizzardUnitSpeedFactor", W.blizzardSpeed);
    if (wt.has("blizzardUnitKillFactor")) { auto iv = interval(wt["blizzardUnitKillFactor"]); W.blizzardResistMin = iv.first; W.blizzardResistMax = iv.second; }
    W.lightningKillTime = wt.getFx("lightningKillTime", W.lightningKillTime);
    W.lightningKillChance = int(wt.getInt("lightningKillFactor", W.lightningKillChance));
    W.lightningRange = D(wt.getFx("lightningKillRange", W.lightningRange * d.scale));
    W.lightningStructureKill = int(wt.getInt("lightningPointKillValue", W.lightningStructureKill));
    W.lightningTime = wt.getFx("lightningTime", W.lightningTime); W.lightningWait = wt.getFx("lightningWaitTime", W.lightningWait);
    if (wt.has("lightningBurn")) {
        const Json& b = wt["lightningBurn"];
        W.burnId = b.getStr("field");
        W.burnValue = int32_t(b.getInt("value", 100));
        W.burnRadius = D(b.getFx("radius", Fixed{}));
    }
    W.rainSpread = fxClamp(wt.getFx("grassRainBurnRandomFactor", W.rainSpread), Fixed{}, Fixed::fromInt(1));
    W.blizzardVar = wt.getStr("blizzardVar", W.blizzardVar);
    if (W.blizzardKillSpeed.raw <= 0 || W.puddlesFill.raw <= 0 || W.puddlesDry.raw <= 0) fail("weatherParams", "speeds and times must be > 0");

    // ---- wind
    const Json& wn = s["wind"];
    if (wn.isObject()) {
        EnvWindDef& N = d.wind;
        N.enabled = wn.getBool("enabled", true);
        N.idleMin = wn.getFx("minIdleDuration", N.idleMin); N.idleMax = wn.getFx("maxIdleDuration", N.idleMax);
        N.blowMin = wn.getFx("minBlowDuration", N.blowMin); N.blowMax = wn.getFx("maxBlowDuration", N.blowMax);
        N.forceMin = D(wn.getFx("minForce", N.forceMin * d.scale)); N.forceMax = D(wn.getFx("maxForce", N.forceMax * d.scale));
    }

    // ---- zones
    for (auto& zj : s["zones"].items()) {
        EnvZone z;
        z.id = zj.getStr("id");
        std::string k = zj.getStr("kind", "other");
        z.kind = k == "water" ? EnvZoneKind::Water : k == "road" ? EnvZoneKind::Road : k == "toad" ? EnvZoneKind::Toad : EnvZoneKind::Other;
        z.slowdown = zj.getFx("slowdown", Fixed::fromInt(1));
        if (z.slowdown.raw <= 0) fail("zones." + z.id, "slowdown must be > 0");
        if (zj.has("circle")) { z.center = pos(zj["circle"]); z.radius = zj["circle"][2].fx(); }
        else if (zj.has("rect")) {
            const Json& rr = zj["rect"];
            Fixed x = rr[0].fx(), y = rr[1].fx(), w = rr[2].fx(), h = rr[3].fx();
            z.poly = {{x, Fixed{}, y}, {x + w, Fixed{}, y}, {x + w, Fixed{}, y + h}, {x, Fixed{}, y + h}};
        } else for (auto& p : zj["poly"].items()) z.poly.push_back(pos(p));
        if (z.radius.raw <= 0 && z.poly.size() < 3) fail("zones." + z.id, "needs \"poly\" (3+ points), \"rect\" or \"circle\"");
        d.zones.push_back(std::move(z));
    }

    // ---- splines (worm paths, auto skills)
    for (auto& sj : s["splines"].items()) {
        EnvSpline sp;
        sp.id = sj.getStr("id");
        std::vector<Vec3> pts;
        for (auto& p : sj["points"].items()) pts.push_back(pos(p));
        if (pts.size() < 2) fail("splines." + sp.id, "needs 2+ points");
        sp.build(pts, sj.getBool("loop", false));
        sp.skill = skillRef(sj["skill"], "splines." + sp.id + ".skill");
        sp.velocity = D(sj.getFx("velocity", Fixed::fromInt(100) * d.scale));
        sp.emitPeriod = sj.getFx("emitPeriod", sp.emitPeriod);
        sp.firstEmission = sj.getFx("firstEmissionCooldown", sp.firstEmission);
        sp.enabled = sj.getBool("enabled", true);
        d.splines.push_back(std::move(sp));
    }
    auto splineRef = [&](const Json& j, const std::string& where) {
        if (j.isNumber()) { int i = int(j.integer()); if (i < 0 || size_t(i) >= d.splines.size()) fail(where, "spline index out of range"); return i; }
        for (size_t i = 0; i < d.splines.size(); ++i) if (d.splines[i].id == j.str()) return int(i);
        fail(where, "unknown spline '" + j.str() + "'");
    };

    // ---- multi-spline auto skills (BW AutoSkills.MultiSplineSkill: the first `order` splines, in turn or at random)
    for (auto& mj : s["multiSplineSkills"].items()) {
        EnvMultiDef md;
        md.skill = skillRef(mj["skill"], "multiSplineSkills.skill");
        md.count = mj["order"].isArray() ? int(mj["order"].size()) : int(mj.getInt("count", 0));
        md.random = mj.getBool("randomJump", false);
        md.transfer = mj.getBool("shouldBuildTransferSpline", true);
        if (md.skill < 0 || md.count <= 0 || size_t(md.count) > d.splines.size()) fail("multiSplineSkills", "needs \"skill\" and 1..splines \"order\" entries");
        d.multis.push_back(md);
    }

    // ---- trample fields (grass / winter); fields are resolved by the runtime (GridFields), ids kept in fieldIndex keys
    for (auto& tj : s["trample"].items()) {
        EnvTrampleDef t;
        t.fieldId = tj.getStr("field");
        if (t.fieldId.empty()) fail("trample", "needs \"field\" (a grid field id)");
        t.perSecond = tj.getFx("perSecond");
        for (auto& row : tj["speed"].items()) t.speed.push_back({row[0].fx(), row[1].fx()});
        t.lerpSpeed = tj.getFx("lerpSpeed", Fixed::fromInt(-1));
        t.restore = tj.getFx("restore");
        t.snowRestore = tj.getBool("snowRestore", false);
        t.killId = tj.getStr("killField");
        t.killAbove = int32_t(tj.getInt("killAbove", 1));
        t.hideStatus = statusRef(tj["hideStatus"], "trample.hideStatus");
        t.hideAbove = tj.getFx("hideAbove");
        t.skipRoads = tj.getBool("skipRoads", true);
        d.trample.push_back(std::move(t));
    }

    // ---- worms (one config per AI complexity, BW WormConfigs)
    for (auto& wj2 : s["worms"].items()) {
        EnvWormDef w;
        w.complexity = wj2.getStr("complexity");
        for (auto& p : wj2["paths"].items()) w.paths.push_back(splineRef(p, "worms.paths"));
        if (w.paths.empty()) fail("worms", "a worm needs \"paths\" (spline ids)");
        w.onGround = intervals(wj2["onTheGroundCycles"]);
        w.underground = intervals(wj2["undergroundCycles"]);
        w.army = intervals(wj2["spawnArmyCycles"]);
        if (w.onGround.empty()) w.onGround = {{Fixed::fromInt(10), Fixed::fromInt(15)}};
        if (w.underground.empty()) w.underground = {{Fixed::fromInt(10), Fixed::fromInt(15)}};
        for (auto& p : wj2["targetPlayers"].items()) w.targetPlayers.push_back(uint8_t(p.integer()));
        const Json& in = wj2["info"].isObject() ? wj2["info"] : wj2;
        w.speed = D(in.getFx("speed", w.speed * d.scale));
        w.noUnits = in.getFx("onTheGroundNoUnitsCoefficient", w.noUnits); w.catching = in.getFx("onTheGroundCatchingUnitsCoefficient", w.catching);
        w.full = in.getFx("onTheGroundFullOfUnitsCoefficient", w.full); w.under = in.getFx("undergroundCoefficient", w.under);
        w.headSwitch = in.getFx("headSwitchPeriod", w.headSwitch); w.scanPeriod = in.getFx("testUnitsPeriod", w.scanPeriod);
        w.killBegin = D(in.getFx("killZoneBegin", w.killBegin * d.scale)); w.killEnd = D(in.getFx("killZoneEnd", w.killEnd * d.scale));
        w.killWidth = D(in.getFx("killZoneWidth", w.killWidth * d.scale)); w.vision = D(in.getFx("visionDistance", w.vision * d.scale));
        w.traceWidth = D(in.getFx("traceWidth", w.traceWidth * d.scale)); w.traceEmit = in.getFx("traceEmitPeriod", w.traceEmit);
        w.traceDecay = in.getFx("traceDecayDuration", w.traceDecay); w.traceSlowdown = in.getFx("traceSlowdownCoefficient", w.traceSlowdown);
        w.minToBuilding = D(in.getFx("minDistanceToBuilding", w.minToBuilding * d.scale)); w.appearDistance = D(in.getFx("appearDistance", w.appearDistance * d.scale));
        w.reverseOffset = D(in.getFx("reverseSkillOffset", w.reverseOffset * d.scale));
        w.armySpeed = in.getFx("armySpeedCoefficient", w.armySpeed);
        w.appearTime = in.getFx("headAppearDuration", w.appearTime); w.disappearTime = in.getFx("headDisappearDuration", w.disappearTime);
        for (auto& t : in["tailData"].items()) w.segments.push_back(D(t.getFx("shift", Fixed::fromInt(30) * d.scale)));
        if (w.segments.empty()) w.segments.assign(6, D(Fixed::fromInt(30) * d.scale));
        w.segments[0] = Fixed{};
        if (w.traceSlowdown.raw <= 0 || w.speed.raw <= 0 || w.scanPeriod.raw <= 0 || w.traceEmit.raw <= 0) fail("worms", "speed, testUnitsPeriod, traceEmitPeriod and traceSlowdownCoefficient must be > 0");
        w.armyUnit = typeRef(wj2["armyUnit"], "worms.armyUnit");
        w.reverseSkill = skillRef(wj2["reverseSkill"], "worms.reverseSkill");
        w.look = typeRef(wj2["look"], "worms.look");
        w.lookTail = wj2.has("lookTail") ? typeRef(wj2["lookTail"], "worms.lookTail") : w.look;
        d.worms.push_back(std::move(w));
    }

    // ---- toads
    auto toadOf = [&](const Json& tj) {
        EnvToadDef t;
        if (!tj.has("pos")) fail("toad", "needs \"pos\"");
        t.pos = pos(tj["pos"]);
        t.attack = tj.getFx("attackDuration", t.attack); t.postAttack = tj.getFx("postAttackDuration", t.postAttack);
        t.idleSwitch = tj.getFx("idleSwitchPeriod", t.idleSwitch);
        t.maxUnits = int(tj.getInt("maxUnits", t.maxUnits));
        t.explodeDelay = tj.getFx("explodeDelay", t.explodeDelay); t.explosion = tj.getFx("explosionDuration", t.explosion);
        t.zone = tj.getStr("zone");
        t.look = typeRef(tj["look"], "toad.look");
        if (t.attack.raw <= 0) fail("toad", "attackDuration must be > 0");
        return t;
    };
    if (s["toad"].isObject() && s["toad"].getBool("enabled", true)) d.toads.push_back(toadOf(s["toad"]));
    for (auto& tj : s["toads"].items()) d.toads.push_back(toadOf(tj));

    // ---- flying bombers
    auto flyerOf = [&](const Json& fj) {
        EnvFlyerDef f;
        for (auto& rj2 : fj["rallyPoints"].items()) f.rally.push_back({pos(rj2["start"]), pos(rj2["end"])});
        if (f.rally.empty()) fail("flyBomb", "needs \"rallyPoints\" [{start, end}]");
        f.velocity = D(fj.getFx("flyVelocity", f.velocity * d.scale));
        f.dropMin = fj.getFx("minDropBombPeriod", f.dropMin); f.dropMax = fj.getFx("maxDropBombPeriod", f.dropMax);
        f.maxBombs = int(fj.getInt("maxBombsByPass", f.maxBombs)); f.health = int(fj.getInt("health", f.health));
        f.idleMin = fj.getFx("minIdleDuration", f.idleMin); f.idleMax = fj.getFx("maxIdleDuration", f.idleMax);
        f.heightMin = D(fj.getFx("minDropBombHeight", f.heightMin * d.scale)); f.heightMax = D(fj.getFx("maxDropBombHeight", f.heightMax * d.scale));
        f.gravity = D(fj.getFx("gravity", f.gravity * d.scale));
        if (fj.has("amplitude")) { f.amplitude = pos(fj["amplitude"]); f.amplitude = f.amplitude / d.scale; } else f.amplitude = f.amplitude / d.scale;
        if (fj.has("frequency")) f.frequency = pos(fj["frequency"]);
        f.skill = skillRef(fj["explosionSkill"], "flyBomb.explosionSkill");
        f.type = typeRef(fj["type"], "flyBomb.type");
        f.bombLook = typeRef(fj["bombLook"], "flyBomb.bombLook");
        if (f.velocity.raw <= 0 || f.gravity.raw >= 0) fail("flyBomb", "flyVelocity must be > 0 and gravity < 0");
        return f;
    };
    if (s["flyBomb"].isObject() && s["flyBomb"].getBool("enabled", true)) d.flyers.push_back(flyerOf(s["flyBomb"]));
    for (auto& fj : s["flyBombs"].items()) d.flyers.push_back(flyerOf(fj));

    // ---- Mouro director
    const Json& mo = s["mouro"];
    if (mo.isObject() && mo.getBool("enabled", true)) {
        EnvMouroDef& M = d.mouro;
        M.enabled = true;
        M.bot = uint8_t(mo.getInt("bot", 0)); M.player = uint8_t(mo.getInt("player", 0));
        M.dominationType = typeRef(mo["dominationType"], "mouro.dominationType");
        M.emotionsCooldown = mo.getFx("emotionsCooldown");
        if (mo.has("castPos")) M.castPos = pos(mo["castPos"]);
        M.morale = int16_t(r.findResource(mo.getStr("moraleResource", "morale")));
        M.energy = int16_t(r.findResource(mo.getStr("energyResource", "energy")));
        static const char* kActs[] = {"castAt", "castRing", "caldron", "skeletonAttack", "averageMorale", "addUnits", "animation", "death"};
        for (auto& hj : mo["actionHolders"].items()) {
            if (!hj.getBool("active", true)) continue;
            EnvHolderDef h;
            for (auto& c : hj["complexities"].items()) h.complexities.push_back(c.str());
            h.startsFrom = hj.getFx("startsFrom"); h.delay = hj.getFx("activationDelay");
            for (auto& aj : hj["actions"].items()) {
                if (!aj.getBool("active", true)) continue;
                EnvActionDef a;
                a.name = aj.getStr("name");
                std::string k = aj.getStr("type", "animation");
                a.kind = 255;
                for (uint8_t i = 0; i < 8; ++i) if (k == kActs[i]) a.kind = i;
                if (a.kind == 255) fail("mouro.actions", "unknown action type '" + k + "' (castAt, castRing, caldron, skeletonAttack, averageMorale, addUnits, animation, death)");
                a.skill = skillRef(aj["skill"], "mouro.actions." + a.name);
                if (a.kind <= kActAverageMorale && a.skill < 0) fail("mouro.actions." + a.name, "needs \"skill\"");
                a.duration = aj.getFx("duration", a.duration); a.castAt = aj.getFx("castAt", fxMin(a.castAt, a.duration));
                a.nextTimeout = aj.getFx("nextActionTimeout");
                a.minDomination = int(aj.getInt("minimumDominationPointsCount", 0));
                a.moraleDiff = aj.getFx("moraleStarDiff");
                a.sources = pointRefs(mapJ, aj["sourcePoints"], "mouro.actions." + a.name);
                a.targets = pointRefs(mapJ, aj["targetPoints"], "mouro.actions." + a.name);
                a.targetCount = int(aj.getInt("targetPointsCount", a.kind == kActSkeletons ? 1 : 3));
                a.unitsPerPoint = int(aj.getInt("unitsPerPoint", 24));
                a.toPlayer = aj.getBool("moveToPlayer", true); a.toBot = aj.getBool("moveToBot", true);
                a.animation = aj.getStr("animation", a.name);
                h.actions.push_back(std::move(a));
            }
            M.holders.push_back(std::move(h));
        }
        std::stable_sort(M.holders.begin(), M.holders.end(), [](const EnvHolderDef& a, const EnvHolderDef& b) { return a.startsFrom < b.startsFrom; });
        static const char* kConds[] = {"cooldown", "badAttack", "dominationCaptured", "botPointCaptured", "probability", "maxPlayCount", "endGame", "skillActivated", "actionExecuted"};
        for (auto& ej : mo["emotions"].items()) {
            EnvEmotionDef e;
            e.emotion = ej.getStr("emotion");
            for (auto& cj : ej["conditions"].items()) {
                EnvCondDef c;
                std::string k = cj.getStr("type");
                c.kind = 255;
                for (uint8_t i = 0; i < 9; ++i) if (k == kConds[i]) c.kind = i;
                if (c.kind == 255) fail("mouro.emotions." + e.emotion, "unknown condition '" + k + "'");
                c.value = cj.getFx("cooldown", cj.getFx("percent"));
                c.count = int(cj.getInt("unitsLostCount", cj.getInt("maxPlayCount", 0)));
                for (auto& n : cj["capturedCount"].items()) c.points.push_back(int(n.integer()));
                c.byBot = cj.getBool("byBot", cj.getBool("capturedByBot", cj.getBool("castedByBot", false)));
                c.byPlayer = cj.getBool("byPlayer", cj.getBool("capturedByPlayer", cj.getBool("castedByPlayer", false)));
                c.win = cj.getBool("mouroWin", false); c.lose = cj.getBool("mouroLose", false);
                e.conds.push_back(std::move(c));
            }
            M.emotions.push_back(std::move(e));
        }
    }

    // ---- map scroller (moving play screen + actions)
    const Json& sc = s["mapScroller"];
    if (sc.isObject()) {
        d.screen.enabled = sc.has("screen");
        if (d.screen.enabled) { d.screen.size = pos(sc["screen"]); d.screen.start = sc.has("start") ? pos(sc["start"]) : Vec3{}; }
        static const char* kKinds[] = {"waitPointsCaptured", "waitTime", "waitKingOfTheHillScore", "waitNoUnits", "moveScreen", "convertAll", "convertOutside", "setInvincible", "moveUnits", "gotoStep"};
        for (auto& aj : sc["actions"].items()) {
            EnvScrollAction a;
            std::string k = aj.getStr("type");
            a.kind = 255;
            for (uint8_t i = 0; i < 10; ++i) if (k == kKinds[i]) a.kind = i;
            if (a.kind == 255) fail("mapScroller.actions", "unknown action '" + k + "'");
            std::string who = aj.getStr("player", aj.getStr("owner", aj.getStr("to", "player")));
            a.who = who == "bot" ? 1 : who == "neutral" ? 2 : 0;
            if (a.kind == kScrConvertOutside) {
                std::string f = aj.getStr("from", "player");
                a.from = f == "bot" ? 1 : f == "neutral" ? 2 : 0;
            }
            a.points = pointRefs(mapJ, aj["points"], "mapScroller.actions");
            a.seconds = aj.getFx("seconds", aj.getFx("duration"));
            a.score = aj.getFx("score");
            if (aj.has("position")) a.pos = pos(aj["position"]);
            if (a.kind == kScrSend) { a.from = pointRef(mapJ, aj["pointFrom"], "mapScroller.moveUnits"); a.to = pointRef(mapJ, aj["pointTo"], "mapScroller.moveUnits"); }
            a.count = int(aj.getInt("count", 0));
            a.step = int(aj.getInt("step", 0));
            a.flag = aj.getBool("invincible", true);
            d.scroller.push_back(std::move(a));
        }
    }

    // ---- off-map senders
    const Json& om = s["outmapUnits"];
    if (om.isObject()) {
        const Json* pick = &om;
        for (auto& it : om["items"].items()) if (it.getStr("complexity") == d.complexity && !d.complexity.empty()) pick = &it;
        d.outmap.repeat = pick->getFx("timerRepeatDuration", Fixed::fromInt(-1));
        d.outmap.sources = pointRefs(mapJ, om["sources"], "outmapUnits.sources");
        for (auto& mv : (*pick)["moveActions"].items()) {
            EnvOutmapMove m;
            m.time = mv.getFx("time");
            m.from = pointRef(mapJ, mv["from"], "outmapUnits.moveActions");
            m.to = pointRef(mapJ, mv["to"], "outmapUnits.moveActions");
            m.count = int(mv.getInt("count", 0));
            d.outmap.moves.push_back(m);
        }
        std::stable_sort(d.outmap.moves.begin(), d.outmap.moves.end(), [](const EnvOutmapMove& a, const EnvOutmapMove& b) { return a.time < b.time; });
    }

    // ---- point modules: graveyard, souls, fire, caves, walls
    const Json& gy = s["graveyard"];
    if (gy.isObject()) {
        const Json& mode = gy[mapJ.getBool("localGame", false) ? "localGame" : "networkGame"].isObject() ? gy[mapJ.getBool("localGame", false) ? "localGame" : "networkGame"] : gy;
        d.graveyard.limit = int(mode.getInt("graveyardLimit", d.graveyard.limit));
        d.graveyard.await = mode.getFx("awaitDuration", d.graveyard.await);
        d.graveyardType = typeRef(gy["type"], "graveyard.type");
    }
    const Json& so = s["souls"];
    if (so.isObject() || d.graveyardType != 0xFFFF) {
        d.souls.enabled = so.getBool("enabled", true);
        d.souls.velocity = D(so.getFx("flyVelocity", d.souls.velocity * d.scale));
        d.souls.jump = D(so.getFx("jumpHeight", d.souls.jump * d.scale));
        d.souls.attackerDivider = std::max<int>(1, int(so.getInt("attackerSoulDivider", 2)));
        d.souls.defenderDivider = std::max<int>(1, int(so.getInt("defenderSoulDivider", 2)));
        d.souls.look = typeRef(so["look"], "souls.look");
        if (d.souls.velocity.raw <= 0) fail("souls", "flyVelocity must be > 0");
    }
    const Json& fi = s["pointFire"];
    if (fi.isObject() || fi.boolean(false)) { d.fire.enabled = fi.getBool("enabled", true); d.fire.duration = fi.getFx("fireDuration", d.fire.duration); }
    for (auto& cg : s["caves"].items()) {
        std::vector<int> g = pointRefs(mapJ, cg, "caves");
        if (g.size() < 2) fail("caves", "a cave group links 2+ map points");
        d.caves.push_back(std::move(g));
    }
    d.noSendTargets = pointRefs(mapJ, s["noSendTargets"], "noSendTargets");
    for (auto& wl : s["walls"].items()) {
        EnvWallDef w;
        w.point = pointRef(mapJ, wl["point"], "walls");
        w.hits = int(wl.getInt("hp", w.hits));
        w.destroyTime = wl.getFx("destroyDuration");
        w.ignoreTowers = wl.getBool("ignoreTowersDamage", false);
        for (auto& c : wl["destructIfCatched"].items()) w.destructIf.push_back(pointRefs(mapJ, c, "walls.destructIfCatched"));
        d.walls.push_back(std::move(w));
    }
}

} // namespace aiverse::game
