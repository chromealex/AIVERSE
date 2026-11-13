// Bunker Wars computer player: the controller loop (AIBase.Update / AI6.Update), AI6MapState, AI6Ranker,
// AI6Worker, the Domination / King of the Hill modes, the per-map custom factories (AI6Map17005, AI6Map11001),
// AI6ActionDetector, defeat conditions and the bot save state. Port of MW2Data/Gameplay/Battle/Modules/AIModule;
// float maths became Fixed, "#AI6FIX" notes mark where a BW crash path (division by zero, -1 routes) is guarded.
#include "RtsBwAIInternal.h"
#include "../../engine/core/Archive.h"
#include "RtsAI.h"

namespace aiverse::game {

namespace {
constexpr Fixed kOne = Fixed::fromInt(1);
int ceilInt(Fixed f) { return int((f + Fixed::fromRaw(Fixed::kOne - 1)).floorInt()); }
int truncInt(Fixed f) { return int(f.toInt()); }
Fixed lerp(Fixed a, Fixed b, Fixed t) { return a + (b - a) * t; }
// Mathf.Clamp: min wins only when value < min, else max wins when value > max (max < min keeps that order)
Fixed clampU(Fixed v, Fixed lo, Fixed hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }

// AI6Ranker tables
const Fixed kVillage[5][5] = {
    {Fixed::fromInt(22), Fixed::fromRatio(57, 2), Fixed::fromInt(32), Fixed::fromRatio(69, 2), Fixed::fromInt(36)},
    {Fixed::fromInt(48), Fixed::fromInt(59), Fixed::fromInt(66), Fixed::fromInt(71), Fixed::fromInt(74)},
    {Fixed::fromInt(78), Fixed::fromRatio(183, 2), Fixed::fromInt(102), Fixed::fromRatio(219, 2), Fixed::fromInt(114)},
    {Fixed::fromInt(112), Fixed::fromInt(126), Fixed::fromInt(140), Fixed::fromInt(150), Fixed::fromInt(156)},
    {Fixed::fromInt(150), Fixed::fromRatio(325, 2), Fixed::fromInt(180), Fixed::fromRatio(385, 2), Fixed::fromInt(200)}};
const Fixed kUpgradeVillage[3][5] = {
    {Fixed::fromInt(30), Fixed::fromRatio(105, 4), Fixed::fromRatio(63, 2), Fixed::fromRatio(141, 4), Fixed::fromRatio(75, 2)},
    {Fixed::fromInt(25), Fixed::fromRatio(175, 8), Fixed::fromRatio(105, 4), Fixed::fromRatio(235, 8), Fixed::fromRatio(125, 4)},
    {Fixed::fromInt(16), Fixed::fromInt(14), Fixed::fromRatio(84, 5), Fixed::fromRatio(94, 5), Fixed::fromInt(20)}};
const Fixed kConvertVillage[3][5] = {
    {Fixed::fromInt(50), Fixed::fromInt(50), Fixed::fromInt(50), Fixed::fromInt(50), Fixed::fromInt(50)},   // to house
    {Fixed::fromInt(60), Fixed::fromInt(50), Fixed::fromInt(40), Fixed::fromInt(30), kBwInf},                 // to smithy
    {Fixed::fromInt(5), Fixed::fromInt(10), Fixed::fromInt(15), Fixed::fromInt(20), Fixed::fromInt(30)}};     // to tower
constexpr Fixed kAggressionTime = Fixed::fromInt(5);
constexpr int kExtraUnitsToDefend = 5;

enum class RankType : uint8_t { Defense, Upgrade, Relocation, Attack, ConvertToHouse, ConvertToSmithy, ConvertToTower };
struct Rank { RankType type = RankType::Attack; int point = -1; Fixed rank{}; int need = 0; };

struct Ranker {
    BwAI::Ctx& c;
    BwMode mode;
    bool custom11001;
    std::vector<Rank> attack, defense;
    int domTotal = 0, domOwn = 0, domEnemy = 0;
    Ranker(BwAI::Ctx& cx, BwMode md, bool m11001) : c(cx), mode(md), custom11001(m11001) {}
    BwAI& ai() { return c.ai; }
    BwWorld& W() { return c.world(); }
    std::vector<BwAI::PS>& ms() { return c.ai.ms_; }
    int upgradeLevel(uint8_t p) { return std::min(W().players[p].smithy, 4); }

    Fixed pointRank(uint8_t player, int i, int level) {
        level = std::max(level, 1);
        const int ul = upgradeLevel(player);
        const BwPoint& p = W().pts[size_t(i)];
        Fixed rank{};
        if (p.dom || p.koth) rank = kVillage[4][ul];
        else switch (p.role) {
            case BwRole::House: rank = kVillage[std::min(level, 5) - 1][ul]; break;
            case BwRole::Tower: case BwRole::Smithy: {
                const int roads = c.roadsInTowerRange(i);
                const int own = c.countRole(player, BwRole::House), all = c.countRole(-1, BwRole::House);
                const Fixed prodRank = all > 0 ? Fixed::fromInt(own * 2) / int64_t(all) : Fixed{};   // #AI6FIX: no houses on the map
                rank = (p.role == BwRole::Tower ? c.prof.towerRankFactor : c.prof.smithyRankFactor) * 5 * roads * prodRank;
                break;
            }
            default: break;
        }
        return rank * c.prof.rankFactor(i, W().time);
    }

    bool canAttack(int i) {   // CanAttackPoint: inScreenBattleTime >= the point's attack timeout
        const Fixed t = c.prof.attackTimeout(i);
        return t.raw <= 0 || W().pts[size_t(i)].screenTime >= t;
    }

    Fixed possibleCounterAttack(uint8_t player, int i) {
        Fixed radius = (c.minDistOwn(i, player) + c.avgDistOwn(i, player)) / 2 * c.prof.counterAttackScanRadiusFactor;
        Fixed need{};
        for (int q = 0; q < int(W().pts.size()); ++q) {
            const uint8_t o = W().pts[size_t(q)].owner;
            if (c.ally(o, player) || c.neutral(o)) continue;
            const Fixed d = c.dist(i, q);
            if (d.raw >= 0 && d <= radius) need += ms()[size_t(q)].available;   // #AI6FIX: unreachable points do not counter-attack
        }
        return need;
    }

