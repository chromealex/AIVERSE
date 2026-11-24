#pragma once
// Native authoritative PHYSIS-compatible lift platform collider publication.
// Building geometry is still owned by RuntimeBuildingGenerator; this file
// only moves dynamic cabin bounds in the *existing* SpatialWorld and marks
// the *existing* nav::Mesh dirty. The host must call this on the world owner
// phase, then refresh navmesh via its normal Source before making queries.
#include "engine/buildings/BuildingNavMeshBridge.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace aiverse::building {
inline world::Bounds buildingLiftPlatformWorldBounds(
    const BuildingLiftPlatformPose& pose,float W,float D,
    Vec3 origin,uint32_t yaw){
    const Vec3 a=buildingLocalToWorld(pose.x0,0,pose.z0,W,D,origin,yaw);
    const Vec3 b=buildingLocalToWorld(pose.x1,0,pose.z1,W,D,origin,yaw);
    const Fixed base=origin.y+Fixed::fromRatio(pose.yMm,1000);
    const Fixed thick=Fixed::fromRatio(
        int64_t(std::llround(double(pose.thickness)*1000.)),1000);
    return {{fxMin(a.x,b.x),base,fxMin(a.z,b.z)},
            {fxMax(a.x,b.x),base+thick,fxMax(a.z,b.z)}};
}
inline void markBuildingNavCollisionChanged(nav::Mesh& mesh,
                                              world::Bounds bounds){
    if(!mesh.built())return;
    const auto& c=mesh.config();
    // Floor division, including negative world coordinates. Include an
    // extra column ring because neighbouring portal links may change.
    const auto floorDiv=[](int64_t n,int64_t d)->int64_t{
        int64_t q=n/d;
        if(n%d<0)--q;
        return q;
    };
    const int64_t x0=floorDiv(bounds.min.x.raw-c.originX.raw,c.cell.raw)-1;
    const int64_t z0=floorDiv(bounds.min.z.raw-c.originZ.raw,c.cell.raw)-1;
    const int64_t x1=floorDiv(bounds.max.x.raw-c.originX.raw,c.cell.raw)+1;
    const int64_t z1=floorDiv(bounds.max.z.raw-c.originZ.raw,c.cell.raw)+1;
    if(x1<0||z1<0||x0>=c.width||z0>=c.depth)return;
    mesh.markDirty(
        int32_t(std::clamp<int64_t>(x0,0,c.width-1)),
        int32_t(std::clamp<int64_t>(z0,0,c.depth-1)),
        int32_t(std::clamp<int64_t>(x1,0,c.width-1)),
        int32_t(std::clamp<int64_t>(z1,0,c.depth-1)));
}
class BuildingLiftColliderOwner {
public:
    bool registerPlatform(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                          uint32_t feature,uint32_t colliderId,
                          const BuildingLiftPlatformPose& pose,
                          float width,float depth,Vec3 origin,uint32_t yaw){
        if(feature!=pose.featureIndex||!colliderId||yaw>3||
           !(width>0)||!(depth>0)||registered_.count(feature)||
           spatial.colliders().contains(colliderId))
            return false;
        const auto bounds=buildingLiftPlatformWorldBounds(
            pose,width,depth,origin,yaw);
        if(!spatial.add({colliderId,bounds}))return false;
        registered_.emplace(feature,State{colliderId,bounds,
            width,depth,origin,yaw,pose.revision,pose.yMm});
        if(navmesh)markBuildingNavCollisionChanged(*navmesh,bounds);
        return true;
    }

    bool applyMovement(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                       const BuildingLiftPlatformMotion& motion){
        auto found=registered_.find(motion.featureIndex);
        if(found==registered_.end()||motion.revision<=found->second.revision||
           motion.oldYmm!=found->second.yMm)
            return false;
        auto& entry=found->second;
        BuildingLiftPlatformPose pose;
        pose.featureIndex=motion.featureIndex;
        pose.yMm=motion.newYmm;
        pose.x0=motion.x0;pose.z0=motion.z0;
        pose.x1=motion.x1;pose.z1=motion.z1;
        pose.thickness=motion.thickness;
        pose.revision=motion.revision;
        const auto next=buildingLiftPlatformWorldBounds(pose,
            entry.width,entry.depth,entry.origin,entry.yaw);
        const auto previous=entry.bounds;
        spatial.remove(entry.id);
        if(!spatial.add({entry.id,next})){
            // Preserve collision ownership on failure and keep the
            // navmesh revision from being declared valid prematurely.
            (void)spatial.add({entry.id,previous});
            if(navmesh)markBuildingNavCollisionChanged(*navmesh,previous);
            return false;
        }
        entry.bounds=next;entry.revision=motion.revision;
        entry.yMm=motion.newYmm;
        if(navmesh){
            markBuildingNavCollisionChanged(*navmesh,previous);
            markBuildingNavCollisionChanged(*navmesh,next);
        }
        return true;
    }

    bool remove(world::SpatialWorld& spatial,nav::Mesh* navmesh,
                uint32_t feature){
        auto it=registered_.find(feature);
        if(it==registered_.end())return false;
        spatial.remove(it->second.id);
        if(navmesh)markBuildingNavCollisionChanged(*navmesh,it->second.bounds);
        registered_.erase(it);
        return true;
    }
    bool contains(uint32_t feature)const{return registered_.contains(feature);}
    size_t size()const{return registered_.size();}

private:
    struct State {
        uint32_t id=0;world::Bounds bounds{};
        float width=0,depth=0;Vec3 origin{};uint32_t yaw=0;
        uint64_t revision=0;
        int64_t yMm=0;
    };
    std::map<uint32_t,State> registered_;
};
} // namespace aiverse::building
