// Driving on a grid map (Driving.h): the grid-map host of the vehicle system (engine/vehicles/Vehicles.h) -
// vehicle adoption, getting in / out, the hero's stick as the driver's controls, the sweep of the body over the map
// grid and other units, decks, damage and the race seats. Fixed maths only; every loop runs in entity-index order.
#include "Driving.h"
#include "engine/core/SweptShapes.h"
namespace {
// Conservative rejection only: exact deckSpot, deck ordering and mouth rules still decide for nearby cars.
// Q16 normalization can displace a projected endpoint by < segment-length raw units per axis.
bool deckMayTouchBody(const aiverse::game::DrivingDeck& d, aiverse::Vec3 center, aiverse::Fixed halfBody) {
    using namespace aiverse;
    if (d.pts.size() < 2 || d.at.size() != d.pts.size()) return true;
    int64_t projectionError = 4;
    for (size_t i = 1; i < d.pts.size(); ++i) projectionError = std::max(projectionError, (d.at[i] - d.at[i - 1]).floorInt() + 4);
    const Fixed margin = halfBody + d.width / 2 + Fixed::fromRatio(1, 16) + Fixed::fromRaw(projectionError);
    return lib::mayTouch(lib::polylineBounds(std::span<const Vec3>(d.pts), margin), center.x, center.z);
}
}
#include "Systems.h"
#include "../core/Archive.h"
#include "../core/FixedMath.h"
#include "../roads/RoadNet.h"
#include "engine/mechanics/VehiclePersistence.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
namespace vl = lib::mechanics;
namespace vs = vehicles;

namespace {
const Fixed kUnit = Fixed::fromInt(1);
const Fixed kGap = Fixed::fromRatio(1, 64);

Vec3 forwardOf(Fixed yaw) { return {fxSinTurns(yaw), Fixed{}, fxCosTurns(yaw)}; }   // yaw 0 = +z, a quarter turn = +x
Vec3 rightOf(Vec3 f) { return {f.z, Fixed{}, -f.x}; }
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
bool levelsApart(const std::vector<DrivingDeck>& decks, int deckA, Vec3 a, int deckB, Vec3 b) {
    if (deckA == deckB) return false;
    return (deckA < 0 || deckDeep(decks[size_t(deckA)], a)) && (deckB < 0 || deckDeep(decks[size_t(deckB)], b));
}
std::vector<DrivingDeck>& decksOf(Simulation& sim) {
    auto* d = sim.world().resource<DrivingDecks>();
    return d ? d->decks : sim.world().setResource<DrivingDecks>().decks;
}
} // namespace

// ================================================================= the vehicle system's host (Vehicles.h)

class Driving::Host final : public vs::VehicleHost {
public:
    Host(Driving& d, Simulation& sim) : d_(d), sim_(sim), w_(sim.world()) {}
    bool active(Entity e, const vs::VehicleDrive&) override {
        if (!w_.alive(e)) return false;
        const Unit& u = w_.get<Unit>(e);
        return u.hp.raw > 0 && !(u.flags & kUnitInCargo);
    }
    bool driverPedals(Entity, const vs::VehicleDrive& c, vl::VehicleInput& in) override {
        bool ok = false;
        in = d_.heroPedals(sim_, c, ok);
        return ok;
    }
    // nobody at the wheel: a traffic car's bot driver (Traffic.cpp)
    bool botPedals(Entity e, const vs::VehicleDrive&, vl::VehicleInput& in) override {
        Traffic* tr = d_.m_->traffic();
        return tr && tr->active() && tr->aiInput(sim_, e, in);
    }
    void sweep(Entity e, vs::VehicleDrive& c, const vl::VehicleStepResult& res, vs::VehicleImpacts& impacts) override {
        const Fixed mpc = d_.rules_.metersPerCell;
        const Vec3 from{res.from.x / mpc, Fixed{}, res.from.z / mpc}, to{c.controller.position.x / mpc, Fixed{}, c.controller.position.z / mpc};
        const Vec3 dv = to - from;
        const int n = int(std::clamp<int64_t>(1 + (dv.length() * 4).toInt(), 1, 8));   // sub-steps of a quarter cell
        Vec3 p = from;
        for (int i = 1; i <= n; ++i) {
            p = from + dv * Fixed::fromRatio(i, n);
            if (d_.collide(sim_, e, p, impacts)) break;
        }
        vs::VehicleDrive& cc = *vs::driveOf(w_, e);
        cc.controller.position = {p.x * mpc, Fixed{}, p.z * mpc};
        Transform& tr = w_.get<Transform>(e);
        tr.pos = {p.x, tr.pos.y, p.z};
    }
    void placed(Entity e, vs::VehicleDrive& c) override {
        const Fixed mpc = d_.rules_.metersPerCell;
        w_.get<Unit>(e).facing = forwardOf(c.controller.yaw);
        if (auto* mb = w_.tryGet<Mobile>(e)) { mb->moving = false; mb->vel = {c.controller.velocity.x / mpc, Fixed{}, c.controller.velocity.z / mpc}; }
        if (c.driver.valid() && w_.alive(c.driver)) w_.get<Transform>(c.driver).pos = w_.get<Transform>(e).pos;
    }
    void damage(Entity victim, Fixed amount, Entity by, uint8_t player) override {
        if (w_.alive(victim) && w_.get<Unit>(victim).hp.raw > 0) d_.m_->dealDamage(sim_, victim, amount, 0, by, player);
    }
private:
    Driving& d_;
    Simulation& sim_;
    World& w_;
};

