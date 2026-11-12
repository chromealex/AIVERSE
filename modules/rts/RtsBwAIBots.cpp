// Bunker Wars computer player: the older algorithms (Impl/AIAshley.cs, Impl/AIBishop.cs + Tasks/*) and the AIBase
// unit estimates they share (GetNeedCount, CalculateNeedCount, GetTargetCount).
#include "RtsBwAIInternal.h"

namespace aiverse::game {

namespace {
constexpr Fixed kOne = Fixed::fromInt(1);
int ceilInt(Fixed f) { return int((f + Fixed::fromRaw(Fixed::kOne - 1)).floorInt()); }
int truncInt(Fixed f) { return int(f.toInt()); }
Fixed lerp(Fixed a, Fixed b, Fixed t) { return a + (b - a) * t; }

Fixed maxRouteLen(BwAI::Ctx& c) {
    Fixed best{};
    const int n = int(c.world().pts.size());
    for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) best = fxMax(best, c.dist(i, j));
    return best;
}

// battle.units.GetTargetCount: units of hostile groups walking at `target`, source = where the largest comes from
int targetCount(BwAI::Ctx& c, int target, int& source) {
    BwWorld& W = c.world();
    int total = 0, best = -1;
    source = -1;
    for (auto& g : W.groups) {
        if (g.to != target || c.ally(g.owner, W.pts[size_t(target)].owner)) continue;
        total += g.need();
        if (g.need() > best && g.from >= 0) { best = g.need(); source = g.from; }
    }
    return total;
}

bool openGroupFrom(BwAI::Ctx& c, int source) {
    for (auto& g : c.world().groups) if (g.from == source && !g.closed()) return true;
    return false;
}
} // namespace

// AIBase.CalculateNeedCount * distance factor + units the target breeds meanwhile
int BwImpl::needCount(Ctx& c, int source, int target) {
    BwWorld& W = c.world();
    if (source < 0 || target < 0) return 0;
    const BwPoint& s = W.pts[size_t(source)];
    const BwPoint& t = W.pts[size_t(target)];
    const Fixed speed = W.players[s.owner].speed;
    const UnitType& tt = c.R.types[t.type];
    Fixed incTime = tt.spawnInterval;
    if (incTime.raw <= 0) incTime = kOne;
    if (!c.route(source, target)) return 0;
    const Fixed d = c.dist(source, target);
    const Fixed maxD = fxMax(maxRouteLen(c), kOne);
    const Fixed distanceFactor = kOne + d / maxD / 2;
    Fixed distanceCount = d / speed / incTime;
    const int mx = t.max - t.value;
    if (mx >= 0) distanceCount = fxClamp(distanceCount, Fixed{}, Fixed::fromInt(mx));
    // CalculateNeedCount
    const Fixed coef = t.def / W.players[s.owner].damage;
    Fixed damage = Fixed::fromInt(t.value) * coef;
    damage += Fixed::fromInt(c.towerKillsOnRoute(source, target, int(damage.floorInt()), c.me()));
    const int su = std::max(1, W.players[s.owner].units);
    const Fixed global = c.neutral(t.owner) ? kOne : Fixed::fromInt(W.players[t.owner].units) / int64_t(su);
    return ceilInt(damage * global * distanceFactor + distanceCount);
}

// ======================================================================= Ashley
namespace {
int unitsPercent(int value, Fixed share) {
    int n = int((Fixed::fromInt(value) * share).floorInt());
    if (n == 0 && value > 0) n = 1;
    return n;
}
int& needOf(BwAI& ai, Entity e) {
    for (auto& [p, n] : ai.ashleyNeed_) if (p == e) return n;
    ai.ashleyNeed_.push_back({e, 0});
    return ai.ashleyNeed_.back().second;
}
bool hasNeed(BwAI& ai, Entity e) { for (auto& [p, n] : ai.ashleyNeed_) if (p == e) return true; return false; }
} // namespace

