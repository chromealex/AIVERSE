// The vehicle system (Vehicles.h): rules, components, the per-tick step, hash and save. Fixed maths only; every loop
// runs in ascending entity index.
#include "Vehicles.h"
#include "VehicleRace.h"
#include "engine/core/Archive.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Hash.h"
#include "engine/mechanics/VehicleData.h"
#include "engine/mechanics/VehiclePersistence.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::vehicles {

namespace {
const Fixed kUnit = Fixed::fromInt(1);
Fixed number(const Json& j, const char* key, Fixed def, const std::string& where, bool positive) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0 || (positive && v.raw == 0))
        throw std::runtime_error(where + "." + key + (positive ? ": must be positive" : ": must not be negative"));
    return v;
}
} // namespace

// ================================================================= rules

const VehicleType* DriveRules::type(int64_t t) const {
    auto it = std::lower_bound(vehicles.begin(), vehicles.end(), t, [](const VehicleType& v, int64_t x) { return v.type < x; });
    return it != vehicles.end() && it->type == t ? &*it : nullptr;
}

DriveRules parseDriving(const Json& j, const std::vector<std::string>& ids) {
    DriveRules r;
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
        VehicleType d;
        d.type = -1;
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) d.type = int64_t(i);
        if (d.type < 0) throw std::runtime_error(where + ": unknown type '" + id + "'");
        d.length = number(v, "length", d.length, where, true);
        d.width = number(v, "width", d.width, where, true);
        if (d.width > d.length) throw std::runtime_error(where + ": width must not exceed length");
        if (d.length > Fixed::fromInt(8)) throw std::runtime_error(where + ".length: at most 8 cells");
        if (v["wheeled"].isObject()) lm::vehicleDataDetail::wheeled(v["wheeled"], where + ".wheeled", d.def.wheeled);
        if (!lm::valid(d.def)) throw std::runtime_error(where + ".wheeled: invalid controller parameters");
        r.vehicles.push_back(std::move(d));
    }
    if (r.vehicles.empty()) throw std::runtime_error("driving.vehicles: list at least one unit type");
    // stable: a type listed twice keeps its first entry (the one type() finds)
    std::stable_sort(r.vehicles.begin(), r.vehicles.end(), [](const VehicleType& a, const VehicleType& b) { return a.type < b.type; });
    r.vehicles.erase(std::unique(r.vehicles.begin(), r.vehicles.end(), [](const VehicleType& a, const VehicleType& b) { return a.type == b.type; }), r.vehicles.end());
    return r;
}

// ================================================================= queries