// The race's seats (VehicleRace.h): player i + 1's hero takes car i, a bot or a player without a hero races with the bot.
class Driving::Seats final : public vs::RaceHost {
public:
    Seats(Driving& d, Simulation& sim, vs::VehicleHost& host) : d_(d), sim_(sim), w_(sim.world()), host_(host) {}
    void placeOnGrid(size_t, Entity car, vs::VehicleDrive& c, const vl::RaceGridSlot& g) override {
        const Fixed mpc = d_.rules_.metersPerCell;
        vl::respawnVehicle(d_.rules_.type(c.type)->def, c.controller, g.position, g.yaw);
        Transform& tr = w_.get<Transform>(car);
        tr.pos = {g.position.x / mpc, tr.pos.y, g.position.z / mpc};
        w_.get<Unit>(car).facing = forwardOf(g.yaw);
    }
    bool seat(size_t i, Entity car, vs::VehicleDrive& c) override {
        auto* td = d_.m_->topDown()->store(sim_);
        const uint8_t player = uint8_t(std::min<size_t>(i + 1, 255));
        const Entity hero = td && player < td->pilots.size() ? td->pilots[player].hero : kNullEntity;
        const bool bot = (i < d_.race_->bots.size() && d_.race_->bots[i]) || !w_.alive(hero) || w_.get<Unit>(hero).hp.raw <= 0;
        if (!bot) {
            if (Entity other = vs::vehicleOf(w_, hero); other.valid()) d_.leave(sim_, other, hero, true);
            const Entity at = vs::driveOf(w_, car)->driver;
            if (at.valid() && at != hero) d_.leave(sim_, car, at, true);
            if (vs::driveOf(w_, car)->driver != hero) d_.seat(sim_, car, player, hero);
        } else {
            if (c.driver.valid()) d_.leave(sim_, car, c.driver, true);
            vs::VehicleDrive& cc = *vs::driveOf(w_, car);
            cc.autopilot = true;   // stepped every tick (a racing bot never sleeps)
            cc.pilot = {};
            cc.pilot.brake = kUnit;
            cc.pilot.handbrake = true;
        }
        return bot;
    }
    vl::VehicleInput pedals(size_t, Entity car, const vs::VehicleDrive& c) override { return vs::ownPedals(host_, car, c); }
    void respawned(size_t, Entity car, vs::VehicleDrive& c) override {
        const Fixed mpc = d_.rules_.metersPerCell;
        Transform& tr = w_.get<Transform>(car);
        tr.pos = {c.controller.position.x / mpc, tr.pos.y, c.controller.position.z / mpc};
        w_.get<Unit>(car).facing = forwardOf(c.controller.yaw);
        if (c.driver.valid() && w_.alive(c.driver)) w_.get<Transform>(c.driver).pos = tr.pos;
    }
    int32_t raceOver(int64_t winner) override {   // the race decides the match: the winner's team (0: a draw)
        int team = 0;
        if (winner >= 1 && size_t(winner) <= sim_.players().size()) team = sim_.players()[size_t(winner - 1)].team;
        d_.m_->state(sim_).winnerTeam = team;
        return team;
    }
private:
    Driving& d_;
    Simulation& sim_;
    World& w_;
    vs::VehicleHost& host_;
};