void BwImpl::ashley(Ctx& c) {
    BwAI& ai = c.ai;
    ai.ashleyTimer_ += c.dt;
    if (ai.ashleyTimer_ < kOne) return;
    ai.ashleyTimer_ = Fixed{};
    BwWorld& W = c.world();
    const int n = int(W.pts.size());
    ai.activeTargets_.erase(std::remove_if(ai.activeTargets_.begin(), ai.activeTargets_.end(), [&](auto& t) { return t.second <= W.time; }), ai.activeTargets_.end());
    auto active = [&](int i) { for (auto& [e, until] : ai.activeTargets_) if (e == W.pts[size_t(i)].e) return true; return false; };
    // LookForTarget
    std::vector<int> targets, counts, delegates;
    for (int s = 0; s < n; ++s) {
        const BwPoint& src = W.pts[size_t(s)];
        if (src.owner != c.me()) continue;
        int enemySrc = -1;
        const int tc = targetCount(c, s, enemySrc);
        if (tc > 0 && enemySrc >= 0) {
            const int lose = needCount(c, enemySrc, s);
            if (tc >= lose) needOf(ai, src.e) += tc - lose;
        }
        for (int i = 0; i < n; ++i) {
            if (c.ally(W.pts[size_t(i)].owner, src.owner) || active(i)) continue;
            const int need = needCount(c, s, i);
            if (need < src.value) {   // delegateCount stays 0 in BW
                if (std::find(targets.begin(), targets.end(), i) == targets.end()) { targets.push_back(i); counts.push_back(need); }
                if (std::find(delegates.begin(), delegates.end(), s) == delegates.end()) delegates.push_back(s);
            }
        }
    }
    for (auto& [e, v] : ai.ashleyNeed_) {
        const int i = W.pointOf(e);
        if (i < 0 || std::find(targets.begin(), targets.end(), i) != targets.end() || v >= 300 || v <= 10) continue;
        targets.push_back(i); counts.push_back(v);
    }
    int target = -1, minReq = 0, myTarget = -1, minReqMy = 0, cm = INT32_MAX;
    for (size_t k = 0; k < targets.size(); ++k) {
        const int cc = counts[k] * 2;
        if (cc < cm && W.pts[size_t(targets[k])].owner == c.me()) { cm = cc; minReqMy = cc; myTarget = targets[k]; }
    }
    cm = INT32_MAX;
    for (size_t k = 0; k < targets.size(); ++k)
        if (counts[k] < cm && W.pts[size_t(targets[k])].owner != c.me()) { cm = counts[k]; minReq = counts[k]; target = targets[k]; }
    if (target < 0) { target = myTarget; minReq = minReqMy; }
    // TryToApply: a quarter, a half, three quarters of every delegate
    bool applied = false;
    if (target >= 0) {
        for (Fixed share : {Fixed::fromRatio(1, 4), Fixed::fromRatio(1, 2), Fixed::fromRatio(3, 4)}) {
            int army = 0;
            for (int s : delegates) army += unitsPercent(W.pts[size_t(s)].value, share);
            if (army < minReq) continue;
            bool out = false;
            for (int s : delegates) {
                if (s == target || unitsPercent(W.pts[size_t(s)].value, share) < 3) continue;
                c.sendPercent(s, target, share);
                out = true;
            }
            if (out) ai.activeTargets_.push_back({W.pts[size_t(target)].e, W.time + Fixed::fromInt(5)});
            applied = out;
            break;
        }
    }
    if (applied) return;
    // TryToUpgrade every own point
    ai.ashleyNeed_.clear();
    const BwPlayer& me = W.players[c.me()];
    for (int s = 0; s < n; ++s) {
        BwPoint& p = W.pts[size_t(s)];
        if (p.owner != c.me() || openGroupFrom(c, s)) continue;
        auto can = [&](int type, int cost) { return type >= 0 && !p.upgrading && p.value >= cost; };
        auto note = [&](int cost) { if (!hasNeed(ai, p.e)) needOf(ai, p.e) = cost; };
        if (p.role == BwRole::House) {
            const int delta = me.maxUnits * 15 / 100;
            if (me.units >= me.maxUnits - delta && c.countRole(c.me(), BwRole::Smithy) == 0 && c.countRole(c.me(), BwRole::House) > 1) {
                const int t = p.convertTo[int(BwRole::Smithy)];
                if (can(t, p.convertCost[int(BwRole::Smithy)])) c.upgrade(s, t);
                else if (t >= 0) note(p.convertCost[int(BwRole::Smithy)]);
                continue;
            }
        }
        if (p.role == BwRole::Smithy && c.countRole(c.me(), BwRole::Smithy) > 1) {
            const int t = p.convertTo[int(BwRole::House)];
            if (can(t, p.convertCost[int(BwRole::House)])) c.upgrade(s, t);
            else if (t >= 0) note(p.convertCost[int(BwRole::House)]);
            continue;
        }
        if (p.role == BwRole::Tower && p.level >= 1) {
            const int t = p.convertTo[int(BwRole::House)];
            if (can(t, p.convertCost[int(BwRole::House)])) c.upgrade(s, t);
            else if (t >= 0) note(p.convertCost[int(BwRole::House)]);
            continue;
        }
        if (can(p.nextLevel, p.upgradeCost)) c.upgrade(s, p.nextLevel);
        else if (p.nextLevel >= 0) note(p.upgradeCost);
    }
}

