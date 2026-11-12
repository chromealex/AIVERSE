// City traffic (rules "traffic", docs/traffic.md): computer-driven cars on lanes, signalised junctions with traffic
// light props, and pedestrians on the sidewalks who cross at the junctions on "walk". Any game with the top-down
// hero and rules "driving" can switch it on with data only.
//
// The road network is built at setup from the map's tile characters (tiles.rows): rules traffic.roads.tiles lists
// the road characters, traffic.pedestrians.walk the sidewalk ones (libraries/include/aiverselib/genre/Traffic.h:
// pieces, junctions, lanes). Cars are ordinary "driving" vehicles: this file is their driver. Each tick it plans
// pedals and steering (pure pursuit along the lane / the turn through the junction, stop lines on yellow and red,
// following distance to whatever is ahead: cars, people, the hero, parked cars), and RtsDriving.cpp feeds that input
// into the same vehicle controller the hero drives with; so traffic cars crash, push and get stolen like any other.
// Cars and people appear out of sight around the heroes (between spawnMin and spawnMax cells) and are removed beyond
// `despawn`, up to `maxCars` / `maxPeople`. Pedestrians walk the sidewalk graph (breadth-first paths), wait at the
// crossing strips next to a signalised junction until their walk window, and panic (run away from the threat, signals
// ignored) when shots, hits or deaths happen near them or a fast car comes at them.
//
// State lives in the TrafficStore world resource: hashed, saved after the vehicles, copied by rollback snapshots.
// Games without the block keep their checksums and save layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"
#include "../../libraries/include/aiverselib/genre/Traffic.h"

namespace aiverse { class Simulation; class Hasher; class Archive; }

