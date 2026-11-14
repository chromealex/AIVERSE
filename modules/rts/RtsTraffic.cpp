// City traffic (rules "traffic", RtsTraffic.h): the road network from the map's tiles, signals and their light props,
// the bot driver of each traffic car, and the pedestrians. Fixed maths only; every loop runs in entity-index order,
// randomness only from sim.rng().
#include "RtsTraffic.h"
#include "RtsInternal.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
namespace tl = lib::genre;

namespace {
const Fixed kUnit = Fixed::fromInt(1);

Fixed number(const Json& j, const char* key, Fixed def, const std::string& where, bool positive) {
    if (!j.isObject() || !j.has(key)) return def;
    Fixed v = j.getFx(key, def);
    if (v.raw < 0 || (positive && v.raw == 0))
        throw std::runtime_error(where + "." + key + (positive ? ": must be positive" : ": must not be negative"));
    return v;
}
int integer(const Json& j, const char* key, int def, int lo, int hi, const std::string& where) {
    if (!j.isObject() || !j.has(key)) return def;
    const int64_t v = j.getInt(key, def);
    if (v < lo || v > hi) throw std::runtime_error(where + "." + key + ": " + std::to_string(lo) + ".." + std::to_string(hi));
    return int(v);
}
// [min, max] pair: "key": [a, b] or a single number
void range(const Json& j, const char* key, Fixed& lo, Fixed& hi, const std::string& where) {
    if (!j.isObject() || !j.has(key)) return;
    const Json& v = j[key];
    if (v.isNumber()) { lo = hi = v.fx(); }
    else if (v.isArray() && v.size() == 2) { lo = v[0].fx(); hi = v[1].fx(); }
    else throw std::runtime_error(where + "." + key + ": a number or [min, max]");
    if (lo.raw < 0 || hi < lo) throw std::runtime_error(where + "." + key + ": 0 <= min <= max");
}
uint32_t ticksOf(Fixed seconds, uint32_t rate) { return uint32_t(std::max<int64_t>(0, (seconds * int64_t(rate)).toInt())); }
Vec3 v3(Vec2 p) { return {p.x, Fixed{}, p.y}; }
Vec2 v2(Vec3 p) { return {p.x, p.z}; }
Vec2 rightOnScreen(Vec2 d) { return {-d.y, d.x}; }   // +x right, +z down the screen
Fixed segDistSq(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const Fixed l2 = ab.lengthSq();
    Fixed t = l2.raw > 0 ? (p - a).dot(ab) / l2 : Fixed{};
    t = fxClamp(t, Fixed{}, kUnit);
    return (p - (a + ab * t)).lengthSq();
}
template <class T> T* byEntity(std::vector<T>& v, Entity e) {
    auto it = std::lower_bound(v.begin(), v.end(), e.index, [](const T& x, uint32_t i) { return x.e.index < i; });
    return it != v.end() && it->e == e ? &*it : nullptr;
}
template <class T> void insertSorted(std::vector<T>& v, T x) {
    auto it = std::lower_bound(v.begin(), v.end(), x.e.index, [](const T& a, uint32_t i) { return a.e.index < i; });
    v.insert(it, std::move(x));
}
// A type change for light props: no kEvUpgraded (the view swaps the look; nothing was upgraded)
void showType(RtsModule& m, Simulation& sim, Entity e, uint16_t type) {
    World& w = sim.world();
    Unit& u = w.get<Unit>(e);
    if (u.type == type) return;
    GameState& gs = m.state(sim);
    const uint8_t owner = w.get<Owner>(e).player;
    if (owner < gs.players.size() && !gs.players[owner].typeCountAll.empty()) {
        if (gs.players[owner].typeCountAll[u.type] > 0) gs.players[owner].typeCountAll[u.type]--;
        gs.players[owner].typeCountAll[type]++;
    }
    u.type = type;
}
void silentRemove(Simulation& sim, Entity e) {
    World& w = sim.world();
    if (!w.alive(e)) return;
    Unit& u = w.get<Unit>(e);
    u.flags |= kUnitSilent;
    u.hp = Fixed{};
}
} // namespace

// ================================================================= data

TrafficRules Traffic::parse(const Json& j, const std::vector<std::string>& ids, uint32_t tickRate) {
    TrafficRules r;
    if (!j.isObject() || !j.getBool("enabled", true)) return r;
    r.enabled = true;
    auto typeOf = [&](const std::string& id, const std::string& where) {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return uint16_t(i);
        throw std::runtime_error(where + ": unknown type '" + id + "'");
    };
    auto weighted = [&](const Json& v, const std::string& where) {
        std::vector<TrafficWeighted> out;
        if (v.isArray()) {
            for (const Json& e : v.items()) {
                TrafficWeighted t;
                if (e.isString()) t.type = typeOf(e.str(), where);
                else if (e.isObject()) { t.type = typeOf(e.getStr("type"), where); t.weight = integer(e, "weight", 1, 1, 1000000, where); }
                else throw std::runtime_error(where + ": type ids or {\"type\", \"weight\"}");
                out.push_back(t);
            }
        } else if (v.isObject()) {
            for (const auto& [id, wgt] : v.members()) {
                TrafficWeighted t; t.type = typeOf(id, where);
                const int64_t n = wgt.integer(1);
                if (n < 1 || n > 1000000) throw std::runtime_error(where + "." + id + ": weight 1..1000000");
                t.weight = int(n);
                out.push_back(t);
            }
        } else if (!v.isNull()) throw std::runtime_error(where + ": a list of type ids or {id: weight}");
        return out;
    };

    const Json& roads = j["roads"];
    r.roadTiles = roads.getStr("tiles");
    if (r.roadTiles.empty()) throw std::runtime_error("traffic.roads.tiles: the map tile characters of road cells");
    r.net.laneWidth = number(roads, "laneWidth", r.net.laneWidth, "traffic.roads", true);
    r.net.parkingLanes = integer(roads, "parkingLanes", 0, 0, 8, "traffic.roads");
    r.net.maxRoadWidth = integer(roads, "maxWidth", 6, 1, 64, "traffic.roads");
    const std::string side = roads.getStr("side", "right");
    if (side == "left") r.net.leftHand = true;
    else if (side != "right") throw std::runtime_error("traffic.roads.side: 'right' or 'left' (got '" + side + "')");

    const Json& c = j["cars"];
    r.cars = weighted(c["types"], "traffic.cars.types");
    r.maxCars = integer(c, "max", r.cars.empty() ? 0 : r.maxCars, 0, 512, "traffic.cars");
    r.carOwner = uint8_t(integer(c, "owner", 0, 0, 255, "traffic.cars"));
    r.spawnMin = number(c, "spawnMin", r.spawnMin, "traffic.cars", false);
    r.spawnMax = number(c, "spawnMax", r.spawnMax, "traffic.cars", true);
    r.despawn = number(c, "despawn", r.despawn, "traffic.cars", true);
    if (r.spawnMax <= r.spawnMin || r.despawn < r.spawnMax) throw std::runtime_error("traffic.cars: spawnMin < spawnMax <= despawn");
    range(c, "speed", r.speedMin, r.speedMax, "traffic.cars");
    if (r.speedMax.raw <= 0) throw std::runtime_error("traffic.cars.speed: must be positive");
    r.turnSpeed = number(c, "turnSpeed", r.turnSpeed, "traffic.cars", true);
    r.decel = number(c, "decel", r.decel, "traffic.cars", true);
    r.gap = number(c, "gap", r.gap, "traffic.cars", false);
    r.straight = integer(c, "straight", r.straight, 0, 1000, "traffic.cars");
    r.turn = integer(c, "turn", r.turn, 0, 1000, "traffic.cars");
    if (r.straight + r.turn == 0) throw std::runtime_error("traffic.cars: straight + turn must be positive");
    r.driver.steerGain = number(c, "steering", r.driver.steerGain, "traffic.cars", true);

    const Json& b = j["buses"];
    r.buses = weighted(b["types"], "traffic.buses.types");
    r.maxBuses = integer(b, "max", r.buses.empty() ? 0 : 2, 0, 128, "traffic.buses");
    if (const Json& stops = b["stops"]; stops.isArray()) for (const Json& e : stops.items()) r.stopTypes.push_back(typeOf(e.str(), "traffic.buses.stops"));
    else if (stops.isString()) r.stopTypes.push_back(typeOf(stops.str(), "traffic.buses.stops"));
    std::sort(r.stopTypes.begin(), r.stopTypes.end());
    r.autoStops = integer(b, "autoStops", 0, 0, 100000, "traffic.buses");
    range(b, "speed", r.busMin, r.busMax, "traffic.buses");
    range(b, "dwell", r.dwellMin, r.dwellMax, "traffic.buses");
    r.passengers = integer(b, "passengers", r.passengers, 0, 16, "traffic.buses");
    if (const Json& ln = b["lines"]; ln.isNumber()) r.autoLines = integer(b, "lines", 4, 0, 7, "traffic.buses");
    else if (ln.isArray()) {
        r.autoLines = 0;
        for (size_t i = 0; i < ln.size(); ++i) {
            const std::string at = "traffic.buses.lines[" + std::to_string(i) + "]";
            TrafficRules::LineDef d;
            d.id = ln[i].getStr("id", std::to_string(i + 1));
            const Json& via = ln[i]["via"];
            if (!via.isArray() || via.size() < 2) throw std::runtime_error(at + ".via: at least two points [[x, z], ...]");
            for (const Json& q : via.items()) { if (q.size() < 2) throw std::runtime_error(at + ".via: points are [x, z]"); d.via.push_back({q[0].fx(), q[1].fx()}); }
            r.lineDefs.push_back(std::move(d));
        }
        if (r.lineDefs.size() > 64) throw std::runtime_error("traffic.buses.lines: at most 64");
    }
    if (!r.buses.empty() && r.busMax.raw <= 0) throw std::runtime_error("traffic.buses.speed: must be positive");

    const Json& em = j["emergency"];
    r.emergency = weighted(em["types"], "traffic.emergency.types");
    r.maxEmergency = integer(em, "max", r.emergency.empty() ? 0 : 1, 0, 32, "traffic.emergency");
    range(em, "speed", r.emMin, r.emMax, "traffic.emergency");
    range(em, "interval", r.emIntervalMin, r.emIntervalMax, "traffic.emergency");
    r.yieldRadius = number(em, "yield", r.yieldRadius, "traffic.emergency", false);
    if (!r.emergency.empty() && r.emMax.raw <= 0) throw std::runtime_error("traffic.emergency.speed: must be positive");

    const Json& s = j["signals"];
    r.signals = !(s.type() == Json::Type::Bool && !s.boolean(true));
    r.greenS = number(s, "green", r.greenS, "traffic.signals", true);
    r.yellowS = number(s, "yellow", r.yellowS, "traffic.signals", false);
    r.allRedS = number(s, "allRed", r.allRedS, "traffic.signals", false);
    r.walkS = number(s, "walk", fxMin(r.walkS, r.greenS), "traffic.signals", false);
    r.timing.green = std::max<uint32_t>(1, ticksOf(r.greenS, tickRate));
    r.timing.yellow = ticksOf(r.yellowS, tickRate);
    r.timing.allRed = ticksOf(r.allRedS, tickRate);
    r.timing.walk = ticksOf(r.walkS, tickRate);
    const std::string off = s.getStr("offset", "random");
    if (off == "random") r.offsetMode = 0; else if (off == "wave") r.offsetMode = 1; else if (off == "none") r.offsetMode = 2;
    else throw std::runtime_error("traffic.signals.offset: 'random', 'wave' or 'none' (got '" + off + "')");
    if (const Json& lights = s["lights"]; lights.isObject()) {
        const char* names[3] = {"green", "yellow", "red"};
        for (int i = 0; i < 3; ++i) {
            const std::string id = lights.getStr(names[i]);
            if (id.empty()) throw std::runtime_error(std::string("traffic.signals.lights.") + names[i] + ": a prop type id");
            r.light[i] = typeOf(id, std::string("traffic.signals.lights.") + names[i]);
        }
    }
    r.placeLights = s.getBool("place", true);
    r.lightRadius = number(s, "radius", fxMax(r.despawn, r.spawnMax) + Fixed::fromInt(4), "traffic.signals", true);

    const Json& p = j["pedestrians"];
    r.walkTiles = p.getStr("walk");
    r.people = weighted(p["types"], "traffic.pedestrians.types");
    if (!r.people.empty() && r.walkTiles.empty()) throw std::runtime_error("traffic.pedestrians.walk: the map tile characters of sidewalk cells");
    r.maxPeople = integer(p, "max", r.people.empty() ? 0 : r.maxPeople, 0, 1024, "traffic.pedestrians");
    r.peopleOwner = uint8_t(integer(p, "owner", 0, 0, 255, "traffic.pedestrians"));
    r.peopleMin = number(p, "spawnMin", r.peopleMin, "traffic.pedestrians", false);
    r.peopleMax = number(p, "spawnMax", r.peopleMax, "traffic.pedestrians", true);
    r.peopleDespawn = number(p, "despawn", r.peopleDespawn, "traffic.pedestrians", true);
    if (r.peopleMax <= r.peopleMin || r.peopleDespawn < r.peopleMax) throw std::runtime_error("traffic.pedestrians: spawnMin < spawnMax <= despawn");
    Fixed rlo = Fixed::fromInt(r.routeMin), rhi = Fixed::fromInt(r.routeMax);
    range(p, "route", rlo, rhi, "traffic.pedestrians");
    r.routeMin = std::max(1, int(rlo.toInt())); r.routeMax = std::max(r.routeMin, int(rhi.toInt()));
    range(p, "pause", r.pauseMin, r.pauseMax, "traffic.pedestrians");
    r.side = number(p, "side", r.side, "traffic.pedestrians", false);
    if (r.side >= kHalf) throw std::runtime_error("traffic.pedestrians.side: below 0.5 cells");
    if (const Json& pn = p["panic"]; pn.isObject()) {
        r.panicRadius = number(pn, "radius", r.panicRadius, "traffic.pedestrians.panic", false);
        r.panicSeconds = number(pn, "seconds", r.panicSeconds, "traffic.pedestrians.panic", false);
        r.panicSpeed = number(pn, "speed", r.panicSpeed, "traffic.pedestrians.panic", true);
    }
    parseCity(j, r, ids, tickRate);
    parsePopulation(j, r, ids, tickRate);
    parseGangs(j, r, ids, tickRate);
    if (p.has("driver")) r.driverPerson = typeOf(p.getStr("driver"), "traffic.pedestrians.driver");
    return r;
}