std::vector<Entity> vehicleEntities(const World& w) {
    std::vector<Entity> out;
    w.eachExistingConst<VehicleDrive>([&](Entity e, const VehicleDrive&) { out.push_back(e); });
    std::sort(out.begin(), out.end(), [](Entity a, Entity b) { return a.index < b.index; });
    return out;
}
VehicleDrive* driveOf(World& w, Entity e) { return w.tryGet<VehicleDrive>(e); }
const VehicleDrive* driveOf(const World& w, Entity e) { return w.tryGetConst<VehicleDrive>(e); }
Entity vehicleOf(const World& w, Entity driver) {
    const auto* o = w.tryGetConst<VehicleOccupant>(driver);
    return o ? o->vehicle : kNullEntity;
}
Fixed speedOf(const World& w, Entity e) {
    const VehicleDrive* d = driveOf(w, e);
    return d ? lm::vehicleForwardSpeed(d->controller) : Fixed{};
}
bool setAutopilot(World& w, Entity e, const lm::VehicleInput& input) {
    VehicleDrive* d = driveOf(w, e);
    if (!d || !lm::valid(input)) return false;
    d->autopilot = true;
    d->pilot = input;
    return true;
}
bool clearAutopilot(World& w, Entity e) {
    VehicleDrive* d = driveOf(w, e);
    if (!d) return false;
    d->autopilot = false;
    d->pilot = {};
    return true;
}
bool setLocked(World& w, Entity e, bool locked) {
    VehicleDrive* d = driveOf(w, e);
    if (!d) return false;
    d->locked = locked;
    return true;
}
bool isLocked(const World& w, Entity e) {
    const VehicleDrive* d = driveOf(w, e);
    return d && d->locked;
}
VehicleDrive& addVehicle(World& w, Entity e, const VehicleType& t, Vec3 position, Fixed yaw) {
    VehicleDrive d;
    d.type = t.type;
    d.controller = lm::makeVehicleState(t.def, position, yaw);
    return w.add<VehicleDrive>(e, std::move(d));
}
namespace {
void pose(VehicleOccupant& o, Entity vehicle, const VehicleDrive& d) {
    o.vehicle = vehicle; o.type = d.type; o.position = d.controller.position; o.yaw = d.controller.yaw;
    o.speed = lm::vehicleForwardSpeed(d.controller); o.deck = d.deck;
}
} // namespace
void seatDriver(World& w, Entity vehicle, VehicleDrive& d, Entity driver, uint8_t player, uint32_t doorUntil) {
    d.driver = driver;
    d.player = player;
    d.doorUntil = doorUntil;
    if (w.alive(driver)) {
        VehicleOccupant o;
        pose(o, vehicle, d);
        if (auto* old = w.tryGet<VehicleOccupant>(driver)) *old = o; else w.add<VehicleOccupant>(driver, o);
    }
}
void unseatDriver(World& w, VehicleDrive* d, Entity driver, uint32_t doorUntil) {
    if (d) { d->driver = kNullEntity; d->player = 0; d->doorUntil = doorUntil; }
    if (w.alive(driver)) w.remove<VehicleOccupant>(driver);
}
lm::VehicleInput stickPedals(const DriveRules& r, const lm::DriveScreenStick& s, const lm::VehicleState& car) {
    return lm::vehicleInputFromStick(r.controls == DriveControls::Tank ? lm::DriveStick::Tank : lm::DriveStick::Steer, s, car);
}

// ================================================================= per tick

lm::VehicleInput ownPedals(VehicleHost& host, Entity e, const VehicleDrive& d) {
    lm::VehicleInput in;
    if (!d.driver.valid()) {   // nobody at the wheel: a host bot (traffic), the autopilot, else parked
        if (host.botPedals(e, d, in)) return in;
        if (d.autopilot) return d.pilot;
        return host.parked(e, d);
    }
    if (host.driverPedals(e, d, in)) return in;
    return host.parked(e, d);
}

namespace {
class Impacts final : public VehicleImpacts {
public:
    Impacts(World& w, VehicleHost& h, const DriveRules& r, const VehicleRace* race, Entity car) : w_(w), host_(h), r_(r), race_(race), car_(car) {}
    void apply(const VehicleSweep& s) override {
        const VehicleDrive* d = driveOf(w_, car_);
        const uint8_t player = d ? d->player : 0;
        struct Hurt { Entity e; Fixed dmg; Entity by; uint8_t owner; };
        std::vector<Hurt> hurt;
        for (const auto& h : s.hits) {
            if (h.kind == VehicleHit::Crash) {
                if (h.speed > r_.crashMinSpeed) hurt.push_back({h.victim, (h.speed - r_.crashMinSpeed) * r_.crashDamage, car_, player});
            } else if (h.speed > r_.runOverMinSpeed) hurt.push_back({h.victim, (h.speed - r_.runOverMinSpeed) * r_.runOverDamage, car_, player});
        }
        if (s.impact > r_.crashMinSpeed && (!race_ || race_->crashHurts(w_, car_)))   // racers: rules race.crashDamage
            hurt.push_back({car_, (s.impact - r_.crashMinSpeed) * r_.crashDamage, kNullEntity, 0});
        for (auto& h : hurt) if (h.dmg.raw > 0) host_.damage(h.e, h.dmg, h.by, h.owner);
    }
private:
    World& w_;
    VehicleHost& host_;
    const DriveRules& r_;
    const VehicleRace* race_;
    Entity car_;
};
} // namespace

