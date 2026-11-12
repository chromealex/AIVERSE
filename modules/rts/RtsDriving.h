// Driving (agent-gta2): the top-down hero gets into a vehicle and drives it. rules "driving" (docs/driving.md).
//
// Vehicles are ordinary units of the types listed in driving.vehicles. Each one owns a kinematic VehicleState of the
// vehicle-controllers library (libraries/include/aiverselib/genre/VehicleControllers.h, wheeled arcade-sim: torque
// curve, gears, tyre grip, handbrake, drift) in metres; this file is the collision owner the library asks for: it
// reports the ground, sweeps the body (three circles along the car) over the map grid and other units, resolves the
// hits (vehicleResolveCollision), turns impact speed into damage and runs pedestrians over.
//
// kCmdTopDownEnter toggles: on foot the hero gets into the nearest free vehicle in range, in a car it gets out (slow
// enough, next to a door, on a passable spot). While driving the hero is off the map (kUnitInCargo) and follows the
// car; the hero's sticky kCmdTopDown input steers it ("tank": stick up = throttle, down = brake / reverse, sideways
// = steering; "stick": the car turns toward the stick direction), the fire bit is the handbrake.
//
// State lives in the DrivingStore world resource: hashed, saved after the top-down heroes, copied by rollback
// snapshots. Games without the block keep their checksums and save layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"
#include "../../libraries/include/aiverselib/genre/VehicleControllers.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

enum class DriveControls : uint8_t { Tank, Stick };

struct DrivingVehicleType {
    uint16_t type = UINT16_MAX;
    Fixed length = Fixed::fromInt(1), width = Fixed::fromRatio(1, 2);   // cells
    lib::genre::VehicleControllerDef def;
};

struct DrivingRules {
    bool enabled = false;
    Fixed metersPerCell = Fixed::fromInt(4);
    Fixed enterRange = Fixed::fromInt(1);          // cells between the hero and the car body
    Fixed exitSpeed = Fixed::fromInt(3);           // m/s: faster than this the doors stay shut
    DriveControls controls = DriveControls::Tank;
    Fixed crashMinSpeed = Fixed::fromInt(5), crashDamage = Fixed::fromInt(4), restitution = Fixed::fromRatio(2, 10);
    Fixed runOverMinSpeed = Fixed::fromInt(4), runOverDamage = Fixed::fromInt(15);
    Fixed wreckDamage{};                           // damage to the driver when the car is destroyed under him
    uint32_t doorTicks = 0;                        // how long a door stays open after getting in / out (view)
    Fixed doorSeconds = Fixed::fromRatio(1, 2);
    std::vector<DrivingVehicleType> vehicles;      // sorted by type
};

struct DrivingCar {
    Entity e = kNullEntity;
    Entity driver = kNullEntity;
    uint8_t player = 0;                            // slot of the driver (0: nobody)
    uint32_t doorUntil = 0;                        // the driver's door is open until this tick
    lib::genre::VehicleState st;
};

struct DrivingStore {
    std::vector<DrivingCar> cars;                  // ascending entity index
};

class Driving {
public:
    bool active() const { return rules_.enabled; }
    const DrivingRules& rules() const { return rules_; }
    static DrivingRules parse(const Json& j, const std::vector<std::string>& typeIds);   // throws readable errors
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdTopDownEnter handled
    void tick(Simulation& sim);                          // after the top-down heroes moved
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    DrivingStore* store(Simulation& sim) const;
    const DrivingCar* carOf(Simulation& sim, Entity driver) const;      // the car this unit drives, or null
    const DrivingCar* car(Simulation& sim, Entity vehicle) const;       // the state of a vehicle unit, or null
    bool isDriving(Simulation& sim, Entity hero) const { return carOf(sim, hero) != nullptr; }
    Fixed speed(Simulation& sim, Entity vehicle) const;                 // forward speed, m/s (0: not a vehicle)
    // takes a just spawned vehicle unit now (instead of at its next tick), rolling forward at `speed` m/s (traffic)
    bool adopt(Simulation& sim, Entity vehicle, Fixed speed);

private:
    const DrivingVehicleType* typeOf(uint16_t unitType) const;
    void adoptAndDrop(Simulation& sim, DrivingStore& st);
    void adoptOne(Simulation& sim, DrivingStore& st, Entity e);
    void enter(Simulation& sim, DrivingStore& st, uint8_t player, Entity hero);
    bool leave(Simulation& sim, DrivingCar& c, bool forced);
    void drive(Simulation& sim, DrivingStore& st, size_t index);
    bool collide(Simulation& sim, DrivingStore& st, size_t index, Vec3& posCells);
    lib::genre::VehicleInput inputOf(Simulation& sim, const DrivingCar& c) const;

    RtsModule* m_ = nullptr;
    DrivingRules rules_;
    std::vector<int32_t> vehicleOf_;                // per unit type: index into rules_.vehicles, -1: not a vehicle (derived)
    std::vector<Fixed> reach_;                      // per unit type: the farthest its body gets from its centre (derived)
};

} // namespace aiverse::game