TrafficStore* Traffic::store(Simulation& sim) const { return active() ? sim.world().resource<TrafficStore>() : nullptr; }

void Traffic::setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map) {
    m_ = &m;
    std::vector<std::string> ids;
    for (auto& t : m.rules().types) ids.push_back(t.id);
    const Json& mapBlock = map["traffic"];
    if (mapBlock.type() == Json::Type::Bool && !mapBlock.boolean(true)) return;   // this map has no traffic
    rules_ = parse(rulesJson["traffic"], ids, sim.tickRate());
    if (!active()) return;
    Driving* drv = m.driving();
    std::vector<std::pair<std::string, const std::vector<TrafficWeighted>*>> fleets = {{"cars", &rules_.cars}, {"buses", &rules_.buses}, {"emergency", &rules_.emergency}};
    for (auto& sv : rules_.services) fleets.push_back({"services." + sv.id, &sv.types});
    for (auto& [name, list] : fleets) {
        if (list->empty()) continue;
        if (!drv || !drv->active()) throw std::runtime_error("traffic." + name + ": needs the 'driving' block (traffic cars are its vehicles)");
        for (auto& t : *list) {
            bool found = false;
            for (auto& v : drv->rules().vehicles) found = found || v.type == t.type;
            if (!found) throw std::runtime_error("traffic." + name + ".types: '" + ids[t.type] + "' is not in driving.vehicles");
        }
    }
    if (drv && drv->active()) mpc_ = drv->rules().metersPerCell;

    // masks from the map's tile characters
    const MapGrid& g = m.state(sim).grid;
    const int w = g.w, h = g.h;
    const Json& rows = map["tiles"]["rows"];
    if (!rows.isArray()) throw std::runtime_error("traffic: the map needs a tiles layer (tiles.rows) to find its roads");
    std::vector<uint8_t> road(size_t(w) * size_t(h), 0), side(road.size(), 0);
    for (int z = 0; z < h && size_t(z) < rows.size(); ++z) {
        const std::string row = rows[size_t(z)].str();
        for (int x = 0; x < w && size_t(x) < row.size(); ++x) {
            if (rules_.roadTiles.find(row[size_t(x)]) != std::string::npos) road[size_t(z * w + x)] = 1;
            else if (!rules_.walkTiles.empty() && rules_.walkTiles.find(row[size_t(x)]) != std::string::npos) side[size_t(z * w + x)] = 1;
        }
    }
    net_ = tl::buildTrafficNet(w, h, road, rules_.net);
    if (!rules_.signals) for (auto& j : net_.junctions) j.signal = false;
    // signal offsets: deterministic from the junction position (no RNG draw at setup)
    offset_.assign(net_.junctions.size(), 0);
    const uint32_t cyc = std::max<uint32_t>(rules_.timing.cycle(), 1);
    for (size_t i = 0; i < net_.junctions.size(); ++i) {
        const auto& jn = net_.junctions[i];
        if (rules_.offsetMode == 0) offset_[i] = (uint32_t(jn.x0) * 73856093u ^ uint32_t(jn.z0) * 19349663u) % cyc;
        else if (rules_.offsetMode == 1) offset_[i] = uint32_t(((int64_t(jn.x0) + jn.z0) * sim.tickRate() / 2) % cyc);
    }
    // pedestrian graph: sidewalks + one crossing strip at each end of a piece that meets a junction
    walk_ = side;
    stripAt_.assign(road.size(), -1);
    strips_.clear();
    for (size_t pi = 0; pi < net_.pieces.size(); ++pi) {
        const auto& p = net_.pieces[pi];
        for (int end = 0; end < 2; ++end) {
            if (p.junction[size_t(end)] < 0 || p.length() < 2) continue;
            const int a = end == 0 ? p.along0 : p.along1;
            // a strip joins two sidewalks: the cells just outside the band on both sides must be walkable
            auto cellOf = [&](int across) { return p.axis == tl::TrafficAxis::X ? across * w + a : a * w + across; };
            const bool inside0 = p.across0 - 1 >= 0, inside1 = p.across1 + 1 < (p.axis == tl::TrafficAxis::X ? h : w);
            if (!inside0 || !inside1 || !side[size_t(cellOf(p.across0 - 1))] || !side[size_t(cellOf(p.across1 + 1))]) continue;
            const int id = int(strips_.size());
            strips_.push_back({int32_t(pi), int32_t(p.junction[size_t(end)]), uint8_t(p.axis)});
            for (int k = p.across0; k <= p.across1; ++k) { const int cc = cellOf(k); stripAt_[size_t(cc)] = int32_t(id); walk_[size_t(cc)] = 1; }
        }
    }
    for (size_t i = 0; i < side.size(); ++i) if (walk_[i] && !g.passable(int(i % size_t(w)), int(i / size_t(w)), kLayerGround)) walk_[i] = side[i] = 0;   // props, buildings
    side_ = std::move(side);
    // signalised junctions by bucket: light props exist only around the heroes
    bw_ = (w + 15) / 16; bh_ = (h + 15) / 16;
    jbucket_.assign(size_t(bw_) * size_t(bh_), {});
    for (size_t i = 0; i < net_.junctions.size(); ++i) {
        const auto& jn = net_.junctions[i];
        if (jn.signal) jbucket_[size_t(((jn.z0 + jn.z1) / 2 / 16) * bw_ + (jn.x0 + jn.x1) / 2 / 16)].push_back(int32_t(i));
    }
    // junctions with an island in the middle (a monument, a roundabout): traffic goes round it
    ring_.assign(net_.junctions.size(), {});
    for (size_t i = 0; i < net_.junctions.size(); ++i) {
        const auto& jn = net_.junctions[i];
        int ix0 = INT32_MAX, iz0 = INT32_MAX, ix1 = -1, iz1 = -1;
        for (int z = jn.z0; z <= jn.z1; ++z)
            for (int x = jn.x0; x <= jn.x1; ++x)
                if (!road[size_t(z * w + x)]) { ix0 = std::min(ix0, x); ix1 = std::max(ix1, x); iz0 = std::min(iz0, z); iz1 = std::max(iz1, z); }
        if (ix1 < 0 || ix0 <= jn.x0 || iz0 <= jn.z0 || ix1 >= jn.x1 || iz1 >= jn.z1) continue;   // none, or not an island
        Ring& r = ring_[i];
        r.on = true;
        r.x0 = Fixed::fromRatio(jn.x0 + ix0, 2); r.x1 = Fixed::fromRatio(ix1 + 1 + jn.x1 + 1, 2);
        r.z0 = Fixed::fromRatio(jn.z0 + iz0, 2); r.z1 = Fixed::fromRatio(iz1 + 1 + jn.z1 + 1, 2);
    }
    // sidewalk islands: cells that can reach each other on foot share a number (a goal on another island is known at once)
    island_.assign(walk_.size(), -1);
    {
        int32_t next = 0;
        std::vector<int32_t> stack;
        for (size_t i = 0; i < walk_.size(); ++i) {
            if (!walk_[i] || island_[i] >= 0) continue;
            island_[i] = next; stack.assign(1, int32_t(i));
            while (!stack.empty()) {
                const int c = stack.back(); stack.pop_back();
                const int cx = c % w, cz = c / w;
                const int nx[4] = {cx - 1, cx + 1, cx, cx}, nz[4] = {cz, cz, cz - 1, cz + 1};
                for (int d = 0; d < 4; ++d) {
                    if (nx[d] < 0 || nz[d] < 0 || nx[d] >= w || nz[d] >= h) continue;
                    const int ni = nz[d] * w + nx[d];
                    if (walk_[size_t(ni)] && island_[size_t(ni)] < 0) { island_[size_t(ni)] = next; stack.push_back(ni); }
                }
            }
            ++next;
        }
        std::vector<int32_t> size(size_t(next), 0);
        for (int32_t v : island_) if (v >= 0) size[size_t(v)]++;
        mainIsland_ = -1;
        for (int32_t v = 0; v < next; ++v) if (mainIsland_ < 0 || size[size_t(v)] > size[size_t(mainIsland_)]) mainIsland_ = v;   // the city's sidewalks (ties: the first)
    }
    findStops(sim);
    setupLines();
    setupCity(sim);
    setupPopulation(sim, map);
    sim.world().setResource<TrafficStore>();
}

// Bus stops: map objects of the stop types (shelters, signs) within 3 cells of a driven road. The bus calls on the
// side of the road the object stands on.
void Traffic::findStops(Simulation& sim) {
    stops_.clear();
    if (rules_.buses.empty()) return;
    World& w = sim.world();
    struct Found { uint32_t index; Vec2 p; };
    std::vector<Found> found;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& t) {
        if (std::binary_search(rules_.stopTypes.begin(), rules_.stopTypes.end(), u.type)) found.push_back({e.index, v2(t.pos)});
    });
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.index < b.index; });
    for (auto& f : found) {
        const int cx = int(f.p.x.floorInt()), cz = int(f.p.y.floorInt());
        int piece = -1;
        for (int r = 0; r <= 3 && piece < 0; ++r)
            for (int dz = -r; dz <= r && piece < 0; ++dz)
                for (int dx = -r; dx <= r; ++dx) {
                    const int x = cx + dx, z = cz + dz;
                    if (x < 0 || z < 0 || x >= net_.w || z >= net_.h) continue;
                    const int pi = net_.pieceAt[size_t(z * net_.w + x)];
                    if (pi >= 0 && net_.pieces[size_t(pi)].lanes > 0 && net_.pieces[size_t(pi)].length() >= 5) { piece = pi; break; }
                }
        if (piece < 0) continue;
        const auto& P = net_.pieces[size_t(piece)];
        const bool high = tl::trafficAcrossOf(P, f.p) >= P.centre();
        BusStop s;
        s.piece = piece;
        s.dir = int8_t((tl::trafficLaneAcross(net_, P, 1, 0) >= P.centre()) == high ? 1 : -1);
        s.along = fxClamp(tl::trafficAlongOf(P, f.p), Fixed::fromInt(P.along0 + 2), Fixed::fromInt(P.along1 - 1));
        s.kerb = f.p;
        stops_.push_back(s);
    }
    if (rules_.autoStops > 0)
        for (size_t pi = 0; pi < net_.pieces.size(); pi += size_t(rules_.autoStops)) {
            const auto& P = net_.pieces[pi];
            if (P.lanes <= 0 || P.length() < 8) continue;
            for (int dir = -1; dir <= 1; dir += 2) {
                BusStop s;
                s.piece = int32_t(pi); s.dir = int8_t(dir);
                s.along = Fixed::fromRatio(P.along0 + P.along1 + 1, 2);
                const Fixed sign = tl::trafficLaneAcross(net_, P, dir, 0) >= P.centre() ? kUnit : -kUnit;
                s.kerb = tl::trafficDetail::at(P, s.along, P.centre() + sign * (Fixed::fromInt(P.width()) / 2 + kHalf));
                stops_.push_back(s);
            }
        }
    std::stable_sort(stops_.begin(), stops_.end(), [](const BusStop& a, const BusStop& b) { return a.piece != b.piece ? a.piece < b.piece : a.along < b.along; });
    // two stops within three cells on the same side are one (a shelter next to an automatic stop: the bus would call
    // at each in turn and never leave); the one placed first stays
    std::vector<BusStop> kept;
    for (auto& s : stops_) {
        bool twin = false;
        for (auto& k : kept) twin = twin || (k.piece == s.piece && k.dir == s.dir && fxAbs(k.along - s.along) < Fixed::fromInt(3));
        if (!twin) kept.push_back(s);
    }
    stops_ = std::move(kept);
}

// ================================================================= queries

tl::TrafficLight Traffic::light(Simulation& sim, int j, tl::TrafficAxis axis) const {
    if (j < 0 || size_t(j) >= net_.junctions.size() || !net_.junctions[size_t(j)].signal) return tl::TrafficLight::Green;
    return tl::trafficLight(rules_.timing, offset_[size_t(j)], sim.tick(), axis);
}

