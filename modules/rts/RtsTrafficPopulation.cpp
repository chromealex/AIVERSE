// Population (rules traffic.population, RtsTraffic.h): thousands of citizens, each with a home, most with a job and
// a shift. Homes and workplaces are places: map objects of the listed types and the entries of the map's "places".
//
// A citizen nobody is near costs nothing but a look every two seconds: at home, at work, or on his way with a
// departure time and an arrival time (his position is the straight line between the two). When that position comes
// near a hero he becomes a real pedestrian on the nearest sidewalk who walks to the door of his destination and
// goes in; when the hero leaves, he goes on unseen from where he was. So anybody can be followed to his workplace.
// A shop with one keeper is open while the keeper is in (Traffic::present). How they travel (on foot, by bus, in
// their own car) is RtsTrafficCommute.cpp.
// Fixed maths and integers only; the citizens are created from the simulation RNG on the first tick.
#include "RtsTraffic.h"
#include "RtsInternal.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
namespace tl = lib::genre;

namespace {
Vec2 v2(Vec3 p) { return {p.x, p.z}; }
uint32_t ticksOf(Fixed seconds, uint32_t rate) { return uint32_t(std::max<int64_t>(0, (seconds * int64_t(rate)).toInt())); }
int minutesOf(const Json& v, const std::string& where) {   // hours as a number (8.5 = 08:30)
    if (!v.isNumber()) throw std::runtime_error(where + ": hours of the day (0..24)");
    const Fixed h = v.fx();
    if (h.raw < 0 || h > Fixed::fromInt(24)) throw std::runtime_error(where + ": hours of the day (0..24)");
    return int((h * 60).toInt()) % 1440;
}
bool inWindow(int minute, int from, int to) { return from <= to ? (minute >= from && minute < to) : (minute >= from || minute < to); }
} // namespace

