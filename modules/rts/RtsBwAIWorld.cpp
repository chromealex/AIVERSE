// Bunker Wars computer player: the battle as BW sees it (points, players, groups in transit), route lengths and
// tower coverage along the routes, commands. Everything here only reads the simulation.
#include "RtsBwAIInternal.h"

#include <algorithm>
#include <map>

namespace aiverse::game {

namespace {
Fixed len2(Vec3 a, Vec3 b) { return (a - b).xz().length(); }
}

// Player-wide stat of a type (no entity): RtsModule::stat without the per-entity parts. moraleDelta adds that many
// levels of the perUnit resources (morale), clamped to their storage (BW GetFullDefenseCoefficientForMorale).
Fixed RtsModule::bwTypeStat(Simulation& sim, uint8_t owner, uint16_t type, Stat s, int moraleDelta) const {
    const UnitType& t = rules_.types[type];
    const Fixed kOne = Fixed::fromInt(1);
    Fixed base = s == Stat::Speed ? t.speed : s == Stat::GarrisonCap ? Fixed::fromInt(t.garrisonMax) : s == Stat::Range ? Fixed{} : kOne;
    auto* gs = sim.world().resource<GameState>();
    Fixed add{}, mul = kOne;
    if (owner < gs->players.size() && !gs->players[owner].mods.empty()) {
        const TypeMods& m = gs->players[owner].mods[type];
        add += m.add[size_t(s)]; mul *= m.mul[size_t(s)];
    }
    if (rules_.advancedEffects && owner < gs->players.size())
        for (auto& in : gs->players[owner].statuses)
            for (auto& m : rules_.statuses[in.def].mods)
                if (m.stat == s) { add += m.add * in.stacks; for (int k = 0; k < in.stacks; ++k) mul *= m.mul; }
    if (rules_.originals) {
        add += playerModsStat(sim, owner, type, s, mul);
        if (moraleDelta != 0 && owner != 0 && owner < gs->players.size())
            for (uint16_t r : rules_.perUnitResources) {
                const auto& eco = gs->players[owner];
                const int n = int(fxMax(Fixed{}, eco.stock[r]).floorInt());
                Fixed cap = eco.cap[r].raw > 0 ? eco.cap[r] : rules_.resources[r].storage;
                int to = std::max(0, n + moraleDelta);
                if (cap.raw > 0) to = std::min(to, int(cap.floorInt()));
                const int extra = to - n;
                if (extra <= 0) continue;
                for (auto& [tags, mod] : rules_.resources[r].perUnitMods) {
                    if (mod.stat != s || (tags && !(t.tags & tags))) continue;
                    add += mod.add * extra;
                    for (int k = 0; k < extra; ++k) mul *= mod.mul;
                }
            }
    }
    return (base + add) * mul;
}

int BwWorld::pointOf(Entity e) const {
    for (size_t i = 0; i < pts.size(); ++i) if (pts[i].e == e) return int(i);
    return -1;
}

BwWorld& BwAI::Ctx::world() {
    if (!built) { built = true; BwImpl::buildWorld(*this, store); }   // (built first: the builder asks for routes)
    return store;
}

bool BwAI::Ctx::ally(uint8_t a, uint8_t b) {
    if (a == b) return true;
    if (a == 0 || b == 0 || a == kHostileNeutral || b == kHostileNeutral) return false;
    return sim.allied(a, b);
}

void BwImpl::refreshRoutes(Ctx& c) {
    BwRoutes& rt = c.ai.routes_;
    GameState& gs = c.m.state(c.sim);
    std::vector<Entity> nodes;
    c.sim.world().each<Garrison, Unit>([&](Entity e, Garrison&, Unit&) { nodes.push_back(e); });
    std::sort(nodes.begin(), nodes.end(), [](Entity a, Entity b) { return a.index < b.index; });
    const bool lanes = c.R.lanes.enabled;
    if (!rt.len.empty() && nodes == rt.nodes && rt.lanes == lanes && rt.lanesCount == gs.lanes.size() &&
        (!lanes || (rt.signature == gs.laneSignature && !gs.lanesDirty))) return;
    rt = BwRoutes{};
    rt.nodes = nodes; rt.lanes = lanes; rt.signature = gs.laneSignature; rt.lanesCount = gs.lanes.size();
    const size_t n = nodes.size();
    rt.len.assign(n * n, Fixed::fromInt(-1));
    rt.geo.assign(n * n, Fixed::fromInt(-1));
    rt.poly.assign(n * n, {});
    rt.cover.assign(n * n * n, -1);
    rt.roadsInRange.assign(n, -1);
    rt.laneCum.resize(gs.lanes.size());
    for (size_t l = 0; l < gs.lanes.size(); ++l) {
        auto& cum = rt.laneCum[l];
        const auto& pts = gs.lanes[l].points;
        cum.assign(pts.size(), Fixed{});
        for (size_t k = 1; k < pts.size(); ++k) cum[k] = cum[k - 1] + len2(pts[k - 1], pts[k]);
    }
    World& w = c.sim.world();
    // first lane per pair = RtsModule::lanesBetween()[0] (smallest |offset|, then lane order), in one pass over the
    // lanes: per pair calls are O(pairs x lanes), minutes on the 99-point / 2615-route BW 8-player map
    std::map<std::pair<uint32_t, uint32_t>, int> first;
    if (lanes)
        for (size_t li = 0; li < gs.lanes.size(); ++li) {
            const Lane& l = gs.lanes[li];
            const auto key = std::minmax(l.a.index, l.b.index);
            auto it = first.find(key);
            if (it == first.end()) first.emplace(key, int(li));
            else if (std::abs(l.offset) < std::abs(gs.lanes[size_t(it->second)].offset)) it->second = int(li);
        }
    const auto& defs = c.m.routeDefs();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            std::vector<Vec3> pl;
            Fixed ai = Fixed::fromInt(-1);
            if (lanes) {
                const auto it = first.find(std::minmax(nodes[i].index, nodes[j].index));
                if (it == first.end()) continue;   // no lane: no route (BW Routes.Find == null)
                const Lane& l = gs.lanes[size_t(it->second)];
                if (!((l.a == nodes[i] && l.b == nodes[j]) || (l.a == nodes[j] && l.b == nodes[i]))) continue;   // stale generation
                pl = l.points;
                // authored routes: BW Route.GetAILength (control points, slowdown segments stretched)
                if (!defs.empty() && size_t(it->second) < gs.routes.laneRoute.size()) {
                    const int r = gs.routes.laneRoute[size_t(it->second)];
                    if (r >= 0 && size_t(r) < defs.size()) ai = routeAiLength(defs[size_t(r)]);
                }
            } else {
                pl = {w.get<Transform>(nodes[i]).pos, w.get<Transform>(nodes[j]).pos};
            }
            Fixed L{};
            for (size_t k = 1; k < pl.size(); ++k) L += len2(pl[k - 1], pl[k]);
            rt.geo[i * n + j] = rt.geo[j * n + i] = L;
            rt.len[i * n + j] = rt.len[j * n + i] = ai.raw >= 0 ? ai : L;
            rt.poly[i * n + j] = pl;
        }
}

