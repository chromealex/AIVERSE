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

// The living city (RtsTrafficCity.cpp): things that happen and the services that answer them.
enum TrafficIncidentKind : uint8_t { kIncidentInjured = 0, kIncidentCrime, kIncidentFire, kIncidentWreck, kIncidentKinds };

struct TrafficService {
    std::string id;
    uint8_t answers = kIncidentInjured;      // TrafficIncidentKind
    std::vector<TrafficWeighted> types;      // vehicles (driving vehicles)
    std::vector<uint16_t> stations;          // map object types it drives out of (sorted); none in range: from out of sight
    Fixed range = Fixed::fromInt(90);        // cells: how far a station may be from the incident
    uint16_t crew = UINT16_MAX;              // people who get out at the scene (medics, officers, firemen); none: the vehicle works
    int crewCount = 2;
    bool custody = false;                // police: officers detain an unarmed offender and take him to the station instead of fighting
    bool carry = false;                  // ambulance: the crew carries the patient to the vehicle and delivers him to a hospital
    uint32_t workTicks = 100, maxWorkTicks = 1200;
    int max = 1;                             // vehicles on call at a time
    Fixed speedMin = Fixed::fromInt(12), speedMax = Fixed::fromInt(15);
    bool siren = true;                       // drives as an emergency vehicle
    uint8_t owner = 0;
};

struct TrafficIncidentRules {
    uint16_t body = UINT16_MAX, wreck = UINT16_MAX, fire = UINT16_MAX;   // prop types left by a death / an explosion
    uint32_t bodyTicks = 1800, wreckTicks = 6000, burnTicks = 900, crimeTicks = 1200, timeoutTicks = 3000;
    Fixed fireDamage = Fixed::fromInt(6), fireRadius = Fixed::fromRatio(6, 5);   // per second, cells
    Fixed lethal = Fixed::fromInt(2);        // a single blow of this many times the max hp is final
    uint32_t treatTicks = 1200;              // in hospital this long, then the person walks out of it
    uint32_t abandonTicks = 0;               // a car a hero left on a driving lane this long is towed (0: never)
    Fixed witness = Fixed::fromInt(7);       // a crime is reported when somebody is this close to it
    std::vector<uint16_t> lots;              // map object types: towed cars are put next to the nearest one
};

// Population (RtsTrafficPopulation.cpp): everybody has a home and most have a job with a shift.
struct TrafficPlaceKind {
    std::string id;
    int lives = 0;                           // residents per place
    int jobs = 0;                            // workers per shift
    int children = 0;                        // a school / kindergarten: children it takes
    std::vector<std::pair<int, int>> shifts; // minutes of the day [from, to); to < from: over midnight
};
struct TrafficPopulationRules {
    int count = 0;                           // citizens (0: no population)
    uint32_t dayTicks = 24000;               // ticks of a game day
    int startMinute = 8 * 60;                // minute of the day at tick 0
    std::vector<TrafficPlaceKind> kinds;
    std::vector<std::pair<uint16_t, uint16_t>> objects;   // (object type, kind): map objects that are places, sorted
    std::vector<TrafficWeighted> types;      // who they are (default: pedestrians.types)
    Fixed walk = Fixed::fromRatio(2, 5);     // cells / s of those nobody watches
    int live = 60;                           // citizens on the street as real people at a time, on top of pedestrians.max
    // how they get to work (RtsTrafficCommute.cpp): by their own car, by bus, the rest on foot
    int carShare = 0, busShare = 0;          // percent of the citizens
    int parents = 0;                         // percent of the working citizens who take a child to school on the way
    std::vector<TrafficWeighted> carTypes;   // their cars (default: cars.types)
    Fixed drive = Fixed::fromRatio(5, 2), ride = Fixed::fromRatio(3, 2);   // cells / s unseen: driving, on the bus
    int parked = 16;                         // their cars standing at the kerb as real cars at a time (near the heroes)
    Fixed stopRange = Fixed::fromInt(30);    // cells: how far from the door a bus stop may be
};
enum TrafficCommute : uint8_t { kCommuteWalk = 0, kCommuteCar, kCommuteBus };
// The stretch of the way a travelling citizen is on: on foot to the door, to the bus stop, waiting at it (only as a
// real person), on the bus, in his car.
enum TrafficLeg : uint8_t { kLegWalk = 0, kLegToStop, kLegAtStop, kLegRide, kLegDrive };
enum TrafficCitizenState : uint8_t { kCitizenHome = 0, kCitizenToWork, kCitizenWork, kCitizenToHome, kCitizenHospital, kCitizenDead,
                                     kCitizenGang, kCitizenJail };   // out with a gang; locked up until `eta`
