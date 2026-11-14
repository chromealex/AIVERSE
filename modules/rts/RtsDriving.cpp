#include "kingston/DeckBroadphase.h"
// Driving (rules "driving", RtsDriving.h): vehicle adoption, getting in / out, the stick -> pedals mapping, the
// vehicle-controllers step and this module's part of the contract: the sweep of the body over the map grid and other
// units, collision response, crash and run-over damage. Fixed maths only; every loop runs in entity-index order.
#include "RtsDriving.h"
#include "RtsInternal.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/FixedMath.h"
#include "../../libraries/include/aiverselib/genre/VehicleData.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
namespace vl = lib::genre;

namespace {
const Fixed kUnit = Fixed::fromInt(1);
const Fixed kGap = Fixed::fromRatio(1, 64);

Vec3 forwardOf(Fixed yaw) { return {fxSinTurns(yaw), Fixed{}, fxCosTurns(yaw)}; }   // yaw 0 = +z, a quarter turn = +x
Vec3 rightOf(Vec3 f) { return {f.z, Fixed{}, -f.x}; }
Fixed wrapTurns(Fixed t) {
    int64_t r = t.raw % Fixed::kOne;
    if (r > Fixed::kOne / 2) r -= Fixed::kOne; else if (r <= -Fixed::kOne / 2) r += Fixed::kOne;
    return Fixed::fromRaw(r);
}
Fixed number(const Json& j, const char* key, Fixed def, const std::string& where, bool positive) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0 || (positive && v.raw == 0))
        throw std::runtime_error(where + "." + key + (positive ? ": must be positive" : ": must not be negative"));
    return v;
}
// Where a point is relative to a deck: distance along the centre line, distance from it, the way back to it.
struct DeckSpot {
    Fixed along, side;
    Vec3 toAxis;
    bool outside = true;   // past either end (the open mouths), or the deck has no line
};
const Fixed kMouth = Fixed::fromRatio(3, 2);   // cells from an end where the deck is still at ground height

DeckSpot deckSpot(const DrivingDeck& d, Vec3 p) {
    DeckSpot best;
    bool have = false;
    for (size_t i = 0; i + 1 < d.pts.size(); ++i) {
        const Vec3 a = flat(d.pts[i]);
        const Fixed len = d.at[i + 1] - d.at[i];
        const Vec3 dir = flat(d.pts[i + 1] - d.pts[i]) / len;
        const Fixed t = flat(p - a).dot(dir), tc = fxClamp(t, Fixed{}, len);
        const Vec3 dv = flat(a + dir * tc - p);
        const Fixed dist = dv.length();
        if (have && dist >= best.side) continue;
        best = {d.at[i] + tc, dist, dv, (i == 0 && t.raw < 0) || (i + 2 == d.pts.size() && t > len)};
        have = true;
    }
    return best;
}
Fixed deckMouth(const DrivingDeck& d) { return fxMin(kMouth, d.length() / 2); }
// deep: far enough from both ends to be above / below the ground level
bool deckDeep(const DrivingDeck& d, Vec3 p) {
    const DeckSpot s = deckSpot(d, p);
    const Fixed m = deckMouth(d);
    return !s.outside && s.along > m && s.along < d.length() - m;
}
// Two bodies on different levels pass each other; near a mouth everybody is on the ground.
bool levelsApart(const DrivingStore& st, int deckA, Vec3 a, int deckB, Vec3 b) {
    if (deckA == deckB) return false;
    return (deckA < 0 || deckDeep(st.decks[size_t(deckA)], a)) && (deckB < 0 || deckDeep(st.decks[size_t(deckB)], b));
}

DrivingCar* findCar(DrivingStore& st, Entity e) {
    auto it = std::lower_bound(st.cars.begin(), st.cars.end(), e.index, [](const DrivingCar& c, uint32_t i) { return c.e.index < i; });
    return it != st.cars.end() && it->e == e ? &*it : nullptr;
}
} // namespace

// ================================================================= data