// ================================================================= data

const std::vector<DrivingDeck>* Driving::decks(Simulation& sim) const {
    if (!active()) return nullptr;
    const auto* d = sim.world().resource<DrivingDecks>();
    return d ? &d->decks : nullptr;
}

void Driving::setup(Simulation& sim, GameHost& m, const Json& rulesJson) {
    m_ = &m;
    std::vector<std::string> ids;
    for (auto& t : m.rules().types) ids.push_back(t.id);
    rules_ = vs::parseDriving(rulesJson["driving"], ids);
    if (!active()) return;
    if (!m.topDown() || !m.topDown()->active()) throw std::runtime_error("driving: needs the 'topDown' block (the hero is the driver)");
    rules_.doorTicks = uint32_t((rules_.doorSeconds * int64_t(sim.tickRate())).toInt());
    // how far a body of each type reaches from its centre (cars: half their length): the sweep's cheap first test
    reach_.assign(m.rules().types.size(), Fixed{});
    for (size_t i = 0; i < reach_.size(); ++i) reach_[i] = m.rules().types[i].radius;
    for (auto& v : rules_.vehicles) if (size_t(v.type) < reach_.size()) reach_[size_t(v.type)] = fxMax(v.length / 2, v.width / 2);
    sim.world().setResource<DrivingDecks>();
    adoptAndDrop(sim);
}

const vs::VehicleDrive* Driving::carOf(Simulation& sim, Entity driver) const {
    if (!active() || !driver.valid()) return nullptr;
    const vs::VehicleDrive* c = vs::driveOf(sim.world(), vs::vehicleOf(sim.world(), driver));
    return c && c->driver == driver ? c : nullptr;
}

Entity Driving::vehicleOf(Simulation& sim, Entity driver) const {
    return carOf(sim, driver) ? vs::vehicleOf(sim.world(), driver) : kNullEntity;
}

const vs::VehicleDrive* Driving::car(Simulation& sim, Entity vehicle) const {
    return active() ? vs::driveOf(sim.world(), vehicle) : nullptr;
}

bool Driving::setAutopilot(Simulation& sim, Entity vehicle, const vl::VehicleInput& input) { return active() && vs::setAutopilot(sim.world(), vehicle, input); }
bool Driving::clearAutopilot(Simulation& sim, Entity vehicle) { return active() && vs::clearAutopilot(sim.world(), vehicle); }
bool Driving::setLocked(Simulation& sim, Entity vehicle, bool locked) { return active() && vs::setLocked(sim.world(), vehicle, locked); }
bool Driving::locked(Simulation& sim, Entity vehicle) const { return active() && vs::isLocked(sim.world(), vehicle); }
Fixed Driving::speed(Simulation& sim, Entity vehicle) const { return active() ? vs::speedOf(sim.world(), vehicle) : Fixed{}; }

void Driving::hash(Simulation& sim, Hasher& h) const {
    if (!active()) return;
    const auto* d = decks(sim);
    vs::hashVehicles(sim.world(), h, d && !d->empty());
    if (race_) race_->hash(sim.world(), h);   // maps with a race only
}

