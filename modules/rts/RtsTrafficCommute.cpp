// How the citizens get to work and back (rules traffic.population: car / bus shares, RtsTraffic.h). A trip is a
// chain of legs: on foot to the door; or to the bus stop, on the bus, from its stop to the door; or the drive in his
// own car. Unseen, a leg is a straight line with a speed and an arrival time. Near a hero it is the real thing:
//  - on foot he walks the shortest sidewalk route of the whole map (A*: round a river to its bridge);
//  - a driver's car stands at the kerb next to the house / the workplace; he pulls out of the parking lane, drives
//    the junction graph to his address (exitToward), pulls up at the kerb there, gets out and walks to the door;
//  - a bus rider walks to the stop near his door and waits; a bus that calls there takes him, and one that calls at
//    the stop near his destination lets him off (the buses have no lines: any of them will do).
// Fixed maths only, entity-index order, randomness from sim.rng().
#include "RtsTraffic.h"
#include "RtsInternal.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>

namespace aiverse::game {
using namespace detail;
namespace tl = lib::genre;

namespace {
Vec3 v3(Vec2 p) { return {p.x, Fixed{}, p.y}; }
Vec2 v2(Vec3 p) { return {p.x, p.z}; }
uint32_t ticksOf(Fixed seconds, uint32_t rate) { return uint32_t(std::max<int64_t>(0, (seconds * int64_t(rate)).toInt())); }
bool living(World& w, Entity e) { return e.valid() && w.alive(e) && w.get<Unit>(e).hp.raw > 0; }
void silentRemove(Simulation& sim, Entity e) {
    World& w = sim.world();
    if (!e.valid() || !w.alive(e)) return;
    Unit& u = w.get<Unit>(e);
    u.flags |= kUnitSilent;
    u.hp = Fixed{};
}
template <class T> void insertSorted(std::vector<T>& v, T x) {
    auto it = std::lower_bound(v.begin(), v.end(), x.e.index, [](const T& a, uint32_t i) { return a.e.index < i; });
    v.insert(it, std::move(x));
}
Fixed nearestSq(const std::vector<Vec2>& foci, Vec2 p) {
    Fixed best = Fixed::fromInt(1 << 20);
    for (auto& f : foci) best = fxMin(best, (f - p).lengthSq());
    return best;
}
} // namespace

// The nearest sidewalk cell; with `mainFirst` one of the city's connected sidewalks when there is one in range (a
// door onto a park path or a fenced yard nobody could walk to).
int Traffic::sidewalkNear(Vec2 p, int radius, bool mainFirst) const {
    const int cx = int(p.x.floorInt()), cz = int(p.y.floorInt());
    for (int pass = mainFirst ? 0 : 1; pass < 2; ++pass)
    for (int r = 0; r <= radius; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != r) continue;   // the ring only
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= net_.w || z >= net_.h || !side_[size_t(z * net_.w + x)]) continue;
                if (pass == 0 && island_[size_t(z * net_.w + x)] != mainIsland_) continue;
                return z * net_.w + x;
            }
    return -1;
}

// Per stop the sidewalk cell people wait on; per place its bus stop and the kerb its cars stand at.
void Traffic::setupCommute() {
    stopCell_.assign(stops_.size(), -1);
    for (size_t i = 0; i < stops_.size(); ++i) stopCell_[i] = sidewalkNear(stops_[i].kerb, 3, true);
    const TrafficPopulationRules& pr = rules_.population;
    for (auto& pl : places_) {
        Fixed best = pr.stopRange * pr.stopRange;
        for (size_t i = 0; i < stops_.size(); ++i) {
            if (stopCell_[i] < 0) continue;
            const Fixed d = (stops_[i].kerb - pl.pos).lengthSq();
            if (d < best) { best = d; pl.stop = int32_t(i); }
        }
        if (!roadSpot(pl.pos, 8, pl.piece, pl.along)) { pl.piece = -1; continue; }
        const auto& P = net_.pieces[size_t(pl.piece)];
        const bool high = tl::trafficAcrossOf(P, pl.pos) >= P.centre();   // the place's side of the road
        pl.dir = int8_t((tl::trafficLaneAcross(net_, P, 1, 0) >= P.centre()) == high ? 1 : -1);
    }
}