    Fixed possibleHelp(int i, Fixed time) {
        const BwAI::PS& ps = ms()[size_t(i)];
        const uint8_t owner = W().pts[size_t(i)].owner;
        const Fixed avg = c.avgDistOwn(i, owner);
        if (avg.raw == 0) return Fixed{};
        const Fixed row = c.rowTime(owner);
        const Fixed delay = avg / W().players[owner].speed;
        const Fixed possible = Fixed::fromInt(kBwRowUnits) * (time - delay) / row;
        const Fixed maxHelp = Fixed::fromInt(W().players[owner].units - ps.defenders) - ps.available;
        return clampU(possible, Fixed{}, maxHelp);
    }

    int alliedOnWay(const BwAI::PS& ps, uint8_t player) {
        int n = 0;
        for (size_t s = 0; s < ps.byPlayer.size(); ++s) if (ps.byPlayer[s] && c.ally(uint8_t(s), player)) n += ps.byPlayer[s];
        return n;
    }

    bool attackRankBase(uint8_t player, int i, Rank& out) {
        if (!c.screen(i)) return false;
        const BwAI::PS& ps = ms()[size_t(i)];
        const BwPoint& p = W().pts[size_t(i)];
        const Fixed cur = pointRank(player, i, p.level), fut = pointRank(player, i, p.level - 1);
        const Fixed s = c.prof.attackDistanceSafety;
        Fixed h{};
        if (s.raw == 0) h = c.minDistOwn(i, player);
        else if (s < Fixed::fromRatio(1, 2)) h = lerp(c.minDistOwn(i, player), c.avgDistOwn(i, player), s * 2);
        else if (s == Fixed::fromRatio(1, 2)) h = c.avgDistOwn(i, player);
        else if (s < kOne) h = lerp(c.avgDistOwn(i, player), c.maxDistOwn(i, player), (s - Fixed::fromRatio(1, 2)) * 2);
        else h = c.maxDistOwn(i, player);
        if (h.raw == 0) return false;
        h = h * c.prof.attackDistanceFactor;
        const BwPlayer& pl = W().players[player];
        const Fixed time = h / pl.speed;
        const Fixed defC = p.defNext, atkC = pl.damage;
        const int attackersOnWay = alliedOnWay(ps, player);
        const int defendingOnWay = ps.total - attackersOnWay;
        const Fixed help = c.neutral(p.owner) ? possibleCounterAttack(player, i) : possibleHelp(i, time);
        const Fixed prod = p.prod, avail = ps.available, maxU = Fixed::fromInt(p.max), row = c.rowTime(player);
        const Fixed producedOnWay = fxMin(prod * time, fxMax(Fixed{}, maxU - avail));
        const Fixed reqDuringProd = (avail + producedOnWay) * defC / atkC;
        const Fixed prodTime = Fixed::fromInt((reqDuringProd / kBwRowUnits).floorInt()) * row;
        const Fixed produced = fxMax(producedOnWay, prod * prodTime);
        const Fixed totalDef = avail + kOne + produced + Fixed::fromInt(defendingOnWay) + help;
        const Fixed totalCoef = atkC / defC - prod * row / kBwRowUnits;
        // #AI6FIX: production outruns the attack (coefficient <= 0): unreachable instead of a division by zero
        const int required = totalCoef.raw > 0 ? ceilInt(totalDef / totalCoef) : 1 << 20;
        int need = required - attackersOnWay;
        if (need >= 0) need = std::max(need, c.prof.minGroup());
        const Fixed factor = c.avgDistOwn(i, player) / c.px(2400);   // AI6Ranker.maxDistance = 160 / (4 / 60)
        if (factor.raw <= 0) return false;
        out = {RankType::Attack, i, (cur + fut - Fixed::fromInt(need)) / factor, need};
        return true;
    }

    void kothStats(uint8_t player, Fixed& sum, Fixed& minEnemy) {
        sum = Fixed{}; minEnemy = c.kothMax();
        for (auto& pi : c.sim.players()) {
            const Fixed sc = c.kothScore(pi.slot);
            sum += sc;
            if (pi.slot != player) minEnemy = fxMin(minEnemy, sc);
        }
    }

    // GetAttackRank with the mode / custom overrides
    bool attackRank(uint8_t player, int i, Rank& out) {
        if (custom11001) {
            if (ai().customState_ == 0) {
                if (i == c.prof.testAttackTarget) { out = {RankType::Attack, i, Fixed::fromInt(1000), 5}; return true; }
                return false;
            }
            if (ai().customState_ == 1)
                for (int s : c.prof.playerSidePoints) if (s == i) return false;
        }
        if (!attackRankBase(player, i, out)) return false;
        const BwPoint& p = W().pts[size_t(i)];
        if (mode == BwMode::Domination && p.dom) {
            const int maxD = std::max(domOwn, domEnemy);
            const int den = domTotal - maxD;
            const Fixed k = Fixed::fromRatio(1, 5) + (den > 0 ? Fixed::fromInt(maxD + std::abs(domOwn - domEnemy)) / int64_t(den) : kBwInf / 1024);
            out.rank = out.rank + fxAbs(out.rank) * k;
        } else if (mode == BwMode::KingOfTheHill && p.koth) {
            const int players = std::max<int>(1, int(c.sim.players().size()));
            const Fixed own = c.kothScore(player), mx = fxMax(c.kothMax(), kOne);
            Fixed sum, minEnemy; kothStats(player, sum, minEnemy);
            const Fixed add = Fixed::fromInt(200) * (kOne - sum / (mx * players)) * fxAbs(own - minEnemy);
            out.rank = fxMax(Fixed{}, out.rank) + add;
        }
        return true;
    }