void Traffic::parsePopulation(const Json& j, TrafficRules& r, const std::vector<std::string>& ids, uint32_t rate) {
    const Json& p = j["population"];
    if (!p.isObject()) return;
    TrafficPopulationRules& pr = r.population;
    const std::string where = "traffic.population";
    auto typeOf = [&](const std::string& id, const std::string& at) {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return uint16_t(i);
        throw std::runtime_error(at + ": unknown type '" + id + "'");
    };
    const int64_t count = p.getInt("count", 0);
    if (count < 0 || count > 200000) throw std::runtime_error(where + ".count: 0..200000");
    pr.count = int(count);
    const Fixed day = p.getFx("dayLength", Fixed::fromInt(1200));
    if (day < Fixed::fromInt(24)) throw std::runtime_error(where + ".dayLength: at least 24 seconds");
    pr.dayTicks = std::max<uint32_t>(1440 / 60, ticksOf(day, rate));
    if (p.has("startHour")) pr.startMinute = minutesOf(p["startHour"], where + ".startHour");
    if (p.has("walk")) pr.walk = p.getFx("walk");
    if (pr.walk.raw <= 0) throw std::runtime_error(where + ".walk: must be positive");
    pr.live = int(std::clamp<int64_t>(p.getInt("live", pr.live), 0, 2000));
    pr.parents = int(std::clamp<int64_t>(p.getInt("parents", 0), 0, 100));
    pr.carShare = int(p.getInt("car", 0));
    pr.busShare = int(p.getInt("bus", 0));
    if (pr.carShare < 0 || pr.busShare < 0 || pr.carShare + pr.busShare > 100) throw std::runtime_error(where + ": car + bus are percent of the citizens (0..100 together)");
    if (p.has("drive")) pr.drive = p.getFx("drive");
    if (p.has("ride")) pr.ride = p.getFx("ride");
    if (pr.drive.raw <= 0 || pr.ride.raw <= 0) throw std::runtime_error(where + ": drive and ride must be positive");
    pr.parked = int(std::clamp<int64_t>(p.getInt("parked", pr.parked), 0, 500));
    if (p.has("stopRange")) pr.stopRange = fxClamp(p.getFx("stopRange"), Fixed{}, Fixed::fromInt(1000));
    {
        const Json& ct = p["carTypes"];
        if (ct.isArray()) for (const Json& e : ct.items()) pr.carTypes.push_back({typeOf(e.str(), where + ".carTypes"), 1});
        else if (ct.isObject()) for (const auto& [tid, wgt] : ct.members()) pr.carTypes.push_back({typeOf(tid, where + ".carTypes"), int(std::clamp<int64_t>(wgt.integer(1), 1, 1000000))});
        if (pr.carTypes.empty()) pr.carTypes = r.cars;
        if (pr.carShare > 0 && pr.carTypes.empty()) throw std::runtime_error(where + ".car: needs carTypes (or cars.types): what they drive");
        if (pr.busShare > 0 && r.buses.empty()) throw std::runtime_error(where + ".bus: needs traffic.buses");
    }
    if (const Json& kinds = p["kinds"]; kinds.isObject())
        for (const auto& [id, v] : kinds.members()) {
            TrafficPlaceKind k;
            k.id = id;
            k.lives = int(std::clamp<int64_t>(v.getInt("lives", 0), 0, 100000));
            k.jobs = int(std::clamp<int64_t>(v.getInt("jobs", 0), 0, 100000));
            k.children = int(std::clamp<int64_t>(v.getInt("children", 0), 0, 100000));
            if (const Json& sh = v["shifts"]; sh.isArray())
                for (size_t i = 0; i < sh.size(); ++i) {
                    const std::string at = where + ".kinds." + id + ".shifts[" + std::to_string(i) + "]";
                    if (!sh[i].isArray() || sh[i].size() != 2) throw std::runtime_error(at + ": [from, to] hours");
                    k.shifts.push_back({minutesOf(sh[i][0], at), minutesOf(sh[i][1], at)});
                }
            if (k.jobs > 0 && k.shifts.empty()) k.shifts.push_back({9 * 60, 18 * 60});
            if (k.shifts.size() > 8) throw std::runtime_error(where + ".kinds." + id + ".shifts: at most 8");
            pr.kinds.push_back(std::move(k));
        }
    if (const Json& objs = p["objects"]; objs.isObject())
        for (const auto& [type, kind] : objs.members()) {
            const std::string kid = kind.str();
            size_t ki = pr.kinds.size();
            for (size_t i = 0; i < pr.kinds.size(); ++i) if (pr.kinds[i].id == kid) ki = i;
            if (ki == pr.kinds.size()) throw std::runtime_error(where + ".objects." + type + ": unknown kind '" + kid + "'");
            pr.objects.push_back({typeOf(type, where + ".objects"), uint16_t(ki)});
        }
    std::sort(pr.objects.begin(), pr.objects.end());
    const Json& types = p["types"];
    if (types.isArray()) for (const Json& e : types.items()) pr.types.push_back({typeOf(e.str(), where + ".types"), 1});
    else if (types.isObject()) for (const auto& [tid, wgt] : types.members()) pr.types.push_back({typeOf(tid, where + ".types"), int(std::clamp<int64_t>(wgt.integer(1), 1, 1000000))});
    if (pr.types.empty()) pr.types = r.people;
    if (pr.count > 0 && pr.types.empty()) throw std::runtime_error(where + ".types: the citizens' unit types (or pedestrians.types)");
    if (pr.count > 0 && r.walkTiles.empty()) throw std::runtime_error(where + ": needs pedestrians.walk (the sidewalk tiles they walk on)");
}

// Places: map objects of the listed types (entity order) and then the map's "places": [{"kind", "pos": [x, z]}].
// A place needs a sidewalk cell within 6 cells: that is its door.
void Traffic::setupPopulation(Simulation& sim, const Json& map) {
    places_.clear();
    const TrafficPopulationRules& pr = rules_.population;
    if (pr.count <= 0) return;
    auto door = [&](Vec2 p) { return sidewalkNear(p, 6, true); };
    struct Found { Entity e; uint16_t kind; Vec2 p; };
    std::vector<Found> found;
    if (!pr.objects.empty())
        sim.world().each<Unit, Transform>([&](Entity e, Unit& u, Transform& t) {
            auto it = std::lower_bound(pr.objects.begin(), pr.objects.end(), std::pair<uint16_t, uint16_t>{u.type, 0});
            if (it != pr.objects.end() && it->first == u.type) found.push_back({e, it->second, v2(t.pos)});
        });
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.e.index < b.e.index; });
    if (const Json& list = map["places"]; list.isArray())
        for (size_t i = 0; i < list.size(); ++i) {
            const Json& v = list[i];
            const std::string kid = v.getStr("kind");
            size_t ki = pr.kinds.size();
            for (size_t k = 0; k < pr.kinds.size(); ++k) if (pr.kinds[k].id == kid) ki = k;
            if (ki == pr.kinds.size()) throw std::runtime_error("map places[" + std::to_string(i) + "]: unknown kind '" + kid + "' (traffic.population.kinds)");
            const Json& pos = v["pos"];
            if (pos.size() < 2) throw std::runtime_error("map places[" + std::to_string(i) + "].pos: [x, z]");
            found.push_back({kNullEntity, uint16_t(ki), Vec2{pos[0].fx(), pos[1].fx()}});
        }
    for (auto& f : found) {
        const int cell = door(f.p);
        if (cell >= 0 && places_.size() < 1000000) { Place pl; pl.kind = f.kind; pl.pos = f.p; pl.cell = cell; pl.object = f.e; places_.push_back(pl); }
    }
    setupCommute();
    gangTargets_.clear();
    for (size_t i = 0; i < places_.size(); ++i)
        for (auto& t : rules_.gangs.targets) if (t.first == places_[i].kind && places_[i].piece >= 0) gangTargets_.push_back({int32_t(i), t.second});
}

