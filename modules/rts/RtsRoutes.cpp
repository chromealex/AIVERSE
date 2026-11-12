// Bunker Wars routes, groups and multiscreen on the generic RTS module (docs/game-format.md § Routes):
// - map "routes" replace the generated lanes: each route becomes `lanes.routes.lines` lanes (BW sub-splines) between
//   its two garrisons; pairs without a route cannot send (BW RoutesModule.FindUnlocked); blockers close a route.
// - rules "lanes.groups": sends become BW groups (UnitsModule Group): rows spawn as the row in front walks
//   rowSpacing, the garrison pays per row, a second send on the same route merges, an owner change closes it.
// - map "screen": BW MultiscreenModule; garrisons off the screen do not grow, scripts move the screen.
#include "RtsInternal.h"
#include "RtsEnv.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;

void io(Archive& a, RouteWalk& r) { a(r.base, r.jitter, r.alpha); }
void io(Archive& a, SendGroup& g) { a(g.id, g.from, g.to, g.owner, g.need, g.spawned, g.rowDist, g.flying); }

namespace {
std::vector<int> intList(const Json& j) {
    std::vector<int> out;
    for (auto& v : j.items()) out.push_back(int(v.integer()));
    return out;
}
} // namespace

void RtsModule::setupRoutes(Simulation& sim, const Json& map, const std::vector<Entity>& placed) {
    World& w = sim.world();
    GameState& gs = state(sim);
    RouteState& rs = gs.routes;
    routeDefs_.clear(); routeLines_.clear(); routeAnchors_.clear(); routeBlockers_.clear(); routeCoverage_.clear();
    screen_ = parseScreen(map["screen"], gs.grid.w, gs.grid.h);
    const Json& ents = map["entities"];
    auto find = [&](const std::string& id, int route) -> int {
        if (!id.empty() && id[0] == '#') {
            const int i = std::atoi(id.c_str() + 1);
            if (i >= 0 && size_t(i) < placed.size()) return i;
        } else
            for (size_t i = 0; i < ents.size() && i < placed.size(); ++i) if (ents[i].getStr("id") == id) return int(i);
        throw std::runtime_error("map route " + std::to_string(route) + ": unknown entity '" + id + "'");
    };
    const Json& routes = map["routes"];
    if (!routes.isNull() && !routes.isArray()) throw std::runtime_error("map routes: expected an array");
    for (size_t i = 0; i < routes.size(); ++i) {
        RouteDef r = parseRoute(routes[i], int(i));
        const int a = find(r.from, int(i)), b = find(r.to, int(i));
        if (!w.has<Garrison>(placed[size_t(a)]) || !w.has<Garrison>(placed[size_t(b)]))
            throw std::runtime_error("map route " + std::to_string(i) + ": '" + r.from + "' and '" + r.to + "' must both be garrison structures");
        // BW RoutesModule.Init: a route between points of different screen groups does not exist
        if (!screenGroupsMatch(intList(ents[size_t(a)]["screenGroups"]), intList(ents[size_t(b)]["screenGroups"]))) continue;
        for (auto& d : routeDefs_)
            if ((d.from == r.from && d.to == r.to) || (d.from == r.to && d.to == r.from))
                throw std::runtime_error("map route " + std::to_string(i) + ": a second route between '" + r.from + "' and '" + r.to + "'");
        std::vector<Entity> blockers;
        for (auto& id : r.blockers) blockers.push_back(placed[size_t(find(id, int(i)))]);
        // BW MapRoutesModule.GetTowersCoverage: 21 values, the stored ones aligned to the end
        std::vector<std::pair<Entity, std::array<Fixed, 21>>> cov;
        for (auto& [id, d] : r.coverage) {
            std::array<Fixed, 21> v{};
            for (size_t k = 0; k < d.size(); ++k) v[21 - d.size() + k] = d[k];
            cov.push_back({placed[size_t(find(id, int(i)))], v});
        }
        routeLines_.push_back(buildRouteLines(r, rules_.routes));
        routeAnchors_.push_back(w.get<Transform>(placed[size_t(a)]).pos);
        routeAnchors_.push_back(w.get<Transform>(placed[size_t(b)]).pos);
        rs.ends.push_back(placed[size_t(a)]);
        rs.ends.push_back(placed[size_t(b)]);
        routeBlockers_.push_back(std::move(blockers));
        routeCoverage_.push_back(std::move(cov));
        routeDefs_.push_back(std::move(r));
    }
    if (!routeDefs_.empty()) rules_.lanes.enabled = true;   // routes are lanes: walkers, skills along lanes, the view
    rs.active = !routes.isNull() || screen_.enabled || rules_.routes.groups;
    rs.screenCenter = {screen_.center.x, Fixed{}, screen_.center.y};
    if (routeDefs_.empty()) return;
    // every route keeps its lanes for the whole match (stable lane indices for walkers); closed routes only refuse sends
    gs.lanes.clear(); rs.laneRoute.clear(); rs.laneScale.clear(); rs.laneFlying.clear();
    for (size_t r = 0; r < routeDefs_.size(); ++r) {
        const auto& lines = routeLines_[r];
        const Fixed len0 = polylineLength(lines[0]);
        for (size_t k = 0; k < lines.size(); ++k) {
            Lane lane{rs.ends[2 * r], rs.ends[2 * r + 1], routeLineOffset(int(k)), {}};
            for (Vec2 p : lines[k]) lane.points.push_back({p.x, Fixed{}, p.y});
            gs.lanes.push_back(std::move(lane));
            rs.laneRoute.push_back(int32_t(r));
            rs.laneScale.push_back(len0.raw > 0 ? polylineLength(lines[k]) / len0 : kOne);
            rs.laneFlying.push_back(0);
        }
    }
    rs.routeOpen.assign(routeDefs_.size(), 1);
    rebuildRoutes(sim);
}