    bool canDefend(uint8_t player, int i, uint8_t enemy) {
        if (!c.screen(i)) return false;
        const Fixed ownAvg = c.avgDistOwn(i, player);
        if (ownAvg.raw == 0) return false;
        const Fixed enemyAvg = c.avgDistOwn(i, enemy);
        if (enemyAvg.raw == 0) return true;
        const Fixed breakT = ms()[size_t(i)].available / kBwRowUnits * c.rowTime(enemy);
        const Fixed ownT = ownAvg / W().players[player].speed;
        const Fixed enemyT = enemyAvg / W().players[enemy].speed + breakT;
        if (ownT.raw <= 0) return true;
        return enemyT / ownT >= c.prof.canDefendPointMinTimeFactor;
    }

    bool defenseRank(uint8_t player, int i, Rank& out) {
        const BwAI::PS& ps = ms()[size_t(i)];
        if (ps.firstEnemy < 0) return false;
        const uint8_t enemy = uint8_t(ps.firstEnemy);
        if (!canDefend(player, i, enemy)) return false;
        Rank er;
        if (!attackRank(enemy, i, er)) return false;
        const int enemyOnWay = size_t(enemy) < ps.byPlayer.size() ? ps.byPlayer[enemy] : 0;
        const Fixed v = Fixed::fromInt(enemyOnWay) * W().players[enemy].damage -
                        (ps.available + Fixed::fromInt(ps.defenders)) * W().pts[size_t(i)].def + Fixed::fromInt(kExtraUnitsToDefend);
        out = {RankType::Defense, i, er.rank, ceilInt(v)};
        return true;
    }

    bool upgradeRank(uint8_t player, int i, Rank& out) {
        if (!c.screen(i)) return false;
        const BwPoint& p = W().pts[size_t(i)];
        const int li = p.level - 1;
        if (li < 0 || li >= 3) return false;
        const int ul = upgradeLevel(player);
        const int need = p.nextLevel >= 0 ? p.upgradeCost : 0;
        if (p.role == BwRole::House) { out = {RankType::Upgrade, i, kUpgradeVillage[li][ul], need}; return true; }
        if (p.role == BwRole::Tower) { out = {RankType::Upgrade, i, pointRank(player, i, p.level), need}; return true; }
        return false;
    }

    // conversion into role r possible: the rules have it, and a smithy still adds something (playerModsMax)
    bool canConvert(const BwPoint& p, BwRole r, uint8_t player) {
        const int t = p.convertTo[int(r)];
        if (t < 0) return false;
        if (r == BwRole::Smithy) {
            const int cap = c.R.types[size_t(t)].playerModsMax;
            if (cap > 0 && W().players[player].smithy >= cap) return false;   // BW: the race allows no more smithies
        }
        return true;
    }

    bool convertRank(uint8_t player, int i, Rank& out) {
        if (!c.screen(i)) return false;
        const BwPoint& p = W().pts[size_t(i)];
        const Fixed cur = pointRank(player, i, p.level);
        const int ul = upgradeLevel(player);
        auto tryOne = [&](int row, BwRole r, RankType t) {
            if (!canConvert(p, r, player)) return false;
            const Fixed rank = kConvertVillage[row][ul];
            if (rank <= cur) return false;
            out = {t, i, rank, p.convertCost[int(r)]};
            return true;
        };
        switch (p.role) {
            case BwRole::House: return tryOne(1, BwRole::Smithy, RankType::ConvertToSmithy) || tryOne(2, BwRole::Tower, RankType::ConvertToTower);
            case BwRole::Tower: return tryOne(0, BwRole::House, RankType::ConvertToHouse) || tryOne(1, BwRole::Smithy, RankType::ConvertToSmithy);
            case BwRole::Smithy: return tryOne(0, BwRole::House, RankType::ConvertToHouse) || tryOne(2, BwRole::Tower, RankType::ConvertToTower);
            default: return false;
        }
    }

    int relocationNeed(uint8_t player, int i, Fixed radius) {
        const BwAI::PS& ps = ms()[size_t(i)];
        int support = 0;
        for (int q = 0; q < int(W().pts.size()); ++q) {
            const uint8_t o = W().pts[size_t(q)].owner;
            if (c.ally(o, player) || c.neutral(o)) continue;
            const Fixed d = c.dist(i, q);
            if (d.raw >= 0 && d <= radius) support += truncInt(ms()[size_t(q)].available);
        }
        return support - truncInt(ps.available) - ps.defenders;
    }

    bool relocationRank(uint8_t player, int i, Rank& out) {
        if (!c.screen(i)) return false;
        const Fixed mn = c.minDistEnemy(i, player), avg = c.avgDistEnemy(i, player);
        if (mn.raw == 0 || avg.raw == 0) return false;
        if (mode == BwMode::Domination && W().pts[size_t(i)].dom) {
            out = {RankType::Relocation, i, mn, relocationNeed(player, i, avg)};
            return true;
        }
        const Fixed radius = (mn + avg) / 2;
        out = {RankType::Relocation, i, radius, relocationNeed(player, i, radius)};
        return true;
    }

    void update(uint8_t player) {
        attack.clear(); defense.clear();
        if (mode == BwMode::Domination) {
            domTotal = c.objectiveCount(true, -1);
            domOwn = c.objectiveCount(true, player);
            domEnemy = domTotal - domOwn - c.objectiveCount(true, 0);
        }
        if (custom11001 && ai().customState_ == 1 && W().time >= c.prof.playerDevelopTimeout) ai().customState_ = 2;
        bool enableUpgrade = !c.prof.limitOneUpgrade || !c.anyUpgrading(player);
        enableUpgrade = enableUpgrade && W().time >= c.prof.upgradeTimeout;
        for (int i = 0; i < int(W().pts.size()); ++i) {
            const uint8_t owner = W().pts[size_t(i)].owner;
            Rank r;
            if (c.ally(owner, player)) {
                if (defenseRank(player, i, r)) attack.push_back(r);
                if (enableUpgrade && owner == player && upgradeRank(player, i, r)) attack.push_back(r);
                if (enableUpgrade && owner == player && convertRank(player, i, r)) attack.push_back(r);
                if (relocationRank(player, i, r)) defense.push_back(r);
            } else {
                if (!canAttack(i) || !attackRank(player, i, r)) continue;
                if (!c.neutral(owner)) {   // FixRankByAggression
                    const Fixed ag = fxMin(kAggressionTime, W().time) / kAggressionTime;
                    if (r.rank.raw >= 0) r.rank = r.rank * ag;
                    else r.rank = ag.raw > 0 ? r.rank / ag : -kBwInf;   // #AI6FIX: rank / 0 at the first tick
                }
                attack.push_back(r);
            }
        }
        std::stable_sort(attack.begin(), attack.end(), [](const Rank& a, const Rank& b) { return a.rank > b.rank; });
        std::stable_sort(defense.begin(), defense.end(), [](const Rank& a, const Rank& b) { return a.rank < b.rank; });
    }
};

struct Worker {
    BwAI::Ctx& c;
    BwMode mode;
    bool custom17005;
    std::vector<int> free;
    Worker(BwAI::Ctx& cx, BwMode md, bool m17005) : c(cx), mode(md), custom17005(m17005) {}
    BwWorld& W() { return c.world(); }
    std::vector<BwAI::PS>& ms() { return c.ai.ms_; }
    uint8_t me() { return c.me(); }

