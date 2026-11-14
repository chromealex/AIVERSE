// Gangs (rules traffic.gangs, RtsTraffic.h): crime that starts by itself. A citizen without work is the ringleader:
// now and then he gets a few others without work together, they take a car, drive to a shop / bank (a place kind
// with loot), go in, come out with the money and drive off to his home. Going in is a crime report: the police
// come; a patrol that arrives while they are inside fights them, one that arrives later chases their car (the call
// follows it). A patrol that gets to the car arrests them (jail, the loot is lost); if none does they are home with
// the money. Nobody near (no hero around the home and the target): the same story as dice and a timer.
// Fixed maths only, entity-index order, randomness from sim.rng().
#include "RtsTraffic.h"
#include "RtsInternal.h"
#include <algorithm>
#include <stdexcept>

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
TrafficCar* carOf(TrafficStore& st, Entity e) {
    auto it = std::lower_bound(st.cars.begin(), st.cars.end(), e.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
    return it != st.cars.end() && it->e == e ? &*it : nullptr;
}
Fixed nearestSq(const std::vector<Vec2>& foci, Vec2 p) {
    Fixed best = Fixed::fromInt(1 << 20);
    for (auto& f : foci) best = fxMin(best, (f - p).lengthSq());
    return best;
}
} // namespace

void Traffic::parseGangs(const Json& j, TrafficRules& r, const std::vector<std::string>& ids, uint32_t rate) {
    const Json& g = j["gangs"];
    if (!g.isObject()) return;
    TrafficGangRules& gr = r.gangs;
    const std::string where = "traffic.gangs";
    auto seconds = [&](const char* key, Fixed def) {
        const Fixed v = g.getFx(key, def);
        if (v.raw < 0) throw std::runtime_error(where + "." + key + ": seconds, not negative");
        return ticksOf(v, rate);
    };
    if (const Json& ev = g["every"]; ev.isArray() && ev.size() == 2) { gr.everyMin = ticksOf(ev[0].fx(), rate); gr.everyMax = ticksOf(ev[1].fx(), rate); }
    else if (ev.isNumber()) gr.everyMin = gr.everyMax = ticksOf(ev.fx(), rate);
    else { gr.everyMin = rate * 120; gr.everyMax = rate * 300; }
    if (gr.everyMax < gr.everyMin || gr.everyMin == 0) throw std::runtime_error(where + ".every: [from, to] seconds, positive");
    if (const Json& sz = g["size"]; sz.isArray() && sz.size() == 2) { gr.sizeMin = int(sz[0].integer(2)); gr.sizeMax = int(sz[1].integer(4)); }
    if (gr.sizeMin < 1 || gr.sizeMax < gr.sizeMin || gr.sizeMax > 8) throw std::runtime_error(where + ".size: [from, to] people, 1..8");
    gr.robTicks = seconds("rob", Fixed::fromInt(8));
    gr.jailTicks = seconds("jail", Fixed::fromInt(240));
    gr.catchChance = int(std::clamp<int64_t>(g.getInt("caughtUnseen", 40), 0, 100));
    gr.owner = uint8_t(std::clamp<int64_t>(g.getInt("owner", r.peopleOwner), 0, 255));
    if (g.has("crew")) {
        const std::string id = g.getStr("crew");
        gr.crew = UINT16_MAX;
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) gr.crew = uint16_t(i);
        if (gr.crew == UINT16_MAX) throw std::runtime_error(where + ".crew: unknown type '" + id + "'");
    }
    if (const Json& t = g["targets"]; t.isObject())
        for (const auto& [kind, loot] : t.members()) {
            size_t ki = r.population.kinds.size();
            for (size_t i = 0; i < r.population.kinds.size(); ++i) if (r.population.kinds[i].id == kind) ki = i;
            if (ki == r.population.kinds.size()) throw std::runtime_error(where + ".targets: unknown place kind '" + kind + "' (traffic.population.kinds)");
            gr.targets.push_back({uint16_t(ki), int32_t(std::clamp<int64_t>(loot.integer(0), 0, 100000000))});
        }
    std::sort(gr.targets.begin(), gr.targets.end());
    gr.on = !gr.targets.empty() && g.getBool("enabled", true);
}