Vec2 Traffic::legTarget(const TrafficCitizen& c) const {
    if ((c.leg == kLegToStop || c.leg == kLegAtStop) && c.stopA >= 0) return stops_[size_t(c.stopA)].kerb;
    if (c.leg == kLegRide && c.stopB >= 0) return stops_[size_t(c.stopB)].kerb;
    const int to = c.state == kCitizenToWork ? c.work : c.home;
    return to >= 0 ? places_[size_t(to)].pos : c.from;
}

void Traffic::travel(Simulation& sim, TrafficCitizen& c, uint8_t leg, Vec2 from) const {
    const TrafficPopulationRules& pr = rules_.population;
    c.leg = leg; c.from = from; c.left = sim.tick();
    const Fixed speed = leg == kLegDrive ? pr.drive : leg == kLegRide ? pr.ride : pr.walk;
    c.eta = sim.tick() + ticksOf((legTarget(c) - from).length() / speed, sim.tickRate()) + 1;
}

// How long the trip takes unseen: he leaves home that much before his shift.
uint32_t Traffic::tripTicks(const TrafficCitizen& c, int from, int to, uint32_t rate) const {
    const TrafficPopulationRules& pr = rules_.population;
    const Place& a = places_[size_t(from)], & b = places_[size_t(to)];
    const Fixed d = (b.pos - a.pos).length();
    if (c.commute == kCommuteCar) return ticksOf(d / pr.drive, rate);
    if (c.commute == kCommuteBus && a.stop >= 0 && b.stop >= 0 && a.stop != b.stop) {
        const Vec2 ka = stops_[size_t(a.stop)].kerb, kb = stops_[size_t(b.stop)].kerb;
        const Fixed onFoot = (ka - a.pos).length() + (b.pos - kb).length();
        if (onFoot < d) return ticksOf(onFoot / pr.walk + (kb - ka).length() / pr.ride, rate);
    }
    return ticksOf(d / pr.walk, rate);
}

void Traffic::arrive(TrafficCitizen& c) const {
    c.state = c.state == kCitizenToWork ? kCitizenWork : kCitizenHome;
    c.e = kNullEntity;
    c.leg = kLegWalk;
    c.stopA = c.stopB = -1;
}

// Out of the door: by car when it stands there (or unseen), to the bus stop when the bus saves walking, else on foot.
void Traffic::depart(Simulation& sim, TrafficStore& st, int32_t id, uint8_t state) {
    World& w = sim.world();
    TrafficCitizen& c = st.citizens[size_t(id)];
    c.state = state;
    c.e = kNullEntity;
    c.stopA = c.stopB = -1;
    const int from = state == kCitizenToWork ? c.home : c.work, to = state == kCitizenToWork ? c.work : c.home;
    const Place& a = places_[size_t(from)], & b = places_[size_t(to)];
    if (c.commute == kCommuteCar) {
        const Entity car = c.car;
        c.car = kNullEntity;
        if (living(w, car)) {
            const DrivingCar* dc = m_->driving() ? m_->driving()->car(sim, car) : nullptr;
            const Vec2 cp = v2(w.get<Transform>(car).pos);
            const bool moved = (cp - a.pos).lengthSq() > Fixed::fromInt(16 * 16);
            if (dc && (dc->driver.valid() || moved)) {   // his car is gone: he tells the police, and is on foot from now on
                report(sim, kIncidentCrime, a.pos, car, dc->driver, true);
                c.commute = kCommuteWalk;
                travel(sim, c, kLegWalk, a.pos);
                return;
            }
            if (startTrip(sim, st, id, car)) { travel(sim, c, kLegDrive, a.pos); c.e = car; return; }
        }
        travel(sim, c, kLegDrive, a.pos);
        return;
    }
    if (c.commute == kCommuteBus && a.stop >= 0 && b.stop >= 0 && a.stop != b.stop) {
        const Fixed onFoot = (stops_[size_t(a.stop)].kerb - a.pos).length() + (b.pos - stops_[size_t(b.stop)].kerb).length();
        if (onFoot < (b.pos - a.pos).length()) {
            c.stopA = a.stop; c.stopB = b.stop;
            travel(sim, c, kLegToStop, a.pos);
            return;
        }
    }
    travel(sim, c, kLegWalk, a.pos);
}

