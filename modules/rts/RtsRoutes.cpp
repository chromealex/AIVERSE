// Bunker Wars routes, groups and multiscreen on the generic RTS module (docs/game-format.md § Routes):
// - map "routes" replace the generated lanes: each route becomes `lanes.routes.lines` lanes (BW sub-splines) between
//   its two garrisons; pairs without a route cannot send (BW RoutesModule.FindUnlocked); blockers close a route.
// - rules "lanes.groups": sends become BW groups (UnitsModule Group): rows spawn as the row in front walks
//   rowSpacing, the garrison pays per row, a second send on the same route merges, an owner change closes it.
// - map "screen": BW MultiscreenModule; garrisons off the screen do not grow, scripts move the screen.
#include "RtsInternal.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;

void io(Archive& a, SendGroup& g) { a(g.id, g.from, g.to, g.owner, g.need, g.spawned, g.rowDist, g.flying, g.payer); }

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
    routeDefs_.clear(); routeLines_.clear(); routeAnchors_.clear(); routeBlockers_.clear();
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
        routeLines_.push_back(buildRouteLines(r, rules_.routes));
        routeAnchors_.push_back(w.get<Transform>(placed[size_t(a)]).pos);
        routeAnchors_.push_back(w.get<Transform>(placed[size_t(b)]).pos);
        rs.ends.push_back(placed[size_t(a)]);
        rs.ends.push_back(placed[size_t(b)]);
        routeBlockers_.push_back(std::move(blockers));
        routeDefs_.push_back(std::move(r));
    }
    if (!routeDefs_.empty()) rules_.lanes.enabled = true;   // routes are lanes: walkers, skills along lanes, the view
    rs.active = !routes.isNull() || screen_.enabled || rules_.routes.groups;
    rs.screenCenter = {screen_.center.x, Fixed{}, screen_.center.y};
    if (!routeDefs_.empty()) rebuildRoutes(sim);
}

void RtsModule::rebuildRoutes(Simulation& sim) {
    World& w = sim.world();
    GameState& gs = state(sim);
    RouteState& rs = gs.routes;
    uint32_t h = 2166136261u;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= uint32_t(v >> (8 * i)) & 0xFF; h *= 16777619u; } };
    // ends: a garrison replaced in place (transform) keeps its routes
    for (size_t i = 0; i < rs.ends.size(); ++i) {
        Entity& e = rs.ends[i];
        if (w.alive(e) && w.has<Garrison>(e)) { mix(e.packed()); continue; }
        Entity best = kNullEntity;
        const Vec3 at = routeAnchors_[i];
        int cx, cy; gs.grid.cellXY(at, cx, cy);
        w.each<Garrison, Footprint>([&](Entity g, Garrison&, Footprint& f) {
            if (!best.valid() && cx >= f.x && cy >= f.y && cx < f.x + f.w && cy < f.y + f.h) best = g;
        });
        e = best;
        mix(e.packed());
    }
    for (auto& bl : routeBlockers_) for (Entity b : bl) mix(w.alive(b) ? 1 : 0);
    gs.lanes.clear();
    rs.laneRoute.clear();
    rs.laneScale.clear();
    for (size_t r = 0; r < routeDefs_.size(); ++r) {
        const Entity a = rs.ends[2 * r], b = rs.ends[2 * r + 1];
        if (!w.alive(a) || !w.alive(b)) continue;
        bool closed = false;
        for (Entity bl : routeBlockers_[r]) if (w.alive(bl)) closed = true;
        if (closed) continue;
        const auto& lines = routeLines_[r];
        const Fixed len0 = polylineLength(lines[0]);
        for (size_t k = 0; k < lines.size(); ++k) {
            Lane lane{a, b, routeLineOffset(int(k)), {}};
            for (Vec2 p : lines[k]) lane.points.push_back({p.x, Fixed{}, p.y});
            gs.lanes.push_back(std::move(lane));
            rs.laneRoute.push_back(int32_t(r));
            rs.laneScale.push_back(len0.raw > 0 ? polylineLength(lines[k]) / len0 : kOne);
        }
    }
    rs.signature = h;
    gs.laneSignature = h ^ 0x9e3779b9u;
    gs.lanesDirty = true;
    sim.emit({kEvLanesChanged, 0, kNullEntity, kNullEntity, int64_t(gs.lanes.size()), {}});
}

