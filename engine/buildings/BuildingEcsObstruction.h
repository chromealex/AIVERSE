#pragma once
// Universal ECS/PHYSIS swept occupancy adapter. No FPS/RTS actor types.
// The authoritative simulation supplies one shared ECS World and a PHYSIS
// Scene if it uses Box3D. The caller owns both and serializes this query
// with the authoritative physics step.
#include "engine/sim/CommonComponents.h"
#include "engine/sim/Simulation.h"
#include "engine/world/SpatialWorld.h"
#include <functional>
#include <cstdint>

namespace aiverse::building {

// Body bounds are gameplay-defined; Transform alone does NOT establish a
// physical volume. One component per collidable ECS entity with positive
// local half extents. Other systems may use their own adapters as long as
// the physical swept callback uses the same canonical ECS/PHYSIS world.
struct BuildingPhysicalExtent {
    Vec3 halfExtents{};
    bool blocksBuildingMotion=true;
    // Transform.pos can be actor feet or body origin; express the real
    // world-space centre of the axis-aligned physical extent explicitly.
    Vec3 centreOffset{};
};
inline void io(Archive& archive,BuildingPhysicalExtent& volume){
    archive(volume.halfExtents,volume.blocksBuildingMotion,volume.centreOffset);
}


// Register during GameModule::setup before PersistentEcsRegistry seals.
// Game-specific ECS body schemas can still provide alternate probes.
inline void registerBuildingPhysicalExtentPersistence(Simulation& sim){
    sim.registerPersistentComponent<BuildingPhysicalExtent>(
        "building.physical_extent",2);
    sim.registerPersistentComponentMigration<BuildingPhysicalExtent>(
        "building.physical_extent",1,[](Archive& a,BuildingPhysicalExtent& v){
            a(v.halfExtents,v.blocksBuildingMotion);v.centreOffset={};
        });
}

inline bool buildingEcsSweptOccupied(
    const World& ecs,const world::Bounds& first,
    const world::Bounds& second){
    bool occupied=false;
    ecs.eachExistingConst<BuildingPhysicalExtent>(
        [&](Entity entity,const BuildingPhysicalExtent& volume){
            if(occupied||!volume.blocksBuildingMotion||!ecs.alive(entity))
                return;
            const auto* local=ecs.tryGetConst<Transform>(entity);
            if(!local||volume.halfExtents.x<=Fixed{}||
               volume.halfExtents.y<=Fixed{}||
               volume.halfExtents.z<=Fixed{})return;
            const Vec3 centre=local->pos+volume.centreOffset;
            const world::Bounds box{
                centre-volume.halfExtents,centre+volume.halfExtents};
            if(world::overlaps(first,box)||world::overlaps(second,box))
                occupied=true;
        });
    return occupied;
}

// Optional PHYSIS/Box3D obstruction checking is implemented in the
// separate BuildingPhysisObstruction.h. This common ECS adapter has no
// mandatory Box3D build/link dependency.
inline auto buildingCommonSweptProbe(const World& ecs){
    return [&ecs](const world::Bounds& first,const world::Bounds& second,
                  uint32_t ownFirst,uint32_t ownSecond){
        (void)ownFirst;(void)ownSecond;
        return buildingEcsSweptOccupied(ecs,first,second);
    };
}

} // namespace aiverse::building
