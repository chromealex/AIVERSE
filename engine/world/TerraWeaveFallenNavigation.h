// TerraWeave: conservative navmesh obstacles from server-owned fallen
// Box3D trees/shrubs. Navigation is rebuilt from an immutable snapshot,
// never by querying a live physics Scene from nav::Source callbacks.
#pragma once
#include "engine/world/TerraWeaveFallenPhysics.h"
#include "engine/world/TerraWeaveNavMeshBridge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct FallenNavigationObstacle {
    AnchoredPlantId id{};
    RectCm bounds{};
};

class FallenNavigationSnapshot {
public:
    static constexpr size_t kMaxObstacles=8192;
    static constexpr int64_t kBucketCm=6400;

    void build(std::vector<FallenNavigationObstacle> obstacles) {
        if(obstacles.size()>kMaxObstacles)
            throw std::length_error("TerraWeave falling navigation budget");
        std::map<AnchoredPlantId,RectCm> ordered;
        std::map<std::pair<int64_t,int64_t>,std::vector<AnchoredPlantId>> bins;
        for(const auto& obstacle:obstacles) {
            const RectCm r=obstacle.bounds;
            if(!obstacle.id.key || !obstacle.id.speciesId ||
               r.minX>=r.maxX || r.minZ>=r.maxZ ||
               r.minX<-kWorldLimitCm || r.maxX>kWorldLimitCm ||
               r.minZ<-kWorldLimitCm || r.maxZ>kWorldLimitCm ||
               r.maxX-r.minX>100000 || r.maxZ-r.minZ>100000)
                throw std::invalid_argument("TerraWeave invalid fallen nav obstacle");
            if(!ordered.emplace(obstacle.id,r).second)
                throw std::invalid_argument("TerraWeave duplicate fallen nav ID");
            const int64_t x0=detail::floorDiv(r.minX,kBucketCm);
            const int64_t x1=detail::floorDiv(r.maxX-1,kBucketCm);
            const int64_t z0=detail::floorDiv(r.minZ,kBucketCm);
            const int64_t z1=detail::floorDiv(r.maxZ-1,kBucketCm);
            if((x1-x0+1)*(z1-z0+1)>1024)
                throw std::length_error("TerraWeave fallen nav per-object bins");
            for(int64_t z=z0;z<=z1;++z)
                for(int64_t x=x0;x<=x1;++x){
                    auto& list=bins[{x,z}];
                    if(list.size()>=128u)
                        throw std::length_error("TerraWeave fallen nav local density");
                    list.push_back(obstacle.id);
                }
        }
        bounds_.swap(ordered);
        bins_.swap(bins);
    }

    size_t size()const{return bounds_.size();}
    bool blockedAtCm(int64_t x,int64_t z)const {
        const auto it=bins_.find({
            detail::floorDiv(x,kBucketCm),detail::floorDiv(z,kBucketCm)});
        if(it==bins_.end())return false;
        for(const auto& id:it->second)
            if(bounds_.at(id).contains(x,z))return true;
        return false;
    }
    std::vector<RectCm> changedFrom(const FallenNavigationSnapshot& old)const {
        std::vector<RectCm> changed;
        for(const auto& [id,rect]:old.bounds_) {
            const auto it=bounds_.find(id);
            if(it==bounds_.end() || !same(it->second,rect))
                changed.push_back(rect); // unblock former footprint
        }
        for(const auto& [id,rect]:bounds_) {
            const auto it=old.bounds_.find(id);
            if(it==old.bounds_.end() || !same(it->second,rect))
                changed.push_back(rect); // block current footprint
        }
        return changed;
    }
private:
    static bool same(RectCm a,RectCm b) {
        return a.minX==b.minX && a.minZ==b.minZ &&
               a.maxX==b.maxX && a.maxZ==b.maxZ;
    }
    std::map<AnchoredPlantId,RectCm> bounds_;
    std::map<std::pair<int64_t,int64_t>,
             std::vector<AnchoredPlantId>> bins_;
};

