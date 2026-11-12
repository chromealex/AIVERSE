#pragma once
// survival-ecology persistence: versioned genre::save/load records (SurvivalState 106, EcologyState 107,
// EcologyAnimalState 108, EcologyResourceState 109; kinds 104..111 are shared with settlement-production, see
// ColonyPersistence.h) and checksum hashing. Bind survivalConfigHash / ecologyDefsHash / ecologyAnimalDefsHash in
// the save envelope; feed survivalHash(state) values into the simulation checksum.
#include "Survival.h"
#include "Ecology.h"
#include "EcologyAnimals.h"
#include "EcologyResources.h"
#include "Persistence.h"
#include "engine/core/Hash.h"
#include <string>
namespace aiverse::lib::genre {
inline void io(Archive& a,SurvivalBody& b){a(b.id,b.coreMicro,b.wetness,b.hunger,b.thirst,b.stamina,b.condition,b.status,b.dead);}
inline void io(Archive& a,SurvivalState& s){a(s.bodies,s.lastTick,s.initialized);}
inline void io(Archive& a,EcologyPopulation& p){a(p.count,p.carry);}
inline void io(Archive& a,EcologyState& s){a(s.populations,s.lastTick,s.initialized);}
inline void io(Archive& a,EcologyAnimal& x){a(x.id,x.species,x.region,x.position.x,x.position.y,x.home.x,x.home.y,x.goal.x,x.goal.y,x.behavior,x.hunger,x.energy,x.target,x.since,x.nextWander,x.calmUntil,x.huntReadyAt,x.alive);}
inline void io(Archive& a,EcologyAnimalState& s){a(s.animals,s.lastTick,s.initialized);}
inline void io(Archive& a,EcologyResourceNode& n){a(n.id,n.resource,n.region,n.amount,n.capacity,n.regrowPerPeriod,n.periodTicks,n.regrowDelayTicks,n.carry,n.depletedAt);}
inline void io(Archive& a,EcologyResourceState& s){a(s.nodes,s.lastTick,s.initialized);}
inline uint16_t stateKind(const SurvivalState&){return 106;}
inline uint16_t stateKind(const EcologyState&){return 107;}
inline uint16_t stateKind(const EcologyAnimalState&){return 108;}
inline uint16_t stateKind(const EcologyResourceState&){return 109;}
// Definitions (hash only).
inline void io(Archive& a,SurvivalWeatherConfig& c){a(c.windChillMilli,c.wetPerTick,c.ratedWindMilli,c.growMinMilli,c.growBestMilli,c.growMaxMilli,c.rainGrowthMilli,c.stormWindMilli);}
inline void io(Archive& a,SurvivalConfig& c){
    a(c.coreNormalMilli,c.comfortLowMilli,c.comfortHighMilli,c.coolingMicro,c.heatingMicro,c.recoveryMicro,c.coldMilli,c.severeColdMilli,c.hotMilli,c.severeHotMilli);
    a(c.exertionHeatMicro,c.hungerPerTick,c.thirstPerTick,c.exertionHungerMilli,c.shiverHungerMilli,c.sweatThirstMilli,c.staminaDrain,c.staminaRegen,c.lowAt);
    a(c.wetChillMilli,c.dryPerTick,c.dryPerDegreeMicro,c.starveDamage,c.thirstDamage,c.coldDamage,c.heatDamage,c.conditionRegen,c.weather);
}
inline void io(Archive& a,EcologyPredation& p){a(p.prey,p.attackMilli,p.halfSaturation,p.conversionMilli);}
inline void io(Archive& a,EcologySpeciesDef& s){a(s.id,s.growthMilli,s.mortalityMilli,s.foodNeedMilli,s.starvationMilli,s.migrateMilli,s.diet);}
inline void io(Archive& a,EcologyRegionDef& r){a(r.id,r.capacity,r.neighbors);}
inline void io(Archive& a,EcologyDefs& d){a(d.periodTicks,d.noiseMilli,d.species,d.regions);}
inline void io(Archive& a,EcologyAnimalDef& x){
    a(x.species,x.prey,x.fears,x.walkSpeed,x.runSpeed,x.fleeDistance,x.huntRange,x.catchDistance,x.wanderRadius,x.hungerPerTick,x.grazeGain,x.huntGain,x.grazeBelow,x.huntBelow,x.fullAt);
    a(x.energyPerTick,x.sleepGain,x.sleepBelow,x.sleepFrom,x.sleepTo,x.wanderTicks,x.huntGiveUpTicks,x.fleeCalmTicks,x.huntCooldownTicks,x.fleeFrom,x.huntFrom);
}
inline void io(Archive& a,EcologyAnimalDefs& d){a(d.ticksPerHour,d.boundsMin.x,d.boundsMin.y,d.boundsMax.x,d.boundsMax.y,d.species);}
template<class T> void hashSurvival(Hasher& h,const T& value){auto copy=value;std::string bytes;Archive a(&bytes);io(a,copy);h.str(bytes);}
template<class T> uint64_t survivalHash(const T& value){Hasher h;hashSurvival(h,value);return h.value();}
inline uint64_t survivalConfigHash(const SurvivalConfig& c){return survivalHash(c);}
inline uint64_t ecologyDefsHash(const EcologyDefs& d){return survivalHash(d);}
inline uint64_t ecologyAnimalDefsHash(const EcologyAnimalDefs& d){return survivalHash(d);}
}