// The vehicles themselves are RTS pools (VehicleDrive, VehicleOccupant: Save.cpp); here the decks and the race.
void Driving::io(Simulation& sim, Archive& a) {
    if (!active()) return;
    auto& decks = decksOf(sim);
    uint32_t nd = uint32_t(decks.size());
    aiverse::io(a, nd);
    if (!a.writing()) { if (nd > 100000) { a.fail(); return; } decks.assign(nd, {}); }
    for (auto& d : decks) a(d.id, d.width, d.pts, d.at);
    if (!a.writing()) {
        World& w = sim.world();
        for (Entity e : vs::vehicleEntities(w)) {
            const vs::VehicleDrive& c = *vs::driveOf(w, e);
            if (!vs::valid(c) || c.deck >= int(nd) || !w.has<Unit>(e) || !rules_.type(c.type)) { a.fail(); return; }
        }
    }
    if (race_) {   // maps with a race only: layout unchanged otherwise
        auto* rs = vs::VehicleRace::state(sim.world());
        vs::VehicleRace::io(a, rs ? *rs : sim.world().setResource<vs::RaceState>());
    }
}

// ================================================================= levels

void Driving::loadMap(Simulation& sim, const Json& map, const Json& roadTypes) {
    if (!active()) return;
    auto& decks = decksOf(sim);
    decks.clear();
    Json list = map["decks"];
    if (!list.isNull() && !list.isArray()) throw std::runtime_error("map decks: expected a list");
    // spline roads: their bridge and tunnel spans are decks too (heights over the ground, engine/roads roadDecks)
    if (map["roadNetwork"].isObject()) {
        const std::vector<roads::RoadTypeDef> types = roads::loadRoadTypes(roadTypes);
        const roads::RoadNetwork net = roads::parseRoadNetwork(map["roadNetwork"], types);
        const Json more = roads::roadDecks(net, types, roads::buildRoadGeometry(net, types));
        if (more.size() > 0) {
            if (list.isNull()) list = Json::array();
            for (auto& d : more.items()) list.push(d);
        }
    }
    if (list.isNull()) return;
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
        if (decks.size() >= 32000) throw std::runtime_error("map decks: too many");
        decks.push_back(std::move(d));
    }
}

int Driving::deckOf(Simulation& sim, Entity vehicle) const {
    const vs::VehicleDrive* c = car(sim, vehicle);
    return c ? c->deck : -1;
}

int Driving::deckIndex(Simulation& sim, const std::string& id) const {
    if (const auto* d = decks(sim))
        for (size_t i = 0; i < d->size(); ++i) if ((*d)[i].id == id) return int(i);
    return -1;
}

Fixed Driving::height(Simulation& sim, Entity vehicle) const {
    const auto* ds = decks(sim);
    const vs::VehicleDrive* c = car(sim, vehicle);
    if (!ds || !c || c->deck < 0) return {};
    const DrivingDeck& d = (*ds)[size_t(c->deck)];
    const Fixed mpc = rules_.metersPerCell;
    const DeckSpot s = deckSpot(d, Vec3{c->controller.position.x / mpc, Fixed{}, c->controller.position.z / mpc});
    for (size_t i = 0; i + 1 < d.pts.size(); ++i)
        if (s.along <= d.at[i + 1] || i + 2 == d.pts.size())
            return d.pts[i].y + (d.pts[i + 1].y - d.pts[i].y) * fxClamp((s.along - d.at[i]) / (d.at[i + 1] - d.at[i]), Fixed{}, kUnit);
    return {};
}

bool Driving::setDeck(Simulation& sim, Entity vehicle, int deck) {
    const auto* ds = decks(sim);
    vs::VehicleDrive* c = ds ? vs::driveOf(sim.world(), vehicle) : nullptr;
    if (!c || deck < -1 || deck >= int(ds->size())) return false;
    if (deck >= 0) {
        const DrivingDeck& d = (*ds)[size_t(deck)];
        const DeckSpot s = deckSpot(d, sim.world().get<Transform>(vehicle).pos);
        if (s.outside || s.side > d.width / 2) return false;
    }
    c->deck = int16_t(deck);
    return true;
}

// ================================================================= getting in and out