int Traffic::minuteOfDay(Simulation& sim) const {
    const TrafficPopulationRules& pr = rules_.population;
    return int((int64_t(pr.startMinute) + int64_t(sim.tick()) * 1440 / pr.dayTicks) % 1440);
}

// Homes fill in place order, jobs shift by shift; who is left over stays at home (out of work).
void Traffic::populate(Simulation& sim, TrafficStore& st) {
    const TrafficPopulationRules& pr = rules_.population;
    if (pr.count <= 0 || !st.citizens.empty()) return;
    struct Slot { int32_t place; uint8_t shift; };
    std::vector<int32_t> beds;
    std::vector<Slot> jobs;
    for (size_t i = 0; i < places_.size(); ++i) {
        const TrafficPlaceKind& k = pr.kinds[places_[i].kind];
        for (int n = 0; n < k.lives && beds.size() < size_t(pr.count); ++n) beds.push_back(int32_t(i));
        for (size_t s = 0; s < k.shifts.size(); ++s)
            for (int n = 0; n < k.jobs && jobs.size() < size_t(pr.count) * 2; ++n) jobs.push_back({int32_t(i), uint8_t(s)});
    }
    if (beds.empty()) return;   // nowhere to live: no population on this map
    // shuffle the jobs (Fisher-Yates): who works where is random, every job has the same chance to be taken
    for (size_t i = jobs.size(); i > 1; --i) std::swap(jobs[i - 1], jobs[sim.rng().below(uint32_t(i))]);
    std::vector<Slot> serviceJobs;
    for(size_t i=0;i<places_.size();++i){
        const auto& kind=pr.kinds[places_[i].kind];
        if(kind.id!="tow_depot")continue;
        for(size_t shift=0;shift<kind.shifts.size();++shift)
            for(int j=0;j<kind.jobs;++j)serviceJobs.push_back({int32_t(i),uint8_t(shift)});
    }
    jobs.erase(std::remove_if(jobs.begin(),jobs.end(),[&](const Slot& slot){return pr.kinds[places_[size_t(slot.place)].kind].id=="tow_depot";}),jobs.end());
    jobs.insert(jobs.begin(),serviceJobs.begin(),serviceJobs.end());
    const int n = std::min<int>(pr.count, int(beds.size()));
    st.citizens.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        TrafficCitizen& c = st.citizens[size_t(i)];
        c.type = pick(sim, pr.types);
        c.home = beds[size_t(i)];
        if (size_t(i) < jobs.size()) { c.work = jobs[size_t(i)].place; c.shift = jobs[size_t(i)].shift; }
        c.state = kCitizenHome;
        // how he gets about: one draw each, so the shares hold whatever the map
        const int roll = int(sim.rng().below(100));
        c.commute = roll < pr.carShare ? kCommuteCar : roll < pr.carShare + pr.busShare ? kCommuteBus : kCommuteWalk;
        if (c.commute == kCommuteCar) c.carType = pick(sim, pr.carTypes);
    }
    // children: a working parent with a day shift who walks or drives takes his to the nearest school with room
    std::vector<int32_t> room(places_.size(), 0);
    bool schools = false;
    for (size_t i = 0; i < places_.size(); ++i) { room[i] = pr.kinds[places_[i].kind].children; schools = schools || room[i] > 0; }
    if (schools && pr.parents > 0)
        for (auto& c : st.citizens) {
            if (c.work < 0 || c.commute == kCommuteBus) continue;
            const int from = pr.kinds[places_[size_t(c.work)].kind].shifts[c.shift].first;
            if (from < 5 * 60 || from > 12 * 60 || int(sim.rng().below(100)) >= pr.parents) continue;
            Fixed best{};
            for (size_t i = 0; i < places_.size(); ++i) {
                if (room[i] <= 0) continue;
                const Fixed d = (places_[i].pos - places_[size_t(c.home)].pos).lengthSq();
                if (c.school < 0 || d < best) { best = d; c.school = int32_t(i); }
            }
            if (c.school >= 0) room[size_t(c.school)]--;
        }
    // whoever is on shift at the start is at work already
    const int minute = minuteOfDay(sim);
    for (auto& c : st.citizens) if (onShift(c, minute, 0)) c.state = kCitizenWork;
}