int Traffic::junctionAt(Fixed x, Fixed z) const {
    const int cx = int(x.floorInt()), cz = int(z.floorInt());
    int best = -1; int64_t bd = 0;
    for (size_t i = 0; i < net_.junctions.size(); ++i) {
        const auto& jn = net_.junctions[i];
        const int dx = cx < jn.x0 ? jn.x0 - cx : cx > jn.x1 ? cx - jn.x1 : 0, dz = cz < jn.z0 ? jn.z0 - cz : cz > jn.z1 ? cz - jn.z1 : 0;
        const int64_t d = int64_t(dx) * dx + int64_t(dz) * dz;
        if (d > 9) continue;
        if (best < 0 || d < bd) { best = int(i); bd = d; }
    }
    return best;
}

bool Traffic::stripAtEnd(const tl::TrafficPiece& p, int end) const {
    const int a = end == 0 ? p.along0 : p.along1;
    const int cell = p.axis == tl::TrafficAxis::X ? p.across0 * net_.w + a : a * net_.w + p.across0;
    return stripAt(cell) >= 0;
}

bool Traffic::onRoad(Vec2 p) const {
    const int x = int(p.x.floorInt()), z = int(p.y.floorInt());
    if (x < 0 || z < 0 || x >= net_.w || z >= net_.h) return false;
    const size_t i = size_t(z * net_.w + x);
    return net_.pieceAt[i] >= 0 || net_.junctionAt[i] >= 0;
}

bool Traffic::walkAllowed(Simulation& sim, int strip) const {
    if (strip < 0 || size_t(strip) >= strips_.size()) return true;
    const Strip& s = strips_[size_t(strip)];
    if (!net_.junctions[size_t(s.junction)].signal) return true;
    // no new start in the last moments of the window: the walker would reach the road after it closed
    return tl::trafficWalk(rules_.timing, offset_[size_t(s.junction)], sim.tick(), tl::TrafficAxis(s.axis), sim.tickRate() * 3 / 2);
}

bool Traffic::aiInput(Simulation& sim, Entity vehicle, tl::VehicleInput& out) const {
    auto* st = store(sim);
    if (!st) return false;
    auto it = std::lower_bound(st->cars.begin(), st->cars.end(), vehicle.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
    if (it == st->cars.end() || it->e != vehicle) return false;
    out = it->input;
    return true;
}

uint16_t Traffic::pick(Simulation& sim, const std::vector<TrafficWeighted>& list) const {
    int total = 0;
    for (auto& t : list) total += t.weight;
    if (total <= 0) return UINT16_MAX;
    int r = int(sim.rng().below(uint32_t(total)));
    for (auto& t : list) { if (r < t.weight) return t.type; r -= t.weight; }
    return list.back().type;
}

bool Traffic::isPersonType(uint16_t t) const {
    for (auto& p : rules_.people) if (p.type == t) return true;
    for (auto& p : rules_.population.types) if (p.type == t) return true;
    return false;
}

void Traffic::focus(Simulation& sim, std::vector<Vec2>& out) const {
    out.clear();
    TopDown* td = m_->topDown();
    auto* ts = td && td->active() ? td->store(sim) : nullptr;
    if (!ts) return;
    World& w = sim.world();
    for (auto& p : ts->pilots) if (p.hero.valid() && w.alive(p.hero)) out.push_back(v2(w.get<Transform>(p.hero).pos));
}

namespace {
Fixed nearestSq(const std::vector<Vec2>& foci, Vec2 p) {
    Fixed best = Fixed::fromInt(1 << 20);
    for (auto& f : foci) best = fxMin(best, (f - p).lengthSq());
    return best;
}
} // namespace

// ================================================================= hash / save

void Traffic::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.u32(uint32_t(st->cars.size()));
    for (auto& c : st->cars) {
        h.u64(c.e.packed()); h.u32(c.kind); h.i32(c.lastStop); h.u32(c.dwellUntil); h.i32(c.line); h.u32(c.lineLeg);
        h.i32(c.service); h.i32(c.citizen); h.u32(c.task); h.u32(c.call); h.u32(c.taskUntil); h.i32(c.destPiece); h.i32(c.homePiece); h.fx(c.destAlong); h.fx(c.homeAlong);
        h.u32(uint32_t(c.crew.size()));
        for (Entity m : c.crew) h.u64(m.packed());
        h.u32(c.wp); h.i32(c.tailPiece); h.i32(c.tailDir); h.u32(c.tailLane); h.fx(c.cruise);
        h.u32(c.stopped); h.u64(c.blocker.packed()); h.u64(c.ignore.packed()); h.u32(c.ignoreUntil); h.u32(c.reverseUntil); h.u32(c.lostSince);
        h.u32(uint32_t(c.route.size()));
        for (auto& p : c.route) { h.fx(p.p.x); h.fx(p.p.y); h.i32(p.junction); h.u32(uint32_t(p.axis) | uint32_t(p.turn) << 8); }
    }
    h.u32(uint32_t(st->people.size()));
    for (auto& p : st->people) {
        h.u64(p.e.packed()); h.u32(uint32_t(p.state) | uint32_t(p.panic) << 8); h.u32(p.idx); h.u32(p.legEnd); h.u32(p.until); h.u32(p.moved);
        h.i32(p.citizen); h.i32(p.goal);
        h.fx(p.side); h.fx(p.hp); h.v3(p.lastPos); h.u32(p.legPt); h.u32(uint32_t(p.leg.size()));
        for (const Vec3& q : p.leg) h.v3(q);
        h.u32(uint32_t(p.path.size()));
        for (int32_t c : p.path) h.i32(c);
    }
    h.u32(uint32_t(st->lights.size()));
    for (auto& l : st->lights) { h.u64(l.e.packed()); h.i32(l.junction); h.u32(uint32_t(l.axis) | uint32_t(l.shown) << 8); }
    h.u32(uint32_t(st->incidents.size()));
    for (auto& i : st->incidents) {
        h.u32(i.id); h.u32(uint32_t(i.kind) | uint32_t(i.state) << 8); h.fx(i.pos.x); h.fx(i.pos.y); h.u64(i.subject.packed()); h.u64(i.offender.packed());
        h.u32(i.born); h.u32(i.assigned); h.u64(i.responder.packed()); h.u32(i.victim); h.u32(i.fatal); h.i32(i.citizen);
    }
    h.u32(uint32_t(st->patients.size()));
    for (auto& q : st->patients) { h.u32(q.type); h.fx(q.at.x); h.fx(q.at.y); h.u32(q.release); h.i32(q.citizen); }
    h.u32(uint32_t(st->citizens.size())); h.u32(st->citizenCursor);
    {
        const TrafficGang& g = st->gang;
        h.u32(g.state); h.i32(g.leader); h.i32(g.target); h.i32(g.loot); h.u64(g.car.packed()); h.u32(g.until); h.u32(g.call);
        h.u32(uint32_t(g.members.size())); for (int32_t m : g.members) h.i32(m);
        h.u32(uint32_t(g.crew.size())); for (Entity m : g.crew) h.u64(m.packed());
        h.u32(st->nextGang); h.u64(uint64_t(st->gangLoot)); h.u32(st->gangRobberies); h.u32(st->gangCaught);
    }
    for (auto& c : st->citizens) {
        h.u32(uint32_t(c.type) | uint32_t(c.shift) << 16 | uint32_t(c.state) << 24); h.i32(c.home); h.i32(c.work); h.u64(c.e.packed());
        h.fx(c.from.x); h.fx(c.from.y); h.u32(c.left); h.u32(c.eta);
        h.u32(uint32_t(c.commute) | uint32_t(c.leg) << 8 | uint32_t(c.carType) << 16); h.u64(c.car.packed()); h.i32(c.stopA); h.i32(c.stopB); h.i32(c.line); h.i32(c.school); h.u32(c.via);
    }
    h.u32(uint32_t(st->blows.size()));
    for (auto& b : st->blows) { h.u64(b.e.packed()); h.fx(b.damage); h.u32(b.tick); }
    h.u32(uint32_t(st->happened.size()));
    for (auto& e : st->happened) { h.u32(uint32_t(e.type) | uint32_t(e.unitType) << 16); h.v3(e.pos); h.u64(e.a.packed()); h.u64(e.b.packed()); h.fx(e.blow); }
    h.u32(st->nextIncident); h.u32(st->towed); h.u32(uint32_t(st->used.size()));
    for (auto& u : st->used) { h.u64(u.e.packed()); h.u32(u.since); }
    h.u32(uint32_t(st->lit.size()));
    for (int32_t j : st->lit) h.i32(j);
    h.u32(uint32_t(st->placed)); h.u32(st->nextAdopt); h.u32(st->nextEmergency);
}

void io(Archive& a, TrafficWaypoint& p) { a(p.p.x, p.p.y, p.junction, p.axis, p.turn); }
void io(Archive& a, TrafficCar& c) {
    a(c.service, c.task, c.call, c.taskUntil, c.destPiece, c.homePiece, c.destAlong, c.homeAlong, c.crew, c.citizen, c.line, c.lineLeg);
    a(c.e, c.kind, c.lastStop, c.dwellUntil, c.route, c.wp, c.tailPiece, c.tailDir, c.tailLane, c.cruise, c.stopped, c.blocker, c.ignore, c.ignoreUntil, c.reverseUntil, c.lostSince);
}
void io(Archive& a, TrafficPerson& p) { a(p.e, p.state, p.path, p.idx, p.legEnd, p.leg, p.legPt, p.until, p.moved, p.side, p.hp, p.lastPos, p.panic, p.citizen, p.goal); }
void io(Archive& a, TrafficCitizen& c) { a(c.type, c.home, c.work, c.shift, c.state, c.e, c.from.x, c.from.y, c.left, c.eta, c.commute, c.leg, c.carType, c.car, c.stopA, c.stopB, c.line, c.school, c.via); }
void io(Archive& a, TrafficIncident& i) { a(i.id, i.kind, i.state, i.pos.x, i.pos.y, i.subject, i.offender, i.born, i.assigned, i.responder, i.victim, i.fatal, i.citizen); }
void io(Archive& a, TrafficPatient& q) { a(q.type, q.at.x, q.at.y, q.release, q.citizen); }
void io(Archive& a, TrafficBlow& b) { a(b.e, b.damage, b.tick); }
void io(Archive& a, TrafficEvent& e) { a(e.type, e.unitType, e.pos, e.a, e.b, e.blow); }
void io(Archive& a, TrafficUsedCar& u) { a(u.e, u.since); }
void io(Archive& a, TrafficLightProp& l) { a(l.e, l.junction, l.axis, l.shown); }

void Traffic::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    a(st->cars, st->people, st->lights, st->lit, st->placed, st->nextAdopt, st->nextEmergency);
    a(st->incidents, st->nextIncident, st->used, st->towed, st->patients, st->happened, st->blows, st->citizens, st->citizenCursor);
    {
        TrafficGang& g = st->gang;
        a(g.state, g.leader, g.target, g.loot, g.members, g.car, g.crew, g.until, g.call, st->nextGang, st->gangLoot, st->gangRobberies, st->gangCaught);
    }
    if (!a.writing()) for (auto& c : st->cars) c.input = {};
}

// ================================================================= per tick

void Traffic::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    std::vector<Vec2> foci;
    focus(sim, foci);
    if (!st->placed || sim.tick() % 10 == 0) syncLights(sim, *st, foci);
    updateLights(sim, *st);
    if (!st->placed) populate(sim, *st);
    incidents(sim, *st);
    cars(sim, *st, foci);
    population(sim, *st, foci);
    gangs(sim, *st, foci);
    people(sim, *st, foci);
    st->happened.clear();
    st->placed = true;
}

void Traffic::collect(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    st->blows.erase(std::remove_if(st->blows.begin(), st->blows.end(), [&](const TrafficBlow& b) { return b.tick + 3 < sim.tick(); }), st->blows.end());
    for (const SimEvent& ev : sim.events()) {
        if (ev.type != kEvAttack && ev.type != kEvHit && ev.type != kEvDied) continue;
        Fixed blow{};
        if (ev.type == kEvHit && st->blows.size() < 1024) st->blows.push_back({ev.a, Fixed::fromRaw(ev.value), sim.tick()});
        if (ev.type == kEvDied) for (auto& b : st->blows) if (b.e == ev.a) blow = fxMax(blow, b.damage);
        if (st->happened.size() < 4096) st->happened.push_back({ev.type, ev.type == kEvDied ? uint16_t(ev.value) : uint16_t(0), ev.pos, ev.a, ev.b, blow});
    }
}

// ----------------------------------------------------------------- lights

