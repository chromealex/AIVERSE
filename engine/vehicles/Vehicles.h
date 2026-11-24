// One vehicle system for every host (AGENTS.md "NO GENRE SPLIT", docs/driving.md, docs/no-genre-split-audit.md
// stage B). A vehicle is an entity with the VehicleDrive component: the vehicle-controllers state
// (engine/mechanics/VehicleControllers.h), who sits at the wheel, its autopilot pedals, lock,
// door timer, level (deck) and its seat list. Whoever drives carries VehicleOccupant (the car it sits in and the
// car's last pose, so the driver can be put out when the car disappears).
//
// The host (a grid map with units in cells, a metre map with a triangle world, ...) implements VehicleHost: the
// controls of whoever is at the wheel (through the shared mappings of mechanics/VehicleDrive.h), the ground under a
// vehicle, the body sweep against its static world and other bodies, damage, and syncing its own pose / occupants.
// stepVehicles() is the one per-tick system: pedals (race > driver > traffic bot > autopilot > parked), sleep of
// resting parked vehicles, controller step, sweep, crash / run-over damage. Fixed maths, ascending entity index.
//
// Rules: the `driving` block (DriveRules, parseDriving) and the typed vehicle definitions of `vehicleTypes`
// become the same VehicleType list. Races: engine/vehicles/VehicleRace.h (one adapter over the race session).
#pragma once
#include "engine/core/ECS.h"
#include "engine/core/Fixed.h"
#include "engine/data/Json.h"
#include "engine/mechanics/VehicleDrive.h"
#include "engine/mechanics/VehicleSeats.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aiverse { class Hasher; class Archive; }

