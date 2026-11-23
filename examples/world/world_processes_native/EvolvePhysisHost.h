// Install EVOLVE's optional PHYSIS climate binding after actual PHYSIS
// and EvolveRegion entities exist. No engine-level automatic copy of weather.
#pragma once
#include "aiverselib/world/EvolvePhysisBridge.h"
#include <string>

namespace aiverse::examples::world_processes_native {
inline bool attachEvolveClimate(World& world,Entity producer,
                                Entity region,Entity physisCell,
                                evolve::ClimateLink authored,
                                std::string* error=nullptr){
    const auto fail=[&](const char* reason){
        if(error)*error=reason;
        return false;
    };
    const World& current=world;
    if(!current.alive(producer)||!current.alive(region)||
       !current.alive(physisCell)||
       !current.has<evolve::Region>(region)||
       !current.has<physis::Atmosphere>(physisCell)||
       !current.has<physis::Surface>(physisCell)||
       current.has<evolve::ClimateLink>(producer)||
       current.has<evolve::Pressure>(producer)||
       !evolve::valid(authored))
        return fail("invalid EVOLVE climate source, target or ownership");
    authored.region=region;
    authored.weatherCell=physisCell;
    evolve::Pressure pressure;
    pressure.region=region;
    pressure.enabled=authored.enabled;
    world.add<evolve::ClimateLink>(producer,authored);
    world.add<evolve::Pressure>(producer,pressure);
    return true;
}
} // namespace aiverse::examples::world_processes_native