DrivingRules Driving::parse(const Json& j, const std::vector<std::string>& ids) {
    DrivingRules r;
    if (!j.isObject()) return r;
    r.enabled = true;
    r.metersPerCell = number(j, "metersPerCell", r.metersPerCell, "driving", true);
    r.enterRange = number(j, "enterRange", r.enterRange, "driving", false);
    r.exitSpeed = number(j, "exitSpeed", r.exitSpeed, "driving", false);
    r.wreckDamage = number(j, "wreckDamage", r.wreckDamage, "driving", false);
    r.doorSeconds = number(j, "doorTime", r.doorSeconds, "driving", false);
    const std::string controls = j.getStr("controls", "tank");
    if (controls == "tank") r.controls = DriveControls::Tank;
    else if (controls == "stick") r.controls = DriveControls::Stick;
    else throw std::runtime_error("driving.controls: 'tank' or 'stick' (got '" + controls + "')");
    if (const Json& c = j["crash"]; c.isObject()) {
        r.crashMinSpeed = number(c, "minSpeed", r.crashMinSpeed, "driving.crash", false);
        r.crashDamage = number(c, "damage", r.crashDamage, "driving.crash", false);
        r.restitution = number(c, "restitution", r.restitution, "driving.crash", false);
        if (r.restitution > kUnit) throw std::runtime_error("driving.crash.restitution: 0..1");
    }
    if (const Json& c = j["runOver"]; c.isObject()) {
        r.runOverMinSpeed = number(c, "minSpeed", r.runOverMinSpeed, "driving.runOver", false);
        r.runOverDamage = number(c, "damage", r.runOverDamage, "driving.runOver", false);
    }
    const Json& vs = j["vehicles"];
    if (vs.isObject()) for (auto& [id, v] : vs.members()) {
        const std::string where = "driving.vehicles." + id;
        DrivingVehicleType d;
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) d.type = uint16_t(i);
        if (d.type == UINT16_MAX) throw std::runtime_error(where + ": unknown type '" + id + "'");
        d.length = number(v, "length", d.length, where, true);
        d.width = number(v, "width", d.width, where, true);
        if (d.width > d.length) throw std::runtime_error(where + ": width must not exceed length");
        if (d.length > Fixed::fromInt(8)) throw std::runtime_error(where + ".length: at most 8 cells");
        if (v["wheeled"].isObject()) vl::vehicleDataDetail::wheeled(v["wheeled"], where + ".wheeled", d.def.wheeled);
        if (!vl::valid(d.def)) throw std::runtime_error(where + ".wheeled: invalid controller parameters");
        r.vehicles.push_back(std::move(d));
    }
    if (r.vehicles.empty()) throw std::runtime_error("driving.vehicles: list at least one unit type");
    std::sort(r.vehicles.begin(), r.vehicles.end(), [](const DrivingVehicleType& a, const DrivingVehicleType& b) { return a.type < b.type; });
    return r;
}

const DrivingVehicleType* Driving::typeOf(uint16_t unitType) const {
    if (unitType < vehicleOf_.size()) return vehicleOf_[unitType] >= 0 ? &rules_.vehicles[size_t(vehicleOf_[unitType])] : nullptr;
    for (auto& v : rules_.vehicles) if (v.type == unitType) return &v;   // before setup
    return nullptr;
}

DrivingStore* Driving::store(Simulation& sim) const { return active() ? sim.world().resource<DrivingStore>() : nullptr; }

void Driving::setup(Simulation& sim, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    std::vector<std::string> ids;
    for (auto& t : m.rules().types) ids.push_back(t.id);
    rules_ = parse(rulesJson["driving"], ids);
    if (!active()) return;
    if (!m.topDown() || !m.topDown()->active()) throw std::runtime_error("driving: needs the 'topDown' block (the hero is the driver)");
    rules_.doorTicks = uint32_t((rules_.doorSeconds * int64_t(sim.tickRate())).toInt());
    // how far a body of each type reaches from its centre (cars: half their length): the sweep's cheap first test
    vehicleOf_.assign(m.rules().types.size(), -1);   // unit type -> vehicle (the first one, as the list is searched)
    for (size_t i = rules_.vehicles.size(); i-- > 0;) if (rules_.vehicles[i].type < vehicleOf_.size()) vehicleOf_[rules_.vehicles[i].type] = int32_t(i);
    reach_.assign(m.rules().types.size(), Fixed{});
    for (size_t i = 0; i < reach_.size(); ++i) reach_[i] = m.rules().types[i].radius;
    for (auto& v : rules_.vehicles) if (v.type < reach_.size()) reach_[v.type] = fxMax(v.length / 2, v.width / 2);
    auto& st = sim.world().setResource<DrivingStore>();
    adoptAndDrop(sim, st);
}

const DrivingCar* Driving::carOf(Simulation& sim, Entity driver) const {
    auto* st = store(sim);
    if (!st || !driver.valid()) return nullptr;
    for (auto& c : st->cars) if (c.driver == driver) return &c;
    return nullptr;
}

const DrivingCar* Driving::car(Simulation& sim, Entity vehicle) const {
    auto* st = store(sim);
    return st ? findCar(*st, vehicle) : nullptr;
}