// Route ends (a garrison replaced in place keeps its routes) and open flags; lane geometry never changes.
void RtsModule::rebuildRoutes(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = state(sim);
    RouteState& rs = gs.routes;
    uint32_t h = 2166136261u;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= uint32_t(v >> (8 * i)) & 0xFF; h *= 16777619u; } };
    for (size_t i = 0; i < rs.ends.size(); ++i) {
        Entity& e = rs.ends[i];
        if (!(w.alive(e) && w.has<Garrison>(e))) {
            Entity best = kNullEntity;
            int cx, cy; gs.grid.cellXY(routeAnchors_[i], cx, cy);
            w.each<Garrison, Footprint>([&](Entity g, Garrison&, Footprint& f) {
                if (!best.valid() && cx >= f.x && cy >= f.y && cx < f.x + f.w && cy < f.y + f.h) best = g;
            });
            e = best;
        }
        mix(e.packed());
    }
    for (size_t r = 0; r < routeDefs_.size(); ++r) {
        bool open = w.alive(rs.ends[2 * r]) && w.alive(rs.ends[2 * r + 1]);
        for (Entity bl : routeBlockers_[r]) if (w.alive(bl)) open = false;
        rs.routeOpen[r] = open ? 1 : 0;
        mix(open);
    }
    for (size_t l = 0; l < gs.lanes.size(); ++l) {
        if (rs.laneFlying[l]) continue;
        const size_t r = size_t(rs.laneRoute[l]);
        gs.lanes[l].a = rs.ends[2 * r]; gs.lanes[l].b = rs.ends[2 * r + 1];
    }
    if (h == rs.signature) return;
    rs.signature = h;
    gs.laneSignature = h ^ 0x9e3779b9u ^ uint32_t(gs.lanes.size());
    gs.lanesDirty = true;
    sim.emit({kEvLanesChanged, 0, kNullEntity, kNullEntity, int64_t(gs.lanes.size()), {}});
}

// sysLanes for maps with routes: ends / blockers every 20 ticks; finished walkers dropped every tick
void RtsModule::sysRoutes(Simulation& sim) {
    World& w = sim.world();
    RouteState& rs = state(sim).routes;
    for (auto it = rs.walks.begin(); it != rs.walks.end();) {
        Entity e; e.index = uint32_t(it->first & 0xFFFFFFFFu); e.gen = uint32_t(it->first >> 32);
        if (!w.alive(e) || !w.has<LaneWalker>(e)) it = rs.walks.erase(it); else ++it;
    }
    if (sim.tick() % 20 == 0) rebuildRoutes(sim);
}

int RtsModule::routeIndex(Simulation& sim, Entity a, Entity b) const {
    const RouteState& rs = sim.world().resource<GameState>()->routes;
    for (size_t r = 0; r < routeDefs_.size(); ++r)
        if ((rs.ends[2 * r] == a && rs.ends[2 * r + 1] == b) || (rs.ends[2 * r] == b && rs.ends[2 * r + 1] == a)) return int(r);
    return -1;
}

