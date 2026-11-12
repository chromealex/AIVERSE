// Match modes (agent-bw-modes): Bunker Wars / Mushroom Wars 2 victory modes and match clocks as generic data
// (RtsModes.h, docs/game-format.md § Match modes). Ported 1:1 in fixed point:
//   ModsModule/Impl/Domination.cs   -> modes.domination   (hold timer per point, holdAny = FFA, holdAll = COOP)
//   ModsModule/Impl/KingOfTheHill.cs + PointModules/BaseModule.UpdateKingOfTheHillScore -> modes.kingOfTheHill
//   ModsModule/Impl/TimeMod.cs      -> modes.timeLimit    (speed steps, MaxPoints / PointsComparison)
//   SuddenDeathModule.cs            -> modes.suddenDeath  (counter, preview speed, factor status, end by points)
//   BattleRoyal/BattleRoyalModule.cs -> modes.zones       (per-player rect chain, points outside turn neutral)
//   CustomRulesInfo.TimeSlots       -> timedRules         (player statuses switched by time slots)
//   MoraleInfo levels + Player.UpdateMorale -> resource "levels" (level mods, decay after a quiet period)
//   PlayerStatistic counters        -> matchStats
// Clocks: BW scales the delta time of gameplay modules (TimeMod speed x sudden death speedFactor) while zones and
// sudden death count unscaled ticks. Here the simulation tick stays fixed: speedScale() makes the client run that
// many ticks per real tick, gameplay timers use sim time, zones / sudden death / time slots use realTime
// (sum of dt / speed). Outcomes are deterministic: every value is Fixed and part of hashState.
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;
namespace detail { void changeUnitType(RtsModule& m, Simulation& sim, Entity e, uint16_t type); }   // RtsEffects.cpp

namespace {

bool inRect(const ModeRect& r, Vec3 p) { return p.x >= r.x && p.x < r.x + r.w && p.z >= r.z && p.z < r.z + r.h; }
ModeRect lerpRect(const ModeRect& a, const ModeRect& b, Fixed t) {
    return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
}
bool isWavePlayer(const GameState& gs, uint8_t slot) {
    return std::find(gs.wavePlayers.begin(), gs.wavePlayers.end(), slot) != gs.wavePlayers.end();
}
bool neutralOwner(uint8_t p) { return p == 0 || p == kHostileNeutral; }

// Battle-royal rect of a zone chain at `t` seconds (BattleRoyalModule.UpdateRectByTick, ticks -> seconds).
void zoneAt(const ModesRules& m, ZoneState& z, Fixed t) {
    if (z.start < 0) return;
    const ZoneStep* el = &m.zoneStart[size_t(z.start)];
    int cur = -1;
    ModeRect rect = el->rect;
    Fixed d = t;
    while (d.raw > 0) {
        d -= el->stay;
        if (d.raw <= 0) break;                                    // stays on the current rect
        if (d - el->move < Fixed{}) {                             // fading to the next rect
            const ZoneStep* nx = el->next < 0 ? el : &m.zoneSteps[size_t(el->next)];
            rect = lerpRect(el->rect, nx->rect, d / el->move);
            break;
        }
        if (el->next == -1) break;                                // the last rect: stays
        d -= el->move;
        cur = el->next;
        el = &m.zoneSteps[size_t(cur)];
        rect = el->rect;
    }
    z.cur = cur;
    z.rect = rect;
}
} // namespace

// ================================================================= setup