bool Driving::setAutopilot(Simulation& sim, Entity vehicle, const vl::VehicleInput& input) {
    auto* st = store(sim);
    DrivingCar* c = st ? findCar(*st, vehicle) : nullptr;
    if (!c || !vl::valid(input)) return false;
    c->autopilot = true;
    c->pilot = input;
    return true;
}

bool Driving::clearAutopilot(Simulation& sim, Entity vehicle) {
    auto* st = store(sim);
    DrivingCar* c = st ? findCar(*st, vehicle) : nullptr;
    if (!c) return false;
    c->autopilot = false;
    c->pilot = {};
    return true;
}

Fixed Driving::speed(Simulation& sim, Entity vehicle) const {
    const DrivingCar* c = car(sim, vehicle);
    return c ? vl::vehicleForwardSpeed(c->st) : Fixed{};
}

void Driving::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.u32(uint32_t(st->cars.size()));
    for (auto& c : st->cars) {
        const auto& s = c.st;
        h.u64(c.e.packed()); h.u64(c.driver.packed()); h.u32(c.player); h.u32(c.doorUntil);
        if (!st->decks.empty()) h.i32(c.deck);   // maps without decks keep their checksums
        h.v3(s.position); h.v3(s.velocity);
        for (Fixed f : {s.yaw, s.yawRate, s.pitch, s.roll, s.steer, s.rpm, s.airspeed, s.sink, s.longAccel, s.trackLeft, s.trackRight}) h.fx(f);
        h.i32(s.gear); h.u32(s.shiftUntil); h.u32(s.slipMask); h.u32(s.lastTick);
        h.u32(uint32_t(s.grounded) | uint32_t(s.drifting) << 1 | uint32_t(s.stalled) << 2 | uint32_t(s.initialized) << 3 | uint32_t(c.autopilot) << 4);
        for (Fixed f : {c.pilot.throttle, c.pilot.brake, c.pilot.steer}) h.fx(f);
        h.u32(uint32_t(c.pilot.handbrake) | uint32_t(c.pilot.shiftUp) << 1 | uint32_t(c.pilot.shiftDown) << 2);
    }
}

void Driving::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->cars.size());
    aiverse::io(a, n);
    if (!a.writing()) { if (n > 100000) { a.fail(); return; } st->cars.assign(n, {}); }
    for (auto& c : st->cars) {
        auto& s = c.st;
        a(c.e, c.driver, c.player, c.doorUntil, s.position, s.velocity, s.yaw, s.yawRate, s.pitch, s.roll, s.steer, s.rpm);
        a(s.airspeed, s.sink, s.longAccel, s.trackLeft, s.trackRight, s.gear, s.shiftUntil, s.slipMask, s.lastTick);
        a(s.grounded, s.drifting, s.stalled, s.initialized);
        a(c.autopilot, c.pilot.throttle, c.pilot.brake, c.pilot.steer, c.pilot.handbrake, c.pilot.shiftUp, c.pilot.shiftDown);
        a(c.deck);
    }
    uint32_t nd = uint32_t(st->decks.size());
    aiverse::io(a, nd);
    if (!a.writing()) { if (nd > 100000) { a.fail(); return; } st->decks.assign(nd, {}); }
    for (auto& d : st->decks) a(d.id, d.width, d.pts, d.at);
    if (!a.writing())
        for (auto& c : st->cars) if (c.deck >= int(nd)) { a.fail(); return; }
}

// ================================================================= levels

void Driving::loadMap(Simulation& sim, const Json& map) {
    auto* st = store(sim);
    if (!st) return;
    st->decks.clear();
    const Json& list = map["decks"];
    if (list.isNull()) return;
    if (!list.isArray()) throw std::runtime_error("map decks: expected a list");
    for (auto& dj : list.items()) {
        DrivingDeck d;
        d.id = dj.getStr("id");
        const std::string where = "map decks" + (d.id.empty() ? std::string() : " '" + d.id + "'");
        d.width = number(dj, "width", Fixed::fromInt(2), where, true);
        const Json& path = dj["path"];
        if (!path.isArray() || path.size() < 2) throw std::runtime_error(where + ".path: needs at least two points [x, z] or [x, z, height]");
        for (auto& pj : path.items()) {
            if (!pj.isArray() || pj.size() < 2 || pj.size() > 3) throw std::runtime_error(where + ".path: a point is [x, z] or [x, z, height]");
            const Vec3 p{pj[size_t(0)].fx(), pj.size() > 2 ? pj[size_t(2)].fx() : Fixed{}, pj[size_t(1)].fx()};
            Fixed at{};
            if (!d.pts.empty()) {
                const Fixed len = flat(p - d.pts.back()).length();
                if (len < Fixed::fromRatio(1, 16)) throw std::runtime_error(where + ".path: two points in the same place");
                at = d.at.back() + len;
            }
            d.pts.push_back(p);
            d.at.push_back(at);
        }
        if (st->decks.size() >= 32000) throw std::runtime_error("map decks: too many");
        st->decks.push_back(std::move(d));
    }
}