std::vector<int> RtsModule::routeLanes(Simulation& sim, Entity from, Entity to, bool flying) const {
    if (routeDefs_.empty() && !flying) return lanesBetween(sim, from, to);
    const GameState& gs = *sim.world().resource<GameState>();
    const RouteState& rs = gs.routes;
    std::vector<int> out;
    if (!flying) {
        const int r = routeIndex(sim, from, to);
        if (r < 0 || !rs.routeOpen[size_t(r)]) return out;
    }
    for (int l : lanesBetween(sim, from, to))
        if (size_t(l) < rs.laneFlying.size() ? bool(rs.laneFlying[size_t(l)]) == flying : !flying) out.push_back(l);
    return out;
}

std::vector<int> RtsModule::flyingLanes(Simulation& sim, Entity from, Entity to) {
    std::vector<int> have = routeLanes(sim, from, to, true);
    if (!have.empty()) return have;
    World& w = sim.world();
    GameState& gs = state(sim);
    RouteState& rs = gs.routes;
    if (rs.laneFlying.size() < gs.lanes.size()) {   // generated lanes before: mark them ground
        rs.laneFlying.resize(gs.lanes.size(), 0); rs.laneRoute.resize(gs.lanes.size(), -1); rs.laneScale.resize(gs.lanes.size(), kOne);
    }
    const Vec3 a = w.get<Transform>(from).pos, b = w.get<Transform>(to).pos;
    const RouteDef fr = flyingRoute({a.x, a.z}, {b.x, b.z}, rules_.routes);   // BW RoutesModule.CreateFlyingRoute
    const auto lines = buildRouteLines(fr, rules_.routes);
    const Fixed len0 = polylineLength(lines[0]);
    for (size_t k = 0; k < lines.size(); ++k) {
        Lane lane{from, to, routeLineOffset(int(k)), {}};
        for (Vec2 p : lines[k]) lane.points.push_back({p.x, Fixed{}, p.y});
        gs.lanes.push_back(std::move(lane));
        rs.laneRoute.push_back(-1);
        rs.laneScale.push_back(len0.raw > 0 ? polylineLength(lines[k]) / len0 : kOne);
        rs.laneFlying.push_back(1);
    }
    gs.laneSignature ^= 0x5bd1e995u;   // the view rebuilds its lane ribbons
    gs.lanesDirty = true;
    return routeLanes(sim, from, to, true);
}

bool RtsModule::sendsFlying(Simulation& sim, Entity from) const {
    if (!rules_.advancedEffects || !(statusFlags(sim, from) & kStFlying)) return false;
    // one flying group at a time (BW PointsFlyingUnits.flyGroup)
    for (const auto& g : sim.world().resource<GameState>()->routes.groups) if (g.from == from && g.flying) return false;
    return true;
}

bool RtsModule::canSend(Simulation& sim, Entity from, Entity to) const {
    if (sendsFlying(sim, from)) return true;   // BW FindFlyingRoute ignores ground routes and locks
    if (routeDefs_.empty()) return true;
    return !routeLanes(sim, from, to, false).empty();
}

Fixed RtsModule::routeLength(Simulation& sim, Entity from, Entity to) const {
    const GameState& gs = *sim.world().resource<GameState>();
    auto lanes = routeLanes(sim, from, to, false);
    if (lanes.empty()) return Fixed::fromInt(-1);
    Fixed l{};
    const auto& pts = gs.lanes[size_t(lanes[0])].points;
    for (size_t i = 1; i < pts.size(); ++i) l += dist2D(pts[i], pts[i - 1]);
    return l;
}

Fixed RtsModule::routeAiLength(Simulation& sim, Entity from, Entity to) const {
    const int r = routeIndex(sim, from, to);
    return r < 0 ? Fixed::fromInt(-1) : game::routeAiLength(routeDefs_[size_t(r)]);
}

Fixed RtsModule::towerCoverage(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range) const {
    const int r = routeIndex(sim, from, to);
    if (r < 0) return Fixed{};
    for (auto& [t, v] : routeCoverage_[size_t(r)]) {
        if (t != tower) continue;
        const Fixed step = rules_.routes.coverageStep;
        if (range.raw <= 0) return v[0];
        if (range >= step * 20) return v[20];
        const int idx = std::clamp(int((range / step).floorInt()), 0, 19);
        const Fixed k = (range - step * idx) / step;
        return v[size_t(idx)] + (v[size_t(idx + 1)] - v[size_t(idx)]) * k;   // Mathf.Lerp
    }
    return Fixed{};
}

int RtsModule::towerUnitsDamage(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range, Fixed firePerSecond, int count, Fixed rowDistance, Fixed unitSpeed) const {
    Fixed covered = towerCoverage(sim, from, to, tower, range);
    if (covered.raw <= 0 || firePerSecond.raw <= 0 || unitSpeed.raw <= 0) return 0;
    const Fixed cooldown = kOne / firePerSecond;
    covered += rowDistance * std::max(0, (count - 1) / 8);
    const Fixed duration = covered / unitSpeed;
    return int(-((Fixed{} - duration / cooldown).floorInt()));   // Mathf.CeilToInt
}