    int calculateFree(Ranker& rk) {
        const size_t n = W().pts.size();
        free.assign(n, 0);
        for (auto& r : rk.attack) {
            if (r.type != RankType::Defense || W().pts[size_t(r.point)].owner != me()) continue;
            const BwAI::PS& ps = ms()[size_t(r.point)];
            const int need = r.need + ps.defenders;
            const int avail = truncInt(ps.available);
            free[size_t(r.point)] = need >= 0 ? -avail : -(avail - std::abs(need) + 5);
        }
        const Fixed reserve = c.prof.reserveUnitsForCounterAttackFactor;
        if (reserve.raw > 0)
            for (size_t i = 0; i < n; ++i)
                if (W().pts[i].owner == me() && free[i] == 0) free[i] = -ceilInt(rk.possibleCounterAttack(me(), int(i)) * reserve);
        int total = 0;
        for (size_t i = 0; i < n; ++i) {
            if (W().pts[i].owner != me()) continue;
            int f = truncInt(ms()[i].available);
            const int reserved = -free[i];
            if (reserved > 0) f = std::max(f - reserved, 0);
            free[i] = f;
            total += f;
        }
        if (mode == BwMode::Domination) {   // AI6DominationWorker: keep units on the objectives the enemy is ahead on
            const int totalDom = c.objectiveCount(true, -1);
            const int own = c.objectiveCount(true, me()), neutral = c.objectiveCount(true, 0);
            const int enemy = totalDom - own - neutral;
            const Fixed k = Fixed::fromRatio(2, 3) * (Fixed::fromInt(enemy) / int64_t(std::max(1, totalDom - 1)));
            for (size_t i = 0; i < n; ++i) {
                if (free[i] == 0 || !W().pts[i].dom) continue;
                free[i] = truncInt(fxMax(Fixed{}, Fixed::fromInt(free[i]) - ms()[i].available * k));
            }
        }
        return total;
    }

    bool canSend(const Rank& r, int src, int units) {
        if (!custom17005) return true;
        const bool avoid = (r.type == RankType::Attack && c.prof.avoidFreezeWaveForAttacks) || (r.type == RankType::Defense && c.prof.avoidFreezeWaveForDefenses);
        if (!avoid) return true;
        Fixed interval = c.prof.freezeWaveInterval;
        if (interval.raw <= 0 && c.prof.freezeWaveAbility >= 0) interval = c.R.playerAbilities[size_t(c.prof.freezeWaveAbility)].autoEvery;
        if (interval.raw <= 0) return true;
        const Fixed d = c.dist(r.point, src), now = W().time;
        const Fixed since = now - interval * (now / interval).floorInt();
        if (since < c.prof.refreshTimeAfterFreezeWave) return false;
        const Fixed toNext = interval - since;
        const Fixed arrival = d / W().players[me()].speed;
        const Fixed attackT = Fixed::fromInt(units / kBwRowUnits) * c.rowTime(me());   // BW: integer division
        return toNext >= arrival + attackT;
    }

    struct SendInfo { int src; int units; Fixed dist; };
    bool shouldAttackOrDefend(const Rank& r, int deficit) {
        if (deficit <= 0 || (r.type == RankType::Defense && deficit < 5)) return true;
        if (mode == BwMode::KingOfTheHill && W().pts[size_t(r.point)].koth) {
            Ranker tmp(c, mode, false);
            const Fixed own = c.kothScore(me());
            Fixed sum, minEnemy; tmp.kothStats(me(), sum, minEnemy);
            const Fixed lo = fxMin(own, minEnemy);
            const Fixed k = lo.raw > 0 ? fxAbs(own - minEnemy) / lo : kBwInf;
            if (k >= Fixed::fromRatio(1, 2) && deficit < r.need / 3) return true;
        }
        return false;
    }