void RtsModule::modesSetup(Simulation& sim, const Json& rulesJson, const Json& map) {
    std::string preset;
    const Json eff = effectiveModes(rulesJson, map, &preset);
    modes_ = parseModes(rules_, eff);
    modes_.any = modes_.any || rules_.modes.any;
    GameState& gs = state(sim);
    if (!preset.empty() && !map.has("victory"))
        if (const Json& v = rulesJson["matchModes"][preset]["victory"]; !v.isNull()) gs.victory = loadVictory(v, gs.victory);
    if (!modes_.any) return;
    World& w = sim.world();
    ModesState& ms = gs.modes;
    const size_t np = gs.players.size();
    ms.timeLeft = modes_.timeSeconds;
    // per player morale multipliers (BW moraleIncreaseCoefficient, AI complexity increase / decrease coefficients)
    modes_.playerGain.assign(np, Fixed::fromInt(1));
    modes_.playerDecay.assign(np, Fixed::fromInt(1));
    for (size_t i = 0; i < map["players"].size() && i + 1 < np; ++i) {
        const Json& pj = map["players"][i];
        modes_.playerGain[i + 1] = pj.getFx("moraleGain", Fixed::fromInt(1));
        modes_.playerDecay[i + 1] = pj.getFx("moraleDecay", Fixed::fromInt(1));
    }

    // ---- mode points: map entities / types with "modePoint" (BW PointInfo.isDominationPoint / isKingOfTheHillPoint)
    auto kindOf = [](const std::string& s) -> uint8_t { return s == "domination" ? 1 : s == "kingOfTheHill" ? 2 : 0; };
    std::vector<uint8_t> typeKind(rules_.types.size(), 0);
    for (auto& [id, tj] : rulesJson["types"].members())
        if (tj.has("modePoint")) {
            int t = rules_.findType(id);
            uint8_t k = kindOf(tj.getStr("modePoint"));
            if (!k) throw std::runtime_error("type '" + id + "': modePoint must be domination or kingOfTheHill");
            if (t >= 0) typeKind[size_t(t)] = k;
        }
    struct Marked { uint16_t type; Vec3 pos; uint8_t kind; };
    std::vector<Marked> marked;
    for (auto& ej : map["entities"].items()) {
        if (!ej.has("modePoint")) continue;
        uint8_t k = kindOf(ej.getStr("modePoint"));
        if (!k) throw std::runtime_error("map entity: modePoint must be domination or kingOfTheHill");
        int t = rules_.findType(ej.getStr("type"));
        if (t >= 0) marked.push_back({uint16_t(t), mapPos(sim, ej.has("at") ? ej["at"] : ej["pos"]), k});
    }
    std::vector<std::pair<Entity, uint8_t>> found;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& tr) {
        if (!rules_.types[u.type].structure) return;
        uint8_t k = typeKind[u.type];
        for (auto& mk : marked) if (mk.type == u.type && dist2D(mk.pos, tr.pos) < Fixed::fromRatio(1, 2)) k = mk.kind;
        if ((k == 1 && modes_.domination) || (k == 2 && modes_.koh)) found.push_back({e, k});
    });
    for (auto& [e, k] : found) {
        const uint16_t to = k == 1 ? modes_.domType : modes_.kohType;
        if (to != 0xFFFF) changeUnitType(*this, sim, e, to);
        const int garrison = k == 1 ? modes_.domGarrison : modes_.kohGarrison;
        if (auto* g = w.tryGet<Garrison>(e); g && garrison >= 0) g->count = garrison;
        ModePoint p; p.e = e; p.kind = k; p.lastOwner = w.get<Owner>(e).player;
        if (!neutralOwner(p.lastOwner)) p.hold = modes_.domHold;   // BW: the first owner set on load starts the timer
        ms.points.push_back(p);
    }

    // ---- king of the hill: one score per team (COOP shares it; FFA teams have one player)
    int maxTeam = 0;
    std::vector<int> teamSize(256, 0);
    for (auto& p : sim.players()) { maxTeam = std::max(maxTeam, int(p.team)); ++teamSize[p.team]; }
    bool coop = false;
    for (int t = 0; t <= maxTeam; ++t) coop = coop || teamSize[size_t(t)] > 1;
    ms.kohScore.assign(size_t(maxTeam) + 1, coop ? modes_.kohTeamScore : modes_.kohScore);
    ms.kohAlarmed.assign(size_t(maxTeam) + 1, 0);

    // ---- zones: each player's chain starts at the start rect holding its first structure
    ms.zone.assign(np, ZoneState{});
    if (modes_.zones)
        for (size_t p = 1; p < np; ++p) {
            bool done = false;
            w.each<Unit, Owner, Transform>([&](Entity, Unit& u, Owner& o, Transform& tr) {
                if (done || o.player != p || !rules_.types[u.type].structure) return;
                done = true;
                for (size_t i = 0; i < modes_.zoneStart.size(); ++i)
                    if (inRect(modes_.zoneStart[i].rect, tr.pos)) {
                        ZoneState& z = ms.zone[p];
                        z.on = true; z.start = int(i); z.rect = modes_.zoneStart[i].rect;
                        break;
                    }
            });
        }

    // ---- custom rule sets chosen for this match (BW CustomRules / HeroCustomRules from the backend -> map "ruleSets")
    for (auto& id : map["ruleSets"].items()) {
        const Json& rs = rulesJson["ruleSets"][id.str()];
        if (!rs.isArray()) throw std::runtime_error("map ruleSets: unknown rule set '" + id.str() + "'");
        for (auto& r : parseTimedRules(rules_, "ruleSets." + id.str(), rs)) modes_.timedRules.push_back(std::move(r));
    }
    // ---- sudden death custom configs: one rule set for everyone, slots shifted on zone maps
    if (!modes_.sdRuleSets.empty()) {
        ms.sdRuleSet = int(sim.rng().below(uint32_t(modes_.sdRuleSets.size())));
        const Fixed shift = modes_.zones ? modes_.zoneSdDelay : Fixed{};
        for (TimedRule r : modes_.sdRuleSets[size_t(ms.sdRuleSet)]) {
            for (auto& s : r.slots) if (s.checkTime) { s.from += shift; s.to += shift; }
            modes_.timedRules.push_back(std::move(r));
        }
    }
    ms.ruleOn.assign(modes_.timedRules.size() * np, 0);

    // ---- resource levels, statistics, outcomes
    const size_t nl = modes_.levels.size();
    ms.level.assign(nl, std::vector<int16_t>(np, -1));
    ms.decayTimer.assign(nl, std::vector<Fixed>(np));
    ms.lastStock.assign(nl, std::vector<Fixed>(np));
    ms.lastGain.assign(nl, std::vector<uint32_t>(np, 0));
    for (size_t l = 0; l < nl; ++l)
        for (size_t p = 0; p < np; ++p) {
            ms.lastStock[l][p] = gs.players[p].stock[size_t(modes_.levels[l].res)];
            ms.level[l][p] = int16_t(modeLevel(sim, uint8_t(p), int16_t(l)));
        }
    ms.stats.assign(np, std::vector<int64_t>(kMsCount, 0));
    ms.abilityUses.assign(np, std::vector<int32_t>(rules_.playerAbilities.size(), 0));
    ms.outcome.assign(np, 0);
    if (rules_.core.any) coreSetup(sim, map);   // battle core: point values per difficulty (RtsCore.cpp)
}