// The gang is over: its people are where `state` says (home, jail, the hospital).
void Traffic::gangEnd(Simulation& sim, TrafficStore& st, uint8_t state, bool paid) {
    TrafficGang& g = st.gang;
    for (Entity m : g.crew) silentRemove(sim, m);
    for (size_t k = 0; k < g.members.size(); ++k) {
        const int32_t id = g.members[k];
        if (id < 0 || size_t(id) >= st.citizens.size()) continue;
        TrafficCitizen& c = st.citizens[size_t(id)];
        if (c.state != kCitizenGang) continue;
        // whoever was taken hold of at the scene is in jail however it ended for the rest (the last of the list)
        const uint8_t his = k + g.held >= g.members.size() ? uint8_t(kCitizenJail) : state;
        c.state = his; c.e = kNullEntity;
        c.eta = sim.tick() + (his == kCitizenJail ? rules_.gangs.jailTicks : rules_.incidents.treatTicks);
    }
    if (TrafficCar* c = carOf(st, g.car); c && c->task == kTaskGang) {   // the car stays where it is, nobody's
        const Entity e = c->e;
        st.cars.erase(st.cars.begin() + (c - st.cars.data()));
        if (Driving* drv = m_->driving()) drv->adopt(sim, e, Fixed{});
    }
    // (with a prisoner taken the call stays with whoever holds him)
    if (g.call && !g.held) st.incidents.erase(std::remove_if(st.incidents.begin(), st.incidents.end(), [&](const TrafficIncident& i) { return i.id == g.call; }), st.incidents.end());
    if (paid) { st.gangLoot += g.loot; ++st.gangRobberies; }
    if (state == kCitizenJail || g.held) ++st.gangCaught;
    g = TrafficGang{};
}