// ======================================================================= Bishop
namespace {
using BP = BwAI::BPoint;

BP* dataOf(BwAI& ai, int point) {
    for (auto& d : ai.bScanData_) if (d.point == point) return &d;
    return nullptr;
}

int reservedFor(BwAI::Ctx& c, int point) {
    int r = 0;
    for (auto& t : c.ai.tasks_)
        for (auto& s : t.src) if (c.world().pointOf(s.from) == point) { r += s.count; break; }
    return r;
}

// GetUnitsIncreaseCount: units a house breeds in `time`
Fixed unitsIncrease(BwAI::Ctx& c, int i, Fixed time, bool useLimit = true) {
    BwWorld& W = c.world();
    const BwPoint& p = W.pts[size_t(i)];
    if (p.role != BwRole::House) return Fixed{};
    if (p.max == 0 || (p.max <= p.value && useLimit)) return Fixed{};
    if (p.prod.raw <= 0) return Fixed{};
    const Fixed restored = c.prof.increaseUnitsCoefficient * p.prod * time;
    if (!useLimit) return restored;
    const BwPlayer& o = W.players[p.owner];
    const Fixed limit = fxMax(Fixed{}, Fixed::fromInt(std::min(p.max - p.value, o.maxUnits - o.units)));
    return fxClamp(restored, Fixed{}, limit);
}

Fixed avgLength(BwAI::Ctx& c, int i, bool neutralOnly) {
    Fixed sum{}; int n = 0;
    const uint8_t owner = c.world().pts[size_t(i)].owner;
    for (auto& d : c.ai.bScanData_) {
        if (d.point == i) continue;
        const uint8_t o = c.world().pts[size_t(d.point)].owner;
        if (neutralOnly && !(c.neutral(o) && !c.ally(owner, o))) continue;
        if (!c.route(i, d.point)) continue;
        sum += c.dist(i, d.point); ++n;
    }
    return n ? sum / int64_t(n) : kBwInf;
}

// GetAverage{Damage,Defense}ToPoint over the arrival timing of every other point
Fixed timedValue(BwAI::Ctx& c, const BP& target, bool damage, Fixed& firstTime) {
    BwWorld& W = c.world();
    firstTime = Fixed{};
    std::map<Fixed, int> timing;   // BW SortedDictionary: equal times overwrite
    for (auto& d : c.ai.bScanData_) {
        if (d.point == target.point || !c.route(d.point, target.point)) continue;
        timing[c.dist(d.point, target.point) / W.players[W.pts[size_t(d.point)].owner].speed] = d.point;
    }
    const uint8_t towner = W.pts[size_t(target.point)].owner;
    const Fixed tdef = W.pts[size_t(target.point)].def;
    Fixed sum{}, minTime{};
    for (auto& [time, src] : timing) {
        if (minTime.raw <= 0) minTime = time;
        const BwPoint& sp = W.pts[size_t(src)];
        if (c.neutral(sp.owner)) continue;
        bool match;
        if (damage) match = c.neutral(towner) ? c.ally(sp.owner, c.me()) : !c.ally(sp.owner, towner);
        else match = c.neutral(towner) ? !c.ally(sp.owner, c.me()) : c.ally(sp.owner, towner);
        if (!match) continue;
        if (firstTime.raw <= 0) firstTime = time;
        Fixed k = time.raw > 0 ? minTime / time : kOne;
        k = k * k * k * k * k;
        const BP* sd = dataOf(c.ai, src);
        Fixed value = damage ? (sd ? sd->real : Fixed{}) - (sd ? sd->incomeDamage : Fixed{}) / sp.def
                             : Fixed::fromInt(sp.value) - (sd ? sd->incomeDamage : Fixed{}) / tdef;
        value = fxMax(value, Fixed{});
        if (value.raw > 0) value -= Fixed::fromInt(c.towerKillsOnRoute(src, target.point, truncInt(value), sp.owner));
        value = fxMax(value, Fixed{});
        sum += k * (damage ? value * W.players[sp.owner].damage : value * tdef);
    }
    return damage ? sum : sum * tdef;
}

void scan(BwAI::Ctx& c) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    const int n = int(W.pts.size());
    ai.bMaxRoute_ = fxMax(maxRouteLen(c), kOne);
    ai.bScanData_.clear();
    const Fixed myDmg = W.players[c.me()].damage;
    for (int i = 0; i < n; ++i) {
        if (ai.hasIgnore_ && std::find(ai.ignore_.begin(), ai.ignore_.end(), i) != ai.ignore_.end()) continue;
        const BwPoint& p = W.pts[size_t(i)];
        BP d; d.point = i;
        Fixed near = kBwInf;
        for (int q = 0; q < n; ++q) if (c.route(i, q)) near = fxMin(near, c.dist(i, q));
        d.nearest = near;
        Fixed value = Fixed::fromInt(p.value);
        if (!c.neutral(p.owner) && !c.ally(c.me(), p.owner)) value += unitsIncrease(c, i, c.prof.attack.delay);
        d.timeToAttack = d.timeToDefence = kBwInf;
        for (auto& g : W.groups) {
            if (c.ally(g.owner, c.me()) && g.fake) continue;
            if (!g.closed() && g.from == i) value -= Fixed::fromInt(g.need());
            if (g.to != i) continue;
            const Fixed t = g.minDist / W.players[g.owner].speed;
            if (c.ally(g.owner, p.owner)) {
                d.incomeDefenseCount += g.need();
                d.incomeDefense += p.def * g.need();
                d.timeToDefence = fxMin(d.timeToDefence, t);
            } else {
                d.incomeDamage += W.players[g.owner].damage * g.need();
                d.timeToAttack = fxMin(d.timeToAttack, t);
                if (g.owner == c.me()) {
                    d.myAttack += myDmg * g.count;
                    d.waveDuration = fxMax(d.waveDuration, Fixed::fromInt(g.need()) / W.releaseRate);
                }
            }
        }
        value = fxMax(Fixed{}, value);
        d.real = value;
        d.attackValue = W.players[p.owner].damage * value;
        d.protection = Fixed::fromInt(p.value) * p.def;
        for (int j = 0; j < n; ++j) {
            if (j == i || !c.route(j, i)) continue;
            const BwPoint& s = W.pts[size_t(j)];
            Fixed val = Fixed::fromInt(s.value);
            if (!c.ally(s.owner, p.owner)) val += unitsIncrease(c, j, c.prof.attack.delay);
            val = fxMax(Fixed{}, val);
            const Fixed len = c.dist(j, i);
            val = fxMax(Fixed{}, val - Fixed::fromInt(c.towerKillsOnRoute(j, i, truncInt(val), s.owner)));
            const Fixed dv = (ai.bMaxRoute_ - len - d.nearest) / ai.bMaxRoute_;
            if (c.ally(s.owner, p.owner)) { if (!c.neutral(p.owner)) d.potDefense += p.def * val * dv; }
            else if (!c.neutral(s.owner)) d.potDamage += W.players[s.owner].damage * val * dv;
            if (s.owner == c.me() && !c.ally(p.owner, s.owner)) d.potByMe += myDmg * val * dv;
        }
        ai.bScanData_.push_back(d);
    }
    // towers dangerous for our own roads
    for (auto& d : ai.bScanData_) {
        const BwPoint& t = W.pts[size_t(d.point)];
        if (!t.tower || c.ally(t.owner, c.me())) continue;
        std::vector<std::pair<int, int>> byTower;
        for (auto& a : ai.bScanData_) {
            if (!c.ally(W.pts[size_t(a.point)].owner, c.me())) continue;
            for (auto& b : ai.bScanData_) {
                if (b.point <= a.point || !c.ally(W.pts[size_t(b.point)].owner, c.me()) || !c.route(a.point, b.point)) continue;
                c.towerKillsOnRoute(a.point, b.point, 0, c.me(), &byTower);
                for (auto& [k, kills] : byTower) if (k == d.point) d.danger += Fixed::fromInt(kills);
            }
        }
    }
    ai.bAttacks_ = 0;
    for (auto& d : ai.bScanData_) if (d.myAttack.raw > 0) ++ai.bAttacks_;
    for (auto& d : ai.bScanData_) {
        d.avgLenEnemies = avgLength(c, d.point, true);
        d.reserved = reservedFor(c, d.point);
        Fixed t1, t2;
        d.potDamageT = lerp(timedValue(c, d, true, t1), Fixed{}, ai.bError_);
        d.potDefenseT = lerp(timedValue(c, d, false, t2), Fixed{}, ai.bError_);
        d.minTimeEnemies = t1; d.minTimeAllies = t2;
    }
    // clusters: own points reachable from each other cheaper than the way to the enemy
    ai.bMine_.clear(); ai.bClusters_.clear();
    for (size_t k = 0; k < ai.bScanData_.size(); ++k) if (W.pts[size_t(ai.bScanData_[k].point)].owner == c.me()) ai.bMine_.push_back(int(k));
    for (int a : ai.bMine_) {
        std::vector<int> cl{a};
        const BP& pa = ai.bScanData_[size_t(a)];
        const int va = W.pts[size_t(pa.point)].value;
        for (int b : ai.bMine_) {
            if (b == a) continue;
            const BP& pb = ai.bScanData_[size_t(b)];
            if (!c.route(pa.point, pb.point) || c.dist(pa.point, pb.point) > pa.avgLenEnemies) continue;
            if (c.towerKillsOnRoute(pa.point, pb.point, va, c.me()) >= va / 2) continue;
            cl.push_back(b);
        }
        const bool all = cl.size() == ai.bMine_.size();
        ai.bClusters_.push_back(cl);
        if (all) break;
    }
}