    bool tryAttackOrDefend(const Rank& r) {
        struct Cand { int src; Fixed rk; int free; };
        std::vector<Cand> cands;
        for (int s = 0; s < int(free.size()); ++s) {
            if (!c.screen(s) || s == r.point || free[size_t(s)] <= 0 || !c.route(s, r.point)) continue;
            const Fixed d = c.dist(s, r.point);
            if (d.raw <= 0) continue;
            const Fixed rk = r.type == RankType::Defense ? c.avgDistEnemy(s, me()) / d : ms()[size_t(s)].available / d;
            cands.push_back({s, rk, free[size_t(s)]});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.rk > b.rk; });
        const BwPoint& tp = W().pts[size_t(r.point)];
        const Fixed k = tp.defNext / W().players[me()].damage;
        std::vector<SendInfo> sends;
        int need = r.need;
        for (auto& cd : cands) {
            const int maxU = cd.free;
            int units = std::clamp(need, 0, cd.free), dead = 0;
            for (;; ++units) {
                dead = c.towerKillsOnRoute(cd.src, r.point, units, me());
                if (units >= maxU || Fixed::fromInt(units) - Fixed::fromInt(dead) * k >= Fixed::fromInt(need)) break;
            }
            // BW: the "out of screen" branch is always taken (one screen), so dead <= units passes too
            bool ok = dead <= units * 2 / 3 || dead <= units || (tp.role == BwRole::Tower && dead < units);
            ok = ok && canSend(r, cd.src, units);
            if (ok) { sends.push_back({cd.src, units, c.dist(r.point, cd.src)}); need -= units - dead; }
            if (need <= 0) break;
        }
        if (sends.empty() || !shouldAttackOrDefend(r, need)) return false;
        if (r.type == RankType::Attack && c.prof.structuredAttacks) {
            std::stable_sort(sends.begin(), sends.end(), [](const SendInfo& a, const SendInfo& b) { return a.dist > b.dist; });
            const int batch = int((W().tick << 8) | uint32_t(r.point & 0xFF));
            const Fixed longest = sends[0].dist, speed = W().players[me()].speed;
            for (auto& s : sends) {
                BwAI::Deferred d; d.batch = batch; d.from = W().pts[size_t(s.src)].e; d.to = tp.e; d.units = s.units; d.left = (longest - s.dist) / speed;
                c.ai.deferred_.push_back(d);
                if (size_t(s.src) < c.ai.reserved_.size()) c.ai.reserved_[size_t(s.src)] += s.units;
            }
        } else {
            const int limit = r.type == RankType::Attack ? c.prof.attackLimitPerUpdate : c.prof.defenseLimitPerUpdate;
            int total = 0;
            for (auto& s : sends) {
                if (tp.owner == me() || tp.screenTime >= c.prof.attackTimeout(r.point)) { c.send(s.src, r.point, s.units); ++total; }
                if (limit > 0 && total >= limit) break;
            }
        }
        return true;
    }

    bool tryRelocation(std::vector<Rank>& ranks, int totalFree) {
        const int own = c.ai.ownPoints_;
        if (ranks.empty() || own < 3) return false;
        int target = 0;
        const int risk = std::clamp(own - 3, 0, 4);
        for (int i = 0; i <= risk && i < int(ranks.size()); ++i)
            if (ranks[size_t(i)].need > ranks[size_t(target)].need && ranks[size_t(i)].need <= totalFree) target = i;
        const int minG = c.prof.minGroup();
        int need = std::max(ranks[size_t(target)].need, minG);
        const int tp = ranks[size_t(target)].point;
        const int limit = c.prof.defenseLimitPerUpdate;
        int total = 0;
        for (int i = int(ranks.size()) - 1; i >= risk + 2; --i) {
            if (need <= 0) break;
            const int src = ranks[size_t(i)].point;
            const int sf = free[size_t(src)];
            if (sf < minG || !c.route(src, tp)) continue;
            const int units = sf;   // BW: inside the screen the whole free garrison goes
            const int dead = c.towerKillsOnRoute(src, tp, units, me());
            if (dead < units / 5 || dead <= units) {
                c.send(src, tp, units);
                need -= units;
                ++total;
                if (limit > 0 && total >= limit) break;
            }
        }
        return total > 0;
    }

    bool tryUpgrade(const Rank& r) {
        const BwPoint& p = W().pts[size_t(r.point)];
        if (p.upgrading || free[size_t(r.point)] < r.need || p.nextLevel < 0) return false;
        c.upgrade(r.point, p.nextLevel);
        return true;
    }

    bool tryConvert(const Rank& r) {
        const BwPoint& p = W().pts[size_t(r.point)];
        if (p.upgrading || free[size_t(r.point)] < r.need) return false;
        if (!c.prof.allowConvert) return false;
        const BwRole to = r.type == RankType::ConvertToHouse ? BwRole::House : r.type == RankType::ConvertToSmithy ? BwRole::Smithy : BwRole::Tower;
        if (p.convertTo[int(to)] < 0) return false;
        c.upgrade(r.point, p.convertTo[int(to)]);
        return true;
    }

    bool action(Ranker& rk) {
        const int totalFree = calculateFree(rk);
        c.ai.attackStrategy_ = !c.ai.attackStrategy_;
        if (c.ai.attackStrategy_) {
            bool flag = false;
            for (auto& r : rk.attack) {
                if (r.need > 0) {
                    switch (r.type) {
                        case RankType::Attack: case RankType::Defense: flag = tryAttackOrDefend(r); break;
                        case RankType::Upgrade: flag = tryUpgrade(r); break;
                        case RankType::ConvertToHouse: case RankType::ConvertToSmithy: case RankType::ConvertToTower: flag = tryConvert(r); break;
                        default: break;
                    }
                }
                if (flag) return true;
            }
            return false;
        }
        return tryRelocation(rk.defense, totalFree);
    }
};

BwMode modeOf(BwAI::Ctx& c) {
    if (!c.cfg.modeAuto) return c.cfg.mode;
    for (auto& p : c.world().pts) { if (p.dom) return BwMode::Domination; if (p.koth) return BwMode::KingOfTheHill; }
    return BwMode::Standard;
}
} // namespace