// ----------------------------------------------------------------- on foot

Entity Traffic::citizenPerson(Simulation& sim, TrafficStore& st, int32_t id, Vec3 spot, int goalCell) {
    World& w = sim.world();
    TrafficCitizen& c = st.citizens[size_t(id)];
    const Entity e = m_->spawn(sim, c.type, rules_.peopleOwner, spot);
    if (!e.valid()) return e;
    c.e = e;
    TrafficPerson p;
    p.e = e; p.hp = w.get<Unit>(e).hp; p.lastPos = spot; p.until = sim.tick();
    p.side = Fixed::fromRatio(int(id % 21) - 10, 10) * rules_.side;
    p.citizen = id;
    p.goal = goalCell;
    insertSorted(st.people, std::move(p));
    return e;
}

// The whole way to the cell over the sidewalks and crossings of the map.
bool Traffic::walkTo(Simulation& sim, TrafficPerson& p, int goalCell) {
    const MapGrid& g = m_->state(sim).grid;
    const int here = g.cellOf(sim.world().get<Transform>(p.e).pos);
    if (here < 0 || size_t(here) >= walk_.size() || goalCell < 0 || size_t(goalCell) >= walk_.size()) return false;
    int start = here;
    if (!walk_[size_t(start)]) {   // off the sidewalk (out of a car, pushed): from the nearest walk cell
        const int W = net_.w, cx = start % W, cz = start / W;
        int best = -1;
        for (int r = 1; r <= 6 && best < 0; ++r)
            for (int dz = -r; dz <= r && best < 0; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= W || z >= net_.h) continue;
                    if (walk_[size_t(z * W + x)] && stripAt(z * W + x) < 0) { best = z * W + x; break; }
                }
        if (best < 0) return false;
        start = best;
    }
    if (island_[size_t(start)] != island_[size_t(goalCell)]) return false;   // no way on foot: known without a search
    const bool okRoute = tl::trafficWalkRoute(net_.w, net_.h, walk_, start, goalCell, search_, p.path);
    if (!okRoute || p.path.size() > 60000) { p.path.clear(); return false; }
    if (start != here) p.path.insert(p.path.begin(), start);
    p.idx = 0; p.legEnd = 0;
    return !p.path.empty();
}

// A citizen standing on the street decides what next (Traffic::people, between walks). True: p.path is his way.
bool Traffic::citizenStep(Simulation& sim, TrafficStore& st, TrafficPerson& p, Vec2 pos) {
    TrafficCitizen& c = st.citizens[size_t(p.citizen)];
    const uint32_t rate = sim.tickRate();
    const int to = c.state == kCitizenToWork ? c.work : c.home;
    auto onFoot = [&] {   // no bus for him today
        c.leg = kLegWalk; c.stopA = c.stopB = -1;
        p.goal = to >= 0 ? places_[size_t(to)].cell : -1;
        p.until = sim.tick() + 1;
    };
    if (c.leg == kLegAtStop) {   // waiting for the bus (Traffic::passengers takes him)
        if (sim.tick() < c.eta) p.until = sim.tick() + rate;
        else onFoot();
        return false;
    }
    const Vec2 goal{Fixed::fromInt(p.goal % net_.w) + kHalf, Fixed::fromInt(p.goal / net_.w) + kHalf};
    const Fixed d2 = p.goal >= 0 ? (pos - goal).lengthSq() : Fixed{};
    // the whole way when the sidewalks connect; else as near as they get (then the rest unseen)
    if (d2 > Fixed::fromRatio(9, 4) && (walkTo(sim, p, p.goal) || newWalk(sim, p, nullptr, p.goal))) return true;
    if (c.leg == kLegToStop) {
        if (d2 <= Fixed::fromInt(16)) { c.leg = kLegAtStop; c.eta = sim.tick() + rate * 120; p.until = sim.tick() + rate; }
        else onFoot();   // no way to the stop
        return false;
    }
    // there: through the door. Or the sidewalks lead no nearer (an island of them): the rest of the way unseen
    if (d2 <= Fixed::fromInt(16)) arrive(c);
    else { c.e = kNullEntity; travel(sim, c, kLegWalk, pos); }
    p.citizen = -1;
    silentRemove(sim, p.e);
    return false;
}

