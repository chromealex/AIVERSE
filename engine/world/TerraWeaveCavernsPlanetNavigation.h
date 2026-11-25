#pragma once
// Stage54v2a: immutable chart coverage for the shared navigation providers.
// This is a native publication adapter, not a pathfinder or physics owner.
#include "engine/world/TerraWeaveCavernsPlanetQueries.h"
#include "engine/world/TerraWeaveCavernsPlanetDemand.h"
#include "engine/world/TerraWeaveCavernsNavigationEnvironment.h"

namespace aiverse::terraweave::caverns {
class CavePlanetNavigationCoverage {
public:
    static CavePlanetNavigationCoverage certify(const CavePlanetAnchor& anchor,
            const CavePlanetDemandConfig& input,const std::set<ChunkKey3,KeyLess>& keys){
        const auto cfg=resolvePlanetDemandLattice(anchor,input);
        const auto expected=planetInteriorKeys(anchor,cfg);
        // Compare using the actual key comparator, including empty mesh slots.
        if(keys.size()!=expected.size())throw std::invalid_argument("Caverns navigation incomplete lattice");
        for(const auto& key:expected)if(!keys.contains(key))
            throw std::invalid_argument("Caverns navigation missing lattice slot");
        const int64_t span=int64_t(cfg.resolution)*cfg.spacingCm;
        int64_t low=0;
        for(const auto& key:keys)low=std::min({low,key.x,key.y,key.z});
        return CavePlanetNavigationCoverage(-low*span);
    }
    bool contains(PointCm lo,PointCm hi)const noexcept{
        // Inclusive query bounds must lie inside the complete sampled cohort;
        // leave the outer boundary itself to the baseline/adjacent owner.
        return lo.x<=hi.x&&lo.y<=hi.y&&lo.z<=hi.z&&
            lo.x> -halfExtentCm_&&lo.y> -halfExtentCm_&&lo.z> -halfExtentCm_&&
            hi.x<halfExtentCm_&&hi.y<halfExtentCm_&&hi.z<halfExtentCm_;
    }
    int64_t halfExtentCm()const noexcept{return halfExtentCm_;}
private:
    explicit CavePlanetNavigationCoverage(int64_t cap):halfExtentCm_(cap){}
    int64_t halfExtentCm_;
};
// Lease carries both native epoch and restoration generation; the SDF and
// environment revisions remain those of the existing provider. A retained
// lease can be queried off-thread, but a host must reject stale results.
struct CavePlanetNavigationLease {
    uint64_t regionId=0,generation=0,nativeRevision=0;
    std::shared_ptr<const CaveNavigationDataProvider> provider;
    bool matches(uint64_t region,uint64_t epoch,uint64_t native,
            uint64_t source,uint64_t environment)const noexcept{
        return provider&&provider->scopeId()==region&&regionId==region&&generation==epoch&&nativeRevision==native&&
            provider->revision()==source&&provider->environmentRevision()==environment;
    }
};
// Host supplies the canonical environment epoch and immutable callbacks.
// The epoch changes when PHYSIS/NEREID or policy/mapping changes. Never use
// wall-clock time, GPU completion or native revision as a lockstep epoch.
struct CavePlanetNavigationInputs {
    uint64_t environmentRevision=0;
    CavePhysisCellMapper physisCell;
    CaveEnvironmentCostSettings settings;
    CaveNavigationCallbacks policies;
    Fixed sphereSeconds{};
};
using CavePlanetNavigationInputSource=std::function<std::optional<CavePlanetNavigationInputs>(const World&,uint64_t)>;
using CavePlanetNavigationLeaseSet=std::map<uint64_t,CavePlanetNavigationLease>;
inline std::shared_ptr<const CaveNavigationDataProvider> makeResidentCavePlanetNavigation(
        CavePlanetFrame frame,std::shared_ptr<const CaveSnapshotSpatialIndex> index,
        std::shared_ptr<const World> frozen,uint64_t environmentRevision,
        CavePlanetNavigationCoverage coverage,CavePhysisCellMapper mapper,
        CaveEnvironmentCostSettings settings={},CaveNavigationCallbacks policies={},uint64_t scopeId=0,Fixed sphereSeconds={}){
    if(!index||!frozen||!environmentRevision||!mapper||policies.submerged||policies.traversalCost)
        throw std::invalid_argument("Caverns planetary navigation environment owner");
    CaveEnvironmentCoordinates coords;
    coords.waterPosition=[frame](PointCm p){return frame.toSimulation(CavePlanetFrame::metres(p));};
    coords.physisCell=std::move(mapper);
    coords.sphereSeconds=sphereSeconds;
    auto environment=makeCaveEnvironmentCallbacks({index->revision(),0,frozen},settings,std::move(coords));
    policies.submerged=std::move(environment.submerged);
    policies.traversalCost=std::move(environment.traversalCost);
    return makeCavePlanetNavigationProvider(std::move(frame),std::move(index),std::move(frozen),
        environmentRevision,std::move(policies),[coverage](PointCm lo,PointCm hi){
            return coverage.contains(lo,hi);
        },scopeId);
}
} // namespace aiverse::terraweave::caverns
