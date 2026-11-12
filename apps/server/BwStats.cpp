#include "BwStats.h"
#include "GameData.h"
#include "modules/rts/RtsModule.h"

#include <algorithm>

namespace aiverse::netgame {

using namespace aiverse::game;

namespace {
// PlayerStatistic.Key
enum Key : size_t {
    UnitsDeadByAttack = 1, UnitsDeadByDefense, UnitsDeadByTower, UnitsDeadBySkills, UnitsKilledByPlayerAttack, UnitsKilledByPlayerDefense,
    UnitsKilledByPlayerTower, UnitsKilledByPlayerSkills, UnitsProduceCount, PointsConverted, PointsUpgraded, PointsConvertedToHouse,
    PointsConvertedToTower, PointsConvertedToSmithy, PointsConvertedToSpecial1, PointsCatched, PointsLost, AttackCount, AttackSuccessCount,
    AttackFailCount, DefenseCount, DefenseSuccessCount, DefenseFailCount, UnitsKilledByBoss, KingOfTheHillPoints, SkillSlot1,
    SkillGreen = 31, MoraleAddStar = 35, MoraleLostStar, MoraleAddStarWithSkill, MoraleLostStarWithSkill, MaxUnitsOnTheField,
    PointsCount, MaxMoraleStars, LastPointsCountWhenEnemyPointCatched, MaxAttackLevel, CampaignStars, MultiplayerWins,
    PointsCatchedWhenYouHomeless, PointsCatchedBySaboutage, YouHaveOnlyOnePointLeft, Emotion1, MaxMoraleStarsAndMaxForges = 53,
    SkillSlot1AffectedUnits, DeadByUnit = 59, KilledByUnit, NeutralUnitsKilled, PointsCatchedTower, PointsCatchedSmithy,
    PointsCatchedHouse, PointsCatchedNeutral, EnergySpent, EnergyReceive, PointsUpgradedHouse, PointsUpgradedTower, PointsCatchedAll,
};
enum class Role { Other, House, Tower, Smithy };
Role roleOf(const Rules& R, uint16_t type) {   // as bwRoleOf without tags: shoots / player mods / breeds
    if (type >= R.types.size()) return Role::Other;
    const UnitType& t = R.types[type];
    if (!t.weapons.empty()) return Role::Tower;
    if (!t.playerMods.empty()) return Role::Smithy;
    if (t.spawnInterval.raw > 0 || t.garrisonMax > 0) return Role::House;
    return Role::Other;
}
RtsModule* rts(Simulation& sim) { return dynamic_cast<RtsModule*>(&sim.module()); }
constexpr uint64_t kAttackGapTicks = 40;   // arrivals more than 2 s (20 Hz) apart are a new attack
} // namespace

void BwStatsCollector::begin(Simulation& sim) {
    const size_t np = sim.players().size() + 1;
    v_.assign(np, BwStatValues{});
    RtsModule* m = rts(sim);
    energyRes_ = -1;
    if (m) for (size_t i = 0; i < m->rules().resources.size(); ++i) if (m->rules().resources[i].id == "energy") energyRes_ = int(i);
    energy_.assign(np, 0);
    if (m && energyRes_ >= 0) {
        auto& gs = m->state(sim);
        for (size_t p = 0; p < np && p < gs.players.size(); ++p) energy_[p] = gs.players[p].stock[size_t(energyRes_)].toDouble();
    }
    tick_ = 0;
    snapshot(sim);
}

void BwStatsCollector::snapshot(Simulation& sim) {
    World& w = sim.world();
    movers_.clear();
    garrisons_.clear();
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        RtsModule* m = rts(sim);
        if (!m || u.type >= m->rules().types.size()) return;
        if (m->rules().types[u.type].structure) {
            types_[e.packed()] = u.type;
            if (auto* g = w.tryGet<Garrison>(e)) garrisons_[e.packed()] = {o.player, g->count};
            return;
        }
        uint64_t target = 0;
        if (auto* q = w.tryGet<Orders>(e); q && !q->queue.empty() && q->queue.front().target != kNullEntity)
            target = q->queue.front().target.packed();
        movers_[e.packed()] = {o.player, target};
    });
}