void Traffic::gangs(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci) {
    const TrafficGangRules& gr = rules_.gangs;
    if (!gr.on || st.citizens.empty() || gangTargets_.empty()) return;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    Driving* drv = m_->driving();
    TrafficGang& g = st.gang;
    const uint32_t rate = sim.tickRate();
    if (g.state == kGangNone) {
        if (st.nextGang == 0) st.nextGang = sim.tick() + gr.everyMin + sim.rng().below(gr.everyMax - gr.everyMin + 1);
        if (sim.tick() < st.nextGang) return;
        st.nextGang = sim.tick() + gr.everyMin + sim.rng().below(gr.everyMax - gr.everyMin + 1);
        // the ringleader and his people: citizens without work who are at home, from a random place in the list on
        const size_t n = st.citizens.size(), from = sim.rng().below(uint32_t(n));
        const int want = gr.sizeMin + int(sim.rng().below(uint32_t(gr.sizeMax - gr.sizeMin + 1)));
        std::vector<int32_t> crew;
        for (size_t k = 0; k < n && int(crew.size()) < want; ++k) {
            const TrafficCitizen& c = st.citizens[(from + k) % n];
            if (c.work < 0 && c.state == kCitizenHome && c.home >= 0) crew.push_back(int32_t((from + k) % n));
        }
        if (int(crew.size()) < gr.sizeMin) return;
        const auto& target = gangTargets_[sim.rng().below(uint32_t(gangTargets_.size()))];
        const Place& home = places_[size_t(st.citizens[size_t(crew[0])].home)], & shop = places_[size_t(target.first)];
        g = TrafficGang{};
        g.leader = crew[0]; g.members = crew; g.target = target.first; g.loot = target.second;
        for (int32_t id : crew) { st.citizens[size_t(id)].state = kCitizenGang; st.citizens[size_t(id)].e = kNullEntity; }
        // somebody around to see it? Then a car is taken: a parked one by the house, else one from round the corner
        const Fixed sight = rules_.spawnMax * rules_.spawnMax;
        Entity car = kNullEntity;
        if (!foci.empty() && drv && drv->active() && shop.piece >= 0 && (nearestSq(foci, home.pos) <= sight || nearestSq(foci, shop.pos) <= sight)) {
            Fixed best = Fixed::fromInt(12 * 12);
            gs.hash.query(v3(home.pos), Fixed::fromInt(12), [&](Entity o) {
                const DrivingCar* dc = drv->car(sim, o);
                if (!dc || dc->driver.valid() || !living(w, o) || carOf(st, o)) return;
                bool cruiser = false;   // not a patrol car, a bus, an ambulance
                for (auto& sv : rules_.services) for (auto& t : sv.types) cruiser = cruiser || t.type == w.get<Unit>(o).type;
                for (auto& t : rules_.buses) cruiser = cruiser || t.type == w.get<Unit>(o).type;
                const Fixed d = (v2(w.get<Transform>(o).pos) - home.pos).lengthSq();
                if (!cruiser && (d < best || (d == best && o.index < car.index))) { best = d; car = o; }
            });
            Fixed along;
            if (!car.valid() && !rules_.cars.empty() && kerbSpot(sim, home, home.dir, along, kNullEntity, nullptr, Fixed{})) {
                const auto& P = net_.pieces[size_t(home.piece)];
                car = m_->spawn(sim, pick(sim, rules_.cars), rules_.carOwner, v3(tl::trafficLanePoint(net_, P, home.dir, P.lanes, along)));
                if (car.valid()) w.get<Unit>(car).facing = v3(tl::trafficDetail::axisDir(P.axis, home.dir));
            }
        }
        if (car.valid()) {
            TrafficCar t;
            t.e = car; t.task = kTaskGang;
            t.destPiece = shop.piece; t.destAlong = shop.along;
            t.cruise = rules_.speedMax;
            const Vec3 f = w.get<Unit>(car).facing;
            if (reroute(sim, t, v2(w.get<Transform>(car).pos), Vec2{f.x, f.z}) && drv->adopt(sim, car, Fixed{})) {
                auto it = std::lower_bound(st.cars.begin(), st.cars.end(), car.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
                st.cars.insert(it, std::move(t));
                g.car = car; g.state = kGangDrive; g.until = sim.tick() + rate * 180;
                return;
            }
        }
        // unseen: there, the robbery, back
        const Fixed way = (shop.pos - home.pos).length() / rules_.population.drive;
        g.state = kGangUnseen;
        g.until = sim.tick() + ticksOf(way * 2, rate) + gr.robTicks + 1;
        return;
    }
    if (g.state == kGangUnseen) {
        if (sim.tick() < g.until) return;
        const bool caught = int(sim.rng().below(100)) < gr.catchChance;
        gangEnd(sim, st, caught ? uint8_t(kCitizenJail) : uint8_t(kCitizenHome), !caught);
        return;
    }
    // the real thing
    TrafficCar* car = carOf(st, g.car);
    if (!living(w, g.car)) { gangEnd(sim, st, kCitizenHospital, false); return; }   // their car was wrecked
    if (!car || car->task != kTaskGang) { gangEnd(sim, st, kCitizenHome, false); return; }   // somebody took it from them
    const Vec3 pos3 = w.get<Transform>(g.car).pos;
    const Vec2 pos = v2(pos3);
    const Place& shop = places_[size_t(g.target)];
    const bool slow = fxAbs(drv->speed(sim, g.car)) < Fixed::fromRatio(3, 5);
    const Vec2 door{Fixed::fromInt(shop.cell % net_.w) + kHalf, Fixed::fromInt(shop.cell / net_.w) + kHalf};
    g.crew.erase(std::remove_if(g.crew.begin(), g.crew.end(), [&](Entity m) { return !living(w, m); }), g.crew.end());
    // a robber somebody has taken hold of (disabled, or loaded into a vehicle: the game's own arrest) is theirs now
    for (size_t i = g.crew.size(); i-- > 0;)
        if (w.get<Unit>(g.crew[i]).flags & (kUnitDisabled | kUnitInCargo)) { g.crew.erase(g.crew.begin() + long(i)); ++g.held; }
    if (g.crew.empty() && g.held && g.state != kGangDrive && g.state != kGangFlee) { gangEnd(sim, st, kCitizenJail, false); return; }
    auto onPiece = [&](int32_t piece, Fixed along, Fixed within) {
        const int cell = gs.grid.cellOf(pos3);
        return cell >= 0 && piece >= 0 && net_.pieceAt[size_t(cell)] == piece && fxAbs(along - tl::trafficAlongOf(net_.pieces[size_t(piece)], pos)) < within;
    };
    switch (g.state) {
    case kGangDrive: {
        if (sim.tick() >= g.until) { gangEnd(sim, st, kCitizenHome, false); break; }   // never got there
        if (!slow || !onPiece(car->destPiece, car->destAlong, Fixed::fromRatio(3, 2))) break;
        const Vec3 f = w.get<Unit>(g.car).facing;
        const Vec3 right{-f.z, Fixed{}, f.x};
        for (size_t i = 0; i < g.members.size(); ++i) {
            Vec3 spot = pos3 + right * Fixed::fromRatio(3, 5) + f * Fixed::fromRatio(int(i) * 2 - int(g.members.size() - 1), 5);
            if (!gs.grid.passableAt(spot, kLayerGround)) spot = pos3 - right * Fixed::fromRatio(3, 5);
            if (!gs.grid.passableAt(spot, kLayerGround)) continue;
            const uint16_t type = gr.crew != UINT16_MAX ? gr.crew : st.citizens[size_t(g.members[i])].type;
            const Entity m = m_->spawn(sim, type, gr.owner, spot);
            if (!m.valid()) continue;
            Order go; go.type = OrderType::Move; go.point = v3(door);
            m_->issue(sim, m, go, false);
            g.crew.push_back(m);
            st.exempt.insert(std::lower_bound(st.exempt.begin(), st.exempt.end(), m, [](Entity a, Entity b) { return a.index < b.index; }), m);   // not a passer-by
        }
        if (g.crew.empty()) { gangEnd(sim, st, kCitizenHome, false); break; }
        g.state = kGangOut; g.until = sim.tick() + rate * 20;
        break;
    }
    case kGangOut: {
        if (g.crew.empty()) { gangEnd(sim, st, kCitizenHospital, false); break; }
        bool there = sim.tick() >= g.until;
        for (Entity m : g.crew) there = there || (v2(w.get<Transform>(m).pos) - door).lengthSq() < Fixed::fromRatio(9, 4);
        if (!there) break;
        g.call = report(sim, kIncidentCrime, shop.pos, g.car, g.crew[0], true);   // the alarm
        g.state = kGangRob; g.until = sim.tick() + gr.robTicks;
        break;
    }
    case kGangRob: {
        if (g.crew.empty()) { gangEnd(sim, st, kCitizenHospital, false); break; }   // shot at the door
        if (sim.tick() < g.until) break;
        Order back; back.type = OrderType::Move; back.point = pos3;
        for (Entity m : g.crew) m_->issue(sim, m, back, false);
        g.state = kGangBack; g.until = sim.tick() + rate * 20;
        break;
    }
    case kGangBack: {
        if (g.crew.empty()) { gangEnd(sim, st, kCitizenHospital, false); break; }
        bool back = sim.tick() >= g.until;
        if (!back) { back = true; for (Entity m : g.crew) back = back && (v2(w.get<Transform>(m).pos) - pos).lengthSq() < Fixed::fromRatio(36, 25); }
        if (!back) break;
        for (Entity m : g.crew) silentRemove(sim, m);
        g.crew.clear();
        const Place& home = places_[size_t(st.citizens[size_t(g.leader)].home)];
        if (home.piece < 0) { gangEnd(sim, st, kCitizenHome, true); break; }
        car->destPiece = home.piece; car->destAlong = home.along;
        g.state = kGangFlee; g.until = sim.tick() + rate * 180;
        break;
    }
    case kGangFlee: {
        // the call follows the car; a patrol that gets to it makes the arrest
        TrafficIncident* inc = nullptr;
        for (auto& i : st.incidents) if (i.id == g.call) inc = &i;
        if (inc && !g.held) inc->pos = pos;
        bool caught = false;
        const int cell = gs.grid.cellOf(pos3);
        const int piece = cell >= 0 ? net_.pieceAt[size_t(cell)] : -1;
        for (auto& o : st.cars) {
            if (o.service < 0 || o.call != g.call || g.call == 0 || g.held || !living(w, o.e)) continue;
            if (piece >= 0 && o.task == kTaskGoing && sim.tick() % 20 == 0) { o.destPiece = piece; o.destAlong = tl::trafficAlongOf(net_.pieces[size_t(piece)], pos); }
            caught = caught || (v2(w.get<Transform>(o.e).pos) - pos).lengthSq() < Fixed::fromRatio(49, 4);
        }
        if (caught) { gangEnd(sim, st, kCitizenJail, false); break; }
        if ((slow && onPiece(car->destPiece, car->destAlong, Fixed::fromInt(2))) || sim.tick() >= g.until) gangEnd(sim, st, kCitizenHome, true);   // got away
        break;
    }
    default: break;
    }
}

} // namespace aiverse::game
