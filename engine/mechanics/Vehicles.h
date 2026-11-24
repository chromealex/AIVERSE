#pragma once
#include "engine/mechanics/Namespace.h"
// vehicle-controllers umbrella: controllers (wheeled/tracked/hover/flight), damage zones, seats, race checkpoints and
// positions, the race bot policy (bots/RaceBot.h) and race sessions from grid to results (RaceSession.h). VehicleDefinition groups one vehicle's immutable data.
#include "engine/mechanics/VehicleControllers.h"
#include "engine/mechanics/VehicleDamage.h"
#include "engine/mechanics/VehicleSeats.h"
#include "engine/mechanics/RaceCourse.h"
#include "engine/bots/RaceBot.h"
#include "engine/mechanics/RaceSession.h"
namespace aiverse::lib::mechanics {
struct VehicleDefinition{int64_t id=0;VehicleControllerDef controller;VehicleDamageDef damage;VehicleSeatsDef seats;};
inline bool valid(const VehicleDefinition& d){return d.id>0&&valid(d.controller)&&valid(d.damage)&&valid(d.seats);}
}