void BwStatsCollector::afterStep(Simulation& sim) {
    RtsModule* m = rts(sim);
    if (!m || v_.empty()) return;
    ++tick_;
    World& w = sim.world();
    const Rules& R = m->rules();
    const size_t np = v_.size();
    auto ok = [&](uint8_t p) { return p > 0 && p < np; };
    std::map<uint64_t, bool> diedLoudly;
    for (const SimEvent& ev : sim.events()) {
        switch (ev.type) {
        case kEvDied: {
            if (ev.value < 0 || size_t(ev.value) >= R.types.size() || R.types[size_t(ev.value)].structure) break;
            diedLoudly[ev.a.packed()] = true;
            uint8_t killer = 0;
            int16_t killerType = -1;
            if (w.alive(ev.b)) {
                if (auto* o = w.tryGet<Owner>(ev.b)) killer = o->player;
                if (auto* u = w.tryGet<Unit>(ev.b)) killerType = int16_t(u->type);
            }
            const bool tower = killerType >= 0 && R.types[size_t(killerType)].structure;
            const bool unit = killerType >= 0 && !tower;
            if (ok(ev.player)) v_[ev.player][tower ? UnitsDeadByTower : unit ? DeadByUnit : UnitsDeadBySkills] += 1;
            if (ok(killer) && killer != ev.player) {
                v_[killer][tower ? UnitsKilledByPlayerTower : unit ? KilledByUnit : UnitsKilledByPlayerSkills] += 1;
                if (ev.player == 0) v_[killer][NeutralUnitsKilled] += 1;
            }
            break;
        }
        case kEvTrainDone:
        case kEvSpawned:
            if (ok(ev.player) && ev.value >= 0 && size_t(ev.value) < R.types.size() && !R.types[size_t(ev.value)].structure)
                v_[ev.player][UnitsProduceCount] += 1;
            break;
        case kEvCaptured: {
            const uint8_t to = ev.player;
            const int64_t from = ev.value;
            if (!ok(to)) break;
            uint16_t type = 0;
            if (auto* u = w.tryGet<Unit>(ev.a)) type = u->type;
            int ownedBefore = 0;
            for (auto& [e, g] : garrisons_) ownedBefore += g.first == to;
            v_[to][PointsCatchedAll] += 1;
            v_[to][from == 0 ? PointsCatchedNeutral : PointsCatched] += 1;
            if (from != 0) v_[to][LastPointsCountWhenEnemyPointCatched] = ownedBefore + 1;
            if (ownedBefore == 0) v_[to][PointsCatchedWhenYouHomeless] += 1;
            switch (roleOf(R, type)) {
            case Role::House: v_[to][PointsCatchedHouse] += 1; break;
            case Role::Tower: v_[to][PointsCatchedTower] += 1; break;
            case Role::Smithy: v_[to][PointsCatchedSmithy] += 1; break;
            default: break;
            }
            if (from > 0 && size_t(from) < np) v_[size_t(from)][PointsLost] += 1;
            auto at = attacks_.find({ev.a.packed(), to});
            if (at != attacks_.end()) at->second.captured = true;
            break;
        }
        case kEvUpgraded: {
            if (!ok(ev.player) || ev.value < 0) break;
            const uint16_t now = uint16_t(ev.value);
            auto it = types_.find(ev.a.packed());
            const Role before = it != types_.end() ? roleOf(R, it->second) : roleOf(R, now), after = roleOf(R, now);
            if (before != after) {
                v_[ev.player][PointsConverted] += 1;
                v_[ev.player][after == Role::House ? PointsConvertedToHouse : after == Role::Tower ? PointsConvertedToTower
                              : after == Role::Smithy ? PointsConvertedToSmithy : PointsConvertedToSpecial1] += 1;
            } else {
                v_[ev.player][PointsUpgraded] += 1;
                if (after == Role::House) v_[ev.player][PointsUpgradedHouse] += 1;
                if (after == Role::Tower) v_[ev.player][PointsUpgradedTower] += 1;
            }
            break;
        }
        case kEvPlayerAbility:
            if (ok(ev.player) && ev.value >= 0 && ev.value < 5) v_[ev.player][SkillSlot1 + size_t(ev.value)] += 1;
            break;
        default: break;
        }
    }
    // Units gone without a death event: they walked into a garrison (assault / reinforcement).
    for (auto& [packed, mv] : movers_) {
        const Entity e = Entity::fromPacked(packed);
        if (w.alive(e) || diedLoudly.count(packed) || !mv.second) continue;
        auto g = garrisons_.find(mv.second);
        if (g == garrisons_.end()) continue;
        const uint8_t attacker = mv.first, defender = g->second.first;
        if (attacker == defender || !ok(attacker)) continue;
        v_[attacker][UnitsDeadByDefense] += 1;
        if (ok(defender)) v_[defender][UnitsKilledByPlayerDefense] += 1;
        Arrival& a = attacks_[{mv.second, attacker}];
        if (!a.lastTick || tick_ - a.lastTick > kAttackGapTicks) {   // a new attack on this point
            if (a.lastTick) {
                v_[attacker][a.captured ? AttackSuccessCount : AttackFailCount] += 1;
                if (ok(a.defender)) v_[a.defender][a.captured ? DefenseFailCount : DefenseSuccessCount] += 1;
            }
            a = Arrival{attacker, ok(defender) ? defender : uint8_t(0), 0, false};
            v_[attacker][AttackCount] += 1;
            if (ok(defender)) v_[defender][DefenseCount] += 1;
        }
        a.lastTick = tick_;
    }
    // Garrison units killed by assaults: count drops of a point that stayed with its owner, beyond what it sent.
    std::map<uint64_t, int> attackersAt;
    for (auto& [packed, mv] : movers_)
        if (!w.alive(Entity::fromPacked(packed)) && !diedLoudly.count(packed) && mv.second) {
            auto g = garrisons_.find(mv.second);
            if (g != garrisons_.end() && g->second.first != mv.first) attackersAt[mv.second] = mv.first;
        }
    for (auto& [target, attacker] : attackersAt) {
        const Entity e = Entity::fromPacked(target);
        auto* g = w.alive(e) ? w.tryGet<Garrison>(e) : nullptr;
        auto* o = w.alive(e) ? w.tryGet<Owner>(e) : nullptr;
        const auto& before = garrisons_[target];
        const int lost = g && o && o->player == before.first ? std::max(0, before.second - g->count) : before.second;
        if (ok(uint8_t(attacker))) v_[size_t(attacker)][UnitsKilledByPlayerAttack] += lost;
        if (ok(before.first)) v_[before.first][UnitsDeadByAttack] += lost;
    }
    // Energy received / spent, army size, points.
    auto& gs = m->state(sim);
    std::vector<double> units(np, 0), points(np, 0);
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (!ok(o.player) || u.hp.raw <= 0 || u.type >= R.types.size()) return;
        if (R.types[u.type].structure) {
            if (auto* g = w.tryGet<Garrison>(e)) { units[o.player] += g->count; points[o.player] += 1; }
            if (roleOf(R, u.type) == Role::Smithy) v_[o.player][MaxAttackLevel] = std::max(v_[o.player][MaxAttackLevel], double(R.types[u.type].level));
        } else if (!(u.flags & kUnitOffMap)) {
            units[o.player] += 1;
        }
    });
    for (size_t p = 1; p < np; ++p) {
        v_[p][MaxUnitsOnTheField] = std::max(v_[p][MaxUnitsOnTheField], units[p]);
        if (points[p] == 1 && v_[p][PointsCount] > 1) v_[p][YouHaveOnlyOnePointLeft] = 1;
        v_[p][PointsCount] = points[p];
        if (energyRes_ >= 0 && p < gs.players.size()) {
            const double e = gs.players[p].stock[size_t(energyRes_)].toDouble();
            if (e > energy_[p]) v_[p][EnergyReceive] += e - energy_[p];
            else v_[p][EnergySpent] += energy_[p] - e;
            energy_[p] = e;
        }
    }
    snapshot(sim);
}

