// EVOLVE canonical region spatial extents. Boundaries live in persisted ECS,
// not just editor overlay metadata. Forest ecology uses the same source.
#pragma once
#include "EvolveEcs.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace aiverse::evolve {
inline constexpr I kAreaCoordinateCm=100000000;
inline constexpr I kAreaMaxHalfExtentCm=50000;
struct Area {
    I centerXcm=0,centerZcm=0;
    I halfWidthCm=1000,halfDepthCm=1000;
    bool enabled=true;
};
inline bool valid(const Area& a){
    return a.centerXcm>=-kAreaCoordinateCm&&a.centerXcm<=kAreaCoordinateCm&&
           a.centerZcm>=-kAreaCoordinateCm&&a.centerZcm<=kAreaCoordinateCm&&
           a.halfWidthCm>=50&&a.halfWidthCm<=kAreaMaxHalfExtentCm&&
           a.halfDepthCm>=50&&a.halfDepthCm<=kAreaMaxHalfExtentCm&&
           a.centerXcm-a.halfWidthCm>=-kAreaCoordinateCm&&
           a.centerXcm+a.halfWidthCm<=kAreaCoordinateCm&&
           a.centerZcm-a.halfDepthCm>=-kAreaCoordinateCm&&
           a.centerZcm+a.halfDepthCm<=kAreaCoordinateCm;
}
inline void io(Archive& archive,Area& area){
    archive(area.centerXcm,area.centerZcm,
            area.halfWidthCm,area.halfDepthCm,area.enabled);
    if(!archive.writing()&&!valid(area))archive.fail();
}
inline bool setArea(World& world,Entity region,Area next){
    if(!world.alive(region)||!world.has<Region>(region)||!valid(next))
        return false;
    if(world.has<Area>(region))world.get<Area>(region)=next;
    else world.add<Area>(region,next);
    return true;
}
// Existing native forest owner calls this AFTER the completed EVOLVE tick.
// Derived regions are sorted by stable generation-safe ECS Entity identity.
// There is never a parallel mutable region index.
struct Extent {
    Entity region=kNullEntity;
    I minXcm=0,minZcm=0,maxXcm=0,maxZcm=0;
};
inline std::vector<Extent> extents(const World& world){
    std::vector<Extent> out;
    world.eachExistingConst<Area>([&](Entity e,const Area& area){
        if(!world.alive(e)||!area.enabled||!valid(area)||
           !world.has<Region>(e))return;
        out.push_back({e,area.centerXcm-area.halfWidthCm,
                        area.centerZcm-area.halfDepthCm,
                        area.centerXcm+area.halfWidthCm,
                        area.centerZcm+area.halfDepthCm});
    });
    std::sort(out.begin(),out.end(),[](const Extent& a,const Extent& b){
        return a.region.packed()<b.region.packed();
    });
    return out;
}
inline void registerAreaPersistence(Simulation& sim){
    sim.registerPersistentComponent<Area>("evolve.area",1);
}
} // namespace aiverse::evolve
