#pragma once
// Native gameplay demand in simulation-anchor space, not orbit/view space.
// This only chooses complete chart cohorts; existing workers own meshing.
#include "engine/world/TerraWeaveCavernsPlanetRegionSet.h"
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include <span>

namespace aiverse::terraweave::caverns {
struct CavePlanetObserver {int body=-1;Vec3 simulationPosition{};};
struct CavePlanetDemandConfig {
    uint32_t resolution=8;
    int32_t spacingCm=0; // 0: choose a Q16 interior lattice per region
    int32_t enterCm=1000,leaveCm=1500;
    size_t maxRegions=8,maxVolumeSlots=64;
};
struct CavePlanetDemandRegion {
    uint64_t id=0;
    size_t slots=0;
    Fixed distance{};
    bool pinned=false;
};
struct CavePlanetDemand {
    std::map<std::string,std::vector<uint64_t>> bodies;
    std::vector<uint64_t> refused;
    size_t volumeSlots=0;
    bool pinnedOverflow=false;
};
// Resolve a full, bounded chart cohort. Auto spacing is intentionally
// conservative; callers requiring a finer lattice provide an explicit
// spacing and accept a budget refusal rather than an invisible LOD change.
inline CavePlanetDemandConfig resolvePlanetDemandLattice(const CavePlanetAnchor& anchor,
        CavePlanetDemandConfig cfg){
    if(!cfg.resolution||cfg.resolution>64||cfg.spacingCm<0||cfg.spacingCm>10000)
        throw std::invalid_argument("Caverns gameplay lattice config");
    if(!cfg.spacingCm){
        const int64_t cap=anchor.halfExtentCm;
        // The 25cm quantum is compatible with any base resolution.
        const int64_t spacing=25*(cap/(25*int64_t(cfg.resolution)+13));
        if(!spacing||spacing>10000)throw std::length_error("Caverns chart too small for auto lattice");
        cfg.spacingCm=int32_t(spacing);
    }
    return cfg;
}
// Authoritative sampling is never artificially clipped to the chart box.
// Leave a real gradient halo and refuse charts exceeding the native budget.
inline std::set<ChunkKey3,KeyLess> planetInteriorKeys(const CavePlanetAnchor& anchor,
        const CavePlanetDemandConfig& input){
    const auto cfg=resolvePlanetDemandLattice(anchor,input);
    if(!cfg.resolution||cfg.resolution>64||cfg.spacingCm<1||cfg.spacingCm>10000||
       !cfg.maxVolumeSlots||cfg.maxVolumeSlots>64)
        throw std::invalid_argument("Caverns gameplay lattice config");
    const int64_t span=int64_t(cfg.resolution)*cfg.spacingCm;
    if((span*Fixed::kOne)%100)throw std::invalid_argument("Caverns gameplay lattice Q16");
    const int64_t cap=anchor.halfExtentCm,halo=std::max<int64_t>(1,cfg.spacingCm/2);
    const int64_t half=(cap-halo)/span;
    if(half<1||half>2||uint64_t(8*half*half*half)>cfg.maxVolumeSlots)
        throw std::length_error("Caverns gameplay interior slot budget");
    std::set<ChunkKey3,KeyLess> keys;
    for(int64_t z=-half;z<half;++z)for(int64_t y=-half;y<half;++y)
        for(int64_t x=-half;x<half;++x)keys.insert({x,y,z});
    return keys;
}
inline CavePlanetDemand selectPlanetCaveDemand(const CavePlanetAuthoritySet& set,
        const std::shared_ptr<const game::Surface>& surface,
        std::span<const CavePlanetObserver> observers,const std::set<uint64_t>& resident,
        const std::set<uint64_t>& pins,const CavePlanetDemandConfig& cfg){
    if(!surface||observers.size()>128||pins.size()>kMaxPlanetCaveRegions||
       cfg.enterCm<0||cfg.leaveCm<cfg.enterCm||cfg.leaveCm>100000||
       !cfg.maxRegions||cfg.maxRegions>kMaxPlanetCaveRegions||
       !cfg.maxVolumeSlots||cfg.maxVolumeSlots>64)
        throw std::invalid_argument("Caverns planetary demand budget");
    validatePlanetCaveSet(set);
    struct Candidate{CavePlanetDemandRegion region;std::string body;};
    std::vector<Candidate> candidates;
    CavePlanetDemand out;
    std::set<uint64_t> known;
    for(const auto& entry:set.regions){
        known.insert(entry.id);
        const auto frame=CavePlanetFrame::make(surface,entry.region.anchor);
        const auto cap=Fixed::fromRatio(entry.region.anchor.halfExtentCm,100);
        const auto reach=Fixed::fromRatio(resident.contains(entry.id)?cfg.leaveCm:cfg.enterCm,100);
        const auto pinned=pins.contains(entry.id);
        auto best=Fixed::fromInt(100000);
        for(const auto& observer:observers){
            if(observer.body!=frame.body()||!game::Surface::onSphere(observer.simulationPosition)||
               surface->bodyAt(observer.simulationPosition)!=observer.body)continue;
            const auto rel=observer.simulationPosition-game::Surface::anchor(observer.body)-frame.originBody();
            // Bound before products to avoid overflow from invalid observers.
            if(fxAbs(rel.x)>Fixed::fromInt(4096)||fxAbs(rel.y)>Fixed::fromInt(4096)||
               fxAbs(rel.z)>Fixed::fromInt(4096))continue;
            const auto local=frame.vectorFromBody(rel);
            const auto distance=std::max({Fixed{},fxAbs(local.x)-cap,fxAbs(local.y)-cap,fxAbs(local.z)-cap});
            best=fxMin(best,distance);
        }
        if(!pinned&&best>reach)continue;
        try{
            const auto keys=planetInteriorKeys(entry.region.anchor,cfg);
            candidates.push_back({{entry.id,keys.size(),best,pinned},entry.region.anchor.bodyId});
        }catch(const std::length_error&){
            out.refused.push_back(entry.id);out.pinnedOverflow|=pinned;
        }
    }
    for(auto id:pins)if(!known.contains(id)){out.refused.push_back(id);out.pinnedOverflow=true;}
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        if(a.region.pinned!=b.region.pinned)return a.region.pinned>b.region.pinned;
        if(a.region.distance!=b.region.distance)return a.region.distance<b.region.distance;
        return a.region.id<b.region.id;
    });
    size_t count=0;
    for(const auto& c:candidates){
        if(count>=cfg.maxRegions||c.region.slots>cfg.maxVolumeSlots-out.volumeSlots){
            out.refused.push_back(c.region.id);out.pinnedOverflow|=c.region.pinned;continue;
        }
        out.bodies[c.body].push_back(c.region.id);out.volumeSlots+=c.region.slots;++count;
    }
    for(auto& [body,ids]:out.bodies){(void)body;std::sort(ids.begin(),ids.end());}
    std::sort(out.refused.begin(),out.refused.end());
    return out;
}
} // namespace aiverse::terraweave::caverns