// ================================================================= queries

double RtsModule::speedScale(Simulation& sim) {
    if (!modes_.any) return 1.0;
    auto* gs = sim.world().resource<GameState>();
    return gs ? gs->modes.speed.toDouble() : 1.0;
}

bool RtsModule::modeZone(Simulation& sim, uint8_t player, ModeRect& out) const {
    auto* gs = sim.world().resource<GameState>();
    if (!gs || !modes_.zones || player >= gs->modes.zone.size() || !gs->modes.zone[player].on) return false;
    out = gs->modes.zone[player].rect;
    return true;
}

int64_t RtsModule::matchStat(Simulation& sim, uint8_t player, MatchStat key) const {
    auto* gs = sim.world().resource<GameState>();
    if (!gs || player >= gs->modes.stats.size() || key >= kMsCount) return 0;
    return gs->modes.stats[player][key];
}

// level index of `player` in levels entry `l` (MoraleInfo.GetLevelFromPoints): the last level reached, -1 below the first
int RtsModule::modeLevel(Simulation& sim, uint8_t player, int16_t l) const {
    auto* gs = sim.world().resource<GameState>();
    if (!gs || l < 0 || size_t(l) >= modes_.levels.size() || player >= gs->players.size()) return -1;
    const ResourceLevels& rl = modes_.levels[size_t(l)];
    const Fixed v = gs->players[player].stock[size_t(rl.res)];
    int idx = -1;
    for (size_t i = 0; i < rl.levels.size(); ++i) { if (v < rl.levels[i].at) break; idx = int(i); }
    return idx;
}

// stat mods of the player's current levels (MoraleInfo damagePercent / shieldPercent / speedPercent)
Fixed RtsModule::modesLevelStat(Simulation& sim, uint8_t player, uint16_t type, Stat s, Fixed& mul) const {
    Fixed add{};
    for (size_t l = 0; l < modes_.levels.size(); ++l) {
        int lv = modeLevel(sim, player, int16_t(l));
        if (lv < 0) continue;
        const LevelDef& d = modes_.levels[l].levels[size_t(lv)];
        for (size_t k = 0; k < d.modStat.size(); ++k) {
            if (Stat(d.modStat[k]) != s || (d.modTags[k] && !(rules_.types[type].tags & d.modTags[k]))) continue;
            add += d.modAdd[k];
            mul = mul * d.modMul[k];
        }
    }
    return add;
}

// ================================================================= tick