void BwImpl::buildWorld(Ctx& c, BwWorld& W) {
    refreshRoutes(c);
    World& w = c.sim.world();
    const Rules& R = c.R;
    GameState& gs = c.m.state(c.sim);
    const bool adv = R.advancedEffects, statDef = R.advancedEffects || R.originals;
    W = BwWorld{};
    W.tick = c.sim.tick();
    W.time = Fixed::fromInt(int64_t(c.sim.tick())) / int64_t(c.sim.tickRate());
    W.releaseRate = Fixed::fromInt(2 * int64_t(c.sim.tickRate()));   // sysGarrison streams 2 units per tick out of a garrison
    W.rows = R.routes.groups;                                          // BW groups: rows walk rowSpacing apart
    W.rowDistance = W.rows ? R.routes.rowSpacing : c.px(70);           // BW BattleParameters units.rowsOffset
    W.mapW = Fixed::fromInt(std::max(1, gs.grid.w)); W.mapH = Fixed::fromInt(std::max(1, gs.grid.h));
    const size_t nPlayers = std::max<size_t>(gs.players.size(), 2);
    W.players.assign(std::max<size_t>(nPlayers, 256), BwPlayer{});
    uint16_t anyUnit = kNoType;
    for (size_t t = 0; t < R.types.size() && anyUnit == kNoType; ++t) if (R.types[t].entersGarrison) anyUnit = uint16_t(t);
    for (auto& p : c.sim.players()) {
        BwPlayer& bp = W.players[p.slot];
        bp.alive = p.alive; bp.neutral = false; bp.team = p.team; bp.unitType = anyUnit;
    }
    // points in entity order (= map order: the n-th garrison structure of the map is BW point n)
    for (Entity e : c.ai.routes_.nodes) {
        if (!w.alive(e) || !w.has<Garrison>(e)) continue;
        const Unit& u = w.get<Unit>(e);
        const UnitType& t = R.types[u.type];
        const Garrison& g = w.get<Garrison>(e);
        BwPoint p;
        p.e = e; p.type = u.type; p.owner = w.get<Owner>(e).player; p.level = t.level;
        p.role = bwRoleOf(R, c.cfg, u.type);
        p.value = g.count; p.pendingOut = g.pendingOut;
        p.pos = w.get<Transform>(e).pos;
        p.onScreen = c.m.onScreen(c.sim, p.pos);
        {   // inScreenBattleTime: seconds since the point came onto the screen (multiscreen maps scroll)
            auto& since = c.ai.screenSince_;
            auto it = std::find_if(since.begin(), since.end(), [&](auto& s) { return s.first == e; });
            if (!p.onScreen) { if (it != since.end()) since.erase(it); }
            else {
                if (it == since.end()) { since.push_back({e, c.sim.tick() == 0 || !c.m.screen().enabled ? 0u : c.sim.tick()}); it = since.end() - 1; }
                p.screenTime = Fixed::fromInt(int64_t(c.sim.tick() - it->second)) / int64_t(c.sim.tickRate());
            }
        }
        if (const auto& pc = gs.modes.pointProduced; e.index < pc.size()) p.produced = int(pc[e.index]);
        if (const auto& pk = gs.modes.pointKills; e.index < pk.size()) p.kills = int(pk[e.index]);
        p.max = adv ? int(c.m.stat(c.sim, e, Stat::GarrisonCap).floorInt()) : t.garrisonMax;
        p.def = t.garrisonDefense * (statDef ? c.m.stat(c.sim, e, Stat::Defense) : Fixed::fromInt(1));
        p.defNext = p.def;
        if (R.originals && p.owner != 0) {
            const Fixed now = c.m.bwTypeStat(c.sim, p.owner, u.type, Stat::Defense, 0);
            if (now.raw > 0) p.defNext = p.def * c.m.bwTypeStat(c.sim, p.owner, u.type, Stat::Defense, 1) / now;
        }
        if (t.spawnInterval.raw > 0 && p.owner != 0)
            p.prod = (adv ? c.m.stat(c.sim, e, Stat::ProductionSpeed) : Fixed::fromInt(1)) / t.spawnInterval;
        p.dom = c.cfg.dominationTags && (t.tags & c.cfg.dominationTags);
        p.koth = c.cfg.kothTags && (t.tags & c.cfg.kothTags);
        for (auto& mp : gs.modes.points) if (mp.e == e) { p.dom = p.dom || mp.kind == 1; p.koth = p.koth || mp.kind == 2; }   // match modes
        if (!t.weapons.empty()) {
            p.tower = true;
            const WeaponDef& wd = t.weapons[0];
            p.towerRange = wd.range + c.m.stat(c.sim, e, Stat::Range);
            const Fixed as = c.m.stat(c.sim, e, Stat::AttackSpeed);
            p.towerCooldown = as.raw > 0 ? wd.cooldown / as : wd.cooldown;
            const Fixed hp = anyUnit != kNoType ? fxMax(R.types[anyUnit].maxHp, Fixed::fromRatio(1, 100)) : Fixed::fromInt(1);
            p.towerKills = fxMax(Fixed::fromRatio(1, 100), wd.damage * c.m.stat(c.sim, e, Stat::Damage) / hp);
            if (p.towerCooldown.raw <= 0) p.towerCooldown = Fixed::fromRatio(1, 10);
        }
        if (auto* pr = w.tryGet<Production>(e)) for (auto& it : pr->queue) if (it.upgrade) p.upgrading = true;
        // upgrade chain: next level of the same role, conversions into the other roles (lowest level)
        for (uint16_t up : t.upgrades) {
            const UnitType& ut = R.types[up];
            const BwRole r = bwRoleOf(R, c.cfg, up);
            if (r == p.role && ut.level == t.level + 1) { p.nextLevel = up; p.upgradeCost = ut.upgradeGarrison; }
            else if (r != p.role) {
                const int cur = p.convertTo[int(r)];
                if (cur < 0 || ut.level < R.types[size_t(cur)].level) { p.convertTo[int(r)] = up; p.convertCost[int(r)] = ut.upgradeGarrison; }
            }
        }
        if (p.owner < W.players.size() && p.owner != 0) {
            BwPlayer& bp = W.players[p.owner];
            bp.units += g.count + g.pendingOut;
            bp.maxUnits += p.max;
            if (t.spawnUnit != kNoType && (bp.unitType == anyUnit || bp.unitType == kNoType)) bp.unitType = t.spawnUnit;
        }
        W.pts.push_back(p);
    }
    // own commands the simulation has not executed yet
    for (auto& pd : c.ai.pending_) {
        if (pd.until < c.sim.tick()) continue;
        const int i = W.pointOf(pd.e);
        if (i < 0) continue;
        W.pts[size_t(i)].value = std::max(0, W.pts[size_t(i)].value - pd.units);
        if (pd.upgrade) W.pts[size_t(i)].upgrading = true;
    }
    // players: coefficients
    for (size_t s = 1; s < W.players.size(); ++s) {
        BwPlayer& bp = W.players[s];
        if (bp.neutral || bp.unitType == kNoType) continue;
        bp.damage = R.originals ? c.m.bwTypeStat(c.sim, uint8_t(s), bp.unitType, Stat::Damage, 0) : Fixed::fromInt(1);
        bp.speed = c.m.bwTypeStat(c.sim, uint8_t(s), bp.unitType, Stat::Speed, 0);
        if (bp.speed.raw <= 0) bp.speed = Fixed::fromInt(1);
        for (uint16_t src : R.playerModTypes) {
            int n = src < gs.players[s].typeCount.size() && s < gs.players.size() ? gs.players[s].typeCount[src] : 0;
            if (R.types[src].playerModsMax > 0) n = std::min(n, R.types[src].playerModsMax);
            bp.smithy += n;
        }
        for (uint16_t r : R.perUnitResources) if (s < gs.players.size()) { bp.morale += gs.players[s].stock[r]; bp.moraleLevel += int(fxMax(Fixed{}, gs.players[s].stock[r]).floorInt()); }
    }
    // groups in transit: units walking into a garrison
    std::map<uint64_t, size_t> byKey;
    w.each<Unit, Owner, Orders, Transform>([&](Entity e, Unit& u, Owner& o, Orders& ord, Transform& tr) {
        if (ord.queue.empty() || ord.queue.front().type != OrderType::Enter || u.hp.raw <= 0 || !R.types[u.type].entersGarrison) return;
        const int to = W.pointOf(ord.queue.front().target);
        if (to < 0) return;
        uint64_t key; int from = -1; uint32_t id = 0;
        if (auto* gt = w.tryGet<GroupTag>(e)) { key = gt->id; id = gt->id; from = W.pointOf(gt->from); }
        else key = (uint64_t(1) << 40) | (uint64_t(o.player) << 16) | uint64_t(to);
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            BwGroup g; g.id = id; g.owner = o.player; g.from = from; g.to = to;
            it = byKey.emplace(key, W.groups.size()).first;
            W.groups.push_back(g);
        }
        BwGroup& g = W.groups[it->second];
        if (auto* f = w.tryGet<Statuses>(e); f && R.advancedEffects && (c.m.statusFlags(c.sim, e) & kStFake)) g.fake = true;
        // distance left along the lane (BW Unit.distanceToPoint walks the spline), else straight
        Fixed d = len2(tr.pos, W.pts[size_t(to)].pos);
        if (auto* lw = w.tryGet<LaneWalker>(e); lw && lw->lane >= 0 && size_t(lw->lane) < c.ai.routes_.laneCum.size()) {
            const auto& cum = c.ai.routes_.laneCum[size_t(lw->lane)];
            if (!cum.empty()) { const size_t k = std::min<size_t>(lw->pos, cum.size() - 1); d = lw->reverse ? cum[k] : cum.back() - cum[k]; }
        }
        if (g.count == 0 || d < g.minDist) g.minDist = d;
        if (g.count == 0 || d > g.maxDist) g.maxDist = d;
        ++g.count;
        g.units.push_back(tr.pos);
        g.ents.push_back(e);
        if (o.player < W.players.size() && o.player != 0) W.players[o.player].units++;
    });
    // BW groups still spawning rows (routes.groups): the rest is still inside the source
    for (auto& sg : gs.routes.groups) {
        const int from = W.pointOf(sg.from), to = W.pointOf(sg.to);
        if (from < 0 || to < 0) continue;
        auto it = byKey.find(sg.id);
        if (it == byKey.end()) {
            BwGroup ng; ng.id = sg.id; ng.owner = sg.owner; ng.from = from; ng.to = to;
            it = byKey.emplace(uint64_t(sg.id), W.groups.size()).first;
            W.groups.push_back(ng);
        }
        BwGroup& g = W.groups[it->second];
        g.pending += std::max(0, sg.need - sg.spawned);
        if (g.from < 0) g.from = from;
    }
    // units still inside their source (sent, not out yet)
    for (size_t i = 0; i < W.pts.size(); ++i) {
        const BwPoint& p = W.pts[i];
        if (p.pendingOut <= 0) continue;
        const Garrison& g = w.get<Garrison>(p.e);
        const int to = W.pointOf(g.pendingTarget);
        if (to < 0) continue;
        const uint64_t key = g.pendingGroup ? uint64_t(g.pendingGroup) : (uint64_t(1) << 40) | (uint64_t(p.owner) << 16) | uint64_t(to);
        auto it = byKey.find(key);
        if (it == byKey.end()) {
            BwGroup ng; ng.id = g.pendingGroup; ng.owner = p.owner; ng.from = int(i); ng.to = to;
            ng.minDist = ng.maxDist = len2(p.pos, W.pts[size_t(to)].pos);
            it = byKey.emplace(key, W.groups.size()).first;
            W.groups.push_back(ng);
        }
        W.groups[it->second].pending += p.pendingOut;
        if (W.groups[it->second].from < 0) W.groups[it->second].from = int(i);
    }
    const size_t nn = c.ai.routes_.nodes.size();
    for (auto& g : W.groups) {
        g.routeLen = g.from >= 0 && size_t(g.from) < nn ? c.ai.routes_.geo[size_t(g.from) * nn + size_t(g.to)] : Fixed::fromInt(-1);
        if (g.routeLen.raw < 0) g.routeLen = g.maxDist;
        if (g.count == 0) g.minDist = g.maxDist = g.routeLen;   // nothing out yet: the whole road ahead
        // still inside: the rows / the stream behind the last unit
        if (g.pending > 0) g.maxDist += W.rows ? W.rowDistance * ((g.pending + c.R.routes.lines - 1) / std::max(1, c.R.routes.lines))
                                                : W.players[g.owner].speed * Fixed::fromInt(g.pending) / W.releaseRate;
    }
    // open routes now (RoutesModule.FindUnlocked: blockers close them)
    W.open.assign(nn * nn, 0);
    for (size_t i = 0; i < W.pts.size() && i < nn; ++i)
        for (size_t j = 0; j < W.pts.size() && j < nn; ++j)
            if (i != j && c.ai.routes_.len[i * nn + j].raw >= 0) W.open[i * nn + j] = c.m.canSend(c.sim, W.pts[i].e, W.pts[j].e) ? 1 : 0;
}