// ---- AI6MapState.Update
void BwImpl::updateMapState(Ctx& c) {
    BwWorld& W = c.world();
    BwAI& ai = c.ai;
    const size_t n = W.pts.size();
    if (ai.reserved_.size() != n) ai.reserved_.assign(n, 0);
    size_t slots = 1;
    for (auto& p : c.sim.players()) slots = std::max<size_t>(slots, size_t(p.slot) + 1);
    ai.ms_.assign(n, BwAI::PS{});
    for (auto& ps : ai.ms_) {
        ps.byPlayer.assign(slots, 0);
        ps.minTimeAttack = ps.minTimeDefense = kBwInf;
    }
    for (auto& g : W.groups) {
        const int units = g.closed() ? g.count : g.need();
        if (g.minDist > c.px(200) && g.minDist > g.routeLen / 2) continue;   // not past half of the road yet
        BwAI::PS& ps = ai.ms_[size_t(g.to)];
        ps.total += units;
        if (g.owner < ps.byPlayer.size()) ps.byPlayer[g.owner] += units;
        const BwPoint& tp = W.pts[size_t(g.to)];
        const Fixed speed = W.players[g.owner].speed;
        const Fixed tMin = g.minDist / speed, tMax = g.maxDist / speed;
        if (c.ally(g.owner, tp.owner)) {
            ps.defenders += units;
            ps.defenseOnWay += tp.def * units;
            ps.minTimeDefense = fxMin(ps.minTimeDefense, tMin);
            ps.lastTimeDefense = fxMax(ps.lastTimeDefense, tMax);
        } else {
            ps.attackers += units;
            ps.attackOnWay += W.players[g.owner].damage * units;
            ps.minTimeAttack = fxMin(ps.minTimeAttack, tMin);
            ps.lastTimeAttack = fxMax(ps.lastTimeAttack, tMax);
            if (ps.firstEnemy < 0 && !c.ally(g.owner, c.me())) ps.firstEnemy = g.owner;
        }
    }
    // units of other players' groups that can still leave before ours arrive (BW counts a garrison with them)
    const Fixed minDist = c.minDistEnemyFromOwn(c.me());
    const Fixed minArrival = minDist / W.players[c.me()].speed;
    ai.ownPoints_ = 0;
    for (size_t i = 0; i < n; ++i) {
        const BwPoint& p = W.pts[i];
        Fixed units = Fixed::fromInt(p.value + p.pendingOut);
        for (auto& g : W.groups) {
            if (g.from != int(i) || g.closed()) continue;
            Fixed delta = fxMin(Fixed::fromInt(g.pending), units);
            if (g.owner != c.me()) {
                const Fixed row = c.rowTime(g.owner);
                const Fixed possibleExit = Fixed::fromInt(kBwRowUnits * (minArrival / row).floorInt());
                delta = fxMin(delta, possibleExit);
            }
            units -= delta;
        }
        units -= Fixed::fromInt(ai.reserved_[i]);
        BwAI::PS& ps = ai.ms_[i];
        ps.available = p.onScreen ? fxMax(units, Fixed{}) : Fixed{};   // GetAvailableUnitsInPoint: 0 off the screen
        ps.defense = ps.available * p.def;
        ps.attack = ps.available * W.players[p.owner].damage;
        if (p.owner == c.me()) ++ai.ownPoints_;
    }
}

Fixed BwImpl::calcTimeout(Ctx& c) {
    const BwProfile& p = c.prof;
    if (p.algorithm != BwAlgo::AI6) return kOne + c.ai.rng_.unit() * 2;
    const Fixed avg = p.ignoreAverageActionTime ? Fixed{} : c.ai.avgActionTime_;
    if (avg.raw == 0) return p.minDelay + (p.maxDelay - p.minDelay) / 2;
    return fxClamp(avg, p.minDelay, p.maxDelay);
}

// ---- AI6ActionDetector: how often the fastest non-bot player acts (sends / upgrades), averaged over 15 s windows
void BwImpl::actionDetector(Ctx& c) {
    BwAI& ai = c.ai;
    const auto& cmds = c.sim.executedCommands();
    if (ai.cmdCursor_ > cmds.size()) ai.cmdCursor_ = cmds.size();
    if (ai.actionCounts_.size() < 256) ai.actionCounts_.assign(256, 0);
    for (; ai.cmdCursor_ < cmds.size(); ++ai.cmdCursor_) {
        const Command& cmd = cmds[size_t(ai.cmdCursor_)];
        if (cmd.player == c.me() || (cmd.type != kCmdSend && cmd.type != kCmdUpgrade)) continue;
        if (std::find(c.cfg.botSlots.begin(), c.cfg.botSlots.end(), int(cmd.player)) != c.cfg.botSlots.end()) continue;
        const auto& bots = c.m.bwBots();   // the other BW bots of this process (BW counts player.isBot == false only)
        if (std::find(bots.begin(), bots.end(), cmd.player) != bots.end()) continue;
        ++ai.actionCounts_[cmd.player];
    }
    const Fixed period = Fixed::fromInt(15);
    ai.actionClock_ += c.dt;
    if (ai.actionClock_ < period) return;
    Fixed best = kBwInf;
    for (int n : ai.actionCounts_) if (n > 0) best = fxMin(best, period / int64_t(n));
    if (best < kBwInf) {
        ai.avgActionTime_ = ai.lastAvgActionTime_.raw > 0 ? (ai.lastAvgActionTime_ + best) / 2 : best;
        ai.lastAvgActionTime_ = best;
    } else ai.avgActionTime_ = ai.lastAvgActionTime_ = Fixed{};
    ai.actionClock_ = Fixed{};
    std::fill(ai.actionCounts_.begin(), ai.actionCounts_.end(), 0);
}

// ---- AI6Worker.Update: structured attacks wait so all groups arrive together
void BwImpl::ai6Worker(Ctx& c) {
    BwAI& ai = c.ai;
    if (ai.deferred_.empty()) return;
    BwWorld& W = c.world();
    for (auto& d : ai.deferred_) {
        if (d.dead) continue;
        d.left -= c.dt;
        if (d.left.raw > 0) continue;
        const int s = W.pointOf(d.from), t = W.pointOf(d.to);
        const bool ok = s >= 0 && t >= 0 && W.pts[size_t(s)].value >= d.units && W.pts[size_t(s)].owner == c.me();
        if (!ok) { for (auto& o : ai.deferred_) if (o.batch == d.batch) o.dead = true; }
        else c.send(s, t, d.units);
        d.dead = true;
    }
    for (size_t i = 0; i < ai.deferred_.size();) {
        const auto& d = ai.deferred_[i];
        if (!d.dead) { ++i; continue; }
        const int s = W.pointOf(d.from);
        if (s >= 0 && size_t(s) < ai.reserved_.size()) ai.reserved_[size_t(s)] -= d.units;
        ai.deferred_.erase(ai.deferred_.begin() + long(i));
    }
}