void Driving::adoptAndDrop(Simulation& sim) {
    World& w = sim.world();
    // gone or destroyed vehicles: the driver is thrown out where the wreck is (in the vehicles' entity order)
    struct Gone { Entity car, hero; };
    std::vector<Gone> gone;
    w.eachExisting<vs::VehicleDrive>([&](Entity e, vs::VehicleDrive& c) { if (w.get<Unit>(e).hp.raw <= 0) gone.push_back({e, c.driver}); });
    w.eachExisting<vs::VehicleOccupant>([&](Entity hero, vs::VehicleOccupant& o) { if (!w.alive(o.vehicle)) gone.push_back({o.vehicle, hero}); });
    std::sort(gone.begin(), gone.end(), [](const Gone& a, const Gone& b) { return a.car.index < b.car.index; });
    for (const Gone& g : gone) {
        if (g.hero.valid()) leave(sim, g.car, g.hero, true);
        if (w.alive(g.car)) w.remove<vs::VehicleDrive>(g.car);
    }
    std::vector<Entity> fresh;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform&) {
        if (u.hp.raw <= 0 || (u.flags & (kUnitOffMap | kUnitUnderConstruction)) || !typeOf(u.type)) return;
        if (!w.has<vs::VehicleDrive>(e)) fresh.push_back(e);
    });
    std::sort(fresh.begin(), fresh.end(), [](Entity a, Entity b) { return a.index < b.index; });
    for (Entity e : fresh) adoptOne(sim, e);
}

void Driving::adoptOne(Simulation& sim, Entity e) {
    World& w = sim.world();
    Unit& u = w.get<Unit>(e);
    const vs::VehicleType& vt = *typeOf(u.type);
    const Vec3 p = w.get<Transform>(e).pos;
    vs::addVehicle(w, e, vt, {p.x * rules_.metersPerCell, Fixed{}, p.z * rules_.metersPerCell}, Fixed::fromRaw(TopDown::angleOf(u.facing)));
    w.get<Unit>(e).flags |= kUnitPossessed;          // RTS orders and the movement system leave it to this file
    if (auto* o = w.tryGet<Orders>(e)) o->queue.clear();
    if (w.has<Mobile>(e)) m_->stopMoving(sim, e);
}

bool Driving::adopt(Simulation& sim, Entity e, Fixed speed) {
    World& w = sim.world();
    if (!active() || !w.alive(e) || !w.has<Transform>(e)) return false;
    Unit& u = w.get<Unit>(e);
    if (u.hp.raw <= 0 || (u.flags & (kUnitOffMap | kUnitUnderConstruction)) || !typeOf(u.type)) return false;
    if (!w.has<vs::VehicleDrive>(e)) adoptOne(sim, e);
    vs::VehicleDrive* c = vs::driveOf(w, e);
    if (!c) return false;
    const Vec3 f = forwardOf(c->controller.yaw);
    c->controller.velocity = Vec3{f.x * speed, Fixed{}, f.z * speed};
    return true;
}

Entity Driving::enterTarget(Simulation& sim, Entity hero) const {
    World& w = sim.world();
    const auto* ds = decks(sim);
    if (!ds || !w.alive(hero)) return kNullEntity;
    const Unit& hu = w.get<Unit>(hero);
    if (hu.hp.raw <= 0 || (hu.flags & kUnitOffMap)) return kNullEntity;
    const Vec3 hp = w.get<Transform>(hero).pos;
    const Fixed hr = m_->rules().types[hu.type].radius;
    Entity best = kNullEntity;
    Fixed bestD{};
    for (Entity e : vs::vehicleEntities(w)) {
        const vs::VehicleDrive& c = *vs::driveOf(w, e);
        if (c.driver.valid()) continue;
        const Unit& cu = w.get<Unit>(e);
        if (cu.hp.raw <= 0) continue;
        const Fixed d = flat(w.get<Transform>(e).pos - hp).length() - typeOf(cu.type)->length / 2 - hr;
        if (d > rules_.enterRange) continue;
        if (c.deck >= 0 && deckDeep((*ds)[size_t(c.deck)], w.get<Transform>(e).pos)) continue;   // above / below the hero
        if (!best.valid() || d < bestD) { best = e; bestD = d; }   // entity order: ties keep the lower index
    }
    return best;
}

void Driving::enter(Simulation& sim, uint8_t player, Entity hero) {
    const Entity best = enterTarget(sim, hero);
    if (!best.valid() || vs::isLocked(sim.world(), best)) return;   // a locked door: the game says so and decides who opens it
    seat(sim, best, player, hero);
}