// Light props exist only at the junctions within signals.radius of a hero (the signal phases themselves are a pure
// function of the tick, so nothing is lost while a junction has no props): a city of any size costs the same.
void Traffic::syncLights(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci) {
    if (!rules_.placeLights || rules_.light[0] == UINT16_MAX || foci.empty()) return;
    World& w = sim.world();
    const Fixed R = rules_.lightRadius, out = R + Fixed::fromInt(4);
    // too far from every hero: remove (with some slack, so a hero pacing at the edge does not churn them)
    for (size_t i = 0; i < st.lit.size();) {
        if (nearestSq(foci, net_.junctions[size_t(st.lit[i])].centre()) <= out * out) { ++i; continue; }
        const int32_t j = st.lit[i];
        for (size_t k = 0; k < st.lights.size();) {
            if (st.lights[k].junction != j) { ++k; continue; }
            silentRemove(sim, st.lights[k].e);
            st.lights.erase(st.lights.begin() + long(k));
        }
        st.lit.erase(st.lit.begin() + long(i));
    }
    for (const Vec2& f : foci) {
        const int bx0 = std::max(0, int((f.x - R).floorInt()) / 16), bx1 = std::min(bw_ - 1, int((f.x + R).floorInt()) / 16);
        const int bz0 = std::max(0, int((f.y - R).floorInt()) / 16), bz1 = std::min(bh_ - 1, int((f.y + R).floorInt()) / 16);
        for (int bz = bz0; bz <= bz1; ++bz)
            for (int bx = bx0; bx <= bx1; ++bx)
                for (int32_t j : jbucket_[size_t(bz * bw_ + bx)]) {
                    if ((net_.junctions[size_t(j)].centre() - f).lengthSq() > R * R) continue;
                    auto it = std::lower_bound(st.lit.begin(), st.lit.end(), j);
                    if (it != st.lit.end() && *it == j) continue;
                    st.lit.insert(it, j);
                    placeLightsAt(sim, st, j);
                }
    }
    (void)w;
}

void Traffic::placeLightsAt(Simulation& sim, TrafficStore& st, int junction) {
    const size_t ji = size_t(junction);
    {
        const auto& jn = net_.junctions[ji];
        for (int sd = 0; sd < 4; ++sd) {
            const int pi = jn.piece[size_t(sd)];
            if (pi < 0 || net_.pieces[size_t(pi)].lanes <= 0) continue;
            const auto& p = net_.pieces[size_t(pi)];
            const int out = tl::trafficExitDir(sd), in = -out;            // traffic of this arm drives `in` into the box
            const Fixed edge = tl::trafficPieceEnd(p, in);                 // stop line
            // the curb on the right of the incoming traffic, just outside the band, a little before the stop line
            const Fixed laneOuter = tl::trafficLaneAcross(net_, p, in, 0) - p.centre();
            const Fixed sign = laneOuter.raw >= 0 ? kUnit : -kUnit;
            const Fixed across = p.centre() + sign * (Fixed::fromInt(p.width()) / 2 + Fixed::fromRatio(3, 10));
            const Fixed back = stripAtEnd(p, in > 0 ? 1 : 0) ? Fixed::fromRatio(13, 10) : Fixed::fromRatio(3, 10);   // before the crossing
            const Fixed along = edge - Fixed::fromInt(in) * back;
            const Vec2 pos = tl::trafficDetail::at(p, along, across);
            const Entity e = m_->spawn(sim, rules_.light[0], 0, v3(pos));
            if (!e.valid()) continue;
            const Vec2 face = tl::trafficDetail::axisDir(p.axis, out);    // looks at the incoming traffic
            sim.world().get<Unit>(e).facing = v3(face);
            st.lights.push_back({e, int32_t(ji), uint8_t(p.axis), 255});
        }
    }
}

void Traffic::updateLights(Simulation& sim, TrafficStore& st) {
    World& w = sim.world();
    for (auto& l : st.lights) {
        if (!w.alive(l.e) || w.get<Unit>(l.e).hp.raw <= 0) continue;
        const uint8_t s = uint8_t(light(sim, l.junction, tl::TrafficAxis(l.axis)));
        if (s == l.shown) continue;
        l.shown = s;
        if (rules_.light[s] != UINT16_MAX) showType(*m_, sim, l.e, rules_.light[s]);
    }
}

// ----------------------------------------------------------------- cars

void Traffic::extendRoute(Simulation& sim, TrafficCar& c) {
    for (int guard = 0; guard < 4 && c.route.size() < size_t(c.wp) + 6; ++guard) {
        if (c.tailPiece < 0 || size_t(c.tailPiece) >= net_.pieces.size()) return;
        const auto& P = net_.pieces[size_t(c.tailPiece)];
        const int s = c.tailDir, k = c.tailLane;
        const Vec2 E = c.route.empty() ? tl::trafficLanePoint(net_, P, s, k, tl::trafficPieceEnd(P, s)) : c.route.back().p;
        const int J = P.junction[s > 0 ? 1 : 0];
        std::vector<Vec2> pts;
        int Q = c.tailPiece, s2 = -s;
        if (J < 0) {   // dead end / map edge: turn around on the road
            const int k2 = std::min(k, std::max(P.lanes - 1, 0));
            const Fixed back = Fixed::fromInt(std::min(2, P.length()));
            const Vec2 X = tl::trafficLanePoint(net_, P, s2, k2, tl::trafficPieceEnd(P, s) - Fixed::fromInt(s) * back);
            const Vec2 C = (E + X) * kHalf + tl::trafficDetail::axisDir(P.axis, s) * Fixed::fromRatio(1, 2);
            tl::trafficCurve(E, C, X, 5, pts);
            for (auto& q : pts) c.route.push_back({q, -1, 0, 1});
            c.tailDir = int8_t(s2); c.tailLane = uint8_t(k2);
            const auto& PP = net_.pieces[size_t(Q)];
            const int jEnd = PP.junction[s2 > 0 ? 1 : 0];
            c.route.push_back({tl::trafficLanePoint(net_, PP, s2, k2, tl::trafficPieceEnd(PP, s2)), int32_t(jEnd), uint8_t(PP.axis), 0});
            continue;
        }
        const auto& jn = net_.junctions[size_t(J)];
        const int inSide = tl::trafficSideOf(P, s > 0 ? 1 : 0);
        int total = 0, cand[4] = {-1, -1, -1, -1}, wgt[4] = {0, 0, 0, 0};
        for (int sd = 0; sd < 4; ++sd) {
            const int q = jn.piece[size_t(sd)];
            if (sd == inSide || q < 0 || net_.pieces[size_t(q)].lanes <= 0) continue;
            cand[sd] = q;
            wgt[sd] = net_.pieces[size_t(q)].axis == P.axis ? rules_.straight : rules_.turn;
            total += wgt[sd];
        }
        int exitSide = inSide;
        if (c.destPiece >= 0 && (c.task == kTaskGoing || c.task == kTaskHome || c.task == kTaskTrip || c.task == kTaskGang)) {   // on call, a citizen's trip: the way to the address
            exitSide = exitToward(J, inSide, c.tailPiece, c.destPiece);
        } else if (c.kind == kTrafficBus && c.line >= 0 && size_t(c.line) < lines_.size()) {   // the line: on to its next point
            const auto& via = lines_[size_t(c.line)].via;
            for (size_t n = 0; n < via.size() && via[c.lineLeg % via.size()] == c.tailPiece; ++n) c.lineLeg = uint16_t((c.lineLeg + 1) % via.size());
            exitSide = exitToward(J, inSide, c.tailPiece, via[c.lineLeg % via.size()]);
        } else if (total > 0) {
            int r = int(sim.rng().below(uint32_t(total)));
            for (int sd = 0; sd < 4; ++sd) { if (cand[sd] < 0) continue; if (r < wgt[sd]) { exitSide = sd; break; } r -= wgt[sd]; }
        }
        Q = exitSide == inSide ? c.tailPiece : cand[exitSide];
        const auto& QP = net_.pieces[size_t(Q)];
        s2 = tl::trafficExitDir(exitSide);
        const int k2 = std::min(k, QP.lanes - 1);
        const Vec2 X = tl::trafficLanePoint(net_, QP, s2, k2, tl::trafficPieceStart(QP, s2));
        if (const Ring& rg = ring_[size_t(J)]; rg.on) {   // round the island, keeping it on the driver's side
            // corners in driving order (right-hand traffic: SE, NE, NW, SW on a map with z down the screen)
            const Vec2 rh[4] = {{rg.x1, rg.z1}, {rg.x1, rg.z0}, {rg.x0, rg.z0}, {rg.x0, rg.z1}};
            const Vec2 lh[4] = {{rg.x0, rg.z1}, {rg.x0, rg.z0}, {rg.x1, rg.z0}, {rg.x1, rg.z1}};
            // first corner by the side the car comes in through, last one by the side it leaves through
            static const int inRh[4] = {3, 1, 2, 0}, outRh[4] = {2, 0, 1, 3};   // index: kTrafficWest, East, North, South
            static const int inLh[4] = {1, 3, 2, 0}, outLh[4] = {0, 2, 1, 3};
            const bool left = rules_.net.leftHand;
            int k0 = (left ? inLh : inRh)[inSide];
            const int k1 = (left ? outLh : outRh)[exitSide];
            for (int n = 0; n < 4; ++n) { pts.push_back((left ? lh : rh)[k0]); if (k0 == k1) break; k0 = (k0 + 1) % 4; }
            pts.push_back(X);
        } else if (Q == c.tailPiece) {   // nowhere else to go: U-turn inside the box
            const Fixed depth = Fixed::fromInt(P.axis == tl::TrafficAxis::X ? jn.x1 - jn.x0 + 1 : jn.z1 - jn.z0 + 1);
            const Vec2 C = (E + X) * kHalf + tl::trafficDetail::axisDir(P.axis, s) * (depth * Fixed::fromRatio(3, 4));
            tl::trafficCurve(E, C, X, 6, pts);
        } else if (QP.axis == P.axis) {
            pts.push_back(X);
        } else {
            const Vec2 C = P.axis == tl::TrafficAxis::X ? Vec2{X.x, E.y} : Vec2{E.x, X.y};
            tl::trafficCurve(E, C, X, 4, pts);
        }
        const uint8_t turning = QP.axis == P.axis && Q != c.tailPiece && !ring_[size_t(J)].on ? 0 : 1;
        for (auto& q : pts) c.route.push_back({q, -1, 0, turning});
        const int jEnd = QP.junction[s2 > 0 ? 1 : 0];
        c.route.push_back({tl::trafficLanePoint(net_, QP, s2, k2, tl::trafficPieceEnd(QP, s2)), int32_t(jEnd), uint8_t(QP.axis), 0});
        c.tailPiece = int32_t(Q); c.tailDir = int8_t(s2); c.tailLane = uint8_t(k2);
    }
}

// New route from where the car is (pushed off its lane, just adopted): the lane of the piece under it that runs most
// like its heading, or the arm of the junction it is in that it faces.
bool Traffic::reroute(Simulation& sim, TrafficCar& c, Vec2 pos, Vec2 fwd) {
    (void)sim;
    const int w = net_.w, h = net_.h;
    const int cx = int(pos.x.floorInt()), cz = int(pos.y.floorInt());
    int piece = -1, junction = -1;
    for (int r = 0; r <= 2 && piece < 0 && junction < 0; ++r)
        for (int dz = -r; dz <= r && piece < 0 && junction < 0; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= w || z >= h) continue;
                const size_t i = size_t(z * w + x);
                if (net_.pieceAt[i] >= 0 && net_.pieces[size_t(net_.pieceAt[i])].lanes > 0) { piece = net_.pieceAt[i]; break; }
                if (net_.junctionAt[i] >= 0) { junction = net_.junctionAt[i]; break; }
            }
    c.route.clear(); c.wp = 0;
    if (piece < 0 && junction >= 0) {
        const auto& jn = net_.junctions[size_t(junction)];
        Fixed best{}; int bestSide = -1;
        for (int sd = 0; sd < 4; ++sd) {
            const int q = jn.piece[size_t(sd)];
            if (q < 0 || net_.pieces[size_t(q)].lanes <= 0) continue;
            const Fixed d = tl::trafficDetail::axisDir(net_.pieces[size_t(q)].axis, tl::trafficExitDir(sd)).dot(fwd);
            if (bestSide < 0 || d > best) { best = d; bestSide = sd; }
        }
        if (bestSide < 0) return false;
        piece = jn.piece[size_t(bestSide)];
        const auto& P = net_.pieces[size_t(piece)];
        const int s = tl::trafficExitDir(bestSide);
        c.route.push_back({tl::trafficLanePoint(net_, P, s, 0, tl::trafficPieceStart(P, s)), -1, 0, 1});
        c.tailPiece = int32_t(piece); c.tailDir = int8_t(s); c.tailLane = 0;
        c.route.push_back({tl::trafficLanePoint(net_, P, s, 0, tl::trafficPieceEnd(P, s)), int32_t(P.junction[s > 0 ? 1 : 0]), uint8_t(P.axis), 0});
        return true;
    }
    if (piece < 0) return false;
    const auto& P = net_.pieces[size_t(piece)];
    const int s = tl::trafficDetail::axisDir(P.axis, 1).dot(fwd).raw >= 0 ? 1 : -1;
    int lane = 0; Fixed bd{};
    for (int k = 0; k < P.lanes; ++k) {
        const Fixed d = fxAbs(tl::trafficLaneAcross(net_, P, s, k) - tl::trafficAcrossOf(P, pos));
        if (k == 0 || d < bd) { bd = d; lane = k; }
    }
    // rejoin a little ahead of where the car is
    const Fixed along = fxClamp(tl::trafficAlongOf(P, pos) + Fixed::fromInt(s) * Fixed::fromRatio(3, 2), Fixed::fromInt(P.along0), Fixed::fromInt(P.along1 + 1));
    c.route.push_back({tl::trafficLanePoint(net_, P, s, lane, along), -1, 0, 0});
    c.tailPiece = int32_t(piece); c.tailDir = int8_t(s); c.tailLane = uint8_t(lane);
    c.route.push_back({tl::trafficLanePoint(net_, P, s, lane, tl::trafficPieceEnd(P, s)), int32_t(P.junction[s > 0 ? 1 : 0]), uint8_t(P.axis), 0});
    return true;
}

