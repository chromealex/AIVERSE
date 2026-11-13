// Battle core (agent-bw-modes): Bunker Wars / Mushroom Wars 2 economy of a garrison battle as generic data
// (rules "battleCore", type "moralePoints", map "difficulty" + entity "values"; docs/game-format.md § Battle core).
// Ported 1:1 in fixed point:
//   Player.AddMoralePoints / SubtractMoralePoints  -> coreMorale (gain / loss multipliers, cap, cooldown reset)
//   BaseModule.Catch (moraleCatchNeutral / Enemy / Lost), IncreaseLevel (moraleUpgradePoints),
//   BaseModule.AttackResult (unitsToPointsCoefficient per attacker lost), TowerModule (tower kills)
//   Player.OnUnitLost -> energy perUnit x (levels - morale index - 1) per own unit lost, every reason but defending
//   Player.GetIncreaseRate / GetUnitsMaxCount -> coreGrowthRates (x1 / 0.75 / 0.5 / 0 by army share of the cap)
//   HouseModule.PrepareUnitsProducedUnitsCount -> neutral garrisons grow up to maxValueNeutral
//   MapPointsModule.GetCurrentValues -> entity "values" per difficulty (start garrison, neutral / mode maximum)
// Morale levels, their stat multipliers and decay are resource "levels" (RtsModes.cpp).
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;

namespace {
int16_t levelsOf(const ModesRules& m, int16_t res) {
    for (size_t i = 0; i < m.levels.size(); ++i) if (m.levels[i].res == res) return int16_t(i);
    return -1;
}
} // namespace

void RtsModule::coreSetup(Simulation& sim, const Json& map) {
    const CoreRules& K = rules_.core;
    World& w = sim.world();
    ModesState& ms = state(sim).modes;
    // point values per difficulty (BW MapPointsModule: easy / normal / hard / expert)
    const std::string diff = map.getStr("difficulty");
    for (auto& ej : map["entities"].items()) {
        const Json& vals = ej["values"];
        if (!vals.isObject() || vals.size() == 0) continue;
        const Json* v = &vals.members().front().second;
        if (!diff.empty()) {
            if (!vals.has(diff)) throw std::runtime_error("map entity values: no difficulty '" + diff + "'");
            v = &vals[diff];
        }
        const Vec3 at = mapPos(sim, ej.has("at") ? ej["at"] : ej["pos"]);
        Entity e;
        w.each<Unit, Transform>([&](Entity c, Unit& u, Transform& tr) {
            if (!e.valid() && rules_.types[u.type].structure && dist2D(tr.pos, at) < Fixed::fromRatio(1, 2)) e = c;   // the type may have changed (mode points)
        });
        if (!e.valid()) continue;
        uint8_t kind = 0;
        for (auto& mp : ms.points) if (mp.e == e) kind = mp.kind;
        const char* pre = kind == 1 ? "domination" : kind == 2 ? "kingOfTheHill" : "";
        const std::string gk = kind ? std::string(pre) + "Garrison" : "garrison", mk = kind ? std::string(pre) + "Max" : "neutralMax";
        if (auto* g = w.tryGet<Garrison>(e); g && v->has(gk)) g->count = int(v->getInt(gk, g->count));
        if (v->has(mk)) {
            if (ms.neutralMax.size() <= e.index) ms.neutralMax.resize(size_t(e.index) + 1, -1);
            ms.neutralMax[e.index] = int32_t(v->getInt(mk, -1));
        }
    }
    (void)K;
}

// Player.AddMoralePoints (gain) / SubtractMoralePoints (loss without the cooldown check)
void RtsModule::coreMorale(Simulation& sim, uint8_t p, Fixed points, bool gain, bool resetCooldown) {
    const CoreRules& K = rules_.core;
    GameState& gs = state(sim);
    if (K.moraleRes < 0 || p == 0 || p >= gs.players.size() || !sim.player(p)) return;
    const ResourceDef& rd = rules_.resources[size_t(K.moraleRes)];
    const Fixed mx = K.moraleMax.raw > 0 ? K.moraleMax : rd.storage;
    const bool hasMax = K.moraleMax.raw > 0 || rd.capped;
    Fixed& v = gs.players[p].stock[size_t(K.moraleRes)];
    if (gain) {
        if (hasMax && v >= mx) return;
        v = v + points * K.gainMult * (p < modes_.playerGain.size() ? modes_.playerGain[p] : Fixed::fromInt(1));
    } else {
        if (v <= rd.min) return;
        v = v - points * K.lossMult;
    }
    v = fxMax(rd.min, v);
    if (hasMax) v = fxMin(mx, v);
    ModesState& ms = gs.modes;
    const int16_t l = levelsOf(modes_, K.moraleRes);
    if (l >= 0 && size_t(l) < ms.lastStock.size() && p < ms.lastStock[size_t(l)].size()) {
        ms.lastStock[size_t(l)][p] = v;   // not a "gain" for the decay detector: the cooldown follows resetCooldown
        if (gain && resetCooldown) ms.lastGain[size_t(l)][p] = sim.tick();
    }
}