void RtsModule::sysModes(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = state(sim);
    ModesState& ms = gs.modes;
    const ModesRules& M = modes_;
    const Fixed dt = sim.dt();
    const size_t np = gs.players.size();
    ms.realTime += dt / fxMax(ms.speed, Fixed::fromRatio(1, 100));   // one tick lasts 1 / speed real ticks
    const Fixed T = ms.realTime;

    auto teamOfSlot = [&](uint8_t p) { return int(teamOf(sim, p)); };
    auto isPoint = [&](Entity e) {
        const Unit& u = w.get<Unit>(e);
        if (u.hp.raw <= 0) return false;
        return M.garrisonPoints ? w.has<Garrison>(e) : (rules_.types[u.type].structure && rules_.types[u.type].countsForVictory);
    };
    auto inZone = [&](uint8_t owner, Vec3 pos) {   // MultiscreenModule.IsPointInsideScreen with battle royal
        if (!M.zones) return !screen_.enabled || onScreen(sim, pos);   // multiscreen maps (routes agent screen API)
        if (owner >= ms.zone.size() || !ms.zone[owner].on) return true;
        return inRect(ms.zone[owner].rect, pos);
    };
    // sides still playing (non-wave players), ascending team id
    std::vector<int> teams;
    for (auto& p : sim.players())
        if (!isWavePlayer(gs, p.slot) && std::find(teams.begin(), teams.end(), int(p.team)) == teams.end()) teams.push_back(int(p.team));
    std::sort(teams.begin(), teams.end());
    const bool decided0 = gs.winnerTeam >= 0;
    auto defeatTeam = [&](int team) {
        for (auto& p : sim.players())
            if (int(p.team) == team && !isWavePlayer(gs, p.slot)) {
                if (p.slot < ms.outcome.size() && ms.outcome[p.slot] == 0) ms.outcome[p.slot] = 2;
                if (!p.alive) continue;
                p.alive = false;
                sim.emit({kEvPlayerDefeated, p.slot, kNullEntity, kNullEntity, kDefeatMode, {}});
            }
    };
    auto markTeam = [&](int team, int8_t outcome) {
        for (auto& p : sim.players()) if (int(p.team) == team && p.slot < ms.outcome.size()) ms.outcome[p.slot] = outcome;
    };
    auto winTeam = [&](int team) {
        gs.winnerTeam = team;
        markTeam(team, 1);
        for (int t : teams) if (t != team) defeatTeam(t);
    };
    auto drawAll = [&]() {
        gs.winnerTeam = 0;
        for (int t : teams) markTeam(t, 3);
    };
    auto pointsOfTeams = [&]() {   // team -> points owned (players.GetPointsCount), only teams owning any
        std::vector<std::pair<int, int>> n;
        w.each<Unit, Owner>([&](Entity e, Unit&, Owner& o) {
            if (neutralOwner(o.player) || !isPoint(e)) return;
            int t = teamOfSlot(o.player);
            auto it = std::find_if(n.begin(), n.end(), [&](auto& x) { return x.first == t; });
            if (it == n.end()) n.push_back({t, 1}); else ++it->second;
        });
        std::sort(n.begin(), n.end());
        return n;
    };

    // ---- mode points: hold timers (BaseModule.OnOwnerChanged / UpdateDominationTimeout), king of the hill score
    for (auto& mp : ms.points) {
        if (!w.alive(mp.e) || !w.has<Owner>(mp.e)) continue;
        const uint8_t owner = w.get<Owner>(mp.e).player;
        if (owner != mp.lastOwner) { mp.lastOwner = owner; mp.hold = M.domHold; }
        if (neutralOwner(owner)) continue;
        if (mp.kind == 1) { if (mp.hold.raw > 0) mp.hold = mp.hold - dt; }
        else if (mp.kind == 2 && inZone(owner, w.get<Transform>(mp.e).pos)) {
            const size_t t = size_t(teamOfSlot(owner));
            if (t >= ms.kohScore.size()) continue;
            const Fixed old = ms.kohScore[t];
            ms.kohScore[t] = fxMax(Fixed{}, old - dt * M.kohRate);
            if (old > M.kohAlarm && ms.kohScore[t] <= M.kohAlarm && !ms.kohAlarmed[t]) {
                ms.kohAlarmed[t] = 1;
                sim.emit({kEvModePhase, owner, mp.e, kNullEntity, kModeKohAlarm, w.get<Transform>(mp.e).pos});
            }
        }
    }

    // king of the hill scores as trigger variables for scripts and the BW computer player (koth_<slot>, koth_max)
    if (M.koh) {
        for (auto& p : sim.players()) {
            const size_t t = size_t(teamOfSlot(p.slot));
            if (t < ms.kohScore.size()) gs.trig.vars["koth_" + std::to_string(p.slot)] = ms.kohScore[t];
        }
        gs.trig.vars["koth_max"] = ms.kohScore.empty() ? Fixed{} : fxMax(M.kohScore, M.kohTeamScore);
    }

    // ---- zones: the rect chain follows real time; points outside their owner's rect turn neutral
    if (M.zones) {
        for (size_t p = 1; p < np; ++p) {
            ZoneState& z = ms.zone[p];
            if (!z.on) continue;
            const int before = z.cur;
            zoneAt(M, z, T);
            if (z.cur != before) sim.emit({kEvModePhase, uint8_t(p), kNullEntity, kNullEntity, kModeZoneChanged, {}});
        }
        std::vector<Entity> out;
        w.each<Unit, Owner, Transform>([&](Entity e, Unit&, Owner& o, Transform& tr) {
            if (!neutralOwner(o.player) && isPoint(e) && !inZone(o.player, tr.pos)) out.push_back(e);
        });
        for (Entity e : out) {
            Owner& o = w.get<Owner>(e);
            const uint8_t old = o.player;
            const uint16_t ty = w.get<Unit>(e).type;
            if (old < gs.players.size() && !gs.players[old].typeCountAll.empty() && gs.players[old].typeCountAll[ty] > 0) gs.players[old].typeCountAll[ty]--;
            o.player = 0;
            if (auto* pr = w.tryGet<Production>(e)) pr->queue.clear();
            const Vec3 pos = w.get<Transform>(e).pos;
            sim.emit({kEvCaptured, 0, e, kNullEntity, old, pos});
            sim.emit({kEvModePhase, old, e, kNullEntity, kModeZoneLost, pos});
            if (M.zoneNeutralType != 0xFFFF) changeUnitType(*this, sim, e, M.zoneNeutralType);
        }
    }

    // ---- timed rules (CustomRules time slots): player statuses on / off
    if (!M.timedRules.empty()) {
        int best = -1;
        for (size_t r = 0; r < M.timedRules.size(); ++r) {
            const TimedRule& tr = M.timedRules[r];
            if (tr.levelRes >= 0) { best = -1; for (size_t p = 1; p < np; ++p) best = std::max(best, modeLevel(sim, uint8_t(p), tr.levelRes)); }
            for (size_t p = 1; p < np; ++p) {
                const auto* pi = sim.player(uint8_t(p));
                bool valid = pi && pi->alive;
                if (valid && !tr.slots.empty()) {
                    valid = false;
                    for (const TimeSlot& s : tr.slots) {
                        bool ok = true, decided = false;
                        if (s.everyAbilities > 0) {
                            int64_t v = ms.stats.empty() ? 0 : ms.stats[p][kMsAbilities];
                            if (v <= 0 || v % s.everyAbilities != 0) { ok = false; decided = true; }
                        }
                        if (!decided && s.levelBehind && s.behind != best - modeLevel(sim, uint8_t(p), tr.levelRes)) { ok = false; decided = true; }
                        if (!decided && s.checkTime) {
                            if (s.inverse) { if (T < s.from || T > s.to) { ok = false; decided = true; } }
                            else if (T >= s.from && T <= s.to) { ok = true; decided = true; }
                        }
                        if (!decided && s.iterate && T.raw > 0 && Fixed::fromRaw(T.raw % s.every.raw) <= s.duration) { ok = true; decided = true; }
                        if (!decided) ok = !s.checkTime && !s.iterate;
                        if (ok) { valid = true; break; }
                    }
                }
                uint8_t& on = ms.ruleOn[r * np + p];
                if (valid && !on) {
                    on = 1;
                    EffectCtx ctx; ctx.owner = 0;
                    addPlayerStatus(sim, uint8_t(p), tr.status, Fixed{}, ctx);
                } else if (!valid && on) {
                    on = 0;
                    for (auto& in : gs.players[p].statuses)
                        if (in.def == uint16_t(tr.status) && in.owner == 0 && !in.source.valid() && in.until == UINT32_MAX) { in.until = sim.tick(); break; }
                }
            }
        }
    }

    // ---- resource levels: decay after a quiet period (Player.UpdateMorale / SubtractMoralePoints(checkTime))
    for (size_t l = 0; l < M.levels.size(); ++l) {
        const ResourceLevels& rl = M.levels[l];
        const Fixed floor = rules_.resources[size_t(rl.res)].min;
        for (size_t p = 1; p < np; ++p) {
            Fixed& stock = gs.players[p].stock[size_t(rl.res)];
            if (stock > ms.lastStock[l][p]) ms.lastGain[l][p] = sim.tick();
            const int lv = modeLevel(sim, uint8_t(p), int16_t(l));
            if (lv >= 0) {
                const LevelDef& d = rl.levels[size_t(lv)];
                if (d.decayEvery.raw > 0) {
                    ms.decayTimer[l][p] += p < M.playerDecay.size() ? dt * M.playerDecay[p] : dt;
                    if (ms.decayTimer[l][p] >= d.decayEvery) {
                        ms.decayTimer[l][p] -= d.decayEvery;
                        const Fixed quiet = dt * int64_t(sim.tick() - ms.lastGain[l][p]);
                        if (quiet > d.decayAfter && stock > floor) stock = fxMax(floor, stock - d.decayAmount);
                    }
                }
            }
            const int now = modeLevel(sim, uint8_t(p), int16_t(l));
            if (!ms.stats.empty() && now != ms.level[l][p]) {
                ms.stats[p][now > ms.level[l][p] ? kMsLevelUps : kMsLevelDowns] += std::abs(now - ms.level[l][p]);
                ms.stats[p][kMsMaxLevel] = std::max<int64_t>(ms.stats[p][kMsMaxLevel], now);
            }
            ms.level[l][p] = int16_t(now);
            ms.lastStock[l][p] = stock;
        }
    }

    // ---- time limit (TimeMod): countdown in game time, speed steps, the side with most points wins at zero
    if (M.timeLimit) {
        if (ms.timeLeft.raw > 0) {
            ms.timeLeft -= dt;
            for (size_t i = 0; i < M.timeSpeed.size(); ++i)
                if (int(i) > ms.timeIndex && ms.timeLeft <= M.timeSpeed[i].first) {
                    ms.timeIndex = int(i);
                    ms.timeSpeed = M.timeSpeed[i].second;
                    sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeTimeStep, {}});
                }
            if (ms.timeIndex < 0) ms.timeSpeed = kOne;
            if (ms.timeLeft.raw <= 0 && gs.winnerTeam < 0) {
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeTimeUp, {}});
                auto n = pointsOfTeams();
                int mx = -1;
                for (auto& [t, c] : n) mx = std::max(mx, c);
                if (M.timeWin == TimeWin::MaxPoints) {
                    int cnt = 0, win = -1;
                    for (auto& [t, c] : n) if (c == mx) { ++cnt; win = t; }
                    if (cnt > 1) drawAll();
                    else if (cnt == 1) {
                        gs.winnerTeam = win; markTeam(win, 1);
                        for (auto& [t, c] : n) if (t != win) defeatTeam(t);
                    }
                } else if (!n.empty()) {
                    int cnt = 0, win = -1;
                    for (auto& [t, c] : n) if (M.timeCoef * int64_t(c) > Fixed::fromInt(mx)) { ++cnt; win = t; }
                    if (cnt != 1) drawAll();   // BW: > 1 draws; none qualifying (coefficient <= 1) picks key 0 - a draw here
                    else {
                        gs.winnerTeam = win; markTeam(win, 1);
                        for (auto& [t, c] : n) if (t != win) defeatTeam(t);
                    }
                }
            }
        } else if (!M.timeSpeed.empty()) {
            ms.timeSpeed = M.timeSpeed.back().second;
        }
    }

    // ---- domination: every domination point held (timers run out) -> sides holding none are out (FFA) /
    //      one side holding them all wins (COOP)
    if (M.domination && gs.winnerTeam < 0) {
        bool ready = false, abort = false;
        std::vector<int> held;
        for (auto& mp : ms.points) {
            if (mp.kind != 1 || !w.alive(mp.e) || !w.has<Owner>(mp.e)) continue;
            ready = true;
            if (mp.hold.raw > 0) { abort = true; break; }
            const uint8_t owner = w.get<Owner>(mp.e).player;
            if (!inZone(owner, w.get<Transform>(mp.e).pos)) continue;
            if (neutralOwner(owner)) { abort = true; break; }
            const int t = teamOfSlot(owner);
            if (std::find(held.begin(), held.end(), t) == held.end()) held.push_back(t);
        }
        if (ready && !abort && !held.empty()) {
            DominationRule rule = M.domRule;
            if (rule == DominationRule::Auto) {
                bool ffa = true;
                for (int t : teams) { int n = 0; for (auto& p : sim.players()) if (int(p.team) == t) ++n; ffa = ffa && n <= 1; }
                rule = ffa ? DominationRule::HoldAny : DominationRule::HoldAll;
            }
            if (rule == DominationRule::HoldAny) {
                for (int t : teams) if (std::find(held.begin(), held.end(), t) == held.end()) defeatTeam(t);
            } else if (held.size() == 1) {
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeDomHeld, {}});
                winTeam(held[0]);
            }
        }
    }

    // ---- king of the hill: the first side whose score reaches zero wins
    if (M.koh && gs.winnerTeam < 0) {
        int win = -1;
        for (int t : teams)
            if (size_t(t) < ms.kohScore.size() && ms.kohScore[size_t(t)].raw <= 0 &&
                (win < 0 || ms.kohScore[size_t(t)] < ms.kohScore[size_t(win)])) win = t;
        if (win >= 0) winTeam(win);
    }

    // ---- sudden death: counter, slowed preview, factor status for everyone, optional end by points
    if (M.suddenDeath) {
        const Fixed at = M.sdAt + (M.zones ? M.zoneSdDelay : Fixed{});
        const Fixed start = at + M.sdPreview, end = start + M.sdEnd;
        if (ms.sdPhase == 0) {
            if (M.sdCounter.raw > 0 && !ms.sdCounter && T >= at - M.sdCounter && T < at) {
                ms.sdCounter = true;
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeSdCounter, {}});
            }
            if (T >= at) {
                ms.sdPhase = 1; ms.sdCounter = false; ms.sdSpeed = M.sdPreviewSpeed;
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeSdPreview, {}});
            }
        }
        if (ms.sdPhase == 1 && T >= start) {
            ms.sdPhase = 2; ms.sdSpeed = kOne;
            if (M.sdStatus >= 0)
                for (size_t p = 1; p < np; ++p) { EffectCtx ctx; ctx.owner = 0; addPlayerStatus(sim, uint8_t(p), M.sdStatus, Fixed{}, ctx); }
            sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeSdStart, {}});
        }
        if (ms.sdPhase == 2 && M.sdEnd.raw > 0) {
            if (M.sdCounter.raw > 0 && !ms.sdCounter && T >= end - M.sdCounter && T < end) {
                ms.sdCounter = true;
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeSdCounter, {}});
            }
            if (T >= end) {
                ms.sdPhase = 3; ms.sdCounter = false;
                sim.emit({kEvModePhase, 0, kNullEntity, kNullEntity, kModeSdEnd, {}});
                if (gs.winnerTeam < 0) {
                    // SuddenDeathModule.CheckMode: sides with the most points win; all equal -> draw; a tie of some ->
                    // they win and the others draw (one winning team here: the match result is a draw)
                    std::vector<std::pair<int, int>> sum;
                    for (int t : teams) sum.push_back({t, 0});
                    for (auto& [t, c] : pointsOfTeams())
                        for (auto& s : sum) if (s.first == t) s.second = c;
                    int mx = -1; for (auto& s : sum) mx = std::max(mx, s.second);
                    std::vector<int> win;
                    for (auto& s : sum) if (s.second == mx) win.push_back(s.first);
                    if (win.size() == sum.size()) drawAll();
                    else if (win.size() > 1) {
                        gs.winnerTeam = 0;
                        for (auto& s : sum) markTeam(s.first, std::find(win.begin(), win.end(), s.first) != win.end() ? 1 : 3);
                    } else if (win.size() == 1) winTeam(win[0]);
                }
            }
        }
    }
    (void)decided0;
    ms.speed = ms.timeSpeed * ms.sdSpeed;

    // ---- HUD / script / AI view of the modes as trigger variables (overlays: "{var:mode_time_left}", conditions)
    auto& V = gs.trig.vars;
    if (M.timeLimit || M.suddenDeath) V["mode_speed"] = ms.speed;
    if (M.timeLimit) V["mode_time_left"] = fxMax(Fixed{}, ms.timeLeft);
    if (M.suddenDeath) {
        const Fixed at = M.sdAt + (M.zones ? M.zoneSdDelay : Fixed{});
        const Fixed next = ms.sdPhase == 0 ? at : ms.sdPhase == 1 ? at + M.sdPreview : M.sdEnd.raw > 0 ? at + M.sdPreview + M.sdEnd : Fixed{};
        V["mode_sd_phase"] = Fixed::fromInt(ms.sdPhase);
        V["mode_sd_left"] = next.raw > 0 && ms.sdPhase < 3 ? fxMax(Fixed{}, next - T) : Fixed{};
        V["mode_sd_counter"] = Fixed::fromInt(ms.sdCounter ? 1 : 0);
    }
    if (M.domination) {
        std::vector<int> held(np, 0);
        for (auto& mp : ms.points)
            if (mp.kind == 1 && w.alive(mp.e) && w.has<Owner>(mp.e) && w.get<Owner>(mp.e).player < np && mp.hold.raw <= 0) ++held[w.get<Owner>(mp.e).player];
        for (size_t p = 1; p < np; ++p) V["dom_" + std::to_string(p)] = Fixed::fromInt(held[p]);
    }
    if (M.zones)
        for (size_t p = 1; p < np; ++p) {
            const ZoneState& z = ms.zone[p];
            if (!z.on) continue;
            const std::string k = "zone_" + std::to_string(p) + "_";
            V[k + "x"] = z.rect.x; V[k + "z"] = z.rect.z; V[k + "w"] = z.rect.w; V[k + "h"] = z.rect.h;
        }
}