// A random cell of the square around a hero that is a lane of a driven road, in the spawn ring: the cost does not
// depend on the size of the city.
bool Traffic::spawnCar(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist, uint8_t kind, int service, Entity* out) {
    const auto& fleet = service >= 0 ? rules_.services[size_t(service)].types : kind == kTrafficBus ? rules_.buses : kind == kTrafficEmergency ? rules_.emergency : rules_.cars;
    if (fleet.empty()) return false;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const int R = int(rules_.spawnMax.toInt()) + 1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        int cx, cz;
        if (!foci.empty()) {
            const Vec2 f = foci[sim.rng().below(uint32_t(foci.size()))];
            const int fx = int(f.x.floorInt()), fz = int(f.y.floorInt());   // the square around the hero, cut to the map
            cx = sim.rng().range(std::max(0, fx - R), std::max(0, std::min(net_.w - 1, fx + R)));
            cz = sim.rng().range(std::max(0, fz - R), std::max(0, std::min(net_.h - 1, fz + R)));
        } else { cx = int(sim.rng().below(uint32_t(net_.w))); cz = int(sim.rng().below(uint32_t(net_.h))); }
        // a bus of a line: somewhere on the line that is shortest of buses (lines take turns to be tried first)
        const BusLine::Step* onLine = nullptr;
        int line = -1;
        if (kind == kTrafficBus && service < 0 && !lines_.empty()) {
            int bestCount = 0;
            const size_t turn = sim.tick() / 20;
            for (size_t n = 0; n < lines_.size(); ++n) {
                const size_t li = (n + turn) % lines_.size();
                int count = 0;
                for (auto& o : st.cars) count += o.kind == kTrafficBus && o.line == int(li);
                if (line < 0 || count < bestCount) { line = int(li); bestCount = count; }
            }
            const auto& path = lines_[size_t(line)].path;
            onLine = &path[sim.rng().below(uint32_t(path.size()))];
            const auto& LP = net_.pieces[size_t(onLine->piece)];
            if (LP.length() < 3) continue;
            const int a = sim.rng().range(LP.along0 + 1, LP.along1 - 1);
            cx = LP.axis == tl::TrafficAxis::X ? a : LP.across0;
            cz = LP.axis == tl::TrafficAxis::X ? LP.across0 : a;
        }
        if (cx < 0 || cz < 0 || cx >= net_.w || cz >= net_.h) continue;
        const int pi = net_.pieceAt[size_t(cz * net_.w + cx)];
        if (pi < 0) continue;
        const auto& P = net_.pieces[size_t(pi)];
        const int ac = P.axis == tl::TrafficAxis::X ? cx : cz;
        if (P.lanes <= 0 || P.length() < 3 || ac <= P.along0 || ac >= P.along1) continue;   // keep away from the junctions
        int s = sim.rng().below(2) ? 1 : -1;
        if (!foci.empty()) {   // heading toward the hero rather than out of the simulated area
            Vec2 near = foci[0];
            const Vec2 here{Fixed::fromInt(cx) + kHalf, Fixed::fromInt(cz) + kHalf};
            for (auto& f : foci) if ((f - here).lengthSq() < (near - here).lengthSq()) near = f;
            const Fixed toward = tl::trafficDetail::axisDir(P.axis, 1).dot(near - here);
            if (fxAbs(toward) > Fixed::fromInt(3)) s = toward.raw > 0 ? 1 : -1;
        }
        if (onLine) s = onLine->dir;
        const uint16_t lineLeg = onLine ? onLine->leg : 0;
        const int k = kind == kTrafficBus ? P.lanes - 1 : int(sim.rng().below(uint32_t(P.lanes)));   // buses keep to the kerb
        const Fixed along = Fixed::fromInt(ac) + Fixed::fromRatio(int(sim.rng().below(8)), 8);
        const Vec2 pos = tl::trafficLanePoint(net_, P, s, k, along);
        if (!foci.empty()) {
            const Fixed d2 = nearestSq(foci, pos);
            if (d2 < minDist * minDist || d2 > rules_.spawnMax * rules_.spawnMax) continue;
        }
        if (!gs.grid.passableAt(v3(pos), kLayerGround)) continue;
        // the lane must be free a few cells around the spot (parked cars at the curb next to it are fine)
        const Vec2 dir = tl::trafficDetail::axisDir(P.axis, s);
        bool clear = true;
        const uint16_t type = pick(sim, fleet);
        if (type == UINT16_MAX) return false;
        const Fixed body = m_->rules().types[type].radius;
        gs.hash.query(v3(pos), Fixed::fromInt(6) + body, [&](Entity o) {
            auto* ou = w.tryGet<Unit>(o);
            if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap) || m_->rules().types[ou->type].structure) return;
            const Vec2 rel = v2(w.get<Transform>(o).pos) - pos;
            if (fxAbs(tl::trafficDetail::cross(dir, rel)) < Fixed::fromRatio(7, 10) &&
                fxAbs(rel.dot(dir)) < Fixed::fromInt(3) + body + m_->rules().types[ou->type].radius) clear = false;
        });
        if (!clear) continue;
        const Entity e = m_->spawn(sim, type, service >= 0 ? rules_.services[size_t(service)].owner : rules_.carOwner, v3(pos));
        if (!e.valid()) return false;
        w.get<Unit>(e).facing = v3(dir);
        TrafficCar c;
        c.e = e;
        c.kind = kind;
        const Fixed lo = service >= 0 ? rules_.services[size_t(service)].speedMin : kind == kTrafficBus ? rules_.busMin : kind == kTrafficEmergency ? rules_.emMin : rules_.speedMin;
        const Fixed hi = service >= 0 ? rules_.services[size_t(service)].speedMax : kind == kTrafficBus ? rules_.busMax : kind == kTrafficEmergency ? rules_.emMax : rules_.speedMax;
        c.service = int8_t(service);
        c.line = int16_t(line); c.lineLeg = lineLeg;
        if (out) *out = e;
        c.cruise = lo + (hi - lo) * sim.rng().unit();
        c.tailPiece = int32_t(pi); c.tailDir = int8_t(s); c.tailLane = uint8_t(k);
        c.route.push_back({pos, -1, 0, 0});
        c.route.push_back({tl::trafficLanePoint(net_, P, s, k, tl::trafficPieceEnd(P, s)), int32_t(P.junction[s > 0 ? 1 : 0]), uint8_t(P.axis), 0});
        c.wp = 1;
        m_->driving()->adopt(sim, e, c.cruise * Fixed::fromRatio(3, 4));
        insertSorted(st.cars, std::move(c));
        return true;
    }
    return false;
}

void Traffic::cars(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci) {
    World& w = sim.world();
    Driving* drv = m_->driving();
    if (!drv || !drv->active()) return;
    // gone, wrecked or taken by a hero: no longer traffic
    for (size_t i = 0; i < st.cars.size();) {
        TrafficCar& c = st.cars[i];
        const DrivingCar* dc = w.alive(c.e) && w.get<Unit>(c.e).hp.raw > 0 ? drv->car(sim, c.e) : nullptr;
        if (dc && !dc->driver.valid()) { ++i; continue; }
        if (dc && dc->driver.valid() && (rules_.driverPerson != UINT16_MAX || c.citizen >= 0)) {   // hijacked: the driver gets out on the far side
            const Vec3 cp = w.get<Transform>(c.e).pos, hp = w.get<Transform>(dc->driver).pos;
            Vec3 away = flat(cp - hp);
            const Fixed l = away.length();
            away = l.raw > 0 ? away / l : Vec3{kUnit, Fixed{}, Fixed{}};
            Vec3 spot = cp + away * Fixed::fromRatio(4, 5);
            if (!m_->state(sim).grid.passableAt(spot, kLayerGround)) spot = cp - away * Fixed::fromRatio(4, 5);
            const bool room = m_->state(sim).grid.passableAt(spot, kLayerGround);
            if (c.citizen >= 0) {   // a citizen on his way: he is on the street now and goes on on foot
                const Entity pe = tripDriverOut(sim, st, c, spot, room);
                if (pe.valid()) panic(sim, *byEntity(st.people, pe), v2(hp));
            } else if (room) {
                const Entity pe = m_->spawn(sim, rules_.driverPerson, rules_.peopleOwner, spot);
                if (pe.valid()) {
                    TrafficPerson p; p.e = pe; p.hp = w.get<Unit>(pe).hp; p.lastPos = spot;
                    p.side = Fixed::fromRatio(int(sim.rng().below(21)) - 10, 10) * rules_.side;
                    insertSorted(st.people, p);
                    panic(sim, *byEntity(st.people, pe), v2(hp));
                }
            }
        }
        if (c.kind == kTrafficBus) busGone(sim, st, c.e);   // taken by a hero or wrecked: everybody out (unseen)
        dropCall(sim, st, c);
        st.cars.erase(st.cars.begin() + long(i));
    }
    // too far from every hero: gone (when nobody watches)
    if (!foci.empty())
        for (auto& c : st.cars) {
            if (c.task != kTaskNone && c.task != kTaskTrip) continue;   // on call: it gets there wherever the heroes are
            const Vec2 p = v2(w.get<Transform>(c.e).pos);
            const Fixed d2 = nearestSq(foci, p);
            const bool stuckAway = c.stopped > sim.tickRate() * 30 && d2 > rules_.spawnMin * rules_.spawnMin;
            const bool lostAway = c.lostSince && sim.tick() - c.lostSince > sim.tickRate() * 5 && d2 > rules_.spawnMin * rules_.spawnMin;
            if (d2 > rules_.despawn * rules_.despawn || stuckAway || lostAway) { tripGone(sim, st, c); if (c.kind == kTrafficBus) busGone(sim, st, c.e); silentRemove(sim, c.e); }
        }
    for (size_t i = 0; i < st.cars.size();) {
        if (w.get<Unit>(st.cars[i].e).hp.raw <= 0) { dropCall(sim, st, st.cars[i]); st.cars.erase(st.cars.begin() + long(i)); continue; }
        ++i;
    }
    responders(sim, st);
    trips(sim, st);
    for (size_t i = 0; i < st.cars.size();) {   // drove into its station
        if (w.get<Unit>(st.cars[i].e).hp.raw <= 0) { st.cars.erase(st.cars.begin() + long(i)); continue; }
        ++i;
    }
    // top up: the first tick fills the whole area, later ones only out of sight; a couple per tick at most
    const Fixed minDist = st.placed ? rules_.spawnMin : Fixed::fromInt(4);
    int count[3] = {0, 0, 0};
    for (auto& c : st.cars) if (c.service < 0) count[c.kind]++;
    int budget = st.placed ? 1 : rules_.maxCars * 3;   // the first tick fills the streets
    if (!st.placed || sim.tick() % 5 == 0)
        while (budget-- > 0 && count[kTrafficCar] < rules_.maxCars)
            if (spawnCar(sim, st, foci, minDist, kTrafficCar)) ++count[kTrafficCar];
    if ((!st.placed || sim.tick() % 20 == 0) && count[kTrafficBus] < rules_.maxBuses) spawnCar(sim, st, foci, minDist, kTrafficBus);
    if (rules_.maxEmergency > 0 && !rules_.emergency.empty()) {   // a call-out now and then
        auto draw = [&] { return sim.tick() + std::max<uint32_t>(1, ticksOf(rules_.emIntervalMin + (rules_.emIntervalMax - rules_.emIntervalMin) * sim.rng().unit(), sim.tickRate())); };
        if (st.nextEmergency == 0) st.nextEmergency = draw();
        else if (sim.tick() >= st.nextEmergency) {
            if (count[kTrafficEmergency] < rules_.maxEmergency) spawnCar(sim, st, foci, rules_.spawnMin, kTrafficEmergency);
            st.nextEmergency = draw();
        }
    }
    occupied_.clear();
    for (auto& c : st.cars) {
        const int cell = m_->state(sim).grid.cellOf(w.get<Transform>(c.e).pos);
        const int j = cell >= 0 && size_t(cell) < net_.junctionAt.size() ? net_.junctionAt[size_t(cell)] : -1;
        if (j < 0 || !ring_[size_t(j)].on) continue;
        bool longOne = false;
        for (auto& t : drv->rules().vehicles) if (t.type == w.get<Unit>(c.e).type) longOne = t.length > Fixed::fromInt(2);
        auto it = std::lower_bound(occupied_.begin(), occupied_.end(), j, [](const Occupied& o, int32_t q) { return o.junction < q; });
        if (it == occupied_.end() || it->junction != j) it = occupied_.insert(it, {j, 0, false});
        it->count++; it->longOne = it->longOne || longOne;
    }
    sirens_.clear();
    for (auto& c : st.cars)
        if (c.kind == kTrafficEmergency && (c.task == kTaskNone || c.task == kTaskGoing))   // standing at the scene it is in nobody's way
            if (const DrivingCar* dc = drv->car(sim, c.e)) sirens_.push_back({v2(w.get<Transform>(c.e).pos), Vec2{fxSinTurns(dc->st.yaw), fxCosTurns(dc->st.yaw)}});
    for (auto& c : st.cars) plan(sim, st, c);
}