struct Move { int src = -1, target = -1, count = 0, damage = 0; Fixed length = kBwInf, time{}, potDamage{}, potDefense{}, minTimeEnemies{}, minTimeAllies{}; bool route = false; };

bool addTask(BwAI::Ctx& c, std::vector<Move>& moves, int target, uint8_t type) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    if (moves.empty()) return false;
    for (auto& mv : moves) {
        BP* d = dataOf(ai, mv.src);
        if (!d || W.pts[size_t(mv.src)].value - d->reserved - mv.count < 0) return false;
    }
    for (auto& mv : moves) dataOf(ai, mv.src)->reserved += mv.count;
    const Entity te = W.pts[size_t(target)].e;
    ai.tasks_.erase(std::remove_if(ai.tasks_.begin(), ai.tasks_.end(), [&](auto& t) { return t.target == te; }), ai.tasks_.end());
    BwAI::Task t; t.type = type; t.target = te;
    Fixed maxT{};
    for (auto& mv : moves) maxT = fxMax(maxT, mv.time);
    for (auto& mv : moves) t.src.push_back({W.pts[size_t(mv.src)].e, mv.count, type == 1 ? Fixed{} : maxT - mv.time});
    ai.tasks_.push_back(t);
    return true;
}

bool clusterDefense(BwAI::Ctx& c, const std::vector<int>& cl) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    std::vector<int> needIn, able;
    for (int k : cl) if (ai.bScanData_[size_t(k)].fullDefense().raw < 0) needIn.push_back(k);
    std::stable_sort(needIn.begin(), needIn.end(), [&](int a, int b) { return ai.bScanData_[size_t(a)].fullDefense() > ai.bScanData_[size_t(b)].fullDefense(); });
    if (needIn.empty()) return false;
    for (int k : cl) if (std::find(needIn.begin(), needIn.end(), k) == needIn.end() && ai.bScanData_[size_t(k)].fullDefense().raw > 0) able.push_back(k);
    if (able.empty()) return false;
    const auto& par = c.prof.defense;
    const Fixed speed = W.players[c.me()].speed;
    bool moved = false;
    for (int nk : needIn) {
        const BP& pd = ai.bScanData_[size_t(nk)];
        const BwPoint& tp = W.pts[size_t(pd.point)];
        const Fixed needCount = -pd.fullDefense() / tp.def + Fixed::fromInt(10);
        std::vector<Move> cmds;
        for (int ak : able) {
            const BP& x = ai.bScanData_[size_t(ak)];
            const BwPoint& xp = W.pts[size_t(x.point)];
            Move mv; mv.src = x.point; mv.route = c.route(x.point, pd.point);
            mv.length = mv.route ? c.dist(x.point, pd.point) : kBwInf;
            mv.time = mv.length / speed;
            mv.count = x.incomeDamage.raw > 0 ? truncInt(x.real - Fixed::fromInt(x.reserved) - x.incomeDamage / xp.def) : truncInt(x.real) - x.reserved;
            mv.damage = mv.route ? c.towerKillsOnRoute(x.point, pd.point, mv.count, c.me()) : 0;
            if (mv.damage > 0 ? mv.count - mv.damage <= 8 : mv.count <= 0) continue;
            if (mv.count < par.minCount || Fixed::fromInt(mv.count) < Fixed::fromInt(xp.value) * par.minPercent / 100) continue;
            cmds.push_back(mv);
        }
        if (par.maxBuildings > 0 && int(cmds.size()) > par.maxBuildings) cmds.resize(size_t(par.maxBuildings));
        int sum = 0, needN = 0;
        for (size_t j = 0; j < cmds.size(); ++j) {
            sum += cmds[j].count;
            if (Fixed::fromInt(sum) >= needCount) {
                Move& mv = cmds[j];
                mv.count -= sum - truncInt(needCount);
                mv.count = std::max(mv.count, truncInt(Fixed::fromInt(W.pts[size_t(mv.src)].value) * par.minPercent / 100));
                mv.count = std::max(mv.count, par.minCount);
                needN = int(j) + 1;
                break;
            }
        }
        if (needN == 0) continue;
        cmds.resize(size_t(needN));
        cmds.erase(std::remove_if(cmds.begin(), cmds.end(), [&](const Move& mv) { return mv.time > pd.timeToAttack + par.delay + pd.waveDuration; }), cmds.end());
        moved = addTask(c, cmds, pd.point, 1);
    }
    return moved;
}