void stepVehicles(World& w, VehicleHost& host, const DriveRules& rules, uint32_t tick, Fixed dt,
                  std::span<const RacePedals> race, const VehicleRace* raceAdapter) {
    for (Entity e : vehicleEntities(w)) {
        VehicleDrive* d = driveOf(w, e);
        if (!d || !host.active(e, *d)) continue;
        const VehicleType* vt = rules.type(d->type);
        if (!vt) continue;
        // An empty parked vehicle at rest sleeps: no controller step, no sweep (a city has hundreds of them). Whatever
        // hits it gives it a velocity and that wakes it; getting in, the autopilot or a host bot wake it too.
        if (!d->driver.valid() && !d->autopilot && d->seats.occupants.empty() && d->controller.initialized &&
            d->controller.velocity == Vec3{} && d->controller.yawRate.raw == 0) {
            lm::VehicleInput none;
            if (!host.botPedals(e, *d, none)) continue;
            d = driveOf(w, e);
            if (!d) continue;
        }
        const RacePedals* rp = nullptr;
        for (const auto& x : race) if (x.index == e.index) { rp = &x; break; }
        lm::VehicleInput in;
        if (rp && host.controllable(e, *d)) in = rp->input;   // a racer: the race session's pedals
        else in = ownPedals(host, e, *d);
        d = driveOf(w, e);
        if (!d) continue;
        const lm::VehicleContacts k = host.ground(e, *d);
        auto res = lm::stepVehicle(vt->def, d->controller, tick, dt, in, k);
        if (res) {
            Impacts impacts(w, host, rules, raceAdapter, e);
            host.sweep(e, *d, *res, impacts);
            d = driveOf(w, e);
            if (!d) continue;
        }
        host.placed(e, *d);
        if (d->driver.valid()) if (auto* o = w.tryGet<VehicleOccupant>(d->driver)) pose(*o, e, *d);
    }
}

// ================================================================= hash and save

void hashVehicles(const World& w, Hasher& h, bool decks) {
    const auto list = vehicleEntities(w);
    h.u32(uint32_t(list.size()));
    for (Entity e : list) {
        const VehicleDrive& c = *driveOf(w, e);
        const auto& s = c.controller;
        h.u64(e.packed()); h.u64(c.driver.packed()); h.u32(c.player); h.u32(c.doorUntil);
        if (decks) h.i32(c.deck);   // maps without decks keep their checksums
        h.v3(s.position); h.v3(s.velocity);
        for (Fixed f : {s.yaw, s.yawRate, s.pitch, s.roll, s.steer, s.rpm, s.airspeed, s.sink, s.longAccel, s.trackLeft, s.trackRight}) h.fx(f);
        h.i32(s.gear); h.u32(s.shiftUntil); h.u32(s.slipMask); h.u32(s.lastTick);
        h.u32(uint32_t(s.grounded) | uint32_t(s.drifting) << 1 | uint32_t(s.stalled) << 2 | uint32_t(s.initialized) << 3 | uint32_t(c.autopilot) << 4 | uint32_t(c.locked) << 5);
        for (Fixed f : {c.pilot.throttle, c.pilot.brake, c.pilot.steer}) h.fx(f);
        h.u32(uint32_t(c.pilot.handbrake) | uint32_t(c.pilot.shiftUp) << 1 | uint32_t(c.pilot.shiftDown) << 2);
        if (c.seats.initialized || !c.seats.occupants.empty()) lm::hashVehicle(h, c.seats);   // seat lists only
    }
}

void io(Archive& a, VehicleDrive& c) {
    a(c.type);
    lm::io(a, c.controller);
    a(c.driver, c.player, c.doorUntil, c.locked, c.deck, c.autopilot);
    a(c.pilot.throttle, c.pilot.brake, c.pilot.steer, c.pilot.pitch, c.pilot.roll, c.pilot.yaw, c.pilot.handbrake, c.pilot.shiftUp, c.pilot.shiftDown);
    lm::io(a, c.seats);
}

bool valid(const VehicleDrive& d) {
    return lm::valid(d.controller) && lm::valid(d.seats) && lm::valid(d.pilot) && d.deck >= -1;
}

void io(Archive& a, VehicleOccupant& o) { a(o.vehicle, o.type, o.position, o.yaw, o.speed, o.deck); }

} // namespace aiverse::vehicles