void BwImpl::ai6(Ctx& c) {
    const BwMode mode = modeOf(c);
    const bool m11001 = c.prof.custom == "map11001", m17005 = c.prof.custom == "map17005";
    if (m11001) {   // AI6Map11001 events: a group sent at the test target, the player taking its side
        BwWorld& W = c.world();
        BwAI& ai = c.ai;
        if (ai.customFlags_.size() != c.prof.playerSidePoints.size()) {
            ai.customFlags_.assign(c.prof.playerSidePoints.size(), 0);
            for (size_t k = 0; k < c.prof.playerSidePoints.size(); ++k) {
                const int pi = c.prof.playerSidePoints[k];
                if (pi < int(W.pts.size())) {
                    const uint8_t o = W.pts[size_t(pi)].owner;
                    const bool bot = o == c.me() || std::find(c.cfg.botSlots.begin(), c.cfg.botSlots.end(), int(o)) != c.cfg.botSlots.end();
                    if (o != 0 && !bot) ai.customFlags_[k] = 1;
                }
            }
        }
        if (ai.customState_ == 0)
            for (auto& g : W.groups) if (g.to == c.prof.testAttackTarget) ai.customState_ = 1;
        if (ai.customState_ == 1) {
            bool all = true;
            for (size_t k = 0; k < c.prof.playerSidePoints.size(); ++k) {
                const int pi = c.prof.playerSidePoints[k];
                if (pi < int(W.pts.size())) {
                    const uint8_t o = W.pts[size_t(pi)].owner;
                    const bool bot = o == c.me() || std::find(c.cfg.botSlots.begin(), c.cfg.botSlots.end(), int(o)) != c.cfg.botSlots.end();
                    if (o != 0 && !bot) ai.customFlags_[k] = 1;
                }
                all = all && ai.customFlags_[k];
            }
            if (all) ai.customState_ = 2;
        }
    }
    Ranker rk(c, mode, m11001);
    rk.update(c.me());
    Worker wk(c, mode, m17005);
    wk.action(rk);
}

// ---- AI6.CheckDefeat (the bot surrenders when the profile's conditions hold)
void BwImpl::checkDefeat(Ctx& c) {
    BwAI& ai = c.ai;
    for (auto& d : c.prof.defeat) {
        ai.defeatClock_ += c.dt;   // BW: one clock for all conditions
        if (ai.defeatClock_ < d.timeToCheck) continue;
        ai.defeatClock_ = Fixed{};
        const int rnd = int(ai.rng_.below(100));
        if (d.chance <= 0 || d.chance < rnd) continue;
        BwWorld& W = c.world();
        bool pass = true;
        for (auto& ck : d.checks) {
            int my = 0, other = 0;
            bool ok = false;
            switch (ck.kind) {
                case BwDefeatCheck::Points:
                case BwDefeatCheck::Units: {
                    if (ck.kind == BwDefeatCheck::Points) for (auto& p : W.pts) (p.owner == c.me() ? my : other)++;
                    else {
                        my = W.players[c.me()].units;
                        for (auto& pi : c.sim.players()) if (pi.slot != c.me() && pi.alive) other += W.players[pi.slot].units;
                    }
                    const int all = my + other;
                    if (all > 0) ok = bwCompare(ck.cmp, Fixed::fromInt(my * 100 / all + ck.delta), Fixed::fromInt(other * 100 / all));
                    break;
                }
                case BwDefeatCheck::Morale: {
                    Fixed lo = kBwInf;
                    for (auto& pi : c.sim.players()) if (pi.slot != c.me() && pi.alive) lo = fxMin(lo, W.players[pi.slot].morale);
                    ok = lo < kBwInf && bwCompare(ck.cmp, W.players[c.me()].morale + Fixed::fromInt(ck.delta), lo);
                    break;
                }
                case BwDefeatCheck::Time: ok = bwCompare(ck.cmp, W.time, Fixed::fromInt(ck.delta)); break;
            }
            if (!ok) { pass = false; break; }
        }
        if (pass) { Command s; s.type = kCmdSurrender; c.submit(s); return; }
    }
}

void BwImpl::resetBot(Ctx& c) {
    BwAI& ai = c.ai;
    ai.objSources_.clear(); ai.objTargets_.clear(); ai.ignore_.clear();
    ai.hasObjSources_ = ai.hasObjTargets_ = ai.hasIgnore_ = false;
    ai.objCount_ = -1; ai.attackTesting_ = false;
    initDebut(c);
    initCasters(c);
    ai.moveTasks_.clear();
    ai.timeout_ = kOne + ai.rng_.unit() * 2;
    // AI6.CreateInternals: fresh ranker / worker / action detector (reservations of dropped deferred sends released)
    for (auto& d : ai.deferred_) { const int s = c.world().pointOf(d.from); if (s >= 0 && size_t(s) < ai.reserved_.size()) ai.reserved_[size_t(s)] -= d.units; }
    ai.deferred_.clear(); ai.attackStrategy_ = false;
    ai.actionClock_ = ai.avgActionTime_ = ai.lastAvgActionTime_ = Fixed{};
    std::fill(ai.actionCounts_.begin(), ai.actionCounts_.end(), 0);
    ai.customState_ = 0; ai.customFlags_.clear();
    // Bishop / Ashley
    ai.tasks_.clear(); ai.bScanData_.clear(); ai.bClusters_.clear(); ai.bMine_.clear(); ai.bError_ = Fixed{}; ai.bReady_ = false; ai.bAwait_ = Fixed::fromInt(-1);
    ai.ashleyTimer_ = Fixed{}; ai.activeTargets_.clear();
}