bool clusterSupport(BwAI::Ctx& c, const std::vector<int>& cl) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    if (cl.size() <= 1) return false;
    Fixed myCount{}, avg{};
    for (int k : cl) { myCount += ai.bScanData_[size_t(k)].real; avg += ai.bScanData_[size_t(k)].potDamageT; }
    avg = avg / int64_t(cl.size());
    if (avg.raw <= 0) return false;   // BW: NaN differences, nothing qualifies
    struct D { int k; Fixed diff; };
    std::vector<D> donors, weak;
    for (int k : cl) {
        const BP& x = ai.bScanData_[size_t(k)];
        const Fixed diff = (avg - x.potDamageT) * myCount / avg;
        if (x.incomeDamage.raw > 0) continue;
        if (diff.raw > 0) donors.push_back({k, diff});
        if (diff <= Fixed::fromInt(-16)) weak.push_back({k, diff});
    }
    if (donors.empty() || weak.empty()) return false;
    std::stable_sort(weak.begin(), weak.end(), [](const D& a, const D& b) { return a.diff < b.diff; });
    const Fixed speed = W.players[c.me()].speed;
    for (auto& wk : weak) {
        const int wp = ai.bScanData_[size_t(wk.k)].point;
        Fixed need = -wk.diff;
        std::vector<D> pers = donors;
        std::stable_sort(pers.begin(), pers.end(), [&](const D& a, const D& b) {
            const int pa = ai.bScanData_[size_t(a.k)].point, pb = ai.bScanData_[size_t(b.k)].point;
            const Fixed la = c.route(wp, pa) ? c.dist(wp, pa) : kBwInf, lb = c.route(wp, pb) ? c.dist(wp, pb) : kBwInf;
            return la < lb;
        });
        std::stable_sort(pers.begin(), pers.end(), [&](const D& a, const D& b) {
            const BP& x = ai.bScanData_[size_t(a.k)]; const BP& y = ai.bScanData_[size_t(b.k)];
            return W.pts[size_t(x.point)].value - x.reserved > W.pts[size_t(y.point)].value - y.reserved;
        });
        std::vector<Move> mvs;
        for (auto& dn : pers) {
            const BP& x = ai.bScanData_[size_t(dn.k)];
            Move mv; mv.src = x.point; mv.route = c.route(x.point, wp);
            mv.length = mv.route ? c.dist(x.point, wp) : kBwInf;
            mv.time = mv.length / speed;
            mv.count = truncInt(fxMin(Fixed::fromInt(W.pts[size_t(x.point)].value - 20), need - Fixed::fromInt(20)));
            mv.damage = mv.route ? c.towerKillsOnRoute(x.point, wp, mv.count, c.me()) : 0;
            if (mv.count - mv.damage >= mv.count / 2 && mv.count - mv.damage >= 8) mvs.push_back(mv);
        }
        if (mvs.empty()) continue;
        size_t k = 0;
        for (size_t j = 0; j < mvs.size(); ++j) {
            k = j + 1;
            need -= Fixed::fromInt(mvs[j].count);
            if (need.raw <= 0) { mvs[j].count += truncInt(need); break; }
        }
        mvs.resize(k);
        if (addTask(c, mvs, wp, 1)) return true;
    }
    return false;
}