// Gangs (RtsTrafficGangs.cpp): citizens without work rob places that have loot.
struct TrafficGangRules {
    bool on = false;
    uint32_t everyMin = 0, everyMax = 0;     // ticks between attempts to get a gang together
    int sizeMin = 2, sizeMax = 4;
    std::vector<std::pair<uint16_t, int32_t>> targets;   // place kind -> loot
    uint32_t robTicks = 0, jailTicks = 0;
    int catchChance = 40;                    // percent: caught when nobody is around to see how it goes
    uint16_t crew = UINT16_MAX;              // the robbers' unit type (default: the citizens' own)
    uint8_t owner = 0;
};
enum TrafficGangState : uint8_t { kGangNone = 0, kGangUnseen, kGangDrive, kGangOut, kGangRob, kGangBack, kGangFlee };
struct TrafficGang {
    uint8_t state = kGangNone;
    int32_t leader = -1, target = -1, loot = 0;
    std::vector<int32_t> members;            // citizens, the ringleader first
    Entity car = kNullEntity;
    std::vector<Entity> crew;                // out of the car at the target
    uint32_t until = 0, call = 0;            // call: the crime report they caused
    uint8_t held = 0;                        // robbers somebody took hold of (disabled / loaded as cargo): left to them
};
struct TrafficCitizen {
    uint16_t type = UINT16_MAX;
    int32_t home = -1, work = -1;            // places
    uint8_t shift = 0;
    uint8_t state = kCitizenHome;
    Entity e = kNullEntity;                  // on the street as a real person, or the car he drives (near a hero)
    Vec2 from{};                             // travelling unseen: from here since `left`, at the end of the leg at `eta`
    uint32_t left = 0, eta = 0;              // kLegAtStop: eta is when he gives up waiting and walks
    uint8_t commute = kCommuteWalk;          // TrafficCommute
    uint8_t leg = kLegWalk;                  // TrafficLeg
    uint16_t carType = UINT16_MAX;
    Entity car = kNullEntity;                // his car at the kerb where he is, as a real car (near a hero)
    int32_t stopA = -1, stopB = -1;          // this trip's bus stops: gets on / gets off
    int16_t line = -1;                       // the bus line he takes (-1: any bus)
    int32_t school = -1;                     // the place his child goes to (-1: no child)
    uint8_t via = 0;                         // this trip goes by the school first, and it is still ahead
};

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
    // lines: a bus keeps to its line (a loop through the `via` points by the shortest roads) and calls at its stops
    struct LineDef { std::string id; std::vector<Vec2> via; };
    std::vector<LineDef> lineDefs;           // "lines": [{"id", "via": [[x, z], ...]}]
    int autoLines = 4;                       // "lines": N: that many lines laid out over the road network (0: buses roam)
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
    TrafficPopulationRules population;
    TrafficGangRules gangs;
    std::vector<TrafficService> services;
    TrafficIncidentRules incidents;
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
    int16_t line = -1;                       // bus: its line (-1: roams)
    uint16_t lineLeg = 0;                    // bus: the `via` point of the line it is heading for
    // on call (RtsTrafficCity.cpp)
    int8_t service = -1;                     // index into rules.services
    uint8_t task = 0;                        // TrafficTask
    uint32_t call = 0;                       // incident id
    uint32_t taskUntil = 0;
    int32_t destPiece = -1, homePiece = -1;  // where it is going / the station it came out of
    Fixed destAlong{}, homeAlong{};
    int32_t citizen = -1;                    // kTaskTrip: the citizen who drives it to his address
    std::vector<Entity> crew;                // out of the vehicle at the scene
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

enum TrafficTask : uint8_t { kTaskNone = 0, kTaskGoing, kTaskCrewOut, kTaskWorking, kTaskCrewBack, kTaskHome,
                             kTaskTrip,     // a citizen's drive to work / home (RtsTrafficCommute.cpp)
                             kTaskGang };   // a gang's car: to the target, then away (RtsTrafficGangs.cpp)

struct TrafficIncident {
    uint32_t id = 0;
    uint8_t kind = kIncidentInjured;
    uint8_t state = 0;                       // 0 open, 1 a vehicle is on its way, 2 being worked on
    Vec2 pos{};
    Entity subject = kNullEntity;            // the body / fire / wreck or abandoned car
    Entity offender = kNullEntity;           // crime: who did it
    uint32_t born = 0, assigned = 0;
    Entity responder = kNullEntity;
    uint16_t victim = UINT16_MAX;            // injured: the type of the person (he comes back from the hospital)
    int32_t citizen = -1;                    // injured: who it is
    bool fatal = false;                      // injured: the blow was too much (taken to the hospital, never comes back)
};

