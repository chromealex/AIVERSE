#pragma once
// Deterministic planetary ECS navigation adapter. Native GPU/physics epochs
// are deliberately absent from persistent route state and solver selection.
#include "engine/world/TerraWeaveCavernsPlanetNavigation.h"
#include "engine/nav/CaveEcsSteering.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"

namespace aiverse::terraweave::caverns {
// Config is deterministic gameplay configuration, not observer demand.
// Capture once per simulation boundary and reuse the immutable providers.
inline std::shared_ptr<const CaveNavigationDataProvider> captureCavePlanetAuthorityNavigation(
        const std::shared_ptr<const game::Surface>& surface,std::shared_ptr<const World> frozen,
        uint64_t regionId,uint64_t environmentEpoch,const CavePlanetDemandConfig& lattice,
        CavePhysisCellMapper mapper,CaveEnvironmentCostSettings settings={},
        CaveNavigationCallbacks policies={}){
    if(!surface||!frozen||!regionId||!environmentEpoch||!mapper)return {};
    const auto* entry=findPlanetCaveRegion(*frozen,regionId);
    if(!entry)return {};
    const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
    const auto keys=planetInteriorKeys(frame.anchor(),lattice);
    const auto coverage=CavePlanetNavigationCoverage::certify(frame.anchor(),lattice,keys);
    auto index=std::make_shared<const CaveSnapshotSpatialIndex>(freezePlanetCaveRegion(*frozen,regionId));
    return makeResidentCavePlanetNavigation(frame,std::move(index),std::move(frozen),
        environmentEpoch,coverage,std::move(mapper),settings,std::move(policies),regionId);
}
using CaveSimulationPosition=std::function<std::optional<Vec3>(const World&,Entity)>;
inline nav::CaveWorldPositionSource cavePlanetChartPosition(
        std::shared_ptr<const game::Surface> surface,CaveSimulationPosition position){
    if(!surface||!position)throw std::invalid_argument("Caverns planet position source");
    return [surface=std::move(surface),position=std::move(position)](const World& ecs,Entity e)
        ->std::optional<PointCm>{
        const auto* entry=findPlanetCaveRegion(ecs,nav::caveRouteScope(ecs,e));
        const auto p=position(ecs,e);
        if(!entry||!p)return {};
        try{
            const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
            return CavePlanetFrame::centimetres(frame.fromSimulation(*p));
        }catch(const std::exception&){return {};}
    };
}
// Authorized gameplay command only. New scope replaces old route and request.
inline bool assignCavePlanetMotorGoal(World& ecs,Entity e,
        const std::shared_ptr<const game::Surface>& surface,uint64_t regionId,Vec3 simulationGoal,
        CaveAgentProfile profile,std::string algorithm="astar",uint32_t speedCmPerTick=25,
        uint32_t maxVisits=8192){
    const auto* entry=findPlanetCaveRegion(ecs,regionId);
    if(!entry||!surface)return false;
    PointCm goal;
    try{
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        goal=CavePlanetFrame::centimetres(frame.fromSimulation(simulationGoal));
    }catch(const std::exception&){return false;}
    return nav::assignScopedCaveMotorGoal(ecs,e,regionId,goal,profile,
        std::move(algorithm),speedCmPerTick,maxVisits);
}
inline nav::CaveScopedRouteSearch cavePlanetRouteSearch(std::shared_ptr<const CavePathRouter> router){
    if(!router)throw std::invalid_argument("Caverns planet path router");
    return [router=std::move(router)](const std::string& algorithm,const CavePathQuery& query,
        std::shared_ptr<const CaveNavigationDataProvider> source){
        return router->queryWithProvider(algorithm,query,std::move(source));
    };
}
} // namespace aiverse::terraweave::caverns