bool doSupport(BwAI::Ctx& c) {
    if (!c.ai.enableRelocation_) return false;
    for (auto& cl : c.ai.bClusters_) {
        bool under = false;
        for (int k : cl) if (c.ai.bScanData_[size_t(k)].incomeDamage.raw > 0) under = true;
        if (!under && clusterSupport(c, cl)) return true;
    }
    return false;
}

bool attackCountValid(BwAI::Ctx& c) { return c.prof.maxAttackTargets <= 0 || c.ai.bAttacks_ < c.prof.maxAttackTargets; }

// CalcAdditionalDefenseByMorale: the defender reaches its next morale level while it holds (engine morale = perUnit stock)
Fixed moraleDefense(BwAI::Ctx& c, const BP& d) {
    const BwPoint& p = c.world().pts[size_t(d.point)];
    if (c.R.perUnitResources.empty() || c.neutral(p.owner) || p.defNext.raw <= 0) return Fixed{};
    return fxMax(Fixed{}, (d.incomeDefense + d.protection) * p.def / p.defNext);
}

bool clusterAttack(BwAI::Ctx& c, const std::vector<int>& cl) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    if (cl.empty()) return false;
    std::vector<int> enemies, targets;
    for (size_t k = 0; k < ai.bScanData_.size(); ++k) if (!c.ally(c.me(), W.pts[size_t(ai.bScanData_[k].point)].owner)) enemies.push_back(int(k));
    targets = enemies;
    if (ai.hasObjTargets_ && !ai.objTargets_.empty()) {
        targets.clear();
        for (int k : enemies) if (std::find(ai.objTargets_.begin(), ai.objTargets_.end(), ai.bScanData_[size_t(k)].point) != ai.objTargets_.end()) targets.push_back(k);
        if (targets.empty()) targets = enemies;
    }
    if (ai.hasIgnore_) targets.erase(std::remove_if(targets.begin(), targets.end(), [&](int k) { return std::find(ai.ignore_.begin(), ai.ignore_.end(), ai.bScanData_[size_t(k)].point) != ai.ignore_.end(); }), targets.end());
    std::stable_sort(targets.begin(), targets.end(), [&](int a, int b) { return ai.bScanData_[size_t(a)].minTimeEnemies > ai.bScanData_[size_t(b)].minTimeEnemies; });
    const Fixed speed = W.players[c.me()].speed, myDmg = W.players[c.me()].damage;
    const auto& par = c.prof.attack;
    std::vector<std::vector<Move>> pre;
    for (int tk : targets) {
        const BP tgt = ai.bScanData_[size_t(tk)];
        if (tgt.incomeDamage.raw > 0) continue;
        bool tasked = false;
        for (auto& t : ai.tasks_) if (t.target == W.pts[size_t(tgt.point)].e) tasked = true;
        if (tasked || std::find(cl.begin(), cl.end(), tk) != cl.end()) continue;
        std::vector<Move> able;
        auto make = [&](const BP& x) {
            const BwPoint& xp = W.pts[size_t(x.point)];
            Move mv; mv.src = x.point; mv.target = tgt.point; mv.route = c.route(tgt.point, x.point);
            mv.length = mv.route ? c.dist(tgt.point, x.point) : kBwInf;
            mv.time = mv.length / speed;
            mv.count = truncInt(x.real - Fixed::fromInt(x.reserved));
            if (!ai.hasObjSources_ && x.incomeDamage.raw > 0) mv.count = xp.def.raw <= 0 ? 0 : std::max(mv.count, truncInt(x.fullDefense() / xp.def) / 2);
            mv.damage = mv.route ? c.towerKillsOnRoute(x.point, tgt.point, mv.count, c.me()) : 0;
            mv.potDamage = x.potDamageT; mv.potDefense = x.potDefenseT; mv.minTimeEnemies = x.minTimeEnemies; mv.minTimeAllies = x.minTimeAllies;
            return mv;
        };
        if (!ai.hasObjSources_) {
            for (int k : cl) {
                const BP& x = ai.bScanData_[size_t(k)];
                const BwPoint& xp = W.pts[size_t(x.point)];
                if (!(x.real > Fixed::fromInt(x.reserved))) continue;
                if (!(x.fullDefense().raw > 0 || (x.fullDefense().raw < 0 && x.incomeDefense.raw <= 0))) continue;
                if (!(x.potDefenseT + x.protection - xp.def * x.reserved > x.potDamageT)) continue;
                Move mv = make(x);
                if (!mv.route || mv.count < par.minCount || Fixed::fromInt(mv.count) < Fixed::fromInt(xp.value) * par.minPercent / 100) continue;
                able.push_back(mv);
            }
            std::stable_sort(able.begin(), able.end(), [](const Move& a, const Move& b) { return a.minTimeEnemies > b.minTimeEnemies; });
            if (par.maxBuildings > 0 && int(able.size()) > par.maxBuildings) able.resize(size_t(par.maxBuildings));
        } else {
            for (int k : cl) {
                const BP& x = ai.bScanData_[size_t(k)];
                if (std::find(ai.objSources_.begin(), ai.objSources_.end(), x.point) != ai.objSources_.end()) able.push_back(make(x));
            }
        }
        if (able.empty()) continue;
        if (!ai.useAllUnitsForAttack_) {
            int maxDamage = 0; Fixed maxTime{};
            for (auto& mv : able) { maxDamage += mv.damage; maxTime = fxMax(maxTime, mv.time); }
            Fixed inc = unitsIncrease(c, tgt.point, maxTime + ai.bAttack_, c.neutral(W.pts[size_t(tgt.point)].owner));
            inc = lerp(inc, Fixed{}, ai.bError_);
            const Fixed defn = W.pts[size_t(tgt.point)].def;
            Fixed full = tgt.fullDefense() + inc * defn;
            const Fixed waves = Fixed::fromInt(ceilInt(full / myDmg / 8));
            const Fixed attackTime = waves * Fixed::fromInt(8) / W.releaseRate;
            full += unitsIncrease(c, tgt.point, attackTime, false) * defn;
            if (tgt.minTimeAllies <= tgt.minTimeEnemies) full += tgt.potDefenseT;
            Fixed attackValue = -Fixed::fromInt(maxDamage) * myDmg;
            full += lerp(moraleDefense(c, tgt), Fixed{}, ai.bError_);
            full += defn * c.prof.additionalAttackCount;
            size_t k = 0;
            for (size_t j = 0; j < able.size(); ++j) {
                Move& mv = able[j];
                attackValue += myDmg * mv.count;
                if (attackValue >= full) {
                    mv.count -= truncInt((attackValue - full) / myDmg) - 1;
                    mv.count = std::max(mv.count, truncInt(Fixed::fromInt(W.pts[size_t(mv.src)].value) * par.minPercent / 100));
                    mv.count = std::max(mv.count, par.minCount);
                    k = j + 1;
                    break;
                }
            }
            if (attackValue < full && ai.hasObjTargets_ && !ai.objTargets_.empty()) k = able.size();
            if (k == 0) continue;
            able.resize(k);
        }
        pre.push_back(able);
    }
    if (pre.empty()) return false;
    const int myUnits = std::max(1, W.players[c.me()].units);
    std::stable_sort(pre.begin(), pre.end(), [&](const std::vector<Move>& a, const std::vector<Move>& b) {
        auto w = [&](const std::vector<Move>& v) {
            Fixed len{}; int cnt = 0;
            for (auto& mv : v) { len += mv.length; cnt += mv.count; }
            len = len / int64_t(v.size());
            return len / ai.bMaxRoute_ * (Fixed::fromInt(std::max(cnt, 1)) / int64_t(myUnits));
        };
        return w(a) < w(b);
    });
    bool moved = false;
    for (auto& t : pre) {
        if (t.empty() || !addTask(c, t, t[0].target, 0)) continue;
        ++ai.bAttacks_;
        moved = true;
        if (!attackCountValid(c)) break;
    }
    if (moved) doSupport(c);
    return moved;
}