// ================================================================= match statistics (PlayerStatistic)

void RtsModule::sysMatchStats(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = state(sim);
    ModesState& ms = gs.modes;
    if (ms.stats.empty()) return;
    const size_t np = ms.stats.size();
    auto ok = [&](uint8_t p) { return p > 0 && p < np; };
    for (const SimEvent& ev : sim.events()) {
        switch (ev.type) {
        case kEvDied: {
            if (ev.value < 0 || size_t(ev.value) >= rules_.types.size() || rules_.types[size_t(ev.value)].structure) break;
            if (ok(ev.player)) ++ms.stats[ev.player][kMsLost];
            if (w.alive(ev.b))
                if (auto* o = w.tryGet<Owner>(ev.b); o && ok(o->player) && o->player != ev.player) ++ms.stats[o->player][kMsKills];
            break;
        }
        case kEvTrainDone: if (ok(ev.player)) ++ms.stats[ev.player][kMsProduced]; break;
        case kEvCaptured:
            if (ok(ev.player)) ++ms.stats[ev.player][ev.value == 0 ? kMsCapturedNeutral : kMsCaptured];
            if (ev.value > 0 && ev.value < int64_t(np) && ev.value != ev.player) ++ms.stats[size_t(ev.value)][kMsStructuresLost];
            break;
        case kEvUpgraded: if (ok(ev.player)) ++ms.stats[ev.player][kMsUpgrades]; break;
        case kEvPlayerAbility:
            if (!ok(ev.player)) break;
            ++ms.stats[ev.player][kMsAbilities];
            if (ev.value >= 0 && size_t(ev.value) < ms.abilityUses[ev.player].size()) ++ms.abilityUses[ev.player][size_t(ev.value)];
            break;
        default: break;
        }
    }
    std::vector<int64_t> units(np, 0), points(np, 0);
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (!ok(o.player) || u.hp.raw <= 0) return;
        const UnitType& t = rules_.types[u.type];
        if (!t.structure) { if (!(u.flags & kUnitOffMap)) ++units[o.player]; return; }
        if (modes_.garrisonPoints ? w.has<Garrison>(e) : t.countsForVictory) ++points[o.player];
    });
    for (size_t p = 1; p < np; ++p) {
        auto& s = ms.stats[p];
        s[kMsMaxUnits] = std::max(s[kMsMaxUnits], units[p]);
        s[kMsPoints] = points[p];
        s[kMsMaxPoints] = std::max(s[kMsMaxPoints], points[p]);
        const size_t t = size_t(teamOf(sim, uint8_t(p)));
        if (modes_.koh && t < ms.kohScore.size()) s[kMsModeScore] = ms.kohScore[t].raw;
    }
}

