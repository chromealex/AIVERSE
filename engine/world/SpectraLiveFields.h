// Committed simulation -> immutable presentation Data Interfaces.
// No world reference survives capture; no simulation steps or ECS writes.
#pragma once
#include "engine/world/SpectraPhysisSnapshot.h"
#include "engine/world/SpectraWaterSnapshot.h"
#include <memory>

namespace aiverse::world {
class SpectraLiveFields {
public:
    using Registry=std::shared_ptr<const spectra::DataInterfaceRegistry>;
    // Committed owner-thread capture. Host changes sourceEpoch on replacement
    // or restore (including same-tick restores). Region and geometry are explicit.
    // Invalid publication clears the current registry, never reuses stale data.
    Registry publish(const World& world,uint64_t sourceEpoch,uint64_t tick,
                     const SpectraFieldRegion& region,SpectraWaterGeometry geometry,
                     Fixed seconds={}) {
        if(!sourceEpoch||!region.count()||
           (geometry!=SpectraWaterGeometry::Plane&&geometry!=SpectraWaterGeometry::Sphere)){
            clear();return {};
        }
        if(registry_&&sourceEpoch==epoch_&&tick==tick_&&geometry==geometry_&&
           seconds==seconds_&&sameRegion(region,region_))return registry_;
        auto next=std::make_shared<spectra::DataInterfaceRegistry>();
        if(world.resourceConst<physis::State>()) {
            auto weather=SpectraPhysisSnapshot::capture(world,region);
            if(weather)publishSpectraPhysis(*next,std::move(weather));
        }
        // The canonical planar sampler also handles flood resources, so
        // publish even when no ordinary water body is present.
        auto water=SpectraWaterSnapshot::capture(world,region,geometry,tick,seconds);
        if(water)publishSpectraWater(*next,std::move(water));
        registry_=std::move(next);
        epoch_=sourceEpoch;tick_=tick;region_=region;geometry_=geometry;seconds_=seconds;
        return registry_;
    }
    void clear(){registry_.reset();epoch_=tick_=0;}
    const Registry& current()const{return registry_;}
    uint64_t tick()const{return tick_;}
    uint64_t epoch()const{return epoch_;}
private:
    static bool sameRegion(const SpectraFieldRegion& a,const SpectraFieldRegion& b){
        return a.origin.x==b.origin.x&&a.origin.y==b.origin.y&&a.origin.z==b.origin.z&&
               a.spacing==b.spacing&&a.dimensions==b.dimensions;
    }
    Registry registry_;
    uint64_t epoch_=0,tick_=0;
    SpectraFieldRegion region_{};
    SpectraWaterGeometry geometry_=SpectraWaterGeometry::Plane;
    Fixed seconds_{};
};
}