// The bot driver: route upkeep, the speed it may go (cruise, turns, stop lines, whatever is ahead) and the pedals.
void Traffic::plan(Simulation& sim, TrafficStore& st, TrafficCar& c) {
    World& w = sim.world();
    Driving* drv = m_->driving();
    const DrivingCar* dc = drv->car(sim, c.e);
    if (!dc) { c.input = {}; c.input.handbrake = true; return; }
    if (c.task == kTaskCrewOut || c.task == kTaskWorking || c.task == kTaskCrewBack) {   // at the scene
        c.input = {}; c.input.handbrake = true; c.stopped = 0; c.blocker = kNullEntity;
        return;
    }
    const Vec2 pos = v2(w.get<Transform>(c.e).pos);
    const Fixed yaw = dc->st.yaw;
    const Vec2 fwd{fxSinTurns(yaw), fxCosTurns(yaw)};
    const Fixed v = tl::vehicleForwardSpeed(dc->st);
    const auto* vt = [&]() -> const DrivingVehicleType* { for (auto& t : drv->rules().vehicles) if (t.type == w.get<Unit>(c.e).type) return &t; return nullptr; }();
    const Fixed halfLen = vt ? vt->length / 2 : kHalf, halfWid = vt ? vt->width / 2 : Fixed::fromRatio(1, 4);

    // advance along the route
    while (c.wp < c.route.size()) {
        const Vec2 q = c.route[c.wp].p;
        if ((q - pos).lengthSq() < Fixed::fromRatio(9, 25)) { ++c.wp; continue; }
        if (c.wp > 0 && (pos - q).dot(q - c.route[c.wp - 1].p).raw > 0) { ++c.wp; continue; }   // passed it
        break;
    }
    if (c.wp > 8) { c.route.erase(c.route.begin(), c.route.begin() + (c.wp - 1)); c.wp = 1; }
    extendRoute(sim, c);
    // pushed off the route (hit by the hero, a crash): find the road again
    const Vec2 from = c.wp > 0 ? c.route[c.wp - 1].p : pos;
    const bool off = c.wp >= c.route.size() || segDistSq(pos, from, c.route[c.wp].p) > Fixed::fromRatio(9, 4);
    if (off) {
        if (reroute(sim, c, pos, fwd)) { c.lostSince = 0; extendRoute(sim, c); }
        else if (!c.lostSince) c.lostSince = std::max<uint32_t>(sim.tick(), 1);
    }
    if (c.wp >= c.route.size()) { c.input = {}; c.input.handbrake = true; return; }

    std::vector<Vec2> pts;   // the last point passed, then the ones ahead
    pts.push_back(c.wp > 0 ? c.route[c.wp - 1].p : pos);
    for (size_t i = c.wp; i < c.route.size() && pts.size() < 9; ++i) pts.push_back(c.route[i].p);
    const Fixed vc = fxMax(v, Fixed{}) / mpc_;   // cells/s
    const Fixed look = Fixed::fromRatio(6, 5) + vc * Fixed::fromRatio(1, 5);
    Vec2 target = tl::trafficPursuit(pos, pts, look);
    Vec2 dirRef = tl::trafficDetail::normalised(target - pos);
    if (dirRef.lengthSq().raw == 0) dirRef = fwd;
    // sirens: an emergency vehicle rides the centre line. Whoever drives the same way ahead of it keeps going, close
    // to the kerb and through red lights (a queue that stops would trap it); everybody else near it keeps to the
    // kerb side of its lane and waits at the next junction instead of driving into it.
    bool yielding = false, escort = false;
    Fixed shift{};
    if (c.kind == kTrafficEmergency) shift = -Fixed::fromRatio(35, 100);
    else
        for (auto& sr : sirens_) {
            const Vec2 rel = pos - sr.pos;
            if (rel.lengthSq() > rules_.yieldRadius * rules_.yieldRadius || rel.dot(sr.fwd) <= -kUnit) continue;
            if (fwd.dot(sr.fwd) > kHalf && rel.dot(sr.fwd).raw > 0) { escort = true; shift = Fixed::fromRatio(3, 10); }
            else { yielding = true; shift = Fixed::fromRatio(3, 10); }
            break;
        }
    if (shift.raw == 0) shift = tripShift(sim, c, pos, fwd, target);   // pulling in to the kerb at his address
    if (shift.raw != 0) {
        const Vec2 rv = rightOnScreen(dirRef) * (rules_.net.leftHand ? -shift : shift);
        target += rv;
        const Vec2 d2 = tl::trafficDetail::normalised(target - pos);
        if (d2.lengthSq().raw > 0) dirRef = d2;
    }

    Fixed want = c.cruise;
    const MapGrid& gs0 = m_->state(sim).grid;
    // long vehicles swing wide: they take bends at walking pace (slower than anything that would run a person over)
    const Fixed turnSpeed = halfLen > Fixed::fromRatio(4, 5) ? fxMin(rules_.turnSpeed, Fixed::fromRatio(5, 2)) : rules_.turnSpeed;
    // turns and signals ahead, by distance along the route
    bool heldBySignal = false;
    Fixed dist = (c.route[c.wp].p - pos).length();
    for (size_t i = c.wp; i < c.route.size() && dist < Fixed::fromInt(24); ++i) {
        const TrafficWaypoint& q = c.route[i];
        if (q.turn) want = fxMin(want, tl::trafficStopSpeed(fxMax(dist - kHalf, Fixed{}) * mpc_, rules_.decel) + turnSpeed);
        if (q.junction >= 0 && (yielding || net_.junctions[size_t(q.junction)].signal || ring_[size_t(q.junction)].on)) {
            const tl::TrafficLight L = light(sim, q.junction, tl::TrafficAxis(q.axis));   // green without a signal
            // the nose stops at the line; the line is before the pedestrian crossing when there is one
            const Vec2 back = i > 0 ? tl::trafficDetail::normalised(c.route[i - 1].p - q.p) * kHalf : Vec2{};
            const int lineCell = gs0.cellOf(v3(q.p + back));
            const Fixed strip = lineCell >= 0 && stripAt(lineCell) >= 0 ? kUnit : Fixed{};
            const Fixed stopAt = dist - halfLen - Fixed::fromRatio(1, 5) - strip;
            bool stop = (L == tl::TrafficLight::Red && !escort) || yielding;
            if (L == tl::TrafficLight::Yellow) {   // stop when it comfortably can, else go through
                const Fixed need = v * v / (rules_.decel * 2) / mpc_;
                stop = stopAt > need && !escort;
            }
            // the lane round an island is tight: nobody joins a long vehicle on it, and a long vehicle waits for it to
            // be empty (a bus and a car side by side in the bend wedge each other)
            if (ring_[size_t(q.junction)].on && c.kind != kTrafficEmergency) {
                auto it = std::lower_bound(occupied_.begin(), occupied_.end(), q.junction, [](const Occupied& o, int32_t j) { return o.junction < j; });
                if (it != occupied_.end() && it->junction == q.junction && (it->longOne || halfLen > kUnit)) stop = true;
            }
            if (stop && c.kind == kTrafficEmergency) {   // through the red light, carefully
                want = fxMin(want, tl::trafficStopSpeed(fxMax(stopAt, Fixed{}) * mpc_, rules_.decel) + rules_.turnSpeed);
            } else if (stop && stopAt > -halfLen) {
                const Fixed vs = stopAt.raw > 0 ? tl::trafficStopSpeed(stopAt * mpc_, rules_.decel) : Fixed{};
                if (vs < want) { want = vs; heldBySignal = true; }
            }
            break;   // only the next signal matters
        }
        if (i + 1 < c.route.size()) dist = dist + (c.route[i + 1].p - q.p).length();
    }

    // what is ahead: cars, people, the hero, parked cars; a corridor along the pursuit direction
    GameState& gs = m_->state(sim);
    const int hereCell = gs.grid.cellOf(v3(pos));
    const bool inMap = hereCell >= 0 && size_t(hereCell) < net_.pieceAt.size();
    if ((c.task == kTaskGoing || c.task == kTaskHome || c.task == kTaskTrip || c.task == kTaskGang) && inMap && net_.pieceAt[size_t(hereCell)] == c.destPiece && c.destPiece >= 0) {
        const auto& P = net_.pieces[size_t(c.destPiece)];   // pull up at the address
        const int s = tl::trafficDetail::axisDir(P.axis, 1).dot(fwd).raw >= 0 ? 1 : -1;
        const Fixed d = (c.destAlong - tl::trafficAlongOf(P, pos)) * s;
        if (d > -kHalf) { want = fxMin(want, tl::trafficStopSpeed(fxMax(d - Fixed::fromRatio(1, 10), Fixed{}) * mpc_, rules_.decel)); if (d < Fixed::fromInt(3)) heldBySignal = true; }
    }
    if (c.kind == kTrafficBus) {
        if (sim.tick() < c.dwellUntil) { want = Fixed{}; heldBySignal = true; }
        else if (const int pi = inMap ? net_.pieceAt[size_t(hereCell)] : -1; pi >= 0) {
            const auto& P = net_.pieces[size_t(pi)];
            const int s = tl::trafficDetail::axisDir(P.axis, 1).dot(fwd).raw >= 0 ? 1 : -1;
            const Fixed a = tl::trafficAlongOf(P, pos);
            auto it = std::lower_bound(stops_.begin(), stops_.end(), pi, [](const BusStop& b, int q) { return b.piece < q; });
            for (; it != stops_.end() && it->piece == pi; ++it) {
                const int idx = int(it - stops_.begin());
                const Fixed d = (it->along - a) * s;
                if (it->dir != s || idx == c.lastStop || d < -kHalf) continue;
                if (d < Fixed::fromRatio(35, 100) && v < Fixed::fromRatio(3, 5)) {   // at the stop: doors
                    c.lastStop = idx;
                    c.dwellUntil = sim.tick() + std::max<uint32_t>(1, ticksOf(rules_.dwellMin + (rules_.dwellMax - rules_.dwellMin) * sim.rng().unit(), sim.tickRate()));
                    passengers(sim, st, *it, c);
                    want = Fixed{};
                } else want = fxMin(want, tl::trafficStopSpeed(fxMax(d - Fixed::fromRatio(15, 100), Fixed{}) * mpc_, rules_.decel));
                if (d < Fixed::fromInt(3)) heldBySignal = true;
                break;
            }
        }
    }
    const Fixed range = Fixed::fromInt(3) + vc * Fixed::fromRatio(3, 2);
    Entity blocker = kNullEntity;
    Fixed bestGap = range;
    gs.hash.query(v3(pos), range + Fixed::fromInt(1), [&](Entity o) {
        if (o == c.e || (c.ignore == o && sim.tick() < c.ignoreUntil)) return;
        auto* ou = w.tryGet<Unit>(o);
        if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap)) return;
        const UnitType& ot = m_->rules().types[ou->type];
        if (ot.structure || (ot.layer & kLayerAir)) return;
        const Vec2 op = v2(w.get<Transform>(o).pos);
        if (!onRoad(op)) return;   // lamp posts, light props, people on the sidewalk
        const Vec2 rel = op - pos;
        const Fixed ahead = rel.dot(dirRef), lat = fxAbs(tl::trafficDetail::cross(dirRef, rel));
        if (ahead.raw <= 0 || ahead > range + Fixed::fromInt(1)) return;
        Fixed orad = ot.radius, olen = ot.radius;
        Vec2 ofwd{};
        if (const DrivingCar* oc = drv->car(sim, o)) {
            for (auto& t : drv->rules().vehicles) if (t.type == ou->type) { orad = t.width / 2; olen = t.length / 2; }
            if (c.ignore == c.e && sim.tick() < c.ignoreUntil) return;   // pushing through: vehicles give way, people never
            ofwd = Vec2{fxSinTurns(oc->st.yaw), fxCosTurns(oc->st.yaw)};
            // oncoming cars in their own lane pass by
            if (ofwd.dot(dirRef) < -kHalf && lat > halfWid + orad) return;
        }
        // people get a wide berth: they step into the lane from the side (crossings, a turn across a crossing)
        const bool walker = ofwd.lengthSq().raw == 0 && ot.speed.raw > 0;
        const Fixed margin = walker ? Fixed::fromRatio(3, 5) : Fixed::fromRatio(15, 100);
        if (lat > halfWid + orad + margin) return;
        // off the path the car follows (it drifted toward a parked car at the curb and is steering back): not ahead
        Fixed pathD2 = segDistSq(op, pts[0], pts[1]);
        for (size_t i = 1; i + 1 < pts.size() && i < 4; ++i) pathD2 = fxMin(pathD2, segDistSq(op, pts[i], pts[i + 1]));
        const Fixed clearance = halfWid + orad + (walker ? Fixed::fromRatio(3, 5) : Fixed::fromRatio(1, 10));
        if (c.kind != kTrafficEmergency && pathD2 > clearance * clearance) return;
        // the other body's extent along our direction
        const Fixed along = ofwd.lengthSq().raw > 0 ? fxAbs(ofwd.dot(dirRef)) * olen + fxAbs(tl::trafficDetail::cross(ofwd, dirRef)) * orad : orad;
        const Fixed gap = ahead - halfLen - along;
        if (gap < bestGap || (gap == bestGap && (!blocker.valid() || o.index < blocker.index))) { bestGap = gap; blocker = o; }
    });
    if (blocker.valid()) {
        Fixed lead{};
        if (const DrivingCar* oc = drv->car(sim, blocker)) lead = fxMax(tl::vehicleForwardSpeed(oc->st), Fixed{});
        const Fixed room = fxMax(bestGap - rules_.gap, Fixed{});
        const Fixed vf = tl::trafficStopSpeed(room * mpc_, rules_.decel) + lead / 2;
        want = fxMin(want, room.raw > 0 ? vf : Fixed{});
    }
    // waiting: deadlock breaks and detours
    const bool standing = v < Fixed::fromRatio(1, 2) && !heldBySignal;
    c.stopped = standing ? c.stopped + 1 : 0;
    c.blocker = standing ? blocker : kNullEntity;
    if (standing && blocker.valid() && c.stopped > sim.tickRate() * 2) {
        TrafficCar* other = byEntity(st.cars, blocker);
        const bool person = isPersonType(w.get<Unit>(blocker).type) || !drv->car(sim, blocker);
        if (person && !other && m_->rules().types[w.get<Unit>(blocker).type].speed.raw > 0) {
            if (c.stopped > sim.tickRate() * 3) { c.ignore = blocker; c.ignoreUntil = sim.tick() + sim.tickRate() * 3 / 2; }   // creep on: people step aside
        } else if (c.kind == kTrafficEmergency) { c.ignore = c.e; c.ignoreUntil = sim.tick() + sim.tickRate() * 2; }   // pushes through
        else if (other && other->blocker == c.e && c.e.index < blocker.index) {   // they wait for each other: the lower index goes
            c.ignore = blocker; c.ignoreUntil = sim.tick() + sim.tickRate() * 2;
        } else if (!other && c.stopped > sim.tickRate() * 4) {
            const DrivingCar* oc = drv->car(sim, blocker);
            const bool parked = oc ? !oc->driver.valid() : (m_->rules().types[w.get<Unit>(blocker).type].speed.raw == 0);
            bool straight = net_.junctionAt[size_t(gs.grid.cellOf(v3(pos)))] < 0;
            for (size_t i = c.wp; i < c.route.size() && i < size_t(c.wp) + 2; ++i) straight = straight && !c.route[i].turn;
            if (parked && straight) {   // a parked car, a wreck, a prop in the lane: drive round it on the side of the centre line
                const Vec2 bp = v2(w.get<Transform>(blocker).pos);
                Vec2 left = rightOnScreen(dirRef) * -kUnit;
                if (rules_.net.leftHand) left = left * -kUnit;
                const Fixed shift = rules_.net.laneWidth * Fixed::fromRatio(11, 10);
                const Vec2 a = pos + dirRef * Fixed::fromRatio(4, 5) + left * shift, b = bp + dirRef * Fixed::fromRatio(8, 5) + left * shift;
                c.route.insert(c.route.begin() + c.wp, {TrafficWaypoint{a, -1, 0, 0}, TrafficWaypoint{b, -1, 0, 0}});
                c.ignore = blocker; c.ignoreUntil = sim.tick() + sim.tickRate() * 5;
                c.stopped = 0;
            } else if (c.stopped > sim.tickRate() * 8) {   // no way round (a car shoved into a junction): nudge it aside
                c.ignore = blocker; c.ignoreUntil = sim.tick() + sim.tickRate() * 3;
            }
        }
    }
    // creeping past a person it stopped for: walking pace (never fast enough to run anybody over)
    if (c.ignore.valid() && c.ignore != c.e && sim.tick() < c.ignoreUntil && w.alive(c.ignore) && !drv->car(sim, c.ignore))
        want = fxMin(want, Fixed::fromInt(2));
    // wedged (full throttle, nothing ahead, not moving): back off a little and try again
    if (standing && !blocker.valid() && want > Fixed::fromInt(1) && c.stopped > sim.tickRate() * 4) { c.reverseUntil = sim.tick() + sim.tickRate() * 3 / 2; c.stopped = 0; }
    if (sim.tick() < c.reverseUntil) { c.input = {}; c.input.brake = Fixed::fromRatio(7, 10); return; }
    c.input = tl::trafficDriverInput(rules_.driver, yaw, v, pos, target, want);
}