bool doUpgrade(BwAI::Ctx& c) {
    BwAI& ai = c.ai;
    if (!ai.enableUpgrade_) return false;
    BwWorld& W = c.world();
    const bool noHouse = c.prof.deniedConvertRoles & (1u << unsigned(BwRole::House));
    const bool noTower = c.prof.deniedConvertRoles & (1u << unsigned(BwRole::Tower));
    for (int k : ai.bMine_) {
        const BP& d = ai.bScanData_[size_t(k)];
        if (d.incomeDamage.raw > 0 || d.fullDefense().raw <= 0) continue;
        BwPoint& p = W.pts[size_t(d.point)];
        if (p.nextLevel < 0 || d.real <= Fixed::fromInt(p.upgradeCost)) continue;
        const Fixed safety = d.potDamage.raw <= 0 ? Fixed::fromInt(100)
                             : (d.potDefense + (d.real - Fixed::fromInt(p.upgradeCost)) * p.def - d.incomeDamage) / d.potDamage;
        if (safety <= Fixed::fromRatio(6, 5)) continue;
        auto can = [&](int type, int cost) { return type >= 0 && !p.upgrading && p.value >= cost + d.reserved; };
        if (p.role == BwRole::House) {
            if (d.potDefenseT < d.potDamageT) continue;
            if (!noHouse && can(p.nextLevel, p.upgradeCost)) { c.upgrade(d.point, p.nextLevel); return true; }
        } else if (p.role == BwRole::Tower) {
            if (!c.prof.disablePointsConvertation && !noHouse && safety >= Fixed::fromInt(2)) {
                const int t = p.convertTo[int(BwRole::House)];
                if (can(t, p.convertCost[int(BwRole::House)])) { c.upgrade(d.point, t); return true; }
            } else if (!noTower && can(p.nextLevel, p.upgradeCost)) { c.upgrade(d.point, p.nextLevel); return true; }
        }
    }
    return false;
}