void BwAI::update(Simulation& sim, RtsModule& m) {
    if (!m.bwAI()) return;
    cfg_ = m.bwAI();
    const BwProfile& prof = cfg_->profileFor(player_);
    const uint32_t now = sim.tick();
    if (init_ && now <= lastTick_) return;   // once per tick
    const Fixed dt = init_ ? Fixed::fromInt(int64_t(now - lastTick_)) / int64_t(sim.tickRate()) : sim.dt();
    lastTick_ = now;
    Ctx c(sim, m, *this, *cfg_, prof, dt);
    m.bwRegisterBot(player_);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.until < now; }), pending_.end());
    if (!init_) {
        init_ = true;
        timeout_ = kOne + rng_.unit() * 2;
        BwImpl::initCasters(c);
        BwImpl::initDebut(c);
        trigGroupOn_.clear();
        for (auto& g : cfg_->triggers.groups) trigGroupOn_.push_back(g.enabled ? 1 : 0);
        BwImpl::updateMapState(c);
        if (prof.algorithm == BwAlgo::Bishop) {
            bAttack_ = prof.attack.delay; bDefense_ = prof.defense.delay; bUpgrade_ = prof.upgradePeriod; bScan_ = prof.analyzePeriod;
            bAwait_ = fxMax(prof.firstActionDelay, Fixed{});
        }
    }
    auto* pi = sim.player(player_);
    if (!pi || !pi->alive || sim.result() != 0 || prof.algorithm == BwAlgo::None) return;
    const bool played = BwImpl::debut(c);   // AIDebutModule.AdvanceTick + AIBase.Update's ValidateDebut
    if (!disabled_ && !played) {
        // AIBase.Update
        if (timeout_.raw <= 0) timeout_ = BwImpl::calcTimeout(c);
        timeout_ -= dt;
        if (timeout_.raw <= 0) BwImpl::updateMapState(c);
        if (prof.useSkills) BwImpl::skills(c);
        switch (prof.algorithm) {
            case BwAlgo::AI6:
                BwImpl::actionDetector(c);
                BwImpl::ai6Worker(c);
                if (timeout_.raw <= 0) BwImpl::ai6(c);
                BwImpl::checkDefeat(c);
                break;
            case BwAlgo::Ashley: BwImpl::ashley(c); break;
            case BwAlgo::Bishop: BwImpl::bishop(c); break;
            default: break;
        }
    }
    BwImpl::triggers(c);   // Module.TickBots: triggers after the bots
}

// ---- save state (next to Simulation::serialize; RtsAI::saveState v4)
namespace {
void io(Archive& a, BwAI::PS& p) {
    aiverse::io(a, p.total); aiverse::io(a, p.attackers); aiverse::io(a, p.defenders); aiverse::io(a, p.firstEnemy); aiverse::io(a, p.byPlayer);
    aiverse::io(a, p.available); aiverse::io(a, p.attackOnWay); aiverse::io(a, p.defenseOnWay); aiverse::io(a, p.attack); aiverse::io(a, p.defense);
    aiverse::io(a, p.minTimeAttack); aiverse::io(a, p.minTimeDefense); aiverse::io(a, p.lastTimeAttack); aiverse::io(a, p.lastTimeDefense);
}
template <class T, class F> void ioVec(Archive& a, std::vector<T>& v, F&& f) {
    uint32_t n = a.count(v.size(), 4);
    if (!a.writing()) v.assign(n, T{});
    for (uint32_t i = 0; i < n && a.ok(); ++i) f(v[i]);
}
} // namespace

void BwAI::stateIo(Archive& a) {
    using aiverse::io;
    uint32_t version = 2;
    io(a, version);
    if (version < 1 || version > 2) { a.fail(); return; }
    uint64_t rs = rng_.state(), ri = rng_.inc();
    io(a, rs); io(a, ri);
    if (!a.writing()) rng_.restore(rs, ri);
    io(a, player_); io(a, init_); io(a, lastTick_); io(a, timeout_);
    io(a, disabled_); io(a, useAllUnitsForAttack_); io(a, attackTesting_);
    io(a, enableAttack_); io(a, enableRelocation_); io(a, enableDefense_); io(a, enableUpgrade_);
    io(a, objSources_); io(a, objTargets_); io(a, ignore_); io(a, objCount_); io(a, hasObjSources_); io(a, hasObjTargets_); io(a, hasIgnore_);
    io(a, reserved_);
    ioVec(a, pending_, [&](Pending& p) { io(a, p.e); io(a, p.units); io(a, p.until); io(a, p.upgrade); });
    io(a, attackStrategy_);
    ioVec(a, deferred_, [&](Deferred& d) { io(a, d.batch); io(a, d.from); io(a, d.to); io(a, d.units); io(a, d.left); io(a, d.dead); });
    io(a, actionCounts_); io(a, actionClock_); io(a, avgActionTime_); io(a, lastAvgActionTime_); io(a, cmdCursor_);
    io(a, defeatClock_); io(a, customState_); io(a, customFlags_);
    ioVec(a, casters_, [&](Caster& k) { io(a, k.ability); io(a, k.slot); io(a, k.kind); io(a, k.timer); io(a, k.heat); io(a, k.a); io(a, k.b); io(a, k.n); });
    ioVec(a, moveTasks_, [&](MoveTask& t) { io(a, t.from); io(a, t.owner); io(a, t.timer); io(a, t.duration); io(a, t.percent); });
    io(a, lastGroupId_);
    ioVec(a, ms_, [&](PS& p) { game::io(a, p); });
    io(a, ownPoints_);
    io(a, debut_); io(a, debutPlaying_); io(a, debutCursor_);
    io(a, trigGroupOn_); io(a, trigArrived_);
    io(a, trigTimers_); io(a, trigValues_); io(a, lastOwners_); io(a, prevOwners_);
    io(a, ashleyTimer_); io(a, activeTargets_); io(a, ashleyNeed_);
    io(a, bScan_); io(a, bAttack_); io(a, bDefense_); io(a, bUpgrade_); io(a, bAwait_); io(a, bError_); io(a, bReady_);
    ioVec(a, tasks_, [&](Task& t) {
        io(a, t.type); io(a, t.target);
        ioVec(a, t.src, [&](TaskSrc& s) { io(a, s.from); io(a, s.count); io(a, s.timer); });
    });
    io(a, bAttacks_);
    if (version >= 2) io(a, screenSince_);   // stage 2: multiscreen time of the points
    if (!a.writing()) routes_ = BwRoutes{};   // derived: rebuilt from the lanes on the next think
}

BwAIHolder::BwAIHolder() = default;
BwAIHolder::BwAIHolder(const BwAIHolder& o) : p(o.p ? std::make_unique<BwAI>(*o.p) : nullptr) {}
BwAIHolder& BwAIHolder::operator=(const BwAIHolder& o) { if (this != &o) p = o.p ? std::make_unique<BwAI>(*o.p) : nullptr; return *this; }
BwAIHolder::BwAIHolder(BwAIHolder&&) noexcept = default;
BwAIHolder& BwAIHolder::operator=(BwAIHolder&&) noexcept = default;
BwAIHolder::~BwAIHolder() = default;

} // namespace aiverse::game