namespace aiverse::game {

class RtsModule;

struct TrafficWeighted { uint16_t type = UINT16_MAX; int weight = 1; };

struct TrafficRules {
    bool enabled = false;
    std::string roadTiles;                   // map tile characters that are road
    lib::genre::TrafficNetConfig net;
    // cars
    std::vector<TrafficWeighted> cars;       // vehicle types (must be "driving" vehicles), by weight
    int maxCars = 16;
    uint8_t carOwner = 0;
    Fixed spawnMin = Fixed::fromInt(14), spawnMax = Fixed::fromInt(26), despawn = Fixed::fromInt(34);   // cells from a hero
    Fixed speedMin = Fixed::fromInt(8), speedMax = Fixed::fromInt(12);   // m/s cruise
    Fixed turnSpeed = Fixed::fromInt(5);                                 // m/s through turns
    Fixed decel = Fixed::fromInt(5);                                     // m/s^2 planned braking
    Fixed gap = Fixed::fromRatio(1, 2);                                  // cells kept to the obstacle ahead
    int straight = 3, turn = 2;                                          // junction choice weights
    lib::genre::TrafficDriverParams driver;
    // buses: drive the outer lane and call at the stops (map objects of the stop types next to a road)
    std::vector<TrafficWeighted> buses;
    int maxBuses = 0;
    std::vector<uint16_t> stopTypes;         // sorted
    int autoStops = 0;                       // N > 0: a stop on both sides in the middle of every Nth long road piece
    Fixed busMin = Fixed::fromInt(6), busMax = Fixed::fromInt(8);        // m/s
    Fixed dwellMin = Fixed::fromInt(3), dwellMax = Fixed::fromInt(6);    // seconds at a stop
    int passengers = 2;                      // at most this many people get on / off at a stop
    // emergency vehicles: fast, through red lights, the other traffic pulls over
    std::vector<TrafficWeighted> emergency;
    int maxEmergency = 0;
    Fixed emMin = Fixed::fromInt(13), emMax = Fixed::fromInt(16);        // m/s
    Fixed emIntervalMin = Fixed::fromInt(20), emIntervalMax = Fixed::fromInt(60);   // seconds between call-outs
    Fixed yieldRadius = Fixed::fromInt(7);   // cells: traffic this close ahead of / beside it pulls over
    // signals
    bool signals = true;
    lib::genre::TrafficSignalTiming timing;  // ticks (rules in seconds)
    Fixed greenS = Fixed::fromInt(9), yellowS = Fixed::fromInt(3), allRedS = Fixed::fromInt(1), walkS = Fixed::fromInt(6);
    int offsetMode = 0;                      // 0 random (per junction), 1 wave (green wave along x + z), 2 none
    uint16_t light[3] = {UINT16_MAX, UINT16_MAX, UINT16_MAX};   // prop types shown for green / yellow / red
    bool placeLights = true;
    Fixed lightRadius{};                     // cells from a hero within which light props exist (0: despawn + 4)
    // pedestrians
    std::string walkTiles;                   // sidewalk tile characters
    std::vector<TrafficWeighted> people;     // pedestrian types
    int maxPeople = 24;
    uint8_t peopleOwner = 0;
    Fixed peopleMin = Fixed::fromInt(12), peopleMax = Fixed::fromInt(22), peopleDespawn = Fixed::fromInt(30);
    int routeMin = 6, routeMax = 30;         // cells of a walk
    Fixed pauseMin = Fixed::fromInt(1), pauseMax = Fixed::fromInt(6);    // seconds between walks
    Fixed side = Fixed::fromRatio(3, 10);    // cells off the sidewalk centre line (each person keeps one)
    Fixed panicRadius = Fixed::fromInt(6), panicSeconds = Fixed::fromInt(8), panicSpeed = Fixed::fromRatio(9, 5);
    uint16_t driverPerson = UINT16_MAX;      // pedestrian thrown out of a car the hero takes (none: nobody)
};

struct TrafficWaypoint {
    Vec2 p{};
    int32_t junction = -1;                   // stop line of this junction (the car enters it here)
    uint8_t axis = 0;                        // TrafficAxis of the approach
    uint8_t turn = 0;                        // a point of a turn through a junction
};

enum TrafficKind : uint8_t { kTrafficCar = 0, kTrafficBus = 1, kTrafficEmergency = 2 };

struct TrafficCar {
    Entity e = kNullEntity;
    uint8_t kind = kTrafficCar;
    int32_t lastStop = -1;                   // bus: the stop it called at last
    uint32_t dwellUntil = 0;                 // bus: standing at a stop until this tick
    std::vector<TrafficWaypoint> route;      // route[wp] is the next point; route[wp - 1] the last one passed
    uint16_t wp = 0;
    int32_t tailPiece = -1;                  // lane of the route's last point (where the route continues)
    int8_t tailDir = 1;
    uint8_t tailLane = 0;
    Fixed cruise{};                          // m/s
    uint32_t stopped = 0;                    // ticks at a standstill while not held by a signal
    Entity blocker = kNullEntity;            // what it is waiting behind
    Entity ignore = kNullEntity;             // obstacle it drives past (deadlock break / detour) until ignoreUntil;
                                             // its own entity: everything (an emergency vehicle pushing through)
    uint32_t reverseUntil = 0;               // backing off something it is wedged against
    uint32_t ignoreUntil = 0;
    uint32_t lostSince = 0;                  // off every road since this tick (0: on the road)
    lib::genre::VehicleInput input;          // planned this tick (derived: recomputed before the vehicle steps)
};

enum class PedState : uint8_t { Pause, Walk, Wait };

struct TrafficPerson {
    Entity e = kNullEntity;
    PedState state = PedState::Pause;
    std::vector<int32_t> path;               // cells, the walk
    uint16_t idx = 0, legEnd = 0;            // path[idx..legEnd) is the leg being walked
    std::vector<Vec3> leg;                   // its points, handed to the movement system one at a time
    uint16_t legPt = 0;
    uint32_t until = 0;                      // end of a pause / of the panic
    uint32_t moved = 0;                      // last tick it got closer to its leg end
    Fixed side{};
    Fixed hp{};
    Vec3 lastPos{};
    bool panic = false;
};

struct TrafficLightProp { Entity e = kNullEntity; int32_t junction = -1; uint8_t axis = 0; uint8_t shown = 255; };

struct TrafficStore {
    std::vector<TrafficCar> cars;            // ascending entity index
    std::vector<TrafficPerson> people;       // ascending entity index
    std::vector<TrafficLightProp> lights;
    std::vector<int32_t> lit;                // junctions whose light props exist (near the heroes), ascending
    bool placed = false;                     // the first fill done
    uint32_t nextAdopt = 0;
    uint32_t nextEmergency = 0;              // tick of the next emergency call-out (0: not drawn yet)
};

class Traffic {
public:
    bool active() const { return rules_.enabled; }
    const TrafficRules& rules() const { return rules_; }
    const lib::genre::TrafficNet& net() const { return net_; }
    static TrafficRules parse(const Json& j, const std::vector<std::string>& typeIds, uint32_t tickRate);   // throws
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map);
    void tick(Simulation& sim);              // before the vehicles step (inside the movement system)
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    TrafficStore* store(Simulation& sim) const;
    bool aiInput(Simulation& sim, Entity vehicle, lib::genre::VehicleInput& out) const;   // RtsDriving.cpp
    // signal state of `axis` at junction j (Green when the junction has no signal)
    lib::genre::TrafficLight light(Simulation& sim, int junction, lib::genre::TrafficAxis axis) const;
    int junctionAt(Fixed x, Fixed z) const;  // junction of the cell, or the nearest one within 3 cells, -1: none
    bool walkAllowed(Simulation& sim, int strip) const;
    int kindOf(Simulation& sim, Entity vehicle) const;   // TrafficKind of a traffic car, -1: not one
    bool onRoad(Vec2 p) const;               // a road cell (piece or junction)
    uint32_t offset(int junction) const { return junction >= 0 && size_t(junction) < offset_.size() ? offset_[size_t(junction)] : 0; }
    int stripAt(int cell) const { return cell >= 0 && size_t(cell) < stripAt_.size() ? stripAt_[size_t(cell)] : -1; }

private:
    struct Strip { int32_t piece = -1, junction = -1; uint8_t axis = 0; };
    struct BusStop { int32_t piece = -1; int8_t dir = 1; Fixed along{}; Vec2 kerb{}; };
    struct Siren { Vec2 pos{}, fwd{}; };
    struct Ring { bool on = false; Fixed x0{}, z0{}, x1{}, z1{}; };   // lane round the island of a junction
    void focus(Simulation& sim, std::vector<Vec2>& out) const;
    void syncLights(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    void placeLightsAt(Simulation& sim, TrafficStore& st, int junction);
    void updateLights(Simulation& sim, TrafficStore& st);
    void findStops(Simulation& sim);
    bool stripAtEnd(const lib::genre::TrafficPiece& p, int end) const;   // a crossing strip on the last row before the junction
    void passengers(Simulation& sim, TrafficStore& st, const BusStop& stop);
    // cars
    void cars(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    bool spawnCar(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist, uint8_t kind);
    void extendRoute(Simulation& sim, TrafficCar& c);
    bool reroute(Simulation& sim, TrafficCar& c, Vec2 pos, Vec2 fwd);
    void plan(Simulation& sim, TrafficStore& st, TrafficCar& c);
    // people
    void people(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    bool spawnPerson(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist);
    void adoptPeople(Simulation& sim, TrafficStore& st);
    bool newWalk(Simulation& sim, TrafficPerson& p, const Vec2* threat);
    void startLeg(Simulation& sim, TrafficPerson& p);
    void nextPoint(Simulation& sim, TrafficPerson& p);
    void panic(Simulation& sim, TrafficPerson& p, Vec2 threat);
    bool isPersonType(uint16_t t) const;
    uint16_t pick(Simulation& sim, const std::vector<TrafficWeighted>& list) const;

    RtsModule* m_ = nullptr;
    TrafficRules rules_;
    lib::genre::TrafficNet net_;
    std::vector<uint32_t> offset_;           // per junction: signal phase offset (ticks)
    std::vector<uint8_t> walk_;              // per cell: sidewalk or crossing (pedestrian graph)
    std::vector<int32_t> stripAt_;           // per cell: crossing strip or -1
    std::vector<Strip> strips_;
    std::vector<uint8_t> side_;              // per cell: sidewalk
    std::vector<BusStop> stops_;             // sorted by (piece, along)
    std::vector<std::vector<int32_t>> jbucket_;   // signalised junctions by 16-cell bucket (lights near the heroes)
    int bw_ = 0, bh_ = 0;
    std::vector<Ring> ring_;                 // per junction: the way round its island (roundabouts, monuments)
    struct Occupied { int32_t junction; uint16_t count; bool longOne; };
    std::vector<Occupied> occupied_;         // island junctions with traffic inside this tick, ascending (derived)
    std::vector<Siren> sirens_;              // emergency vehicles this tick (derived)
    Fixed mpc_ = Fixed::fromInt(4);          // metres per cell (rules driving)
};

} // namespace aiverse::game