int Driving::deckOf(Simulation& sim, Entity vehicle) const {
    const DrivingCar* c = car(sim, vehicle);
    return c ? c->deck : -1;
}

int Driving::deckIndex(Simulation& sim, const std::string& id) const {
    auto* st = store(sim);
    if (st)
        for (size_t i = 0; i < st->decks.size(); ++i) if (st->decks[i].id == id) return int(i);
    return -1;
}

Fixed Driving::height(Simulation& sim, Entity vehicle) const {
    auto* st = store(sim);
    const DrivingCar* c = car(sim, vehicle);
    if (!st || !c || c->deck < 0) return {};
    const DrivingDeck& d = st->decks[size_t(c->deck)];
    const Fixed mpc = rules_.metersPerCell;
    const DeckSpot s = deckSpot(d, Vec3{c->st.position.x / mpc, Fixed{}, c->st.position.z / mpc});
    for (size_t i = 0; i + 1 < d.pts.size(); ++i)
        if (s.along <= d.at[i + 1] || i + 2 == d.pts.size())
            return d.pts[i].y + (d.pts[i + 1].y - d.pts[i].y) * fxClamp((s.along - d.at[i]) / (d.at[i + 1] - d.at[i]), Fixed{}, kUnit);
    return {};
}

bool Driving::setDeck(Simulation& sim, Entity vehicle, int deck) {
    auto* st = store(sim);
    DrivingCar* c = st ? findCar(*st, vehicle) : nullptr;
    if (!c || deck < -1 || deck >= int(st->decks.size())) return false;
    if (deck >= 0) {
        const DrivingDeck& d = st->decks[size_t(deck)];
        const DeckSpot s = deckSpot(d, sim.world().get<Transform>(vehicle).pos);
        if (s.outside || s.side > d.width / 2) return false;
    }
    c->deck = int16_t(deck);
    return true;
}

// ================================================================= getting in and out

void Driving::adoptAndDrop(Simulation& sim, DrivingStore& st) {
    World& w = sim.world();
    // gone or destroyed vehicles: the driver is thrown out where the wreck is
    for (size_t i = 0; i < st.cars.size();) {
        DrivingCar& c = st.cars[i];
        if (w.alive(c.e) && w.get<Unit>(c.e).hp.raw > 0) { ++i; continue; }
        if (c.driver.valid()) leave(sim, c, true);
        st.cars.erase(st.cars.begin() + long(i));
    }
    std::vector<Entity> fresh;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform&) {
        if (u.hp.raw <= 0 || (u.flags & (kUnitOffMap | kUnitUnderConstruction)) || !typeOf(u.type)) return;
        if (!findCar(st, e)) fresh.push_back(e);
    });
    std::sort(fresh.begin(), fresh.end(), [](Entity a, Entity b) { return a.index < b.index; });
    for (Entity e : fresh) adoptOne(sim, st, e);
}

void Driving::adoptOne(Simulation& sim, DrivingStore& st, Entity e) {
    World& w = sim.world();
    {
        Unit& u = w.get<Unit>(e);
        const DrivingVehicleType& vt = *typeOf(u.type);
        const Vec3 p = w.get<Transform>(e).pos;
        DrivingCar c;
        c.e = e;
        c.st = vl::makeVehicleState(vt.def, {p.x * rules_.metersPerCell, Fixed{}, p.z * rules_.metersPerCell},
                                    Fixed::fromRaw(TopDown::angleOf(u.facing)));
        u.flags |= kUnitPossessed;          // RTS orders and the movement system leave it to this file
        if (auto* o = w.tryGet<Orders>(e)) o->queue.clear();
        if (w.has<Mobile>(e)) m_->stopMoving(sim, e);
        auto it = std::lower_bound(st.cars.begin(), st.cars.end(), e.index, [](const DrivingCar& x, uint32_t i) { return x.e.index < i; });
        st.cars.insert(it, c);
    }
}

bool Driving::adopt(Simulation& sim, Entity e, Fixed speed) {
    auto* st = store(sim);
    World& w = sim.world();
    if (!st || !w.alive(e) || !w.has<Transform>(e)) return false;
    Unit& u = w.get<Unit>(e);
    if (u.hp.raw <= 0 || (u.flags & (kUnitOffMap | kUnitUnderConstruction)) || !typeOf(u.type)) return false;
    if (!findCar(*st, e)) adoptOne(sim, *st, e);
    DrivingCar* c = findCar(*st, e);
    if (!c) return false;
    const Vec3 f = forwardOf(c->st.yaw);
    c->st.velocity = Vec3{f.x * speed, Fixed{}, f.z * speed};
    return true;
}