void BwStatsCollector::finish(Simulation& sim) {
    RtsModule* m = rts(sim);
    if (!m || v_.empty()) return;
    for (auto& [key, a] : attacks_) {   // close the attacks still open
        if (!a.lastTick || !a.attacker) continue;
        v_[a.attacker][a.captured ? AttackSuccessCount : AttackFailCount] += 1;
        if (a.defender > 0 && a.defender < v_.size()) v_[a.defender][a.captured ? DefenseFailCount : DefenseSuccessCount] += 1;
    }
    attacks_.clear();
    const int32_t result = sim.result();
    for (size_t p = 1; p < v_.size(); ++p) {
        // Match modes counters when the rules turn them on (morale stars, king of the hill score).
        v_[p][MoraleAddStar] = double(m->matchStat(sim, uint8_t(p), kMsLevelUps));
        v_[p][MoraleLostStar] = double(m->matchStat(sim, uint8_t(p), kMsLevelDowns));
        v_[p][MaxMoraleStars] = double(m->matchStat(sim, uint8_t(p), kMsMaxLevel));
        v_[p][KingOfTheHillPoints] = Fixed::fromRaw(m->matchStat(sim, uint8_t(p), kMsModeScore)).toDouble();
        const auto& pl = sim.players();
        uint8_t team = 0;
        for (auto& i : pl) if (i.slot == p) team = i.team;
        v_[p][MultiplayerWins] = result > 0 && team == uint8_t(result) ? 1 : 0;
    }
}

