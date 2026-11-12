#pragma once
// vehicle-controllers umbrella: controllers (wheeled/tracked/hover/flight), damage zones, seats, race checkpoints and
// positions, and the race bot policy (bots/RaceBot.h). VehicleDefinition groups one vehicle's immutable data.
#include "VehicleControllers.h"
#include "VehicleDamage.h"
#include "VehicleSeats.h"
#include "RaceCourse.h"
#include "../bots/RaceBot.h"
namespace aiverse::lib::genre {
struct VehicleDefinition{int64_t id=0;VehicleControllerDef controller;VehicleDamageDef damage;VehicleSeatsDef seats;};
inline bool valid(const VehicleDefinition& d){return d.id>0&&valid(d.controller)&&valid(d.damage)&&valid(d.seats);}
}