// People near the stop who are standing about get on, others get off onto the sidewalk next to it.
void Traffic::passengers(Simulation& sim, TrafficStore& st, const BusStop& stop, const TrafficCar& bus) {
    if (rules_.passengers <= 0 || rules_.people.empty()) return;
    World& w = sim.world();
    const int stopIndex = int(&stop - stops_.data());
    int on = int(sim.rng().below(uint32_t(rules_.passengers + 1)));
    for (auto& p : st.people) {
        if (p.citizen >= 0) { busCitizen(sim, st, stopIndex, &p, bus); continue; }   // citizens take the bus at their own stop only
        if (on <= 0) continue;
        if (p.panic || p.state != PedState::Pause || !w.alive(p.e) || w.get<Unit>(p.e).hp.raw <= 0) continue;
        if ((v2(w.get<Transform>(p.e).pos) - stop.kerb).lengthSq() > Fixed::fromInt(4)) continue;
        silentRemove(sim, p.e);
        --on;
    }
    busCitizen(sim, st, stopIndex, nullptr, bus);
    const int off = int(sim.rng().below(uint32_t(rules_.passengers + 1)));
    const int cx = int(stop.kerb.x.floorInt()), cz = int(stop.kerb.y.floorInt());
    int cell = -1;
    for (int r = 0; r <= 2 && cell < 0; ++r)
        for (int dz = -r; dz <= r && cell < 0; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= net_.w || z >= net_.h || !side_[size_t(z * net_.w + x)]) continue;
                cell = z * net_.w + x; break;
            }
    if (cell < 0) return;
    for (int i = 0; i < off && int(st.people.size()) < rules_.maxPeople + rules_.passengers; ++i) {
        const Vec3 pos{Fixed::fromInt(cell % net_.w) + Fixed::fromRatio(int(sim.rng().below(7)) + 2, 10), Fixed{},
                       Fixed::fromInt(cell / net_.w) + Fixed::fromRatio(int(sim.rng().below(7)) + 2, 10)};
        const uint16_t type = pick(sim, rules_.people);
        const Entity e = type == UINT16_MAX ? kNullEntity : m_->spawn(sim, type, rules_.peopleOwner, pos);
        if (!e.valid()) return;
        TrafficPerson p;
        p.e = e; p.hp = w.get<Unit>(e).hp; p.lastPos = pos;
        p.side = Fixed::fromRatio(int(sim.rng().below(21)) - 10, 10) * rules_.side;
        p.until = sim.tick() + uint32_t(i) * sim.tickRate() / 2;
        insertSorted(st.people, p);
    }
}

int Traffic::kindOf(Simulation& sim, Entity vehicle) const {
    auto* st = store(sim);
    if (!st) return -1;
    auto it = std::lower_bound(st->cars.begin(), st->cars.end(), vehicle.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
    return it != st->cars.end() && it->e == vehicle ? int(it->kind) : -1;
}

// ----------------------------------------------------------------- people

bool Traffic::newWalk(Simulation& sim, TrafficPerson& p, const Vec2* threat, int goalCell) {
    World& w = sim.world();
    const MapGrid& g = m_->state(sim).grid;
    const Vec3 pos = w.get<Transform>(p.e).pos;
    int start = g.cellOf(pos);
    const int W = net_.w;
    if (start < 0 || size_t(start) >= walk_.size()) return false;
    if (!walk_[size_t(start)]) {   // off the sidewalk (thrown out of a car, pushed): the nearest walk cell
        const int cx = start % W, cz = start / W;
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
    // the search runs in a window around the walker (any city size costs the same)
    const int R = rules_.routeMax + 8, sx = start % W, sz = start / W;
    const int x0 = std::max(0, sx - R), z0 = std::max(0, sz - R), x1 = std::min(W - 1, sx + R), z1 = std::min(net_.h - 1, sz + R);
    const int lw = x1 - x0 + 1, lh = z1 - z0 + 1;
    std::vector<uint8_t> mask(size_t(lw) * size_t(lh));
    for (int z = 0; z < lh; ++z)
        for (int x = 0; x < lw; ++x) mask[size_t(z * lw + x)] = walk_[size_t((z0 + z) * W + x0 + x)];
    auto global = [&](int local) { return (z0 + local / lw) * W + x0 + local % lw; };
    std::vector<int32_t> dist, parent;
    tl::trafficWalkField(lw, lh, mask, (sz - z0) * lw + (sx - x0), dist, parent, rules_.routeMax + 8);
    int goal = -1;
    if (threat) {   // the reachable cell farthest from the threat (ties: the lower cell index)
        Fixed best{};
        for (size_t i = 0; i < dist.size(); ++i) {
            const int gc = global(int(i));
            if (dist[i] < 4 || stripAt(gc) >= 0) continue;
            const Vec2 c{Fixed::fromInt(gc % W) + kHalf, Fixed::fromInt(gc / W) + kHalf};
            const Fixed d = (c - *threat).lengthSq();
            if (goal < 0 || d > best) { best = d; goal = int(i); }
        }
    } else if (goalCell >= 0) {   // toward an address: the reachable cell of the window nearest to it
        const Vec2 to{Fixed::fromInt(goalCell % W) + kHalf, Fixed::fromInt(goalCell / W) + kHalf};
        Fixed best{};
        for (size_t i = 0; i < dist.size(); ++i) {
            const int gc = global(int(i));
            if (dist[i] < 1 || (stripAt(gc) >= 0 && gc != goalCell)) continue;
            const Vec2 c{Fixed::fromInt(gc % W) + kHalf, Fixed::fromInt(gc / W) + kHalf};
            const Fixed d = (c - to).lengthSq();
            if (goal < 0 || d < best) { best = d; goal = int(i); }
        }
        // no cell of the window is nearer than where he stands: he is as near as the sidewalks get
        const Vec2 here{Fixed::fromInt(sx) + kHalf, Fixed::fromInt(sz) + kHalf};
        if (goal >= 0 && best >= (here - to).lengthSq()) goal = -1;
    } else {
        uint32_t seen = 0;
        for (size_t i = 0; i < dist.size(); ++i) {
            if (dist[i] < rules_.routeMin || dist[i] > rules_.routeMax || stripAt(global(int(i))) >= 0) continue;
            if (sim.rng().below(++seen) == 0) goal = int(i);   // reservoir: uniform over the candidates
        }
    }
    if (goal < 0) return false;
    p.path = tl::trafficWalkPath(parent, dist, goal);
    for (int32_t& c : p.path) c = global(c);
    if (start != g.cellOf(pos)) p.path.insert(p.path.begin(), start);
    p.idx = 0; p.legEnd = 0;
    return !p.path.empty();
}

// Hands the next leg of the walk to the movement system: sidewalk cells up to the next crossing, or the crossing and
// the first sidewalk cell beyond it.
void Traffic::startLeg(Simulation& sim, TrafficPerson& p) {
    World& w = sim.world();
    const int W = net_.w;
    size_t end = p.idx;
    const bool crossing = stripAt(p.path[p.idx]) >= 0;
    while (end < p.path.size() && (stripAt(p.path[end]) >= 0) == crossing) ++end;
    if (crossing && end < p.path.size()) ++end;
    std::vector<Vec3> pts;
    Vec3 prev = w.get<Transform>(p.e).pos;
    auto centre = [&](int cell) { return Vec3{Fixed::fromInt(cell % W) + kHalf, Fixed{}, Fixed::fromInt(cell / W) + kHalf}; };
    for (size_t i = p.idx; i < end; ++i) {
        const Vec3 c = centre(p.path[i]);
        const Vec3 nxt = i + 1 < p.path.size() ? centre(p.path[i + 1]) : c;
        Vec2 d = tl::trafficDetail::normalised(v2(nxt - c));
        const Vec2 din = tl::trafficDetail::normalised(v2(c - prev));
        if (d.lengthSq().raw == 0) d = din;
        // straight on: skip the cell (one waypoint per run)
        if (i + 1 < end && i > p.idx && d == din) { prev = c; continue; }
        const Vec2 r = rightOnScreen(tl::trafficDetail::normalised(d + din)) * (rules_.net.leftHand ? -p.side : p.side);
        pts.push_back(c + v3(r));
        prev = c;
    }
    p.legEnd = uint16_t(end);
    p.leg = std::move(pts);
    p.legPt = 0;
    if (p.leg.empty()) return;
    if (auto* o = w.tryGet<Orders>(p.e)) o->queue.clear();
    p.state = PedState::Walk;
    p.moved = sim.tick();
    nextPoint(sim, p);
}

// One point at a time: when the walker is blocked the movement system re-paths only to the next corner of the
// sidewalk, never across the road to the end of the leg.
void Traffic::nextPoint(Simulation& sim, TrafficPerson& p) {
    if (p.legPt >= p.leg.size()) return;
    if (auto* mb = sim.world().tryGet<Mobile>(p.e)) {
        const Vec3 q = p.leg[p.legPt++];
        mb->path.assign(1, q); mb->pathPos = 0; mb->goal = q; mb->goalRadius = Fixed::fromRatio(1, 10);
        mb->moving = true; mb->stuck = Fixed{}; mb->flow = false; mb->roadPath.clear();
    }
}

void Traffic::panic(Simulation& sim, TrafficPerson& p, Vec2 threat) {
    World& w = sim.world();
    const bool was = p.panic;
    if (!newWalk(sim, p, &threat)) return;
    p.panic = true;
    p.until = sim.tick() + ticksOf(rules_.panicSeconds, sim.tickRate());
    if (!was) if (auto* b = w.tryGet<Buffs>(p.e)) b->list.push_back({Stat::Speed, Fixed{}, rules_.panicSpeed, p.until, kNoType});
    startLeg(sim, p);
}

void Traffic::adoptPeople(Simulation& sim, TrafficStore& st) {
    World& w = sim.world();
    std::vector<Entity> fresh;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform&) {
        if (u.hp.raw <= 0 || (u.flags & kUnitOffMap) || !isPersonType(u.type) || !w.has<Mobile>(e)) return;
        if (!byEntity(st.people, e)) fresh.push_back(e);
    });
    std::sort(fresh.begin(), fresh.end(), [](Entity a, Entity b) { return a.index < b.index; });
    for (Entity e : fresh) {
        TrafficPerson p;
        p.e = e; p.hp = w.get<Unit>(e).hp; p.lastPos = w.get<Transform>(e).pos;
        p.side = Fixed::fromRatio(int(sim.rng().below(21)) - 10, 10) * rules_.side;
        p.until = sim.tick() + sim.rng().below(sim.tickRate() * 3 + 1);
        insertSorted(st.people, p);
    }
}