// Out of sight of every hero: he goes on unseen from where he is.
void Traffic::citizenUnseen(Simulation& sim, TrafficStore& st, TrafficPerson& p, Vec2 pos) {
    if (p.citizen < 0 || size_t(p.citizen) >= st.citizens.size()) return;
    TrafficCitizen& c = st.citizens[size_t(p.citizen)];
    c.e = kNullEntity;
    travel(sim, c, c.leg == kLegAtStop ? uint8_t(kLegRide) : c.leg, pos);   // nobody looks: the bus came
    p.citizen = -1;
}

// ----------------------------------------------------------------- by bus

// A bus stands at the stop. Citizens waiting here get on; those who ride to this stop and are due get off.
// `p`: a person near the stop (boarding), nullptr: let the riders off.
bool Traffic::busCitizen(Simulation& sim, TrafficStore& st, int stop, TrafficPerson* p) {
    World& w = sim.world();
    if (st.citizens.empty() || stop < 0 || size_t(stop) >= stopCell_.size()) return false;   // no population on this map
    const BusStop& bs = stops_[size_t(stop)];
    if (p) {
        if (p->citizen < 0 || size_t(p->citizen) >= st.citizens.size()) return false;
        TrafficCitizen& c = st.citizens[size_t(p->citizen)];
        if (c.leg != kLegAtStop || c.stopA != stop || p->panic || !living(w, p->e)) return false;
        if ((v2(w.get<Transform>(p->e).pos) - bs.kerb).lengthSq() > Fixed::fromInt(36)) return false;
        c.e = kNullEntity;
        travel(sim, c, kLegRide, bs.kerb);
        p->citizen = -1;
        silentRemove(sim, p->e);
        return true;
    }
    const int cell = stopCell_[size_t(stop)];
    if (cell < 0) return false;
    int off = 0;
    for (size_t id = 0; id < st.citizens.size() && off < 4; ++id) {
        TrafficCitizen& c = st.citizens[id];
        if (c.leg != kLegRide || c.stopB != stop || c.e.valid() || (c.state != kCitizenToWork && c.state != kCitizenToHome)) continue;
        if (uint64_t(sim.tick() - c.left) * 2 < uint64_t(c.eta - c.left)) continue;   // still far away on another bus
        const int to = c.state == kCitizenToWork ? c.work : c.home;
        const Vec3 spot{Fixed::fromInt(cell % net_.w) + Fixed::fromRatio(int(id % 7) + 2, 10), Fixed{}, Fixed::fromInt(cell / net_.w) + Fixed::fromRatio(int(id / 7 % 7) + 2, 10)};
        const Entity e = citizenPerson(sim, st, int32_t(id), spot, places_[size_t(to)].cell);
        if (!e.valid()) break;
        c.leg = kLegWalk;
        ++off;
    }
    return off > 0;
}

// ----------------------------------------------------------------- by car