int RtsModule::towerDamageBySpline(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range, Fixed firePerSecond, Fixed waveLength, Fixed unitSpeed) const {
    const Fixed covered = towerCoverage(sim, from, to, tower, range);
    if (covered.raw <= 0 || firePerSecond.raw <= 0) return 0;
    const Fixed cooldown = kOne / firePerSecond;
    const Fixed speed = unitSpeed - waveLength * cooldown / 8;
    if (speed.raw <= 0) return 1000000;
    return int((covered / speed).floorInt());
}

// ---------------------------------------------------------------- walkers: random offset + end fade (BW Unit.Move)

void RtsModule::startRouteWalk(Simulation& sim, Entity unit, int lane) {
    GameState& gs = state(sim);
    if (routeDefs_.empty() && (size_t(lane) >= gs.routes.laneFlying.size() || !gs.routes.laneFlying[size_t(lane)])) return;
    RouteWalk rw;
    rw.base = sim.world().get<Transform>(unit).pos;
    // BW: Vector2.one * random(-1, 1) * positionRandomOffset (one value on both axes)
    rw.jitter = (sim.rng().unit() * 2 - kOne) * rules_.routes.jitter;
    rw.alpha = Fixed{};
    gs.routes.walks[unit.packed()] = rw;
}

Fixed RtsModule::routeAlpha(Simulation& sim, Entity unit) const {
    const auto& walks = sim.world().resource<GameState>()->routes.walks;
    auto it = walks.find(unit.packed());
    return it == walks.end() ? kOne : it->second.alpha;
}

// kCmdSend with lanes.groups: BW UnitsModule.Go -> FindGroup / AddNeedCount or CreateGroup
bool RtsModule::sendAlongRoute(Simulation& sim, Entity from, Entity to, uint8_t player, int n) {
    World& w = sim.world();
    GameState& gs = state(sim);
    auto& g = w.get<Garrison>(from);
    for (auto& sg : gs.routes.groups)
        if (sg.from == from && sg.to == to && sg.owner == player && !sg.flying && !sendsFlying(sim, from)) {
            sg.need = std::min(sg.need + n - sg.spawned, g.count) + sg.spawned;   // BW UnitGroup.AddNeedCount
            return true;
        }
    SendGroup sg;
    sg.id = gs.nextGroupId++;
    sg.from = from; sg.to = to; sg.owner = player; sg.need = n;
    sg.flying = sendsFlying(sim, from);
    if (sg.flying) sg.need = g.count;   // BW PointsFlyingUnits: the fly group takes the whole garrison (maxValue 0)
    sg.rowDist = sg.flying ? rules_.routes.flyRowSpacing : rules_.routes.rowSpacing;   // the first row leaves at once
    gs.routes.groups.push_back(sg);
    g.pendingGroup = sg.id;
    return true;
}

// BW UnitGroup.AdvanceTick: spawn rows, pay per row, close on owner change / shortage / done
void RtsModule::sysSendGroups(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = state(sim);
    auto& groups = gs.routes.groups;
    const Fixed dt = sim.dt();
    std::vector<SendGroup> keep;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        SendGroup sg = groups[gi];
        if (!w.alive(sg.from) || !w.alive(sg.to) || !w.has<Garrison>(sg.from) || w.get<Owner>(sg.from).player != sg.owner) continue;
        const UnitType& ft = rules_.types[w.get<Unit>(sg.from).type];
        if (ft.spawnUnit == kNoType) continue;
        // a group keeps its route once sent (BW: locks only refuse new sends)
        std::vector<int> lanes;
        if (sg.flying) lanes = flyingLanes(sim, sg.from, sg.to);
        else for (int l : lanesBetween(sim, sg.from, sg.to)) if (size_t(l) >= gs.routes.laneFlying.size() || !gs.routes.laneFlying[size_t(l)]) lanes.push_back(l);
        if (lanes.empty()) continue;
        Garrison& g = w.get<Garrison>(sg.from);
        bool closed = false;
        const Fixed spacing = sg.flying ? rules_.routes.flyRowSpacing : rules_.routes.rowSpacing;
        if (sg.spawned < sg.need) {
            if (sg.rowDist >= spacing) {
                const int perRow = std::min<int>(rules_.routes.lines, int(lanes.size()));
                const int toAlloc = std::min(perRow, sg.need - sg.spawned);
                const int alloc = std::clamp(std::min(toAlloc, g.count), 0, toAlloc);   // BW PointInfo.Allocate
                g.count -= alloc;
                const Vec3 to = w.get<Transform>(sg.to).pos;
                for (int k = 0; k < alloc; ++k) {
                    const int lane = lanes[size_t(k)];
                    const Lane& L = gs.lanes[size_t(lane)];
                    const bool reverse = L.a != sg.from;
                    const Vec3 start = L.points[reverse ? L.points.size() - 1 : 0];
                    Entity n = spawn(sim, ft.spawnUnit, sg.owner, start);
                    if (!w.has<Orders>(n)) continue;
                    w.add<LaneWalker>(n, LaneWalker{lane, 0, reverse});
                    startRouteWalk(sim, n, lane);
                    tagReleased(sim, n, sg.id, sg.from, sg.to);
                    Order ord;
                    ord.type = OrderType::Enter; ord.target = sg.to; ord.point = flat(to); ord.state = 3;   // 3 = lane walk
                    issue(sim, n, ord, false);
                }
                sg.spawned += alloc;
                if (alloc < toAlloc || alloc < perRow || sg.spawned >= sg.need) closed = true;
                sg.rowDist = Fixed{};
            }
        } else closed = true;
        if (closed) continue;
        const uint16_t ut = ft.spawnUnit;
        sg.rowDist += typeStat(sim, sg.owner, ut, Stat::Speed, rules_.types[ut].speed) * dt;
        keep.push_back(sg);
    }
    groups = std::move(keep);
}