Fixed BwAI::Ctx::dist(int i, int j) {
    BwWorld& W = world();
    if (i < 0 || j < 0 || size_t(i) >= W.pts.size() || size_t(j) >= W.pts.size()) return Fixed::fromInt(-1);
    if (i == j) return Fixed{};
    const BwRoutes& rt = ai.routes_;
    const size_t n = rt.nodes.size();
    // points index the same node list (both built from the garrison entities in index order)
    return rt.len[size_t(i) * n + size_t(j)];
}

bool BwAI::Ctx::route(int i, int j) {
    BwWorld& W = world();
    const size_t n = ai.routes_.nodes.size();
    if (i == j || i < 0 || j < 0 || size_t(i) >= n || size_t(j) >= n || W.open.size() < n * n) return false;
    return W.open[size_t(i) * n + size_t(j)] != 0;
}

int BwAI::Ctx::towerKillsOnRoute(int from, int to, int count, uint8_t owner, std::vector<std::pair<int, int>>* byTower) {
    BwWorld& W = world();
    if (byTower) byTower->clear();
    if (from == to || dist(from, to).raw < 0) return 0;   // Routes.Find: locked routes still count
    BwRoutes& rt = ai.routes_;
    const size_t n = rt.nodes.size();
    const size_t a = size_t(std::min(from, to)), b = size_t(std::max(from, to));
    const std::vector<Vec3>& pl = rt.poly[a * n + b];
    const Fixed speed = W.players[owner].speed;
    Fixed total{};
    for (size_t k = 0; k < W.pts.size(); ++k) {
        const BwPoint& t = W.pts[k];
        if (!t.tower || t.owner == 0 || ally(t.owner, me())) continue;
        int32_t& cv = rt.cover[(a * n + b) * n + k];
        if (cv < 0) {
            // length of the route within the tower's range, sampled every quarter cell
            const Fixed step = Fixed::fromRatio(1, 4);
            const Fixed r2 = t.towerRange * t.towerRange;
            Fixed inside{};
            for (size_t s = 1; s < pl.size(); ++s) {
                const Fixed L = len2(pl[s - 1], pl[s]);
                const int steps = std::max(1, int((L / step).toInt()));
                for (int q = 0; q < steps; ++q) {
                    const Vec3 pt = pl[s - 1] + (pl[s] - pl[s - 1]) * (Fixed::fromInt(q) / int64_t(steps));
                    if ((pt - t.pos).xz().lengthSq() <= r2) inside += L / int64_t(steps);
                }
            }
            cv = int32_t(std::min<int64_t>(inside.raw, INT32_MAX));
        }
        if (cv <= 0) continue;
        // TowerModule.GetUnitsDamageForSpline / GetDamageBySpline: one unit per shot while the group is in range
        const Fixed cov = Fixed::fromRaw(cv);
        Fixed kills;
        if (count <= 0) {
            const Fixed reverse = W.rowDistance * t.towerCooldown / 8;
            const Fixed spd = speed - reverse;
            kills = spd.raw <= 0 ? Fixed::fromInt(1000000) : Fixed::fromInt((cov / spd).floorInt());
        } else {
            const int rowsCount = std::max(0, (count - 1) / 8);
            const Fixed duration = (cov + W.rowDistance * rowsCount) / speed;
            const Fixed shots = duration / t.towerCooldown;
            kills = Fixed::fromInt((shots + Fixed::fromRaw(Fixed::kOne - 1)).floorInt());
        }
        const int ki = int(kills.floorInt());
        if (ki > 0 && byTower) byTower->push_back({int(k), ki});
        total += kills;
    }
    return int(total.floorInt());
}