// Is the parking lane of side `dir` free at `a` (nothing but `self` within a car's length of it)?
bool Traffic::kerbFree(Simulation& sim, int32_t piece, int dir, Fixed a, Entity self) const {
    const auto& P = net_.pieces[size_t(piece)];
    if (P.lanes <= 0 || a < Fixed::fromInt(P.along0 + 1) || a > Fixed::fromInt(P.along1)) return false;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Vec2 pt = tl::trafficLanePoint(net_, P, dir, P.lanes, a), along = tl::trafficDetail::axisDir(P.axis, dir);
    if (!onRoad(pt) || !gs.grid.passableAt(v3(pt), kLayerGround)) return false;
    bool clear = true;
    gs.hash.query(v3(pt), Fixed::fromInt(2), [&](Entity o) {
        auto* ou = w.tryGet<Unit>(o);
        if (o == self || !ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap) || m_->rules().types[ou->type].structure) return;
        const Vec2 rel = v2(w.get<Transform>(o).pos) - pt;
        if (fxAbs(tl::trafficDetail::cross(along, rel)) < Fixed::fromRatio(7, 10) && fxAbs(rel.dot(along)) < Fixed::fromRatio(7, 5)) clear = false;
    });
    return clear;
}

// A free spot of the parking lane next to the place (side `dir` of its road), nearest to the door first.
// `from`, `ahead`: only spots at least that far ahead of a car at `from` driving `dir`.
bool Traffic::kerbSpot(Simulation& sim, const Place& pl, int dir, Fixed& along, Entity self, const Fixed* from, Fixed ahead) const {
    if (pl.piece < 0 || rules_.net.parkingLanes <= 0) return false;
    static const int kTry[9] = {0, 3, -3, 6, -6, 9, -9, 12, -12};   // halves of a cell along the kerb
    for (int off : kTry) {
        const Fixed a = pl.along + Fixed::fromRatio(off, 2);
        if (from && (a - *from) * dir < ahead) continue;
        if (kerbFree(sim, pl.piece, dir, a, self)) { along = a; return true; }
    }
    return false;
}

// His car while he is at home / at work: a real car at the kerb when a hero is around, nothing when nobody is.
void Traffic::keepCar(Simulation& sim, TrafficStore& st, TrafficCitizen& c, const std::vector<Vec2>& foci, int& parked) {
    (void)st;
    World& w = sim.world();
    const TrafficPopulationRules& pr = rules_.population;
    if (c.car.valid()) {
        if (!living(w, c.car)) { c.car = kNullEntity; return; }
        const DrivingCar* dc = m_->driving() ? m_->driving()->car(sim, c.car) : nullptr;
        if (dc && dc->driver.valid()) return;   // somebody sits in it: he finds out when he comes for it
        if (!foci.empty() && nearestSq(foci, v2(w.get<Transform>(c.car).pos)) > rules_.despawn * rules_.despawn) { silentRemove(sim, c.car); c.car = kNullEntity; --parked; }
        return;
    }
    if ((c.state != kCitizenHome && c.state != kCitizenWork) || parked >= pr.parked || foci.empty() || c.carType == UINT16_MAX) return;
    const Place& pl = places_[size_t(c.state == kCitizenHome ? c.home : c.work)];
    const Fixed d2 = nearestSq(foci, pl.pos);
    const bool first = sim.tick() <= sim.tickRate() * 2 + 1;   // the first look at everybody fills the streets
    if (d2 > rules_.spawnMax * rules_.spawnMax || (!first && d2 < rules_.spawnMin * rules_.spawnMin)) return;
    Fixed along;
    if (!kerbSpot(sim, pl, pl.dir, along, kNullEntity, nullptr, Fixed{})) return;
    const auto& P = net_.pieces[size_t(pl.piece)];
    const Entity e = m_->spawn(sim, c.carType, rules_.carOwner, v3(tl::trafficLanePoint(net_, P, pl.dir, P.lanes, along)));
    if (!e.valid()) return;
    w.get<Unit>(e).facing = v3(tl::trafficDetail::axisDir(P.axis, pl.dir));
    c.car = e;
    ++parked;
}