bool Traffic::spawnPerson(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const int W = net_.w, R = int(rules_.peopleMax.toInt()) + 1;
    for (int attempt = 0; attempt < 12; ++attempt) {
        int cx, cz;   // a random sidewalk cell of the square around a hero
        if (!foci.empty()) {
            const Vec2 f = foci[sim.rng().below(uint32_t(foci.size()))];
            const int fx = int(f.x.floorInt()), fz = int(f.y.floorInt());   // the square around the hero, cut to the map
            cx = sim.rng().range(std::max(0, fx - R), std::max(0, std::min(net_.w - 1, fx + R)));
            cz = sim.rng().range(std::max(0, fz - R), std::max(0, std::min(net_.h - 1, fz + R)));
        } else { cx = int(sim.rng().below(uint32_t(W))); cz = int(sim.rng().below(uint32_t(net_.h))); }
        if (cx < 0 || cz < 0 || cx >= W || cz >= net_.h || !side_[size_t(cz * W + cx)]) continue;
        const int cell = cz * W + cx;
        const Vec2 pos{Fixed::fromInt(cell % W) + Fixed::fromRatio(int(sim.rng().below(9)) + 1, 10),
                       Fixed::fromInt(cell / W) + Fixed::fromRatio(int(sim.rng().below(9)) + 1, 10)};
        if (!foci.empty()) {
            const Fixed d2 = nearestSq(foci, pos);
            if (d2 < minDist * minDist || d2 > rules_.peopleMax * rules_.peopleMax) continue;
        }
        bool clear = true;
        gs.hash.query(v3(pos), Fixed::fromRatio(1, 2), [&](Entity o) {
            auto* ou = w.tryGet<Unit>(o);
            if (ou && ou->hp.raw > 0 && !(ou->flags & kUnitOffMap) && (flat(w.get<Transform>(o).pos) - v3(pos)).length() < Fixed::fromRatio(1, 2)) clear = false;
        });
        if (!clear) continue;
        const uint16_t type = pick(sim, rules_.people);
        if (type == UINT16_MAX) return false;
        const Entity e = m_->spawn(sim, type, rules_.peopleOwner, v3(pos));
        if (!e.valid()) return false;
        TrafficPerson p;
        p.e = e; p.hp = w.get<Unit>(e).hp; p.lastPos = v3(pos);
        p.side = Fixed::fromRatio(int(sim.rng().below(21)) - 10, 10) * rules_.side;
        p.until = sim.tick();
        insertSorted(st.people, p);
        return true;
    }
    return false;
}

void Traffic::people(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci) {
    if (rules_.people.empty() && st.citizens.empty()) return;
    World& w = sim.world();
    if (sim.tick() >= st.nextAdopt) { adoptPeople(sim, st); st.nextAdopt = sim.tick() + sim.tickRate(); }
    for (size_t i = 0; i < st.people.size();) {
        const Entity e = st.people[i].e;
        if (w.alive(e) && w.get<Unit>(e).hp.raw > 0 && !(w.get<Unit>(e).flags & kUnitOffMap)) { ++i; continue; }
        st.people.erase(st.people.begin() + long(i));
    }
    if (!foci.empty())
        for (auto& p : st.people)
            if (nearestSq(foci, v2(w.get<Transform>(p.e).pos)) > rules_.peopleDespawn * rules_.peopleDespawn) {
                citizenUnseen(sim, st, p, v2(w.get<Transform>(p.e).pos));   // a citizen goes on unseen, from where he is
                silentRemove(sim, p.e);
            }
    for (size_t i = 0; i < st.people.size();) {
        if (w.get<Unit>(st.people[i].e).hp.raw <= 0) { st.people.erase(st.people.begin() + long(i)); continue; }
        ++i;
    }
    const Fixed minDist = st.placed ? rules_.peopleMin : Fixed::fromInt(3);
    int budget = st.placed ? 1 : rules_.maxPeople * 3;   // the first tick fills the streets (a failed try is just a bad spot)
    if (!st.placed || sim.tick() % 4 == 0)
        while (budget-- > 0 && int(st.people.size()) < rules_.maxPeople) spawnPerson(sim, st, foci, minDist);

    // threats this tick: shots, hits, deaths; fast cars driven by heroes
    std::vector<Vec2> threats;
    for (const TrafficEvent& ev : st.happened) threats.push_back(v2(ev.pos));
    // fast: driven by a hero (people run from them); coming: anything that rolls (people in its way jump aside)
    struct Fast { Vec2 p, v; };
    struct Coming { Vec2 p, dir; Fixed speed, half; };
    std::vector<Fast> fast;
    std::vector<Coming> coming;
    if (Driving* drv = m_->driving(); drv && drv->active())
        if (auto* ds = drv->store(sim))
            for (auto& c : ds->cars) {
                const Vec2 cp{c.st.position.x / mpc_, c.st.position.z / mpc_}, cv{c.st.velocity.x / mpc_, c.st.velocity.z / mpc_};
                if (c.driver.valid() && fxAbs(tl::vehicleForwardSpeed(c.st)) > Fixed::fromInt(6)) fast.push_back({cp, cv});
                const Fixed sp = cv.length();
                if (sp * mpc_ < Fixed::fromInt(3) || !w.alive(c.e)) continue;
                Fixed half = Fixed::fromRatio(1, 4);
                for (auto& t : drv->rules().vehicles) if (t.type == w.get<Unit>(c.e).type) half = t.width / 2;
                coming.push_back({cp, cv / sp, sp, half});
            }

    const Fixed r2 = rules_.panicRadius * rules_.panicRadius;
    for (auto& p : st.people) {
        const Unit& u = w.get<Unit>(p.e);
        const Vec3 pos3 = w.get<Transform>(p.e).pos;
        const Vec2 pos = v2(pos3);
        // panic triggers
        const Vec2* threat = nullptr;
        Vec2 hurtAt = pos - Vec2{Fixed::fromRatio(1, 10), Fixed{}};
        const bool hurt = u.hp < p.hp;
        if (hurt) threat = &hurtAt;
        for (auto& t : threats) if (!threat && (t - pos).lengthSq() <= r2) threat = &t;
        for (auto& f : fast) {
            if (threat) break;
            const Vec2 rel = pos - f.p;
            if (rel.lengthSq() < Fixed::fromInt(9) && rel.dot(f.v).raw > 0) threat = &f.p;
        }
        p.hp = u.hp;
        // a car about to hit him: a jump to the side, out of its way (wherever that is; back to the sidewalk afterwards)
        const bool dodging = p.state == PedState::Walk && p.path.empty();
        if (!dodging && u.hp.raw > 0)
            for (auto& cm : coming) {
                const Vec2 rel = pos - cm.p;
                const Fixed ahead = rel.dot(cm.dir), lat = tl::trafficDetail::cross(cm.dir, rel);
                if (ahead.raw <= 0 || ahead > cm.speed * Fixed::fromRatio(6, 5) + kUnit || fxAbs(lat) > cm.half + Fixed::fromRatio(2, 5)) continue;
                // to the side he is already on (dead centre: by his entity index), as far as there is room
                const bool neg = lat.raw != 0 ? lat.raw < 0 : (p.e.index & 1) != 0;
                Vec2 perp{-cm.dir.y, cm.dir.x};   // cross(dir, perp) > 0
                if (neg) perp = perp * -kUnit;
                static const int kStep[4] = {12, 8, -12, -8};   // tenths of a cell: his side first, then the other
                for (int stp : kStep) {
                    const Vec3 to = v3(pos + perp * Fixed::fromRatio(stp, 10));
                    if (!m_->state(sim).grid.passableAt(to, kLayerGround)) continue;
                    if (auto* o = w.tryGet<Orders>(p.e)) o->queue.clear();
                    p.path.clear(); p.idx = 0; p.legEnd = 0;
                    p.leg.assign(1, to); p.legPt = 0;
                    p.state = PedState::Walk; p.moved = sim.tick(); p.lastPos = pos3;
                    if (!p.panic) if (auto* b = w.tryGet<Buffs>(p.e)) b->list.push_back({Stat::Speed, Fixed{}, rules_.panicSpeed * 2, sim.tick() + sim.tickRate(), kNoType});
                    nextPoint(sim, p);
                    break;
                }
                break;
            }
        if (p.state == PedState::Walk && p.path.empty() && !hurt) continue;   // jumping: nothing else on his mind
        if (threat && (!p.panic || hurt)) { panic(sim, p, *threat); continue; }
        if (threat && p.panic) p.until = std::max(p.until, sim.tick() + ticksOf(rules_.panicSeconds, sim.tickRate()) / 2);

        auto* mb = w.tryGet<Mobile>(p.e);
        auto* ord = w.tryGet<Orders>(p.e);
        if (!mb) continue;
        switch (p.state) {
        case PedState::Pause:
            if (sim.tick() < p.until) break;
            if (p.citizen >= 0 && size_t(p.citizen) >= st.citizens.size()) p.citizen = -1;
            if (p.citizen >= 0 && !p.panic) {   // on his way to an address: the next stretch, the bus stop, the door
                if (!citizenStep(sim, st, p, pos)) break;
            } else if (!newWalk(sim, p, nullptr)) { p.until = sim.tick() + sim.tickRate() * 2; break; }
            if (stripAt(p.path[0]) >= 0 && !walkAllowed(sim, stripAt(p.path[0]))) { p.state = PedState::Wait; break; }   // starts at the kerb
            startLeg(sim, p);
            break;
        case PedState::Wait:
            if (p.idx >= p.path.size()) { p.state = PedState::Pause; break; }
            if (p.panic || walkAllowed(sim, stripAt(p.path[p.idx]))) startLeg(sim, p);
            break;
        case PedState::Walk: {
            // someone else gave it orders (a script, the computer player): the walk is over
            if (ord && !ord->queue.empty()) { p.state = PedState::Pause; p.until = sim.tick() + sim.tickRate() * 5; break; }
            if ((pos3 - p.lastPos).lengthSq() > Fixed::fromRatio(1, 400)) { p.moved = sim.tick(); p.lastPos = pos3; }
            if (mb->moving && sim.tick() - p.moved < sim.tickRate() * 4) break;
            if (mb->moving) { m_->stopMoving(sim, p.e); p.state = PedState::Pause; p.until = sim.tick() + sim.tickRate(); break; }   // stuck
            if (p.legPt < p.leg.size()) { nextPoint(sim, p); break; }
            p.idx = p.legEnd;
            if (p.idx >= p.path.size()) {
                if (p.panic && sim.tick() < p.until && newWalk(sim, p, &hurtAt)) { startLeg(sim, p); break; }   // keep running
                p.panic = false;
                p.state = PedState::Pause;
                if (p.citizen >= 0) { p.until = sim.tick(); break; }   // no strolling: the next stretch at once
                p.until = sim.tick() + ticksOf(rules_.pauseMin + (rules_.pauseMax - rules_.pauseMin) * sim.rng().unit(), sim.tickRate());
                break;
            }
            if (stripAt(p.path[p.idx]) >= 0 && !p.panic && !walkAllowed(sim, stripAt(p.path[p.idx]))) { p.state = PedState::Wait; break; }
            startLeg(sim, p);
            break;
        }
        }
    }
}

} // namespace aiverse::game