int BwAI::Ctx::roadsInTowerRange(int tower) {
    BwWorld& W = world();
    BwRoutes& rt = ai.routes_;
    if (tower < 0 || size_t(tower) >= rt.roadsInRange.size()) return 0;
    int& r = rt.roadsInRange[size_t(tower)];
    if (r >= 0) return r;
    r = 0;
    const BwPoint& t = W.pts[size_t(tower)];
    const Fixed r2 = t.towerRange * t.towerRange;
    const size_t n = rt.nodes.size();
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            if (rt.len[i * n + j].raw < 0) continue;
            const auto& pl = rt.poly[i * n + j];
            bool hit = false;
            for (size_t s = 1; s < pl.size() && !hit; ++s) {
                const Fixed L = len2(pl[s - 1], pl[s]);
                const int steps = std::max(1, int((L * 2).toInt()));
                for (int q = 0; q <= steps && !hit; ++q)
                    hit = ((pl[s - 1] + (pl[s] - pl[s - 1]) * (Fixed::fromInt(q) / int64_t(steps))) - t.pos).xz().lengthSq() <= r2;
            }
            if (hit) ++r;
        }
    return r;
}

// ---- AI6Shared reducers (BW counts points without a route in the average's divisor: kept)
namespace {
template <class F> void eachPoint(BwAI::Ctx& c, int p, F&& f) { for (int q = 0; q < int(c.world().pts.size()); ++q) if (q != p) f(q); }
}
#define BW_REDUCE(name, filter, init, op, fin)                                                  \
    Fixed BwAI::Ctx::name(int p, uint8_t player) {                                              \
        Fixed acc = init; int n = 0;                                                            \
        if (screen(p)) eachPoint(*this, p, [&](int q) {                                         \
            const uint8_t o = world().pts[size_t(q)].owner;                                     \
            if (!(filter)) return;                                                              \
            ++n; const Fixed d = dist(p, q); if (d.raw < 0) return;                             \
            op;                                                                                 \
        });                                                                                     \
        fin;                                                                                    \
    }