// ================================================================= hash / save

void RtsModule::hashModes(Simulation& sim, Hasher& h) {
    World& w = sim.world();
    const ModesState& ms = state(sim).modes;
    (void)w;
    h.fx(ms.realTime); h.fx(ms.speed); h.fx(ms.timeSpeed); h.fx(ms.sdSpeed); h.fx(ms.timeLeft);
    h.i32(ms.timeIndex); h.u32(ms.sdPhase); h.u32(ms.sdCounter); h.i32(ms.sdRuleSet);
    for (auto& p : ms.points) { h.u64(p.e.packed()); h.u32(p.lastOwner); h.fx(p.hold); }
    for (auto s : ms.kohScore) h.fx(s);
    for (auto& z : ms.zone) { h.u32(z.on); h.i32(z.cur); h.fx(z.rect.x); h.fx(z.rect.z); h.fx(z.rect.w); h.fx(z.rect.h); }
    for (auto r : ms.ruleOn) h.u32(r);
    for (auto& v : ms.level) for (auto x : v) h.i32(x);
    for (auto& v : ms.decayTimer) for (auto x : v) h.fx(x);
    for (auto& v : ms.lastGain) for (auto x : v) h.u32(x);
    for (auto& v : ms.stats) for (auto x : v) h.u64(uint64_t(x));
    for (auto o : ms.outcome) h.i32(o);
    for (auto x : ms.neutralMax) h.i32(x);
}

void io(Archive& a, ModeRect& r) { a(r.x, r.z, r.w, r.h); }
void io(Archive& a, ModePoint& p) { a(p.e, p.kind, p.lastOwner, p.hold); }
void io(Archive& a, ZoneState& z) { a(z.start, z.cur, z.rect, z.on); }

bool ioModesState(RtsModule& m, Simulation& sim, Archive& a) {
    if (!m.modes_.any) return true;   // games without modes keep their save format
    ModesState& s = m.state(sim).modes;
    a(s.realTime, s.speed, s.timeSpeed, s.sdSpeed, s.timeLeft, s.timeIndex, s.sdPhase, s.sdCounter, s.sdRuleSet, s.points, s.kohScore,
      s.kohAlarmed, s.zone, s.ruleOn, s.level, s.decayTimer, s.lastStock, s.lastGain, s.stats, s.abilityUses, s.outcome, s.neutralMax);
    return a.ok();
}

} // namespace aiverse::game
