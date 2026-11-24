#pragma once
// Stage42: native nav::Mesh query backend over the same immutable Cave
// NavigationDataProvider as 3D graph solvers. This is a bounded on-demand
// adapter, not a second NavMesh implementation. Production hosts can
// cache/build one nav::Mesh per published source epoch and refresh dirty tiles.
#include "engine/world/TerraWeaveCavernsNavigationAdapters.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::terraweave::caverns {
inline CavePathResult findCaveNativeNavPath(
    const CavePathQuery& query,
    std::shared_ptr<const CaveNavigationDataProvider> provider,
    const CaveNavConfig& config) {
    CavePathResult result;result.sourceRevision=query.sourceRevision;
    if(!provider||provider->revision()!=query.sourceRevision||
       (query.agent.traversal!=CaveTraversal::Ground &&
        query.agent.traversal!=CaveTraversal::Climbing))
        return result;
    try {
        CaveSharedNavSource source(provider,config,query.agent);
        nav::Mesh mesh;
        std::string error;
        if(!mesh.build(source,caveNavMeshConfig(config),&error))
            return result;
        nav::Query options;
        options.maxVisits=query.maxVisits;
        std::vector<Vec3> path;
        const Vec3 from{
            Fixed::fromRatio(query.start.x,100),
            Fixed::fromRatio(query.start.y,100),
            Fixed::fromRatio(query.start.z,100)};
        const Vec3 to{
            Fixed::fromRatio(query.goal.x,100),
            Fixed::fromRatio(query.goal.y,100),
            Fixed::fromRatio(query.goal.z,100)};
        if(!mesh.findPath(from,to,options,path))return result;
        result.reached=true;
        result.waypoints.reserve(path.size());
        for(const Vec3& p:path){
            result.waypoints.push_back({
                p.x.raw*100/Fixed::kOne,
                p.y.raw*100/Fixed::kOne,
                p.z.raw*100/Fixed::kOne});
        }
        return result;
    }catch(const std::exception&) {
        return result;
    }
}
inline bool registerCaveNativeNavSolver(
    CavePathRouter& router,CaveNavConfig config,
    const std::string& name="navmesh"){
    validateCaveNav(config);
    return router.registerSolver(name,CavePathBackend::NavMesh,
        [config](const CavePathQuery& query,
                 std::shared_ptr<const CaveNavigationDataProvider> provider){
            return findCaveNativeNavPath(query,std::move(provider),config);
        });
}
} // namespace aiverse::terraweave::caverns