// sysLanes for maps with routes: rebuilt when a route end is replaced or a blocker dies
void RtsModule::sysRoutes(Simulation& sim) {
    if (sim.tick() % 20 != 0) return;
    World& w = sim.world();
    RouteState& rs = state(sim).routes;
    uint32_t h = 2166136261u;
    auto mix = [&](uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= uint32_t(v >> (8 * i)) & 0xFF; h *= 16777619u; } };
    bool stale = false;
    for (Entity e : rs.ends) { if (!w.alive(e) || !w.has<Garrison>(e)) stale = true; mix(e.packed()); }
    for (auto& bl : routeBlockers_) for (Entity b : bl) mix(w.alive(b) ? 1 : 0);
    if (!stale && h == rs.signature) return;
    rebuildRoutes(sim);
}

bool RtsModule::canSend(Simulation& sim, Entity from, Entity to) const {
    if (routeDefs_.empty()) return true;
    return !lanesBetween(sim, from, to).empty();
}

Fixed RtsModule::routeLength(Simulation& sim, Entity from, Entity to) const {
    const GameState& gs = *sim.world().resource<GameState>();
    auto lanes = lanesBetween(sim, from, to);
    if (lanes.empty()) return Fixed::fromInt(-1);
    Fixed l{};
    const auto& pts = gs.lanes[size_t(lanes[0])].points;
    for (size_t i = 1; i < pts.size(); ++i) l += dist2D(pts[i], pts[i - 1]);
    return l;
}

// kCmdSend with lanes.groups: BW UnitsModule.Go -> FindGroup / AddNeedCount or CreateGroup
bool RtsModule::sendAlongRoute(Simulation& sim, Entity from, Entity to, uint8_t player, int n) {
    World& w = sim.world();
    GameState& gs = state(sim);
    auto& g = w.get<Garrison>(from);
    for (auto& sg : gs.routes.groups)
        if (sg.from == from && sg.to == to && sg.owner == player) {
            sg.need = std::min(sg.need + n - sg.spawned, g.count) + sg.spawned;   // BW UnitGroup.AddNeedCount
            return true;
        }
    SendGroup sg;
    sg.id = gs.nextGroupId++;
    sg.from = from; sg.to = to; sg.owner = player; sg.need = n;
    sg.flying = rules_.advancedEffects && (statusFlags(sim, from) & kStFlying);
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
        if (!w.alive(sg.from) || !w.alive(sg.to) || !w.has<Garrison>(sg.from) || w.get<Owner>(sg.from).player != (sg.payer ? sg.payer : sg.owner)) continue;
        const UnitType& ft = rules_.types[w.get<Unit>(sg.from).type];
        if (ft.spawnUnit == kNoType) continue;
        std::vector<int> lanes = lanesBetween(sim, sg.from, sg.to);
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
    if (!screen_.enabled) return true;
    const Vec3 c = screenCenter(sim);
    return fxAbs(p.x - c.x) <= screen_.size.x / 2 && fxAbs(p.z - c.z) <= screen_.size.y / 2;   // BW IsPointInsideScreen
}

void RtsModule::setScreenCenter(Simulation& sim, Vec3 c) {
    if (!screen_.enabled) return;
    Vec3& sc = state(sim).routes.screenCenter;
    sc.x = fxClamp(c.x, screen_.center.x - screen_.left, screen_.center.x + screen_.right);
    sc.z = fxClamp(c.z, screen_.center.y - screen_.top, screen_.center.y + screen_.bottom);
    sc.y = Fixed{};
}

// ---------------------------------------------------------------- save / hash

void RtsModule::ioRoutes(Archive& a, GameState& gs) {
    RouteState& rs = gs.routes;
    if (!rs.active) return;
    a(rs.ends, rs.laneRoute, rs.laneScale, rs.groups, rs.signature, rs.screenCenter);
}

void RtsModule::hashRoutes(Simulation& sim, Hasher& h) {
    const RouteState& rs = state(sim).routes;
    if (!rs.active) return;
    h.u32(rs.signature);
    for (Entity e : rs.ends) h.u64(e.packed());
    for (const auto& g : rs.groups) { h.u32(g.id); h.u64(g.from.packed()); h.u64(g.to.packed()); h.i32(g.need); h.i32(g.spawned); h.fx(g.rowDist); if (g.payer) h.u32(uint32_t(g.payer) | uint32_t(g.owner) << 8); }
    h.v3(rs.screenCenter);
}

} // namespace aiverse::game