struct TrafficPatient { uint16_t type = UINT16_MAX; Vec2 at{}; uint32_t release = 0; int32_t citizen = -1; };   // in hospital until `release`

// What the traffic remembers of a tick (kEvAttack / kEvHit / kEvDied) until it runs again: the simulation forgets
// its events at the start of the next step, before the movement system.
struct TrafficEvent { uint16_t type = 0; uint16_t unitType = 0; Vec3 pos{}; Entity a = kNullEntity, b = kNullEntity; Fixed blow{}; };   // blow: the hardest hit the dead unit took in its last ticks
struct TrafficBlow { Entity e = kNullEntity; Fixed damage{}; uint32_t tick = 0; };

struct TrafficUsedCar { Entity e = kNullEntity; uint32_t since = 0; };   // a car a hero drove; left alone since (0: in use)

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
    int32_t citizen = -1;                    // a citizen on his way (RtsTrafficPopulation.cpp), -1: just a passer-by
    int32_t goal = -1;                       // the cell he is going to
};

struct TrafficLightProp { Entity e = kNullEntity; int32_t junction = -1; uint8_t axis = 0; uint8_t shown = 255; };

struct TrafficStore {
    std::vector<TrafficCar> cars;            // ascending entity index
    std::vector<TrafficPerson> people;       // ascending entity index
    std::vector<TrafficLightProp> lights;
    std::vector<TrafficCitizen> citizens;    // the population; the index is the citizen's id
    TrafficGang gang;                        // the one that is out now
    std::vector<Entity> exempt;              // people of pedestrian types the crowd must leave alone (robbers), by index
    uint32_t nextGang = 0;
    int64_t gangLoot = 0;                    // taken home so far
    uint32_t gangRobberies = 0, gangCaught = 0;
    uint32_t citizenCursor = 0;              // the next one to look at (a slice per tick)
    std::vector<TrafficIncident> incidents;  // in the order they happened
    uint32_t nextIncident = 1;
    std::vector<TrafficUsedCar> used;        // ascending entity index
    std::vector<TrafficEvent> happened;      // last tick's events (collect), consumed by tick
    std::vector<TrafficBlow> blows;          // recent hits on people (was the last one fatal?), kept three ticks
    std::vector<TrafficPatient> patients;    // in the order they were brought in
    uint32_t towed = 0;                      // cars put on the lots so far (picks the next spot)
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
    void collect(Simulation& sim);           // after the death system: remembers the tick's events
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    TrafficStore* store(Simulation& sim) const;
    bool aiInput(Simulation& sim, Entity vehicle, lib::genre::VehicleInput& out) const;   // RtsDriving.cpp
    // signal state of `axis` at junction j (Green when the junction has no signal)
    lib::genre::TrafficLight light(Simulation& sim, int junction, lib::genre::TrafficAxis axis) const;
    int junctionAt(Fixed x, Fixed z) const;  // junction of the cell, or the nearest one within 3 cells, -1: none
    bool walkAllowed(Simulation& sim, int strip) const;
    // something happened (scripts: game.traffic_report): the services answer it. Returns the incident id, 0: ignored
    // `witnessed`: somebody certainly knows (a crime nobody is near is not reported otherwise)
    uint32_t report(Simulation& sim, uint8_t kind, Vec2 pos, Entity subject = kNullEntity, Entity offender = kNullEntity, bool witnessed = false);
    static void parseGangs(const Json& j, TrafficRules& r, const std::vector<std::string>& ids, uint32_t tickRate);
    static void parsePopulation(const Json& j, TrafficRules& r, const std::vector<std::string>& typeIds, uint32_t tickRate);
    struct Place {
        uint16_t kind = 0; Vec2 pos{}; int32_t cell = -1; Entity object = kNullEntity;
        int32_t stop = -1;                   // the nearest bus stop (within population.stopRange)
        int32_t piece = -1; int8_t dir = 1; Fixed along{};   // the kerb his car stands at: road piece, side, where
    };
    const std::vector<Place>& places() const { return places_; }
    size_t lineCount() const { return lines_.size(); }
    const std::string& lineId(int line) const { static const std::string none; return line >= 0 && size_t(line) < lines_.size() ? lines_[size_t(line)].id : none; }
    const std::vector<int32_t>& lineStops(int line) const { return lines_[size_t(line)].stops; }
    int minuteOfDay(Simulation& sim) const;  // 0..1439
    const TrafficCitizen* citizenOf(Simulation& sim, Entity person) const;   // a person on the street or the car he drives
    int present(Simulation& sim, int place) const;   // workers at work in the place now (a shop is open when > 0)
    static void parseCity(const Json& j, TrafficRules& r, const std::vector<std::string>& typeIds, uint32_t tickRate);
    int kindOf(Simulation& sim, Entity vehicle) const;   // TrafficKind of a traffic car, -1: not one
    bool onRoad(Vec2 p) const;               // a road cell (piece or junction)
    uint32_t offset(int junction) const { return junction >= 0 && size_t(junction) < offset_.size() ? offset_[size_t(junction)] : 0; }
    int stripAt(int cell) const { return cell >= 0 && size_t(cell) < stripAt_.size() ? stripAt_[size_t(cell)] : -1; }

// Project-owned jobs reuse the authoritative lane planner and citizen scheduler.
     bool projectRoute(Simulation& sim, TrafficCar& c, Vec2 goal, Vec2 pos, Vec2 fwd) {
         if (!roadSpot(goal, 10, c.destPiece, c.destAlong)) return false;
         c.task=250; return reroute(sim,c,pos,fwd);
     }
     bool projectSidewalk(Vec2 p) const {
         int x=int(p.x.floorInt()),z=int(p.y.floorInt());
         return x>=0&&z>=0&&x<net_.w&&z<net_.h&&side_[size_t(z*net_.w+x)];
     }
     bool projectOnShift(const TrafficCitizen& c, int minute) const { return onShift(c,minute,0); }
     bool projectReturnCar(Simulation& sim, TrafficStore& st, int id, Entity car);
    void projectHome(Simulation& sim, TrafficStore& st, int id) { depart(sim,st,id,kCitizenToHome); }
private:
    struct Strip { int32_t piece = -1, junction = -1; uint8_t axis = 0; };
    struct BusStop { int32_t piece = -1; int8_t dir = 1; Fixed along{}; Vec2 kerb{}; };
    struct Siren { Vec2 pos{}, fwd{}; };
    struct Station { int32_t piece = -1; int8_t dir = 1; Fixed along{}; Vec2 pos{}; };
    struct Ring { bool on = false; Fixed x0{}, z0{}, x1{}, z1{}; };   // lane round the island of a junction
    void focus(Simulation& sim, std::vector<Vec2>& out) const;
    void syncLights(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    void placeLightsAt(Simulation& sim, TrafficStore& st, int junction);
    void updateLights(Simulation& sim, TrafficStore& st);
    void findStops(Simulation& sim);
    // the living city (RtsTrafficCity.cpp)
    void setupCity(Simulation& sim);
    void incidents(Simulation& sim, TrafficStore& st);
    void responders(Simulation& sim, TrafficStore& st);
    bool dispatch(Simulation& sim, TrafficStore& st, TrafficIncident& inc, int service);
    void finish(Simulation& sim, TrafficStore& st, TrafficCar& c, bool done);
    void resolve(Simulation& sim, TrafficStore& st, TrafficIncident& inc);
    void dropCall(Simulation& sim, TrafficStore& st, TrafficCar& c);
    bool roadSpot(Vec2 p, int radius, int32_t& piece, Fixed& along) const;
    int exitToward(int junction, int inSide, int32_t fromPiece, int32_t destPiece) const;
    bool isCasualtyType(uint16_t t) const;
    bool stripAtEnd(const lib::genre::TrafficPiece& p, int end) const;   // a crossing strip on the last row before the junction
    void passengers(Simulation& sim, TrafficStore& st, const BusStop& stop, const TrafficCar& bus);
    // bus lines (RtsTrafficCommute.cpp)
    struct BusLine {
        std::string id;
        std::vector<int32_t> via;            // road pieces the loop goes through, in order
        struct Step { int32_t piece; int8_t dir; uint16_t leg; };
        std::vector<Step> path;              // the loop: lane by lane, with the via point headed for
        std::vector<int32_t> stops;          // its stops, in the order of the loop
    };
    void setupLines();
    void gangs(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    void gangEnd(Simulation& sim, TrafficStore& st, uint8_t state, bool paid);
    void busGone(Simulation& sim, TrafficStore& st, Entity bus);
    // cars
    void cars(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    bool spawnCar(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist, uint8_t kind, int service = -1, Entity* out = nullptr);
    void extendRoute(Simulation& sim, TrafficCar& c);
    bool reroute(Simulation& sim, TrafficCar& c, Vec2 pos, Vec2 fwd);
    void plan(Simulation& sim, TrafficStore& st, TrafficCar& c);
    // people
    void people(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    bool spawnPerson(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci, Fixed minDist);
    void adoptPeople(Simulation& sim, TrafficStore& st);
    bool newWalk(Simulation& sim, TrafficPerson& p, const Vec2* threat, int goalCell = -1);
    void setupPopulation(Simulation& sim, const Json& map);
    void populate(Simulation& sim, TrafficStore& st);
    void population(Simulation& sim, TrafficStore& st, const std::vector<Vec2>& foci);
    bool onShift(const TrafficCitizen& c, int minute, int lead) const;
    // getting there (RtsTrafficCommute.cpp)
    void setupCommute();
    void travel(Simulation& sim, TrafficCitizen& c, uint8_t leg, Vec2 from) const;   // unseen from here on this leg
    Vec2 legTarget(const TrafficCitizen& c) const;
    uint32_t tripTicks(const TrafficCitizen& c, int from, int to, uint32_t rate) const;
    void depart(Simulation& sim, TrafficStore& st, int32_t id, uint8_t state);
    void arrive(TrafficCitizen& c) const;
    int destOf(const TrafficCitizen& c) const { return c.via && c.school >= 0 ? c.school : c.state == kCitizenToWork ? c.work : c.home; }   // where this stretch ends
    bool citizenStep(Simulation& sim, TrafficStore& st, TrafficPerson& p, Vec2 pos);   // true: a walk was planned
    void citizenUnseen(Simulation& sim, TrafficStore& st, TrafficPerson& p, Vec2 pos); // his body leaves the street
    Entity citizenPerson(Simulation& sim, TrafficStore& st, int32_t id, Vec3 spot, int goalCell);
    bool walkTo(Simulation& sim, TrafficPerson& p, int goalCell);
    int sidewalkNear(Vec2 p, int radius, bool mainFirst = false) const;
    bool kerbFree(Simulation& sim, int32_t piece, int dir, Fixed along, Entity self) const;
    bool kerbSpot(Simulation& sim, const Place& pl, int dir, Fixed& along, Entity self, const Fixed* from, Fixed ahead) const;
    void keepCar(Simulation& sim, TrafficStore& st, TrafficCitizen& c, const std::vector<Vec2>& foci, int& parked);
    bool startTrip(Simulation& sim, TrafficStore& st, int32_t id, Entity car);          // out of the parking lane
    bool spawnTrip(Simulation& sim, TrafficStore& st, int32_t id, Vec2 at);             // into sight, already driving
    void trips(Simulation& sim, TrafficStore& st);
    void tripGone(Simulation& sim, TrafficStore& st, TrafficCar& c);                    // the car leaves the street
    Entity tripDriverOut(Simulation& sim, TrafficStore& st, TrafficCar& c, Vec3 spot, bool passable);
    Fixed tripShift(Simulation& sim, const TrafficCar& c, Vec2 pos, Vec2 fwd, Vec2 target) const;
    bool busCitizen(Simulation& sim, TrafficStore& st, int stop, TrafficPerson* p, const TrafficCar& bus);
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
    std::vector<Place> places_;              // homes and workplaces (map objects and the map's "places")
    std::vector<std::vector<Station>> stations_;   // per service
    std::vector<Vec2> lots_;
    std::vector<Ring> ring_;                 // per junction: the way round its island (roundabouts, monuments)
    struct Occupied { int32_t junction; uint16_t count; bool longOne; };
    std::vector<Occupied> occupied_;         // island junctions with traffic inside this tick, ascending (derived)
    std::vector<Siren> sirens_;              // emergency vehicles this tick (derived)
    Fixed mpc_ = Fixed::fromInt(4);          // metres per cell (rules driving)
    int32_t mainIsland_ = -1;                // the biggest one: doors and bus stops open onto it when they can
    std::vector<int32_t> island_;            // per cell: which connected part of the pedestrian graph it is in (-1: none)
    std::vector<BusLine> lines_;
    std::vector<std::pair<int32_t, int32_t>> gangTargets_;   // place, loot
    std::vector<int32_t> stopCell_;          // per bus stop: the sidewalk cell people wait on (-1: none)
    mutable lib::genre::TrafficWalkSearch search_;   // work arrays of the long walks (derived)
};

} // namespace aiverse::game