BW_REDUCE(minDistOwn, o == player, kBwInf, acc = fxMin(acc, d), return acc == kBwInf ? Fixed{} : acc)
BW_REDUCE(maxDistOwn, o == player, Fixed{}, acc = fxMax(acc, d), return acc)
BW_REDUCE(avgDistOwn, o == player, Fixed{}, acc += d, return n > 0 ? acc / int64_t(n) : Fixed{})
BW_REDUCE(minDistEnemy, !ally(o, player) && !neutral(o), kBwInf, acc = fxMin(acc, d), return acc == kBwInf ? Fixed{} : acc)
BW_REDUCE(maxDistEnemy, !ally(o, player) && !neutral(o), Fixed{}, acc = fxMax(acc, d), return acc)
BW_REDUCE(avgDistEnemy, !ally(o, player) && !neutral(o), Fixed{}, acc += d, return n > 0 ? acc / int64_t(n) : Fixed{})
#undef BW_REDUCE

Fixed BwAI::Ctx::minDistEnemyFromOwn(uint8_t player) {
    BwWorld& W = world();
    Fixed best = kBwInf;
    for (size_t i = 0; i < W.pts.size(); ++i) {
        const uint8_t o1 = W.pts[i].owner;
        if (neutral(o1)) continue;
        for (size_t j = i; j < W.pts.size(); ++j) {
            const uint8_t o2 = W.pts[j].owner;
            if (neutral(o2)) continue;
            if ((o1 == player && !ally(o2, player)) || (o2 == player && !ally(o1, player))) {
                const Fixed d = dist(int(i), int(j));
                if (d.raw >= 0) best = fxMin(best, d);   // #AI6FIX: a pair without a route (-1) no longer wins
            }
        }
    }
    return best == kBwInf ? Fixed{} : best;
}

