// CROWD -> existing genre-independent NavMesh path planner.
// Native project owns the nav::Mesh and TerraWeave Earthworks ground; the
// deterministic ECS holds only quantized route points and route cursor.
#pragma once
#include "aiverselib/world/CrowdRoutes.h"
#include "engine/nav/NavMesh.h"
#include "engine/world/TerraWeaveEarthworks.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct CrowdNavOptions {
    std::size_t maxVisits=8192;
    std::int64_t arrivalRadiusCm=20;
    std::uint32_t revision=1;
};
inline bool valid(const CrowdNavOptions& options){
    return options.maxVisits>0&&options.maxVisits<=65536&&
           options.arrivalRadiusCm>=0&&
           options.arrivalRadiusCm<=crowd::kMaxRadius;
}
inline bool centimetres(Fixed f,std::int64_t& cm){
    // Fixed raw is quantized, and CROWD stores integer centimetres. The
    // conversion occurs only at the route-install barrier, never per frame.
    const std::int64_t base=f.raw/Fixed::kOne;
    const std::int64_t residue=f.raw%Fixed::kOne;
    if(base< -crowd::kCoord/100||base>crowd::kCoord/100)return false;
    cm=base*100+(residue*100)/Fixed::kOne;
    return cm>=-crowd::kCoord&&cm<=crowd::kCoord;
}
inline bool planCrowdNavRoute(
        World& ecs,const nav::Mesh& mesh,
        const terraweave::EarthworksLayer& ground,
        Entity agent,std::int64_t targetXcm,std::int64_t targetZcm,
        CrowdNavOptions options={},std::string* why=nullptr){
    const auto fail=[&](const char* reason){
        if(why)*why=reason;return false;
    };
    if(!valid(options)||!mesh.built()||!ecs.alive(agent)||
       targetXcm< -crowd::kCoord||targetXcm>crowd::kCoord||
       targetZcm< -crowd::kCoord||targetZcm>crowd::kCoord)return fail("invalid route target/mesh");
    const auto* a=static_cast<const World&>(ecs).tryGetConst<crowd::Agent>(agent);
    if(!a||!a->enabled||!crowd::valid(*a))return fail("missing CROWD agent");
    const auto start=Vec3{
        Fixed::fromRatio(a->x,100),
        Fixed::fromRatio(ground.sampleHeightCm(a->x,a->z),100),
        Fixed::fromRatio(a->z,100)
    };
    const auto goal=Vec3{
        Fixed::fromRatio(targetXcm,100),
        Fixed::fromRatio(ground.sampleHeightCm(targetXcm,targetZcm),100),
        Fixed::fromRatio(targetZcm,100)
    };
    nav::Query query;
    query.maxVisits=options.maxVisits;
    std::vector<Vec3> path;
    if(!mesh.findPath(start,goal,query,path)||path.empty()||
       path.size()>crowd::kMaxRoutePoints+1)
        return fail("NavMesh route not found or over budget");
    std::vector<crowd::Waypoint> converted;
    converted.reserve(path.size());
    for(const auto& p:path){
        crowd::Waypoint next;
        if(!centimetres(p.x,next.x)||!centimetres(p.z,next.z)||
           !crowd::valid(next))
            return fail("NavMesh waypoint outside quantized CROWD bounds");
        if(!converted.empty()&&converted.back().x==next.x&&
           converted.back().z==next.z)continue;
        converted.push_back(next);
    }
    if(converted.empty())return fail("empty quantized NavMesh route");
    // The authoritative host records the requested target and nav revision.
    // The navmesh itself stays the one owned by TerraWeave/engine/nav.
    return crowd::assignRoute(ecs,agent,std::move(converted),
                             options.revision,options.arrivalRadiusCm);
}
} // namespace aiverse::examples::world_processes_native