// Player.OnUnitLost: energy for own units lost (not for defenders killed in the own garrison)
void RtsModule::coreUnitsLost(Simulation& sim, uint8_t p, int count) {
    const CoreRules& K = rules_.core;
    if (K.energyRes < 0 || count <= 0 || p == 0) return;
    const int16_t l = levelsOf(modes_, K.moraleRes);
    const int levels = K.energyLevels > 0 ? K.energyLevels : (l >= 0 ? int(modes_.levels[size_t(l)].levels.size()) : 1);
    const int idx = std::max(0, l >= 0 ? modeLevel(sim, p, l) : 0);
    const Fixed e = K.energyPerUnit * int64_t(std::max(0, levels - idx - 1)) * int64_t(count);
    if (e.raw > 0) addResource(sim, p, uint16_t(K.energyRes), e);
}

// one assaulting unit lost against a defended garrison (BaseModule.AttackResult, per unit)
void RtsModule::coreAssault(Simulation& sim, uint8_t attacker, uint8_t defender) {
    const CoreRules& K = rules_.core;
    if (K.moraleRes >= 0) {
        coreMorale(sim, defender, K.unitPoints, true, true);
        coreMorale(sim, attacker, K.unitPoints, false, false);
    }
    coreUnitsLost(sim, attacker, 1);
}

void RtsModule::coreCaptured(Simulation& sim, uint16_t type, uint8_t from, uint8_t to) {
    const CoreRules& K = rules_.core;
    if (K.moraleRes < 0 || type >= K.lost.size()) return;
    if (from != 0 && from != kHostileNeutral) coreMorale(sim, from, K.lost[type], false, false);
    coreMorale(sim, to, (from == 0 || from == kHostileNeutral) ? K.catchNeutral[type] : K.catchEnemy[type], true, true);
}

void RtsModule::coreUpgraded(Simulation& sim, uint8_t owner, uint16_t type) {
    const CoreRules& K = rules_.core;
    if (K.moraleRes < 0 || type >= K.upgrade.size()) return;
    coreMorale(sim, owner, K.upgrade[type], true, true);
}

// deaths of the tick: energy for own units lost (towers, clashes, skills), morale for tower kills
void RtsModule::sysCore(Simulation& sim) {
    World& w = sim.world();
    const CoreRules& K = rules_.core;
    for (const SimEvent& ev : sim.events()) {
        if (ev.type != kEvDied || ev.value < 0 || size_t(ev.value) >= rules_.types.size() || rules_.types[size_t(ev.value)].structure) continue;
        if (ev.player == 0 || ev.player == kHostileNeutral) continue;
        coreUnitsLost(sim, ev.player, 1);
        if (K.moraleRes < 0 || !w.alive(ev.b) || !w.has<Unit>(ev.b) || !w.has<Owner>(ev.b)) continue;
        const UnitType& kt = rules_.types[w.get<Unit>(ev.b).type];
        const uint8_t killer = w.get<Owner>(ev.b).player;
        if (!kt.structure || kt.weapons.empty() || killer == ev.player) continue;
        coreMorale(sim, killer, K.towerPoints, true, false);
        coreMorale(sim, ev.player, K.towerPoints, false, false);
    }
}

// Player.GetIncreaseRate per player slot (empty: no production limit)
std::vector<Fixed> RtsModule::coreGrowthRates(Simulation& sim) const {
    const CoreRules& K = rules_.core;
    if (!K.production) return {};
    World& w = sim.world();
    auto* gs = w.resource<GameState>();
    const size_t np = gs->players.size();
    std::vector<int64_t> count(np, 0), cap(np, 0);
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (o.player == 0 || o.player >= np || u.hp.raw <= 0) return;
        const UnitType& t = rules_.types[u.type];
        if (auto* g = w.tryGet<Garrison>(e)) {
            count[o.player] += g->count + g->pendingOut;
            if (!screen_.enabled || onScreen(sim, w.get<Transform>(e).pos))
                cap[o.player] += rules_.advancedEffects ? stat(sim, e, Stat::GarrisonCap).floorInt() : t.garrisonMax;
        } else if (!t.structure && !(u.flags & (kUnitSilent | kUnitOffMap))) ++count[o.player];
    });
    std::vector<Fixed> rate(np, Fixed::fromInt(1));
    for (size_t p = 1; p < np; ++p) {
        const Fixed c = K.limitMult * cap[p];
        if (c.raw <= 0) { rate[p] = count[p] > 0 ? Fixed{} : Fixed::fromInt(1); continue; }   // BW 0 / 0 is NaN: full rate
        const Fixed share = Fixed::fromInt(count[p]) / c;
        for (auto& [at, r] : K.tiers) if (share >= at) rate[p] = r;
    }
    return rate;
}

int RtsModule::coreNeutralMax(Simulation& sim, Entity e, const UnitType& t) const {
    auto* gs = sim.world().resource<GameState>();
    const auto& nm = gs->modes.neutralMax;
    if (e.index < nm.size() && nm[e.index] >= 0) return nm[e.index];
    return t.garrisonMax;
}

} // namespace aiverse::game