int BwAI::Ctx::countRole(int player, BwRole r) {
    int n = 0;
    for (auto& p : world().pts) if (p.role == r && (player < 0 || p.owner == player)) ++n;
    return n;
}

int BwAI::Ctx::objectiveCount(bool dom, int player) {
    int n = 0;
    for (auto& p : world().pts) if ((dom ? p.dom : p.koth) && (player < 0 || p.owner == player)) ++n;
    return n;
}

bool BwAI::Ctx::anyUpgrading(uint8_t player) {
    for (auto& p : world().pts) if (p.owner == player && p.upgrading) return true;
    return false;
}

namespace {
std::string fill(std::string s, int player) {
    const std::string k = "{player}";
    for (size_t at; (at = s.find(k)) != std::string::npos;) s.replace(at, k.size(), std::to_string(player));
    return s;
}
}
// King of the Hill score (match modes, BW Player.kingOfTheHillScore): the team's score; trigger variable vars.kothScore
// for games scripting their own mode.
Fixed BwAI::Ctx::kothScore(uint8_t player) {
    GameState& gs = m.state(sim);
    const uint8_t team = world().players[player].team;
    if (m.modes().koh && team < gs.modes.kohScore.size()) return gs.modes.kohScore[team];
    auto it = gs.trig.vars.find(fill(cfg.kothScoreVar, player));
    if (it != gs.trig.vars.end()) return it->second;
    return team < gs.score.size() ? gs.score[team] : Fixed{};
}
Fixed BwAI::Ctx::kothMax() {   // MapHeader.kingOfTheHillFFAScore
    if (m.modes().koh) return m.modes().kohScore;
    GameState& gs = m.state(sim);
    auto it = gs.trig.vars.find(cfg.kothMaxVar);
    return it != gs.trig.vars.end() ? it->second : Fixed::fromInt(300);
}
// Environment.IsBlizzard (map environment weather), else the trigger variable vars.blizzard != 0.
bool BwAI::Ctx::blizzard() {
    if (Environment* env = m.environment()) { const EnvState* st = env->state(sim); return st && env->def().weather.blizzard && st->blizzardState == 0; }
    GameState& gs = m.state(sim);
    auto it = gs.trig.vars.find(cfg.blizzardVar);
    return it != gs.trig.vars.end() && it->second.raw != 0;
}