// He gets into his parked car and it becomes traffic with an address.
bool Traffic::startTrip(Simulation& sim, TrafficStore& st, int32_t id, Entity car) {
    World& w = sim.world();
    const TrafficCitizen& c = st.citizens[size_t(id)];
    const int to = c.state == kCitizenToWork ? c.work : c.home;
    Driving* drv = m_->driving();
    if (to < 0 || places_[size_t(to)].piece < 0 || !drv || !drv->active()) return false;
    for (auto& o : st.cars) if (o.e == car) return false;
    TrafficCar t;
    t.e = car;
    t.citizen = id;
    t.task = kTaskTrip;
    t.destPiece = places_[size_t(to)].piece; t.destAlong = places_[size_t(to)].along;
    t.cruise = rules_.speedMin + (rules_.speedMax - rules_.speedMin) * sim.rng().unit();
    const Vec3 f = w.get<Unit>(car).facing;
    if (!reroute(sim, t, v2(w.get<Transform>(car).pos), Vec2{f.x, f.z})) return false;
    if (!drv->adopt(sim, car, Fixed{})) return false;
    insertSorted(st.cars, std::move(t));
    return true;
}

// A citizen who drives unseen comes into sight: his car on the lane nearest to where he is, heading for his address.
bool Traffic::spawnTrip(Simulation& sim, TrafficStore& st, int32_t id, Vec2 at) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    TrafficCitizen& c = st.citizens[size_t(id)];
    const int to = c.state == kCitizenToWork ? c.work : c.home;
    Driving* drv = m_->driving();
    int32_t pi; Fixed along;
    if (to < 0 || places_[size_t(to)].piece < 0 || c.carType == UINT16_MAX || !drv || !drv->active() || !roadSpot(at, 6, pi, along)) return false;
    const Place& pl = places_[size_t(to)];
    const auto& P = net_.pieces[size_t(pi)];
    const Vec2 here = tl::trafficLanePoint(net_, P, 1, 0, along);
    const Fixed toward = pi == pl.piece ? pl.along - along : tl::trafficDetail::axisDir(P.axis, 1).dot(pl.pos - here);
    const int s = toward.raw >= 0 ? 1 : -1, k = int(uint32_t(id) % uint32_t(P.lanes));
    const Vec2 pos = tl::trafficLanePoint(net_, P, s, k, along), dir = tl::trafficDetail::axisDir(P.axis, s);
    if (!gs.grid.passableAt(v3(pos), kLayerGround)) return false;
    const Fixed body = m_->rules().types[c.carType].radius;
    bool clear = true;
    gs.hash.query(v3(pos), Fixed::fromInt(6) + body, [&](Entity o) {
        auto* ou = w.tryGet<Unit>(o);
        if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap) || m_->rules().types[ou->type].structure) return;
        const Vec2 rel = v2(w.get<Transform>(o).pos) - pos;
        if (fxAbs(tl::trafficDetail::cross(dir, rel)) < Fixed::fromRatio(7, 10) && fxAbs(rel.dot(dir)) < Fixed::fromInt(3) + body + m_->rules().types[ou->type].radius) clear = false;
    });
    if (!clear) return false;
    const Entity e = m_->spawn(sim, c.carType, rules_.carOwner, v3(pos));
    if (!e.valid()) return false;
    w.get<Unit>(e).facing = v3(dir);
    TrafficCar t;
    t.e = e;
    t.citizen = id;
    t.task = kTaskTrip;
    t.destPiece = pl.piece; t.destAlong = pl.along;
    t.cruise = rules_.speedMin + (rules_.speedMax - rules_.speedMin) * sim.rng().unit();
    t.tailPiece = pi; t.tailDir = int8_t(s); t.tailLane = uint8_t(k);
    t.route.push_back({pos, -1, 0, 0});
    t.route.push_back({tl::trafficLanePoint(net_, P, s, k, tl::trafficPieceEnd(P, s)), int32_t(P.junction[s > 0 ? 1 : 0]), uint8_t(P.axis), 0});
    t.wp = 1;
    drv->adopt(sim, e, t.cruise * Fixed::fromRatio(3, 4));
    insertSorted(st.cars, std::move(t));
    c.e = e;
    return true;
}