void Driving::seat(Simulation& sim, Entity car, uint8_t player, Entity hero) {
    World& w = sim.world();
    vs::seatDriver(w, car, *vs::driveOf(w, car), hero, player, sim.tick() + rules_.doorTicks);
    w.get<Unit>(hero).flags |= kUnitInCargo;   // off the map: not hit, not targeted, not drawn; it follows the car
    if (auto* c = w.tryGet<Combat>(hero)) c->target = kNullEntity;
    if (auto* td = m_->topDown()->store(sim); td && player < td->pilots.size()) { td->pilots[player].vel = {}; td->pilots[player].target = kNullEntity; }
    w.get<Transform>(hero).pos = w.get<Transform>(car).pos;
}

// The car's pose comes from its VehicleDrive, or (the car is gone) from the driver's VehicleOccupant.
bool Driving::leave(Simulation& sim, Entity car, Entity hero, bool forced) {
    World& w = sim.world();
    vs::VehicleDrive* c = w.alive(car) ? vs::driveOf(w, car) : nullptr;
    if (!w.alive(hero)) { if (c) { c->driver = kNullEntity; c->player = 0; } return true; }
    const vs::VehicleOccupant* o = w.tryGet<vs::VehicleOccupant>(hero);
    if (!c && !o) return true;
    const Vec3 at = c ? c->controller.position : o->position;
    const Fixed yaw = c ? c->controller.yaw : o->yaw, v = c ? vl::vehicleForwardSpeed(c->controller) : o->speed;
    const int deck = c ? c->deck : o->deck;
    if (!forced && fxAbs(v) > rules_.exitSpeed) return false;
    Unit& hu = w.get<Unit>(hero);
    const UnitType& ht = m_->rules().types[hu.type];
    const MapGrid& g = m_->state(sim).grid;
    const Vec3 pos{at.x / rules_.metersPerCell, w.get<Transform>(hero).pos.y, at.z / rules_.metersPerCell};
    if (const auto* ds = decks(sim); ds && deck >= 0 && deck < int(ds->size()) && !forced && deckDeep((*ds)[size_t(deck)], pos)) return false;   // no way down from a bridge, no door room in a tunnel
    const Vec3 f = forwardOf(yaw), r = rightOf(f);
    Fixed len = kUnit, wid = kHalf;
    if (w.alive(car)) if (auto* vt = typeOf(w.get<Unit>(car).type)) { len = vt->length; wid = vt->width; }
    const Fixed side = wid / 2 + ht.radius + Fixed::fromRatio(1, 10), ends = len / 2 + ht.radius + Fixed::fromRatio(1, 10);
    const Vec3 spots[4] = {pos + r * side, pos - r * side, pos - f * ends, pos + f * ends};   // driver's door (left on screen) first
    Vec3 out = pos;
    bool found = false;
    for (const Vec3& s : spots) if (g.passableAt(s, ht.pathLayer)) { out = s; found = true; break; }
    if (!found && !forced) return false;
    hu.flags = uint16_t(hu.flags & ~kUnitInCargo);
    w.get<Transform>(hero).pos = out;
    hu.facing = f;
    vs::unseatDriver(w, c, hero, sim.tick() + rules_.doorTicks);
    if (forced && rules_.wreckDamage.raw > 0) m_->dealDamage(sim, hero, rules_.wreckDamage, 0, car, 0);
    return true;
}

bool Driving::policeExit(Simulation& sim, Entity hero) {
    if (!active()) return false;
    const Entity car = vs::vehicleOf(sim.world(), hero);
    if (!car.valid()) return true;
    if (fxAbs(speed(sim, car)) > rules_.exitSpeed) return false;
    return leave(sim, car, hero, true);
}

bool Driving::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdTopDownEnter) return false;
    auto* td = active() ? m_->topDown()->store(sim) : nullptr;
    if (!td || cmd.player == 0 || cmd.player >= td->pilots.size()) return true;
    const Entity hero = td->pilots[cmd.player].hero;
    if (!sim.world().alive(hero)) return true;
    if (const Entity car = vs::vehicleOf(sim.world(), hero); car.valid()) {
        if (!race_ || !race_->holds(sim.world(), car)) leave(sim, car, hero, false);
        return true;
    }
    enter(sim, cmd.player, hero);
    return true;
}