void Driving::enter(Simulation& sim, DrivingStore& st, uint8_t player, Entity hero) {
    World& w = sim.world();
    if (!w.alive(hero)) return;
    Unit& hu = w.get<Unit>(hero);
    if (hu.hp.raw <= 0 || (hu.flags & kUnitOffMap)) return;
    const Vec3 hp = w.get<Transform>(hero).pos;
    const Fixed hr = m_->rules().types[hu.type].radius;
    DrivingCar* best = nullptr;
    Fixed bestD{};
    for (auto& c : st.cars) {
        if (c.driver.valid() || !w.alive(c.e)) continue;
        const Unit& cu = w.get<Unit>(c.e);
        if (cu.hp.raw <= 0) continue;
        const Fixed d = flat(w.get<Transform>(c.e).pos - hp).length() - typeOf(cu.type)->length / 2 - hr;
        if (d > rules_.enterRange) continue;
        if (c.deck >= 0 && deckDeep(st.decks[size_t(c.deck)], w.get<Transform>(c.e).pos)) continue;   // above / below the hero
        if (!best || d < bestD) { best = &c; bestD = d; }   // cars are in entity order: ties keep the lower index
    }
    if (!best) return;
    best->driver = hero;
    best->player = player;
    best->doorUntil = sim.tick() + rules_.doorTicks;
    hu.flags |= kUnitInCargo;               // off the map: not hit, not targeted, not drawn; it follows the car
    if (auto* c = w.tryGet<Combat>(hero)) c->target = kNullEntity;
    if (auto* td = m_->topDown()->store(sim); td && player < td->pilots.size()) { td->pilots[player].vel = {}; td->pilots[player].target = kNullEntity; }
    w.get<Transform>(hero).pos = w.get<Transform>(best->e).pos;
}

bool Driving::leave(Simulation& sim, DrivingCar& c, bool forced) {
    World& w = sim.world();
    const Entity hero = c.driver;
    if (!w.alive(hero)) { c.driver = kNullEntity; c.player = 0; return true; }
    if (!forced && fxAbs(vl::vehicleForwardSpeed(c.st)) > rules_.exitSpeed) return false;
    Unit& hu = w.get<Unit>(hero);
    const UnitType& ht = m_->rules().types[hu.type];
    const MapGrid& g = m_->state(sim).grid;
    const Vec3 pos{c.st.position.x / rules_.metersPerCell, w.get<Transform>(hero).pos.y, c.st.position.z / rules_.metersPerCell};
    if (auto* st = store(sim); st && c.deck >= 0 && !forced && deckDeep(st->decks[size_t(c.deck)], pos)) return false;   // no way down from a bridge, no door room in a tunnel
    const Vec3 f = forwardOf(c.st.yaw), r = rightOf(f);
    Fixed len = kUnit, wid = kHalf;
    if (w.alive(c.e)) if (auto* vt = typeOf(w.get<Unit>(c.e).type)) { len = vt->length; wid = vt->width; }
    const Fixed side = wid / 2 + ht.radius + Fixed::fromRatio(1, 10), ends = len / 2 + ht.radius + Fixed::fromRatio(1, 10);
    const Vec3 spots[4] = {pos + r * side, pos - r * side, pos - f * ends, pos + f * ends};   // driver's door (left on screen) first
    Vec3 out = pos;
    bool found = false;
    for (const Vec3& s : spots) if (g.passableAt(s, ht.pathLayer)) { out = s; found = true; break; }
    if (!found && !forced) return false;
    hu.flags = uint16_t(hu.flags & ~kUnitInCargo);
    w.get<Transform>(hero).pos = out;
    hu.facing = f;
    const Entity wreck = c.e;
    c.driver = kNullEntity;
    c.player = 0;
    c.doorUntil = sim.tick() + rules_.doorTicks;
    if (forced && rules_.wreckDamage.raw > 0) m_->dealDamage(sim, hero, rules_.wreckDamage, 0, wreck, 0);
    return true;
}

bool Driving::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdTopDownEnter) return false;
    auto* st = store(sim);
    auto* td = st ? m_->topDown()->store(sim) : nullptr;
    if (!td || cmd.player == 0 || cmd.player >= td->pilots.size()) return true;
    const Entity hero = td->pilots[cmd.player].hero;
    if (!sim.world().alive(hero)) return true;
    for (auto& c : st->cars) if (c.driver == hero) { leave(sim, c, false); return true; }
    enter(sim, *st, cmd.player, hero);
    return true;
}