void runTasks(BwAI::Ctx& c) {
    BwAI& ai = c.ai;
    BwWorld& W = c.world();
    for (auto& t : ai.tasks_) {
        const int tp = W.pointOf(t.target);
        if (tp < 0) { t.src.clear(); continue; }
        if (t.type == 0 && ai.attackTesting_) {   // AttackPoint.TestAttack
            bool ready = false;
            for (auto& s : t.src) if (s.timer - c.dt <= Fixed{}) ready = true;
            if (ready) {
                if (c.ally(W.pts[size_t(tp)].owner, c.me())) t.src.clear();
                else if (BP* td = dataOf(ai, tp)) {
                    Fixed dmg{};
                    for (auto& s : t.src) {
                        const int sp = W.pointOf(s.from);
                        if (sp < 0 || W.pts[size_t(sp)].owner != c.me()) continue;
                        BP* sd = dataOf(ai, sp);
                        if (sd && sd->fullDefense().raw <= 0) continue;
                        dmg += W.players[c.me()].damage * s.count;
                    }
                    if (dmg < td->fullDefense()) t.src.clear();
                }
            }
        }
        for (auto& s : t.src) {
            s.timer -= c.dt;
            const int sp = W.pointOf(s.from);
            if (s.timer.raw <= 0 && sp >= 0 && W.pts[size_t(sp)].owner == c.me()) {
                c.send(sp, tp, std::min(s.count, W.pts[size_t(sp)].value));
                ai.bError_ = Fixed{};   // MoveUnits resets the error
            }
        }
        t.src.erase(std::remove_if(t.src.begin(), t.src.end(), [](const BwAI::TaskSrc& s) { return s.timer.raw <= 0; }), t.src.end());
    }
    ai.tasks_.erase(std::remove_if(ai.tasks_.begin(), ai.tasks_.end(), [](const BwAI::Task& t) { return t.src.empty(); }), ai.tasks_.end());
}
} // namespace

void BwImpl::bishop(Ctx& c) {
    BwAI& ai = c.ai;
    const BwProfile& p = c.prof;
    // AwaitForAction
    bool wait = false;
    if (!ai.bReady_) {
        if (p.awaitForPlayerAction) {
            bool any = false;
            for (auto& cmd : c.sim.executedCommands())
                if (cmd.player != c.me() && std::find(c.cfg.botSlots.begin(), c.cfg.botSlots.end(), int(cmd.player)) == c.cfg.botSlots.end()) { any = true; break; }
            wait = !any;
        }
        if (!wait) {
            if (ai.bAwait_.raw < 0) ai.bAwait_ = fxMax(p.firstActionDelay, Fixed{});
            ai.bAwait_ -= c.dt;
            if (ai.bAwait_.raw > 0) wait = true;
            else ai.bReady_ = true;
        }
    }
    if (!wait) {
        ai.bScan_ -= c.dt;
        if (ai.bScan_.raw <= 0 || ai.bScanData_.empty()) { ai.bScan_ = p.analyzePeriod; scan(c); }
        ai.bDefense_ -= c.dt;
        if (ai.bDefense_.raw <= 0) {
            ai.bDefense_ = p.defense.delay;
            ai.bDefense_ += ai.rng_.unit() * (ai.bDefense_ / 2);
            if (ai.enableDefense_) for (auto& cl : ai.bClusters_) if (clusterDefense(c, cl)) break;
            doSupport(c);
        }
        ai.bAttack_ -= c.dt;
        if (ai.bAttack_.raw <= 0) {
            ai.bAttack_ = p.attack.delay;
            ai.bAttack_ += ai.rng_.unit() * (ai.bAttack_ / 2);
            if (attackCountValid(c) && (ai.enableAttack_ || ai.hasObjSources_)) {
                if (ai.bClusters_.size() == 1) clusterAttack(c, ai.bMine_);
                else for (auto& cl : ai.bClusters_) if (clusterAttack(c, cl)) break;
            }
        }
        ai.bUpgrade_ -= c.dt;
        if (ai.bUpgrade_.raw <= 0) { ai.bUpgrade_ = p.upgradePeriod; doUpgrade(c); }
        ai.bError_ = fxClamp(ai.bError_ + c.dt * p.errorPercentPerSecond / 100, Fixed{}, kOne);
    }
    runTasks(c);
}

} // namespace aiverse::game