namespace aiverse::vehicles {
namespace lm = lib::mechanics;
class VehicleRace;

// Screen-stick mapping of a top-down camera (mechanics/VehicleDrive.h): "tank" or "stick" (steer toward the stick).
enum class DriveControls : uint8_t { Tank, Stick };

// One kind of vehicle. type: the host's id (a unit type index, a `vehicleTypes` id). length / width in host units
// (cells on a grid map, metres on a metre map). seats: an optional seat list (passengers, gunners); without one the
// vehicle has the driver's door only (enter range, exit speed, door side: DriveRules).
struct VehicleType {
    int64_t type = 0;
    Fixed length = Fixed::fromInt(1), width = Fixed::fromRatio(1, 2);
    lm::VehicleControllerDef def;
    lm::VehicleSeatsDef seats;
};

// rules "driving" (docs/driving.md); a host without the block fills the fields it needs (metersPerCell 1 on a metre map).
struct DriveRules {
    bool enabled = false;
    Fixed metersPerCell = Fixed::fromInt(4);       // host units -> metres (controller state is in metres)
    Fixed enterRange = Fixed::fromInt(1);          // host units between the driver and the car body
    Fixed exitSpeed = Fixed::fromInt(3);           // m/s: faster than this the doors stay shut
    DriveControls controls = DriveControls::Tank;
    Fixed crashMinSpeed = Fixed::fromInt(5), crashDamage = Fixed::fromInt(4), restitution = Fixed::fromRatio(2, 10);
    Fixed runOverMinSpeed = Fixed::fromInt(4), runOverDamage = Fixed::fromInt(15);
    Fixed wreckDamage{};                           // damage to the driver when the car is destroyed under him
    uint32_t doorTicks = 0;                        // how long a door stays open after getting in / out (view)
    Fixed doorSeconds = Fixed::fromRatio(1, 2);
    std::vector<VehicleType> vehicles;             // sorted by type
    const VehicleType* type(int64_t t) const;      // binary search; null: not a vehicle
};
// The `driving` rules block; typeIds: the host's type names (vehicles are keyed by them). Throws readable errors.
DriveRules parseDriving(const Json& j, const std::vector<std::string>& typeIds);

// ---------------------------------------------------------------- components
struct VehicleDrive {
    int64_t type = 0;                              // VehicleType::type
    lm::VehicleState controller;                   // metres, library yaw (0 = +z, a quarter turn = +x)
    Entity driver = kNullEntity;                   // at the wheel (kNullEntity: nobody)
    uint8_t player = 0;                            // the driver's player slot (0: nobody / a bot)
    uint32_t doorUntil = 0;                        // the driver's door is open until this tick (view)
    bool locked = false;                           // nobody gets in until it is unlocked (setLocked)
    int16_t deck = -1;                             // level: index of the host's deck (bridge / tunnel), -1 on the ground
    bool autopilot = false;                        // pedals come from `pilot` (traffic, scripts, race bots) while nobody drives
    lm::VehicleInput pilot;
    lm::VehicleSeatState seats;                    // seat list occupants (types with VehicleType::seats)
    std::vector<lm::VehicleSeatRequest> requests;  // enter / exit requests of this tick (transient, never saved)
};
// On the driver: the car and its last pose (metres, yaw, forward speed m/s, type, deck), refreshed every step.
struct VehicleOccupant {
    Entity vehicle = kNullEntity;
    int64_t type = 0;
    Vec3 position{};
    Fixed yaw{}, speed{};
    int16_t deck = -1;
};

// ---------------------------------------------------------------- queries and the autopilot / lock API
// Vehicles in ascending entity index (the deterministic order of every loop here).
std::vector<Entity> vehicleEntities(const World& w);
VehicleDrive* driveOf(World& w, Entity vehicle);
const VehicleDrive* driveOf(const World& w, Entity vehicle);
Entity vehicleOf(const World& w, Entity driver);   // the vehicle this entity drives, or kNullEntity
Fixed speedOf(const World& w, Entity vehicle);     // forward speed, m/s (0: not a vehicle)
// Computer drivers (traffic, scripts): the pedals of a vehicle nobody drives. Simulation code only, every tick or on
// change: it is state, hashed and saved. A driver who gets in takes over; when he leaves the autopilot applies again.
bool setAutopilot(World& w, Entity vehicle, const lm::VehicleInput& input);   // false: not a vehicle / input out of range
bool clearAutopilot(World& w, Entity vehicle);                                // back to parked
bool setLocked(World& w, Entity vehicle, bool locked);
bool isLocked(const World& w, Entity vehicle);
// A new vehicle at `position` (metres) facing `yaw`.
VehicleDrive& addVehicle(World& w, Entity e, const VehicleType& type, Vec3 position, Fixed yaw);
// Put / take the driver (component bookkeeping only; the host moves its body and flags).
void seatDriver(World& w, Entity vehicle, VehicleDrive& d, Entity driver, uint8_t player, uint32_t doorUntil);
void unseatDriver(World& w, VehicleDrive* d, Entity driver, uint32_t doorUntil);
// The shared screen-stick mapping with the rules' controls.
lm::VehicleInput stickPedals(const DriveRules& r, const lm::DriveScreenStick& s, const lm::VehicleState& car);

// ---------------------------------------------------------------- the host
struct VehicleHit {
    enum Kind : uint8_t { Crash, RunOver } kind = Crash;   // Crash: another vehicle (closing impulse), RunOver: a pedestrian
    Entity victim = kNullEntity;
    Fixed speed{};                                         // m/s
};
struct VehicleSweep {
    bool hit = false;                                      // the body was stopped (the rest of the motion is dropped)
    Fixed impact{};                                        // hardest impact against the world (m/s): crash damage to itself
    std::vector<VehicleHit> hits;                          // bodies it hurt, in the order met
};
// stepVehicles hands this to VehicleHost::sweep: apply() turns one collision pass into damage at once (a pedestrian
// run over in an early sub-step is not hit again by a later one).
class VehicleImpacts {
public:
    virtual ~VehicleImpacts() = default;
    virtual void apply(const VehicleSweep& s) = 0;
};
class VehicleHost {
public:
    virtual ~VehicleHost() = default;
    virtual bool active(Entity, const VehicleDrive&) { return true; }   // steps this tick (alive, not carried)
    // Someone at the wheel: true + their pedals (shared mappings). false: the vehicle is parked this tick.
    virtual bool driverPedals(Entity, const VehicleDrive&, lm::VehicleInput&) = 0;
    // Nobody at the wheel: a host bot (traffic) drives. Called once more for a resting vehicle (does it wake up?).
    virtual bool botPedals(Entity, const VehicleDrive&, lm::VehicleInput&) { return false; }
    virtual lm::VehicleInput parked(Entity, const VehicleDrive&) { lm::VehicleInput in; in.handbrake = true; return in; }
    // The race's pedals reach this vehicle (default: always).
    virtual bool controllable(Entity, const VehicleDrive&) { return true; }
    virtual lm::VehicleContacts ground(Entity, const VehicleDrive&) { return {}; }
    // Moves the body from `step.from` to the stepped controller position against the host's static world and other
    // bodies, resolving contacts on the controller (vehicleResolveCollision); every collision pass goes to `impacts`.
    virtual void sweep(Entity, VehicleDrive&, const lm::VehicleStepResult& step, VehicleImpacts& impacts) = 0;
    virtual void placed(Entity, VehicleDrive&) = 0;                      // after the step: host pose, occupants
    virtual void damage(Entity victim, Fixed amount, Entity by, uint8_t player) = 0;
};
// The vehicle's own pedals (driver, bot, autopilot, parked) without the race.
lm::VehicleInput ownPedals(VehicleHost& host, Entity e, const VehicleDrive& d);
struct RacePedals { uint32_t index = 0; lm::VehicleInput input; };   // by entity index (VehicleRace::pedals)
// The vehicle system: one step of every vehicle, ascending entity index. race: the race adapter (crash damage rule).
void stepVehicles(World& w, VehicleHost& host, const DriveRules& rules, uint32_t tick, Fixed dt,
                  std::span<const RacePedals> race = {}, const VehicleRace* raceAdapter = nullptr);

// ---------------------------------------------------------------- hash and save
// Hash layout (count, then per vehicle: entity, driver, player, door, [deck], controller, flags, pilot, [seats]).
void hashVehicles(const World& w, Hasher& h, bool decks);   // read-only (never detaches snapshot pools)
// Component records (no requests). A host that saves whole pools (the grid-map host's pool list) finds them by ADL.
void io(Archive& a, VehicleDrive& d);
void io(Archive& a, VehicleOccupant& o);
bool valid(const VehicleDrive& d);

} // namespace aiverse::vehicles