// ================================================================= per tick

// the hero's sticky top-down input through the shared screen-stick mapping (mechanics/VehicleDrive.h)
vl::VehicleInput Driving::heroPedals(Simulation& sim, const vs::VehicleDrive& c, bool& ok) const {
    auto* td = m_->topDown()->store(sim);
    ok = td && c.player < td->pilots.size();
    if (!ok) return {};
    const topdown::Input& s = td->pilots[c.player].input;
    const vl::DriveScreenStick stick{Fixed::fromRaw(s.moveAngle), Fixed::fromRatio(s.moveMag, topdown::kMagMax), s.fire};
    return vs::stickPedals(rules_, stick, c.controller);
}

void Driving::tick(Simulation& sim) {
    if (!active()) return;
    World& w = sim.world();
    adoptAndDrop(sim);
    Host host(*this, sim);
    Seats seats(*this, sim, host);
    std::vector<vs::RacePedals> pedals;
    if (race_ && vs::VehicleRace::state(w)) {
        if (!race_->started(w)) { const auto cars = vs::vehicleEntities(w); race_->start(w, seats, rules_, cars, sim.tick()); }
        pedals = race_->pedals(w, seats, sim.tick());
    }
    vs::stepVehicles(w, host, rules_, sim.tick(), sim.dt(), pedals, race_.get());
    if (race_) race_->after(w, seats, rules_, sim.tick());
}

