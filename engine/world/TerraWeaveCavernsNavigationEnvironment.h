#pragma once
// Stage43: read-only, revision-pinned NEREID + PHYSIS navigation costs.
// No water/weather simulation copies are created. The immutable World
// snapshot is the already committed ECS state from Simulation's fixed tick;
// path worker callbacks must not read a concurrently mutating live World.
#include "engine/world/TerraWeaveCavernsNavigationData.h"
#include "engine/world/nereid/NereidWaterSystems.h"
#include "engine/physis/PhysisEnvironment.h"
#include "engine/core/ECS.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace aiverse::terraweave::caverns {
struct CaveEnvironmentCostSettings {
    int32_t physisCellCm=100;
    int64_t physisOriginXcm=0,physisOriginZcm=0;
    uint32_t baseCost=10;
    uint32_t maxCost=100000;
    uint32_t waterSurcharge=0;
};
struct CaveEnvironmentSource {
    uint64_t sdfRevision=0;
    uint64_t simulationTick=0;
    std::shared_ptr<const World> frozenEcs;
};
inline CaveNavigationCallbacks makeCaveEnvironmentCallbacks(
    CaveEnvironmentSource captured,
    CaveEnvironmentCostSettings settings={}) {
    if(!captured.frozenEcs||!captured.sdfRevision||
       settings.physisCellCm<=0||settings.physisCellCm>1000000||
       !settings.baseCost||settings.maxCost<settings.baseCost)
        throw std::invalid_argument("Caverns environment immutable ECS source");
    const auto ecs=std::move(captured.frozenEcs);
    const auto sampleWaterAt=[ecs](PointCm p){
        return lib::nereid::sampleWater(*ecs,{
            Fixed::fromRatio(p.x,100),
            Fixed::fromRatio(p.y,100),
            Fixed::fromRatio(p.z,100)});
    };
    CaveNavigationCallbacks cb;
    cb.submerged=[sampleWaterAt](PointCm p){
        return sampleWaterAt(p).submerged;
    };
    cb.traversalCost=[ecs,sampleWaterAt,settings](
        PointCm p,CaveTraversal traversal)->uint32_t {
        uint64_t cost=settings.baseCost;
        const auto water=sampleWaterAt(p);
        if(water.submerged && traversal!=CaveTraversal::Swimming)
            cost+=settings.waterSurcharge;
        const int64_t px=floorDiv(p.x-settings.physisOriginXcm,
                                  settings.physisCellCm);
        const int64_t pz=floorDiv(p.z-settings.physisOriginZcm,
                                  settings.physisCellCm);
        if(px>=INT32_MIN&&px<=INT32_MAX&&pz>=INT32_MIN&&pz<=INT32_MAX){
            physis::EnvironmentalResponse environment;
            if(physis::environmentAt(*ecs,int32_t(px),int32_t(pz),
                                     environment)) {
                // Low traction (ice/wetness) increases ground routing cost.
                // Integer-only fixed point. No bogus weather solver under
                // Caverns; read PHYSIS already committed response.
                if(traversal==CaveTraversal::Ground ||
                   traversal==CaveTraversal::Climbing){
                    const int64_t traction=std::max<int64_t>(
                        1,environment.tractionMultiplier.raw);
                    const uint64_t penalty=
                        (uint64_t(settings.baseCost)*Fixed::kOne+
                         uint64_t(traction)-1)/uint64_t(traction);
                    cost=std::max<uint64_t>(cost,penalty);
                }
            }
        }
        return uint32_t(std::min<uint64_t>(cost,settings.maxCost));
    };
    return cb;
}
} // namespace aiverse::terraweave::caverns