// Approaching the address: how far to the kerb side of its lane the car steers (into the parking lane, when the
// spot is free).
Fixed Traffic::tripShift(Simulation& sim, const TrafficCar& c, Vec2 pos, Vec2 fwd, Vec2 target) const {
    if (c.task != kTaskTrip || c.destPiece < 0 || rules_.net.parkingLanes <= 0) return Fixed{};
    GameState& gs = m_->state(sim);
    const int cell = gs.grid.cellOf(v3(pos));
    if (cell < 0 || size_t(cell) >= net_.pieceAt.size() || net_.pieceAt[size_t(cell)] != c.destPiece) return Fixed{};
    const auto& P = net_.pieces[size_t(c.destPiece)];
    const int s = tl::trafficDetail::axisDir(P.axis, 1).dot(fwd).raw >= 0 ? 1 : -1;
    const Fixed d = (c.destAlong - tl::trafficAlongOf(P, pos)) * s;
    if (d > Fixed::fromInt(5) || d < -kOne) return Fixed{};
    const bool taken = !kerbFree(sim, c.destPiece, s, c.destAlong, c.e);
    return taken ? Fixed{} : fxAbs(tl::trafficLaneAcross(net_, P, s, P.lanes) - tl::trafficAcrossOf(P, target));
}

// The cars on a trip: at the address the driver gets out on the kerb side and walks to the door. The car stays at
// the kerb as his parked car; when it could not get to the kerb it drives on (somebody gave him a lift).
void Traffic::trips(Simulation& sim, TrafficStore& st) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    Driving* drv = m_->driving();
    std::vector<Entity> parkedNow;
    auto release = [](TrafficCar& c) { c.citizen = -1; c.task = kTaskNone; c.destPiece = -1; c.dwellUntil = 0; };
    for (size_t i = 0; i < st.cars.size(); ++i) {
        if (st.cars[i].task != kTaskTrip) continue;
        TrafficCar& c = st.cars[i];
        if (c.citizen < 0 || size_t(c.citizen) >= st.citizens.size() || st.citizens[size_t(c.citizen)].e != c.e) { release(c); continue; }
        const int32_t id = c.citizen;
        const Vec3 pos3 = w.get<Transform>(c.e).pos;
        const Vec2 pos = v2(pos3);
        const int cell = gs.grid.cellOf(pos3);
        const bool here = cell >= 0 && size_t(cell) < net_.pieceAt.size() && net_.pieceAt[size_t(cell)] == c.destPiece && c.destPiece >= 0;
        if (!here) { c.dwellUntil = 0; continue; }
        const auto& P = net_.pieces[size_t(c.destPiece)];
        const Vec3 f = w.get<Unit>(c.e).facing;
        const int s = tl::trafficDetail::axisDir(P.axis, 1).dot(Vec2{f.x, f.z}).raw >= 0 ? 1 : -1;
        if (sim.tick() % 10 == 0) {   // on his street: the free spot of the kerb nearest to the door that is still ahead
            const TrafficCitizen& who = st.citizens[size_t(id)];
            const int dest = who.state == kCitizenToWork ? who.work : who.home;
            const Fixed at = tl::trafficAlongOf(P, pos);
            Fixed spotAlong;
            if (dest >= 0 && places_[size_t(dest)].piece == c.destPiece && (c.destAlong - at) * s > Fixed::fromInt(5) &&
                kerbSpot(sim, places_[size_t(dest)], s, spotAlong, c.e, &at, Fixed::fromInt(5))) c.destAlong = spotAlong;
        }
        if (fxAbs(drv->speed(sim, c.e)) >= Fixed::fromRatio(3, 5)) { c.dwellUntil = 0; continue; }
        const Fixed d = fxAbs(c.destAlong - tl::trafficAlongOf(P, pos));
        if (d > Fixed::fromInt(4)) { c.dwellUntil = 0; continue; }
        if (c.dwellUntil == 0) c.dwellUntil = std::max<uint32_t>(sim.tick(), 1);   // slow near the address since
        if (d > Fixed::fromRatio(7, 10) && sim.tick() - c.dwellUntil < sim.tickRate() * 2) continue;
        // there
        const Fixed kerbAcross = tl::trafficLaneAcross(net_, P, s, P.lanes);
        // beside the car, clear of its body
        const Fixed out = (kerbAcross >= P.centre() ? kOne : -kOne) * (m_->rules().types[w.get<Unit>(c.e).type].radius + Fixed::fromRatio(1, 4));
        Vec3 spot = v3(tl::trafficDetail::at(P, tl::trafficAlongOf(P, pos), tl::trafficAcrossOf(P, pos) + out));
        if (!gs.grid.passableAt(spot, kLayerGround)) spot = v3(tl::trafficDetail::at(P, tl::trafficAlongOf(P, pos), tl::trafficAcrossOf(P, pos) - out));
        TrafficCitizen& ct = st.citizens[size_t(id)];
        const int to = ct.state == kCitizenToWork ? ct.work : ct.home;
        const bool atKerb = rules_.net.parkingLanes > 0 && fxAbs(tl::trafficAcrossOf(P, pos) - kerbAcross) < Fixed::fromRatio(9, 20);
        const Entity car = c.e;   // citizenPerson grows st.people only; `c` stays valid
        ct.e = kNullEntity;
        if (to < 0 || !gs.grid.passableAt(spot, kLayerGround) || !citizenPerson(sim, st, id, spot, places_[size_t(to)].cell).valid()) arrive(ct);
        else ct.leg = kLegWalk;
        if (atKerb) { ct.car = car; parkedNow.push_back(car); }
        else release(st.cars[i]);
    }
    for (Entity e : parkedNow)
        for (size_t i = 0; i < st.cars.size(); ++i) if (st.cars[i].e == e) { st.cars.erase(st.cars.begin() + long(i)); break; }
}