// The world query of the sweep: the body is three circles along the car. Returns true when something was hit (the
// rest of the tick's motion is dropped); every pass goes to `impacts` (damage at once).
bool Driving::collide(Simulation& sim, Entity car, Vec3& pos, vs::VehicleImpacts& impacts) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const MapGrid& g = gs.grid;
    std::vector<DrivingDeck>& decks = decksOf(sim);
    vs::VehicleDrive& c = *vs::driveOf(w, car);
    const Unit& u = w.get<Unit>(car);
    const UnitType& t = m_->rules().types[u.type];
    const vs::VehicleType& vt = *typeOf(u.type);
    const Fixed mpc = rules_.metersPerCell;
    const Fixed r = vt.width / 2, half = fxMax(vt.length / 2 - r, Fixed{});
    const Vec3 f = forwardOf(c.controller.yaw);
    const Fixed offs[3] = {-half, Fixed{}, half};
    vs::VehicleSweep out;
    Fixed& crash = out.impact;

    // levels: any part of the body inside a mouth puts the car on that deck; the whole body past an end takes it off
    if (c.deck >= 0) {
        bool allOut = true;
        for (Fixed off : offs) if (!deckSpot(decks[size_t(c.deck)], pos + f * off).outside) allOut = false;
        if (allOut) c.deck = -1;
    }
    for (size_t i = 0; i < decks.size() && c.deck < 0; ++i) {
        const DrivingDeck& d = decks[i];
        if (!deckMayTouchBody(d, pos, half)) continue;
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
                const DrivingDeck& d = decks[size_t(c.deck)];
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
        crash = fxMax(crash, vl::vehicleResolveCollision(c.controller, c.controller.position, normal, rules_.restitution));
        c.controller.yawRate = c.controller.yawRate / 2;
        out.hit = true;
    }

    // other bodies
    struct Near { Entity e; Vec3 p; Fixed r; };
    std::vector<Near> near;
    gs.hash.query(pos, vt.length + Fixed::fromInt(2), [&](Entity o) {
        if (o == car || o == c.driver) return;
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
    for (auto& o : near) {
        vs::VehicleDrive* oc = vs::driveOf(w, o.e);
        if (levelsApart(decks, c.deck, pos, oc ? oc->deck : -1, o.p)) continue;   // one of them is on a bridge or in a tunnel
        const vs::VehicleType* ovt = oc ? typeOf(w.get<Unit>(o.e).type) : nullptr;
        const Fixed orad = ovt ? ovt->width / 2 : o.r;
        const Fixed ohalf = ovt ? fxMax(ovt->length / 2 - orad, Fixed{}) : Fixed{};
        const Vec3 of = oc ? forwardOf(oc->controller.yaw) : Vec3{};
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
            oc->controller.position = {oc->controller.position.x - shift.x * mpc, oc->controller.position.y, oc->controller.position.z - shift.z * mpc};
            Transform& otr = w.get<Transform>(o.e);
            otr.pos = {otr.pos.x - shift.x, otr.pos.y, otr.pos.z - shift.z};
            const Fixed vrel = (c.controller.velocity - oc->controller.velocity).dot(normal);
            if (vrel.raw < 0) {
                const Fixed j = -vrel * (kUnit + rules_.restitution) / 2;
                c.controller.velocity += normal * j;
                oc->controller.velocity = oc->controller.velocity - normal * j;
                crash = fxMax(crash, j);
                out.hits.push_back({vs::VehicleHit::Crash, o.e, j});
            }
            out.hit = true;
            continue;
        }
        const UnitType& ot = m_->rules().types[w.get<Unit>(o.e).type];
        if (ot.speed.raw <= 0) {   // things that never move (trees, lamp posts, hydrants) are solid
            pos += normal * deepest;
            crash = fxMax(crash, vl::vehicleResolveCollision(c.controller, c.controller.position, normal, rules_.restitution));
            out.hit = true;
            continue;
        }
        const Fixed closing = -c.controller.velocity.dot(normal);
        if (closing > rules_.runOverMinSpeed) {   // run over: the car barely notices
            out.hits.push_back({vs::VehicleHit::RunOver, o.e, closing});
            c.controller.velocity = c.controller.velocity * Fixed::fromRatio(98, 100);
            continue;
        }
        const Vec3 np = o.p - normal * deepest;
        if (g.passableAt(np, ot.pathLayer)) {   // slow: people step aside
            Transform& otr = w.get<Transform>(o.e);
            otr.pos = {np.x, otr.pos.y, np.z};
        } else {
            pos += normal * deepest;
            vl::vehicleResolveCollision(c.controller, c.controller.position, normal, Fixed{});
            out.hit = true;
        }
    }
    // static colliders (Colliders.h): solid like the bodies above that never move (bollards, benches, beds)
    if (!gs.colliders.empty() && c.deck < 0)
        gs.colliders.query(pos.xz(), half + r, kLayerGround, [&](size_t, const StaticColliders::Item& o) {
            Fixed deepest{};
            Vec3 normal{};
            for (Fixed off : offs) {
                const Vec3 dv = flat(pos + f * off - Vec3{o.p.x, Fixed{}, o.p.y});
                const Fixed dist = dv.length(), minD = r + o.r;
                if (dist >= minD) continue;
                if (minD - dist > deepest) { deepest = minD - dist; normal = dist.raw > 0 ? dv / dist : Vec3{} - f; }
            }
            if (deepest.raw <= 0) return;
            pos += normal * deepest;
            crash = fxMax(crash, vl::vehicleResolveCollision(c.controller, c.controller.position, normal, rules_.restitution));
            out.hit = true;
        });
    const bool hit = out.hit;
    impacts.apply(out);   // crash / run-over damage (driving.crash, driving.runOver; racers: race.crashDamage)
    return hit;
}

// ================================================================= races (engine/vehicles/VehicleRace.h)

void Driving::loadRace(Simulation& sim, const Json& rules, const Json& map) {
    race_.reset();
    if (!map.has("race")) return;
    if (!active()) throw std::runtime_error("map race: the cars are the vehicles of rules \"driving\"");
    race_ = vs::VehicleRace::read(sim.world(), rules, map, rules_.metersPerCell, sim.tickRate(), rules_);   // map cells -> metres
}

bool Driving::racing(Simulation& sim) const { return race_ && vs::VehicleRace::state(sim.world()); }

vl::RaceView Driving::raceView(Simulation& sim, uint8_t player) const {
    return race_ ? race_->view(sim.world(), player, sim.tick()) : vl::RaceView{};
}

} // namespace aiverse::game