// ---- commands
uint32_t BwAI::Ctx::submit(Command cmd) {
    cmd.player = me();
    return sim.submit(cmd);
}

void BwAI::Ctx::send(int from, int to, int units) {
    BwWorld& W = world();
    if (units <= 0 || from == to || from < 0 || to < 0) return;
    if (!prof.canMoveInBlizzard && blizzard()) return;
    BwImpl::prepareToSend(*this, from, to, units);
    BwPoint& s = W.pts[size_t(from)];
    Command cmd; cmd.type = kCmdSend; cmd.units = {s.e}; cmd.target = W.pts[size_t(to)].e; cmd.param = -int64_t(units);
    const uint32_t at = submit(cmd);
    ai.pending_.push_back({s.e, units, at, false});
    s.value = std::max(0, s.value - units);
}

void BwAI::Ctx::sendPercent(int from, int to, Fixed percent) {
    BwWorld& W = world();
    if (from < 0 || to < 0 || from == to) return;
    BwPoint& s = W.pts[size_t(from)];
    int n = int((Fixed::fromInt(s.value) * percent).floorInt());
    if (n == 0 && s.value > 0) n = 1;
    if (n <= 0) return;
    Command cmd; cmd.type = kCmdSend; cmd.units = {s.e}; cmd.target = W.pts[size_t(to)].e; cmd.param = -int64_t(n);
    const uint32_t at = submit(cmd);
    ai.pending_.push_back({s.e, n, at, false});
    s.value = std::max(0, s.value - n);
}

void BwAI::Ctx::upgrade(int point, int type) {
    BwWorld& W = world();
    if (point < 0 || type < 0) return;
    BwPoint& p = W.pts[size_t(point)];
    Command cmd; cmd.type = kCmdUpgrade; cmd.units = {p.e}; cmd.param = type;
    const uint32_t at = submit(cmd);
    const int cost = R.types[size_t(type)].upgradeGarrison;
    ai.pending_.push_back({p.e, cost, at, true});
    p.value = std::max(0, p.value - cost);
    p.upgrading = true;
}

bool BwAI::Ctx::cast(int ability, Vec3 point, Entity target) {
    if (!m.canUsePlayerAbility(sim, me(), ability)) return false;
    Command cmd; cmd.type = kCmdPlayerAbility; cmd.param = ability; cmd.point = point; cmd.target = target;
    submit(cmd);
    return true;
}

} // namespace aiverse::game
