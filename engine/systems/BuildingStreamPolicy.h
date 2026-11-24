#pragma once
// Presentation-only streaming policy. Never changes authoritative instance
// placement, collision, navigation, ECS or dynamic aperture state.
#include "BuildingInstances.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace aiverse::lib::systems {
struct BuildingStreamPolicy {
    double enterDistance=250,exitDistance=300;
    size_t maxInstances=2048,maxModels=256,maxNewModels=4;
};
struct BuildingStreamPlan {
    // Borrowed pointers: input storage must outlive frame preparation.
    std::vector<const BuildingPlacedInstance*> instances;
    std::set<uint64_t> selectedIds;
    std::set<std::string> selectedModels;
    size_t outside=0,deferred=0,newModels=0;
};
inline bool validBuildingStreamPolicy(const BuildingStreamPolicy& p){
    return std::isfinite(p.enterDistance)&&std::isfinite(p.exitDistance)&&
        p.enterDistance>=0&&p.exitDistance>=p.enterDistance&&p.exitDistance<=4000000&&
        p.maxInstances<=65536&&p.maxModels<=4096&&p.maxNewModels<=4096;
}
// Distance to the rotated mesh AABB, not the placement pivot. Includes
// overhangs and respects the minimum-corner quarter-turn convention.
inline bool buildingStreamDistanceSquared(const BuildingPlacedInstance& i,Vec3 eye,double& out){
    const auto& b=i.resource.local->building;
    const auto& m=b.mesh;
    const double w=b.constructionSize.width,d=b.constructionSize.depth;
    if(!std::isfinite(w)||!std::isfinite(d)||w<=0||d<=0)return false;
    std::array<double,3> lo{m.boundsMin[0],m.boundsMin[1],m.boundsMin[2]};
    std::array<double,3> hi{m.boundsMax[0],m.boundsMax[1],m.boundsMax[2]};
    for(size_t n=0;n<3;++n)
        if(!std::isfinite(lo[n])||!std::isfinite(hi[n])||lo[n]>hi[n])return false;
    const auto a=lo,c=hi;
    switch(i.placement.yaw){
        case 1:lo={d-c[2],a[1],a[0]};hi={d-a[2],c[1],c[0]};break;
        case 2:lo={w-c[0],a[1],d-c[2]};hi={w-a[0],c[1],d-a[2]};break;
        case 3:lo={a[2],a[1],w-c[0]};hi={c[2],c[1],w-a[0]};break;
        default:break;
    }
    const std::array<double,3> delta{
        eye.x.toDouble()-i.placement.origin.x.toDouble(),
        eye.y.toDouble()-i.placement.origin.y.toDouble(),
        eye.z.toDouble()-i.placement.origin.z.toDouble()};
    out=0;
    for(size_t n=0;n<3;++n){
        const double gap=std::max({lo[n]-delta[n],delta[n]-hi[n],0.0});
        out+=gap*gap;
    }
    return std::isfinite(out);
}
// Stable distance/ID priority independent of input order. A shared model
// is charged once, even when multiple placements need it. A zero budget
// is legal and lets the host pause uploads or visual draws.
template<class IsResident>
bool planBuildingStream(std::span<const BuildingPlacedInstance> input,Vec3 eye,
    const BuildingStreamPolicy& policy,const std::set<uint64_t>& previous,
    IsResident isResident,BuildingStreamPlan& out,std::string* error=nullptr){
    auto fail=[&](const char* s){if(error)*error=s;return false;};
    if(input.size()>65536||!validBuildingStreamPolicy(policy))
        return fail("Invalid building stream policy or candidate budget");
    struct Candidate {const BuildingPlacedInstance* instance;double distance;};
    std::vector<Candidate> candidates;candidates.reserve(input.size());
    std::set<uint64_t> ids;
    BuildingStreamPlan next;
    for(const auto& i:input){
        double distance=0;
        if(!i.resource||!validBuildingInstancePlacement(i.placement)||
            !ids.insert(i.placement.id).second||!buildingStreamDistanceSquared(i,eye,distance))
            return fail("Invalid or duplicate building stream candidate");
        const double radius=previous.contains(i.placement.id)?policy.exitDistance:policy.enterDistance;
        if(distance>radius*radius){++next.outside;continue;}
        candidates.push_back({&i,distance});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        if(a.distance!=b.distance)return a.distance<b.distance;
        return a.instance->placement.id<b.instance->placement.id;
    });
    std::map<std::string,bool> residency;
    for(const auto& c:candidates){
        const auto& key=c.instance->resource.modelKey;
        if(next.instances.size()>=policy.maxInstances){++next.deferred;continue;}
        if(!next.selectedModels.contains(key)){
            if(next.selectedModels.size()>=policy.maxModels){++next.deferred;continue;}
            auto it=residency.find(key);
            if(it==residency.end())it=residency.emplace(key,isResident(key)).first;
            if(!it->second&&next.newModels>=policy.maxNewModels){++next.deferred;continue;}
            if(!it->second)++next.newModels;
            next.selectedModels.insert(key);
        }
        next.instances.push_back(c.instance);
        next.selectedIds.insert(c.instance->placement.id);
    }
    out=std::move(next);if(error)error->clear();return true;
}
} // namespace aiverse::lib::systems