BwStatsResult bwStatsReplay(const BwStatsJob& job) {
    BwStatsResult res;
    try {
        MatchData data = loadMatchData(job.gameDir, job.mapFile);
        SimConfig cfg;
        cfg.seed = job.seed; cfg.tickRate = job.tickRate; cfg.inputDelayTicks = job.inputDelay;
        auto sim = makeSimulation(data.module, cfg);
        if (!sim || !dynamic_cast<RtsModule*>(&sim->module())) { res.error = "not an RTS game: " + data.module; return res; }
        sim->setup(data.rules, data.map);
        for (auto& c : job.commands) sim->inject(c);
        BwStatsCollector col;
        col.begin(*sim);
        while (sim->tick() < job.endTick) {
            sim->step();
            col.afterStep(*sim);
            if (sim->result()) break;
        }
        col.finish(*sim);
        res.values = col.values();
        res.result = sim->result();
        res.tick = sim->tick();
        res.hash = sim->checksum();
        res.ok = true;
    } catch (const std::exception& e) {
        res.error = e.what();
    }
    return res;
}

std::function<std::vector<std::vector<double>>(const net::ExternalMatchEnd&, std::string& error)>
bwBattleStats(std::map<std::string, std::string> gameDirs) {
    return [dirs = std::move(gameDirs)](const net::ExternalMatchEnd& e, std::string& error) {
        std::vector<std::vector<double>> out;
        auto it = dirs.find(e.gameId);
        if (it == dirs.end()) { error = "no game data for " + e.gameId; return out; }
        BwStatsJob job;
        job.gameDir = it->second; job.mapFile = e.mapFile; job.seed = e.seed;
        job.tickRate = e.tickRate; job.inputDelay = e.inputDelay; job.endTick = e.endTick; job.commands = e.commands;
        BwStatsResult r = bwStatsReplay(job);
        if (!r.ok) { error = r.error; return out; }
        if (e.endHash && r.hash != e.endHash) { error = "re-simulation differs from the players' result"; return out; }
        for (auto& v : r.values) out.emplace_back(v.begin(), v.end());
        return out;
    };
}

} // namespace aiverse::netgame