// ================================================================= per tick

vl::VehicleInput Driving::inputOf(Simulation& sim, const DrivingCar& c) const {
    vl::VehicleInput in;
    auto* td = m_->topDown()->store(sim);
    if (!c.driver.valid()) {   // nobody at the wheel: a traffic car's bot driver (RtsTraffic.cpp), else parked
        if (Traffic* tr = m_->traffic(); tr && tr->active() && tr->aiInput(sim, c.e, in)) return in;
        if (c.autopilot) return c.pilot;
        in.handbrake = true;
        return in;
    }
    if (!td || c.player >= td->pilots.size()) { in.handbrake = true; return in; }   // parked
    const topdown::Input& s = td->pilots[c.player].input;
    const Fixed mag = Fixed::fromRatio(s.moveMag, topdown::kMagMax), ang = Fixed::fromRaw(s.moveAngle);
    in.handbrake = s.fire;
    if (rules_.controls == DriveControls::Tank) {
        // stick up the screen (-z) = throttle, down = brake / reverse, sideways = steering; diagonals count in full
        const Fixed boost = Fixed::fromRatio(3, 2);
        // the map is seen with +x to the right and +z down the screen, so a turn to the right on screen is a negative
        // library yaw (the library turns from +z toward +x for a positive steer)
        const Fixed t = -fxCosTurns(ang) * mag * boost, st = -fxSinTurns(ang) * mag * boost;
        in.throttle = fxClamp(t, Fixed{}, kUnit);
        in.brake = fxClamp(-t, Fixed{}, kUnit);
        in.steer = fxClamp(st, -kUnit, kUnit);
        return in;
    }
    const Fixed v = vl::vehicleForwardSpeed(c.st);
    if (s.moveMag <= 0) { if (v > kHalf) in.brake = Fixed::fromRatio(3, 10); return in; }   // let go: roll to a stop
    const Fixed diff = wrapTurns(ang - c.st.yaw);
    in.steer = fxClamp(diff * 6, -kUnit, kUnit);
    in.throttle = fxAbs(diff) > Fixed::fromRatio(1, 4) ? mag / 2 : mag;
    return in;
}

void Driving::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    adoptAndDrop(sim, *st);
    for (size_t i = 0; i < st->cars.size(); ++i) drive(sim, *st, i);
}

void Driving::drive(Simulation& sim, DrivingStore& st, size_t index) {
    World& w = sim.world();
    DrivingCar& c = st.cars[index];
    if (!w.alive(c.e)) return;
    Unit& u = w.get<Unit>(c.e);
    if (u.hp.raw <= 0 || (u.flags & kUnitInCargo)) return;
    const DrivingVehicleType& vt = *typeOf(u.type);
    const Fixed mpc = rules_.metersPerCell;
    // A parked car at rest sleeps: no controller step, no sweep (a city has hundreds of them). Whatever hits it
    // gives it a velocity (car against car) and that wakes it; getting in or a traffic driver wake it too.
    if (!c.driver.valid() && !c.autopilot && c.st.initialized && c.st.velocity == Vec3{} && c.st.yawRate.raw == 0) {
        vl::VehicleInput none;
        Traffic* tr = m_->traffic();
        if (!(tr && tr->active() && tr->aiInput(sim, c.e, none))) return;
    }
    auto res = vl::stepVehicle(vt.def, c.st, sim.tick(), sim.dt(), inputOf(sim, c), vl::VehicleContacts{});
    Transform& tr = w.get<Transform>(c.e);
    if (res) {
        const Vec3 from{res->from.x / mpc, Fixed{}, res->from.z / mpc}, to{c.st.position.x / mpc, Fixed{}, c.st.position.z / mpc};
        const Vec3 d = to - from;
        const int n = int(std::clamp<int64_t>(1 + (d.length() * 4).toInt(), 1, 8));   // sub-steps of a quarter cell
        Vec3 p = from;
        for (int i = 1; i <= n; ++i) {
            p = from + d * Fixed::fromRatio(i, n);
            if (collide(sim, st, index, p)) break;
        }
        DrivingCar& cc = st.cars[index];
        cc.st.position = {p.x * mpc, Fixed{}, p.z * mpc};
        tr.pos = {p.x, tr.pos.y, p.z};
    }
    DrivingCar& cc = st.cars[index];
    u.facing = forwardOf(cc.st.yaw);
    if (auto* mb = w.tryGet<Mobile>(c.e)) { mb->moving = false; mb->vel = {cc.st.velocity.x / mpc, Fixed{}, cc.st.velocity.z / mpc}; }
    if (cc.driver.valid() && w.alive(cc.driver)) w.get<Transform>(cc.driver).pos = tr.pos;
}

