#pragma once
// One include for game-client native buildings: CPU generation, room routing,
// existing nav::Mesh adapter, lift controller, dynamic collision owner and
// lift CPU visual model. Dedicated servers may instead include only
// aiverselib/systems/RuntimeBuildingGenerator.h and
// aiverselib/systems/BuildingCirculationRuntime.h.
#include <aiverselib/systems/RuntimeBuildingGenerator.h>
#include <aiverselib/systems/BuildingCirculationRuntime.h>
#include <aiverselib/systems/BuildingRouteExecutor.h>
#include "engine/buildings/RuntimeBuildingModel.h"
#include "engine/buildings/BuildingCirculationHost.h"
#include "engine/buildings/BuildingLiftColliderOwner.h"
#include "engine/buildings/BuildingCirculationScene.h"
#include "engine/buildings/BuildingLiftPlatformModel.h"
#include "engine/buildings/BuildingLiftGameSession.h"
