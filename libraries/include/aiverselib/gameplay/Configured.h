#pragma once
#include "Configuration.h"
#include "Abilities.h"
#include "Construction.h"
#include "Inventory.h"
namespace aiverse::lib::gameplay {
inline AbilityConfig abilityConfig(const AbilitySettings& s,TargetKind target){return {target,s.minRange,s.maxRange,s.energy,s.castTicks,s.cooldownTicks,s.interruptible};}
inline Placement placementConfig(const ConstructionSettings& s,Cell origin,std::int64_t owner,std::int64_t rotation=0){return {origin.x,origin.z,s.width,s.height,rotation,owner,s.maxSlope,s.requireTerritory};}
inline InventoryConfig inventoryConfig(const InventorySettings& s){return {static_cast<std::size_t>(s.slots),s.maxWeight};}
}