// The body is three circles along the car. Returns true when something was hit (the rest of the tick's motion is dropped).
bool Driving::collide(Simulation& sim, DrivingStore& st, size_t index, Vec3& pos) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const MapGrid& g = gs.grid;
    DrivingCar& c = st.cars[index];
    const Unit& u = w.get<Unit>(c.e);
    const UnitType& t = m_->rules().types[u.type];
    const DrivingVehicleType& vt = *typeOf(u.type);
    const Fixed mpc = rules_.metersPerCell;
    const Fixed r = vt.width / 2, half = fxMax(vt.length / 2 - r, Fixed{});
    const Vec3 f = forwardOf(c.st.yaw);
    const Fixed offs[3] = {-half, Fixed{}, half};
    bool hit = false;
    Fixed crash{};

    // levels: any part of the body inside a mouth puts the car on that deck; the whole body past an end takes it off
    if (c.deck >= 0) {
        bool allOut = true;
        for (Fixed off : offs) if (!deckSpot(st.decks[size_t(c.deck)], pos + f * off).outside) allOut = false;
        if (allOut) c.deck = -1;
    }
    for (size_t i = 0; i < st.decks.size() && c.deck < 0; ++i) {
        const DrivingDeck& d = st.decks[i];
        if (!kingston::deckMayTouchBody(d, pos, half)) continue;
        const Fixed m = deckMouth(d);
        for (Fixed off : offs) {
            const DeckSpot s = deckSpot(d, pos + f * off);
            if (!s.outside && s.side <= d.width / 2 && (s.along <= m || s.along >= d.length() - m)) { c.deck = int16_t(i); break; }
        }
    }

    // walls: push out of the deepest overlap, a few times
    for (int pass = 0; pass < 4; ++pass) {
        Fixed deepest{};
        Vec3 normal{};
        for (Fixed off : offs) {
            const Vec3 cp = pos + f * off;
            if (c.deck >= 0) {   // on a deck its own walls count, not the map under / around it; the mouths are open
                const DrivingDeck& d = st.decks[size_t(c.deck)];
                const DeckSpot s = deckSpot(d, cp);
                const Fixed room = fxMax(d.width / 2 - r, Fixed{});
                if (s.outside || s.side <= room) continue;
                if (s.side - room + kGap > deepest) { deepest = s.side - room + kGap; normal = s.toAxis / s.side; }
                continue;
            }
            const int x0 = g.cellX(cp.x - r), x1 = g.cellX(cp.x + r), y0 = g.cellY(cp.z - r), y1 = g.cellY(cp.z + r);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    if (g.passable(x, y, t.pathLayer)) continue;
                    const Vec3 q{fxClamp(cp.x, Fixed::fromInt(x), Fixed::fromInt(x + 1)), Fixed{}, fxClamp(cp.z, Fixed::fromInt(y), Fixed::fromInt(y + 1))};
                    const Vec3 dv = flat(cp - q);
                    const Fixed dist = dv.length();
                    if (dist >= r) continue;
                    Vec3 nrm;
                    if (dist.raw > 0) nrm = dv / dist;
                    else {   // the centre is inside the cell: leave through the nearest face
                        const Fixed l = cp.x - Fixed::fromInt(x), rr = Fixed::fromInt(x + 1) - cp.x, tp = cp.z - Fixed::fromInt(y), bt = Fixed::fromInt(y + 1) - cp.z;
                        const Fixed m = fxMin(fxMin(l, rr), fxMin(tp, bt));
                        if (m == l) nrm = Vec3{-kUnit, Fixed{}, Fixed{}};
                        else if (m == rr) nrm = Vec3{kUnit, Fixed{}, Fixed{}};
                        else if (m == tp) nrm = Vec3{Fixed{}, Fixed{}, -kUnit};
                        else nrm = Vec3{Fixed{}, Fixed{}, kUnit};
                    }
                    const Fixed depth = r - dist + kGap;
                    if (depth > deepest) { deepest = depth; normal = nrm; }
                }
        }
        if (deepest.raw <= 0) break;
        pos += normal * deepest;
        crash = fxMax(crash, vl::vehicleResolveCollision(c.st, c.st.position, normal, rules_.restitution));
        c.st.yawRate = c.st.yawRate / 2;
        hit = true;
    }

    // other bodies
    struct Near { Entity e; Vec3 p; Fixed r; };
    std::vector<Near> near;
    gs.hash.query(pos, vt.length + Fixed::fromInt(2), [&](Entity o) {
        if (o == c.e || o == c.driver) return;
        auto* ou = w.tryGet<Unit>(o);
        if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap)) return;
        const UnitType& ot = m_->rules().types[ou->type];
        if (ot.structure || (ot.layer & kLayerAir)) return;
        // too far to touch whatever the headings (no square root; a city block has dozens of bodies in the query)
        const Vec3 op = w.get<Transform>(o).pos;
        const Fixed touch = half + r + reach_[ou->type] + Fixed::fromRatio(1, 32);   // (a little over: the exact test follows)
        if (flat(pos - op).lengthSq() >= touch * touch) return;
        near.push_back({o, op, ot.radius});
    });
    std::sort(near.begin(), near.end(), [](const Near& a, const Near& b) { return a.e.index < b.e.index; });
    struct Hurt { Entity e; Fixed dmg; Entity by; uint8_t owner; };
    std::vector<Hurt> hurt;
    for (auto& o : near) {
        DrivingCar* oc = findCar(st, o.e);
        if (levelsApart(st, c.deck, pos, oc ? oc->deck : -1, o.p)) continue;   // one of them is on a bridge or in a tunnel
        const DrivingVehicleType* ovt = oc ? typeOf(w.get<Unit>(o.e).type) : nullptr;
        const Fixed orad = ovt ? ovt->width / 2 : o.r;
        const Fixed ohalf = ovt ? fxMax(ovt->length / 2 - orad, Fixed{}) : Fixed{};
        const Vec3 of = oc ? forwardOf(oc->st.yaw) : Vec3{};
        const Fixed ooffs[3] = {-ohalf, Fixed{}, ohalf};
        Fixed deepest{};
        Vec3 normal{};
        for (Fixed off : offs)
            for (int k = 0; k < (oc ? 3 : 1); ++k) {
                const Vec3 dv = flat(pos + f * off - (o.p + of * ooffs[k]));
                const Fixed dist = dv.length(), minD = r + orad;
                if (dist >= minD) continue;
                if (minD - dist > deepest) { deepest = minD - dist; normal = dist.raw > 0 ? dv / dist : Vec3{} - f; }
            }
        if (deepest.raw <= 0) continue;
        if (oc) {   // car against car: share the push, exchange the closing speed
            pos += normal * (deepest / 2);
            const Vec3 shift = normal * (deepest / 2);
            oc->st.position = {oc->st.position.x - shift.x * mpc, oc->st.position.y, oc->st.position.z - shift.z * mpc};
            Transform& otr = w.get<Transform>(o.e);
            otr.pos = {otr.pos.x - shift.x, otr.pos.y, otr.pos.z - shift.z};
            const Fixed vrel = (c.st.velocity - oc->st.velocity).dot(normal);
            if (vrel.raw < 0) {
                const Fixed j = -vrel * (kUnit + rules_.restitution) / 2;
                c.st.velocity += normal * j;
                oc->st.velocity = oc->st.velocity - normal * j;
                crash = fxMax(crash, j);
                if (j > rules_.crashMinSpeed) hurt.push_back({o.e, (j - rules_.crashMinSpeed) * rules_.crashDamage, c.e, c.player});
            }
            hit = true;
            continue;
        }
        const UnitType& ot = m_->rules().types[w.get<Unit>(o.e).type];
        if (ot.speed.raw <= 0) {   // things that never move (trees, lamp posts, hydrants) are solid
            pos += normal * deepest;
            crash = fxMax(crash, vl::vehicleResolveCollision(c.st, c.st.position, normal, rules_.restitution));
            hit = true;
            continue;
        }
        const Fixed closing = -c.st.velocity.dot(normal);
        if (closing > rules_.runOverMinSpeed) {   // run over: the car barely notices
            hurt.push_back({o.e, (closing - rules_.runOverMinSpeed) * rules_.runOverDamage, c.e, c.player});
            c.st.velocity = c.st.velocity * Fixed::fromRatio(98, 100);
            continue;
        }
        const Vec3 np = o.p - normal * deepest;
        if (g.passableAt(np, ot.pathLayer)) {   // slow: people step aside
            Transform& otr = w.get<Transform>(o.e);
            otr.pos = {np.x, otr.pos.y, np.z};
        } else {
            pos += normal * deepest;
            vl::vehicleResolveCollision(c.st, c.st.position, normal, Fixed{});
            hit = true;
        }
    }
    if (crash > rules_.crashMinSpeed) hurt.push_back({c.e, (crash - rules_.crashMinSpeed) * rules_.crashDamage, kNullEntity, 0});
    for (auto& h : hurt)
        if (h.dmg.raw > 0 && w.alive(h.e) && w.get<Unit>(h.e).hp.raw > 0) m_->dealDamage(sim, h.e, h.dmg, 0, h.by, h.owner);
    return hit;
}

} // namespace aiverse::game