// Is it his working time (`lead` minutes early: time to leave home)?
bool Traffic::onShift(const TrafficCitizen& c, int minute, int lead) const {
    if (c.work < 0) return false;
    const auto& sh = rules_.population.kinds[places_[size_t(c.work)].kind].shifts[c.shift];
    return inWindow(minute, ((sh.first - lead) % 1440 + 1440) % 1440, sh.second);
}

const TrafficCitizen* Traffic::citizenOf(Simulation& sim, Entity e) const {
    auto* st = store(sim);
    if (!st) return nullptr;
    int32_t id = -1;
    auto it = std::lower_bound(st->people.begin(), st->people.end(), e.index, [](const TrafficPerson& p, uint32_t i) { return p.e.index < i; });
    if (it != st->people.end() && it->e == e) id = it->citizen;
    else {
        auto ct = std::lower_bound(st->cars.begin(), st->cars.end(), e.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
        if (ct != st->cars.end() && ct->e == e) id = ct->citizen;
    }
    return id >= 0 && size_t(id) < st->citizens.size() ? &st->citizens[size_t(id)] : nullptr;
}

int Traffic::present(Simulation& sim, int place) const {
    auto* st = store(sim);
    int n = 0;
    if (st) for (auto& c : st->citizens) n += c.work == place && c.state == kCitizenWork;
    return n;
}

void Traffic::population(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci) {
    if (st.citizens.empty()) return;
    World& w = sim.world();
    const TrafficPopulationRules& pr = rules_.population;
    const int minute = minuteOfDay(sim);
    const uint32_t rate = sim.tickRate();
    const int64_t minuteTicks = std::max<int64_t>(1, pr.dayTicks / 1440);
    int live = 0, driving = 0, parked = 0;
    for (auto& p : st.people) live += p.citizen >= 0;
    for (auto& c : st.cars) driving += c.citizen >= 0;
    if (pr.carShare > 0) for (auto& c : st.citizens) parked += c.car.valid();
    const int cap = pr.live, carCap = std::max(4, rules_.maxCars / 2);
    // a slice per tick: everybody is looked at every two seconds
    const size_t n = st.citizens.size(), slice = std::max<size_t>(1, (n + rate * 2 - 1) / (rate * 2));
    for (size_t k = 0; k < slice; ++k) {
        const size_t id = st.citizenCursor++ % n;
        TrafficCitizen& c = st.citizens[id];
        if (c.state == kCitizenDead || c.state == kCitizenGang) continue;
        if (c.state == kCitizenJail) { if (sim.tick() >= c.eta) c.state = kCitizenHome; continue; }
        if (m_->state(sim).trig.vars["tow_worker_"+std::to_string(id)].raw>0 || m_->state(sim).trig.vars["tow_claimant_"+std::to_string(id)].raw>0) continue;
        if (c.e.valid() && !(w.alive(c.e) && w.get<Unit>(c.e).hp.raw > 0)) {   // his body (his car) vanished without a report: assume the hospital
            c.e = kNullEntity;
            if (c.state == kCitizenToWork || c.state == kCitizenToHome) { c.state = kCitizenHospital; c.eta = sim.tick() + rules_.incidents.treatTicks; }
        }
        if (c.commute == kCommuteCar) keepCar(sim, st, c, foci, parked);
        if (c.state == kCitizenHospital) {   // nobody brought him back: he is home again after a while
            if (sim.tick() >= c.eta) { c.state = kCitizenHome; c.leg = kLegWalk; }
            continue;
        }
        // Kingston family calendar v2: native/Lua policy submits requests through saved variables.
        auto& familyVars=m_->state(sim).trig.vars;
        const auto suffix=std::to_string(id);
        auto value=[&](const std::string& key) {auto it=familyVars.find(key);return it==familyVars.end()?Fixed{}:it->second;};
        const bool off=value("resident_schedule_off_"+suffix).raw>0;
        // A leisure controller owns this same resident until return/cancellation.
        if(value("resident_visit_active_"+suffix).raw>0)continue;
        const int request=int(value("resident_journey_"+suffix).toInt());
        if(request && (c.state==kCitizenHome || c.state==kCitizenWork)) {
            const int savedWork=c.work;
            // A pickup from home is a return journey via school, with home as its origin.
            if(request==2 && c.state==kCitizenHome)c.work=c.home;
            if(c.work>=0)depart(sim,st,int32_t(id),request==1?kCitizenToWork:kCitizenToHome);
            c.work=savedWork;familyVars["resident_journey_"+suffix]={};
            continue;
        }
        if (c.state == kCitizenHome) {
            if (off || c.work < 0) continue;
            // leave early enough to get there, and not all at the same minute
            const int64_t way = c.school >= 0 ? int64_t(tripTicks(c, c.home, c.school, rate)) + tripTicks(c, c.school, c.work, rate) : int64_t(tripTicks(c, c.home, c.work, rate));
            const int lead = int(std::min<int64_t>(600, way / minuteTicks)) + int(id * 2654435761u % 20);
            if (onShift(c, minute, lead)) depart(sim, st, int32_t(id), kCitizenToWork);
            continue;
        }
        if (c.state == kCitizenWork) {
            if (off || !onShift(c, minute, 120)) depart(sim, st, int32_t(id), kCitizenToHome);   // in early is not "after work"
            continue;
        }
        // on his way
        if (c.e.valid()) continue;   // as a real person or in his real car: Traffic::people / Traffic::cars move him
        const int to = destOf(c);
        if (sim.tick() >= c.eta) {   // the end of the leg
            if ((c.leg == kLegToStop || c.leg == kLegAtStop) && c.stopA >= 0) travel(sim, c, kLegRide, stops_[size_t(c.stopA)].kerb);
            else if (c.leg == kLegRide && c.stopB >= 0) travel(sim, c, kLegWalk, stops_[size_t(c.stopB)].kerb);
            else if (c.via) { const Vec2 school = legTarget(c); c.via = 0; travel(sim, c, c.leg, school); }   // by the school, then on
            else arrive(c);
            continue;
        }
        if (foci.empty() || c.leg == kLegRide) continue;   // a rider gets off when a bus calls at his stop (Traffic::passengers)
        const Fixed t = Fixed::fromRatio(int64_t(sim.tick() - c.left), std::max<int64_t>(1, int64_t(c.eta - c.left)));
        const Vec2 at = c.from + (legTarget(c) - c.from) * t;
        Fixed d2 = Fixed::fromInt(1 << 20);
        for (auto& f : foci) d2 = fxMin(d2, (f - at).lengthSq());
        if (c.leg == kLegDrive) {   // his car drives into sight
            if (driving < carCap && d2 <= rules_.spawnMax * rules_.spawnMax && d2 >= rules_.spawnMin * rules_.spawnMin && spawnTrip(sim, st, int32_t(id), at)) ++driving;
            continue;
        }
        if (live >= cap) continue;
        // into sight from the edge of it; out of a door he may come right in front of the hero
        const bool atDoor = sim.tick() - c.left <= rate * 3;
        if (d2 > rules_.peopleMax * rules_.peopleMax || (!atDoor && d2 < rules_.peopleMin * rules_.peopleMin)) continue;
        const int cell = atDoor ? sidewalkNear(c.from, 6, true) : sidewalkNear(at, 5, true);
        const int goal = c.leg == kLegToStop && c.stopA >= 0 ? stopCell_[size_t(c.stopA)] : places_[size_t(to)].cell;
        if (cell < 0) continue;
        const Vec3 spot{Fixed::fromInt(cell % net_.w) + kHalf, Fixed{}, Fixed::fromInt(cell / net_.w) + kHalf};
        if (citizenPerson(sim, st, int32_t(id), spot, goal).valid()) ++live;
    }
}

} // namespace aiverse::game