// Quantize authoritative Box3D body poses into centimetres only after the
// scene's server tick. A rotated trunk is approximated by the X/Z AABB of
// its quaternion-rotated local +Y axis plus collision/agent clearance.
// This is an intentionally conservative navigation obstacle, NOT an exact
// per-branch collider or deterministic Box3D simulation across devices.
inline std::shared_ptr<const FallenNavigationSnapshot>
captureFallenNavigation(const FallenPlantPhysics& falling,
                        const physics::Scene& scene,
                        uint32_t clearanceCm=70) {
    if(clearanceCm<5u || clearanceCm>1000u)
        throw std::invalid_argument("TerraWeave falling nav clearance");
    std::vector<FallenNavigationObstacle> obstacles;
    obstacles.reserve(falling.count());
    for(const auto& item:falling.snapshot()) {
        if(!scene.valid(item.body))continue;
        const b3Vec3 p=scene.position(item.body);
        const b3Quat q=scene.rotation(item.body);
        const double x=double(p.x)*100.0,z=double(p.z)*100.0;
        const double halfLength=double(item.heightCm)*
                                item.modelScalePermille/2000.0;
        const double ax=2.0*(double(q.v.x)*q.v.y-double(q.s)*q.v.z);
        const double az=2.0*(double(q.v.y)*q.v.z+double(q.s)*q.v.x);
        if(!std::isfinite(x) || !std::isfinite(z) ||
           !std::isfinite(ax) || !std::isfinite(az) ||
           !std::isfinite(halfLength) || halfLength>50000)
            throw std::invalid_argument("TerraWeave invalid fallen body pose");
        const int64_t extentX=int64_t(
            std::ceil(std::min(1.0,std::abs(ax))*halfLength))+
            clearanceCm;
        const int64_t extentZ=int64_t(
            std::ceil(std::min(1.0,std::abs(az))*halfLength))+
            clearanceCm;
        const int64_t cx=int64_t(std::llround(x));
        const int64_t cz=int64_t(std::llround(z));
        if(cx<=-kWorldLimitCm+extentX ||
           cx>= kWorldLimitCm-extentX ||
           cz<=-kWorldLimitCm+extentZ ||
           cz>= kWorldLimitCm-extentZ)
            throw std::out_of_range("TerraWeave fallen body navigation extent");
        obstacles.push_back({item.id,{
            cx-extentX,cz-extentZ,cx+extentX+1,cz+extentZ+1}});
    }
    auto snapshot=std::make_shared<FallenNavigationSnapshot>();
    snapshot->build(std::move(obstacles));
    return snapshot;
}

// Compose with authored building/water blockers without scanning live
// physics state or building one ECS entity per grass blade. The returned
// callable is safe on immutable-snapshot navigation read jobs.
inline EarthworksNavSource::Blocked fallenNavigationBlocker(
        std::shared_ptr<const FallenNavigationSnapshot> snapshot,
        EarthworksNavConfig config,
        EarthworksNavSource::Blocked other={}) {
    validateEarthworksNavConfig(config);
    return [snapshot=std::move(snapshot),
            config,other=std::move(other)](int32_t x,int32_t z){
        if(other && other(x,z))return true;
        if(!snapshot || x<0 || z<0 ||
           x>=config.width || z>=config.depth)return false;
        const int64_t cx=config.originXcm+
            int64_t(x)*config.cellCm+config.cellCm/2;
        const int64_t cz=config.originZcm+
            int64_t(z)*config.cellCm+config.cellCm/2;
        return snapshot->blockedAtCm(cx,cz);
    };
}

inline void markFallenNavigationDirty(
        nav::Mesh& navmesh,const EarthworksNavSource& source,
        const FallenNavigationSnapshot& current,
        const FallenNavigationSnapshot& previous) {
    for(const RectCm rect:current.changedFrom(previous))
        markEarthworksNavDirty(navmesh,source,rect);
}

} // namespace aiverse::terraweave