// The car is about to leave the street (out of sight, stuck out of sight): he drives on unseen.
void Traffic::tripGone(Simulation& sim, TrafficStore& st, TrafficCar& c) {
    if (c.citizen >= 0 && size_t(c.citizen) < st.citizens.size() && st.citizens[size_t(c.citizen)].e == c.e) {
        TrafficCitizen& ct = st.citizens[size_t(c.citizen)];
        ct.e = kNullEntity;
        travel(sim, ct, kLegDrive, v2(sim.world().get<Transform>(c.e).pos));
    }
    c.citizen = -1;
}

// A hero took the car: its driver is on the street at `spot` and goes on on foot. Null: no citizen drove it.
Entity Traffic::tripDriverOut(Simulation& sim, TrafficStore& st, TrafficCar& c, Vec3 spot, bool passable) {
    if (c.citizen < 0 || size_t(c.citizen) >= st.citizens.size() || st.citizens[size_t(c.citizen)].e != c.e) { c.citizen = -1; return kNullEntity; }
    const int32_t id = c.citizen;
    c.citizen = -1;
    TrafficCitizen& ct = st.citizens[size_t(id)];
    const int to = ct.state == kCitizenToWork ? ct.work : ct.home;
    ct.e = kNullEntity;
    const Entity e = passable && to >= 0 ? citizenPerson(sim, st, id, spot, places_[size_t(to)].cell) : kNullEntity;
    if (e.valid()) ct.leg = kLegWalk;
    else travel(sim, ct, kLegWalk, Vec2{spot.x, spot.z});
    return e;
}

} // namespace aiverse::game
