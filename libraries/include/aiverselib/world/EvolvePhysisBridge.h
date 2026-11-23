// EVOLVE consumes canonical PHYSIS climate/surface ECS samples.
// This is an opt-in bridge, not another weather or ecosystem simulator.
#pragma once
#include "EvolveEcs.h"
#include "engine/physis/Physis.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::evolve {
inline constexpr std::size_t kMaxClimateLinks=8192;
// One climate link writes ONE EvolvePressure component on the same
// generation-safe Entity. The existing PHYSIS world owns Atmosphere/Surface.
struct ClimateLink {
    Entity region=kNullEntity;
    Entity weatherCell=kNullEntity;
    I baseWear=0,temperatureWear=0;
    I rainEnrichment=0,droughtConsumption=0;
    I rainPurification=0,stormPollution=0;
    I warmGrowth=0,frostRegression=0;
    bool enabled=true;
};
inline bool valid(const ClimateLink& v){
    return inRate(v.baseWear)&&inRate(v.temperatureWear)&&
           inRate(v.rainEnrichment)&&inRate(v.droughtConsumption)&&
           inRate(v.rainPurification)&&inRate(v.stormPollution)&&
           inRate(v.warmGrowth)&&inRate(v.frostRegression);
}
inline void io(Archive& a,ClimateLink& v){
    a(v.region,v.weatherCell,v.baseWear,v.temperatureWear,
      v.rainEnrichment,v.droughtConsumption,v.rainPurification,
      v.stormPollution,v.warmGrowth,v.frostRegression,v.enabled);
    if(!a.writing()&&!valid(v))a.fail();
}
// All rates come from a host-authored climate coupling. The PHYSIS Fixed
// inputs are read-only and sampled only at the ECS owner tick barrier.
// Only the dedicated Pressure on this link entity is modified.
inline bool applyClimate(World& world){
    const World& read=world;
    if(read.countExisting<ClimateLink>()>kMaxClimateLinks)return false;
    std::vector<std::pair<Entity,Pressure>> staged;
    std::size_t activeLinks=0;
    read.eachExistingConst<ClimateLink>([&](Entity e,const ClimateLink& v){
        if(!read.alive(e))return;
        ++activeLinks;
        if(!valid(v))return;
        const auto* pressure=read.tryGetConst<Pressure>(e);
        if(!pressure)return;
        staged.push_back({e,*pressure});
    });
    if(staged.size()!=activeLinks)return false;
    std::sort(staged.begin(),staged.end(),
        [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
    // Validate all sources before touching persistent fields.
    for(auto& [entity,p]:staged){
        const auto* link=read.tryGetConst<ClimateLink>(entity);
        if(!link||!valid(*link)||!valid(p))return false;
        if(!link->enabled){p.enabled=false;continue;}
        if(!read.alive(link->region)||!read.has<Region>(link->region)||
           !read.alive(link->weatherCell))return false;
        const auto* air=read.tryGetConst<physis::Atmosphere>(link->weatherCell);
        const auto* ground=read.tryGetConst<physis::Surface>(link->weatherCell);
        if(!air||!ground)return false;
        // PHYSIS quantities are Fixed (Q47.16). Integer permille
        // conversion is safe for signed and negative temperatures.
        const auto permille=[](Fixed value){
            // Saturate before multiplying: corrupt/extreme Fixed raw input
            // must never produce undefined signed overflow.
            constexpr int64_t bound=1000*Fixed::kOne;
            const int64_t raw=std::clamp(value.raw,-bound,bound);
            return (raw*1000)/Fixed::kOne;
        };
        const I rain=std::clamp(permille(air->rain),I(0),I(1000));
        const I wet=std::clamp(permille(ground->wetness),I(0),I(1000));
        const I temp=std::clamp(permille(air->temperature),I(-1000),I(1000));
        const auto scale=[](I amount,I multiplier){
            return std::min(kMaxRate,(amount*multiplier)/1000);
        };
        p.region=link->region;
        p.wear=std::min(kMaxRate,link->baseWear+
                    scale(std::max(I(0),temp-350),link->temperatureWear));
        p.enrichment=scale(rain,link->rainEnrichment);
        p.consumption=scale(1000-wet,link->droughtConsumption);
        p.purification=scale(rain,link->rainPurification);
        p.contamination=scale(std::max(I(0),rain-650),link->stormPollution);
        p.growth=scale(std::max(I(0),temp),link->warmGrowth);
        p.regression=scale(std::max(I(0),-temp),link->frostRegression);
        p.enabled=true;
    }
    if(!staged.empty()&&!world.prepareExistingWrite<Pressure>())return false;
    for(const auto& [e,p]:staged)world.get<Pressure>(e)=p;
    return true;
}
inline void registerClimatePersistence(Simulation& sim){
    sim.registerPersistentComponent<ClimateLink>("evolve.climate_link",1);
}
inline void installClimateBridge(Simulation& sim){
    sim.addSystem("evolve.physis_climate",[](Simulation& s){
        (void)applyClimate(s.world());
    });
}
} // namespace aiverse::evolve