// ---------------------------------------------------------------- multiscreen

Vec3 RtsModule::screenCenter(Simulation& sim) const { return sim.world().resource<GameState>()->routes.screenCenter; }

bool RtsModule::onScreen(Simulation& sim, Vec3 p) const {
    if (!screen_.enabled) {   // the environment's map scroller screen (RtsEnv.cpp), else everything is on screen
        if (const Environment* ev = environment(); ev && ev->def().screen.enabled) return ev->insideScreen(sim, p);
        return true;
    }
    const Vec3 c = screenCenter(sim);
    return fxAbs(p.x - c.x) <= screen_.size.x / 2 && fxAbs(p.z - c.z) <= screen_.size.y / 2;   // BW IsPointInsideScreen
}

bool RtsModule::screenView(Simulation& sim, Vec3& center, Vec2& size) const {
    if (screen_.enabled) { center = screenCenter(sim); size = screen_.size; return true; }
    if (const Environment* ev = environment(); ev && ev->def().screen.enabled)
        if (const EnvState* st = ev->state(sim)) {
            const Vec3 s = ev->def().screen.size;
            center = st->screenPos + s / Fixed::fromInt(2);
            size = {s.x, s.z};
            return true;
        }
    return false;
}

void RtsModule::setScreenCenter(Simulation& sim, Vec3 c) {
    if (!screen_.enabled) {
        if (Environment* ev = environment(); ev && ev->def().screen.enabled)
            if (EnvState* st = ev->state(sim)) st->screenPos = flat(c) - ev->def().screen.size / Fixed::fromInt(2);
        return;
    }
    Vec3& sc = state(sim).routes.screenCenter;
    sc.x = fxClamp(c.x, screen_.center.x - screen_.left, screen_.center.x + screen_.right);
    sc.z = fxClamp(c.z, screen_.center.y - screen_.top, screen_.center.y + screen_.bottom);
    sc.y = Fixed{};
}

// ---------------------------------------------------------------- save / hash

void RtsModule::ioRoutes(Archive& a, GameState& gs) {
    RouteState& rs = gs.routes;
    if (!rs.active) return;
    a(rs.ends, rs.laneRoute, rs.laneScale, rs.laneFlying, rs.routeOpen, rs.groups, rs.signature, rs.screenCenter, rs.walks);
}

void RtsModule::hashRoutes(Simulation& sim, Hasher& h) {
    const RouteState& rs = state(sim).routes;
    if (!rs.active) return;
    h.u32(rs.signature);
    for (Entity e : rs.ends) h.u64(e.packed());
    for (const auto& g : rs.groups) { h.u32(g.id); h.u64(g.from.packed()); h.u64(g.to.packed()); h.i32(g.need); h.i32(g.spawned); h.fx(g.rowDist); }
    h.v3(rs.screenCenter);
    for (uint8_t o : rs.routeOpen) h.u32(o);
    h.u32(uint32_t(rs.laneFlying.size()));
    for (const auto& [k, v] : rs.walks) { h.u64(k); h.v3(v.base); h.fx(v.jitter); }
}

} // namespace aiverse::game
