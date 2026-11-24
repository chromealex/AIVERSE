#pragma once
// Canonical derived navigation epoch, NOT a second environment simulation.
// Capture after PHYSIS/NEREID commits and before the existing cave repath.
#include "engine/world/TerraWeaveCavernsPlanetEcsNavigation.h"
#include "engine/core/Hash.h"
#include <algorithm>

namespace aiverse::terraweave::caverns {
struct CavePlanetEnvironmentEpoch {
    uint64_t revision=0,fingerprint=0,policyRevision=0;
    Fixed sphereSeconds{};
};
inline void io(Archive& a,CavePlanetEnvironmentEpoch& e){
    a(e.revision,e.fingerprint,e.policyRevision,e.sphereSeconds);
    if(!a.writing()&&(!e.revision||!e.policyRevision))a.fail();
}
namespace cave_environment_detail {
template<class T,class Feed>
inline void orderedComponents(const World& world,Hasher& h,Feed feed){
    std::vector<Entity> ids;
    world.eachExistingConst<T>([&](Entity e,const T&){if(world.alive(e))ids.push_back(e);});
    std::sort(ids.begin(),ids.end(),[](Entity a,Entity b){return a.packed()<b.packed();});
    h.u64(ids.size());
    for(auto e:ids){h.u64(e.packed());feed(e,*world.tryGetConst<T>(e));}
}
template<class T>
inline void archivedComponents(const World& world,Hasher& h){
    orderedComponents<T>(world,h,[&](Entity,const T& value){
        auto copy=value;Archive ar(h);ar(copy);
        if(!ar.ok())throw std::invalid_argument("Caverns invalid environment component");
    });
}
}
// Fingerprint exactly the sampled gravity/traction inputs and water
// passability owners. No PHYSIS clocks or unrelated actor/contact state.
// Some water component edits conservatively trigger extra replans.
inline uint64_t cavePlanetEnvironmentFingerprint(const World& world,uint64_t policyRevision,Fixed seconds){
    using namespace cave_environment_detail;
    using namespace lib::nereid;
    Hasher h;h.u64(policyRevision);h.fx(seconds);
    const physis::GravitySettings defaults;
    const auto* gravity=world.resourceConst<physis::GravitySettings>();
    const auto& gs=gravity?*gravity:defaults;
    h.v3(gs.baseline);h.fx(gs.maxAcceleration);
    orderedComponents<physis::GravityField>(world,h,[&](Entity,const auto& f){
        h.u32(uint32_t(f.mode));h.v3(f.center);h.fx(f.radiusMeters);
        h.v3(f.acceleration);h.fx(f.radialMagnitude);h.i32(f.priority);
        h.u32(f.overrideBaseline);h.u32(f.enabled);
    });
    const auto* state=world.resourceConst<physis::State>();
    h.u32(bool(state));
    if(state){
        const auto& cfg=state->settings;
        if(cfg.width<=0||cfg.height<=0||int64_t(cfg.width)*cfg.height>1048576||
           state->cells.size()!=size_t(cfg.width)*size_t(cfg.height)||cfg.cellSizeMeters<=0||
           int64_t(cfg.originX)+cfg.width>INT32_MAX||int64_t(cfg.originZ)+cfg.height>INT32_MAX)
            throw std::invalid_argument("Caverns PHYSIS grid");
        h.i32(cfg.originX);h.i32(cfg.originZ);h.i32(cfg.width);h.i32(cfg.height);h.i32(cfg.cellSizeMeters);
        for(int32_t z=0;z<cfg.height;++z)for(int32_t x=0;x<cfg.width;++x){
            physis::EnvironmentalResponse response;
            const bool found=physis::environmentAt(world,cfg.originX+x,cfg.originZ+z,response);
            h.u32(found);if(found)h.fx(response.tractionMultiplier);
        }
    }
    archivedComponents<WaterBodyComponent>(world,h);
    archivedComponents<WaterGeometryComponent>(world,h);
    archivedComponents<WaterRiverGradeComponent>(world,h);
    archivedComponents<WaterSphereComponent>(world,h);
    archivedComponents<WaterSphereWavesComponent>(world,h);
    const auto* flood=world.resourceConst<WaterFloodResource>();
    h.u32(bool(flood));
    if(flood){
        h.i32(flood->gridCm);h.i64(flood->originXcm);h.i64(flood->originZcm);
        h.u64(flood->cells.size());
        for(const auto& c:flood->cells){
            h.i32(c.x);h.i32(c.z);h.i32(c.groundCm);h.u32(c.volumeCm);h.u64(c.waterBody.packed());
        }
    }
    return h.value();
}
inline CavePlanetEnvironmentEpoch advanceCavePlanetEnvironment(World& world,uint64_t policyRevision,Fixed seconds){
    if(!policyRevision||seconds<Fixed{})throw std::invalid_argument("Caverns environment epoch policy");
    bool waves=false;
    world.eachExistingConst<lib::nereid::WaterSphereWavesComponent>([&](Entity e,const auto& w){
        const auto* body=world.tryGetConst<lib::nereid::WaterSphereComponent>(e);
        waves|=body&&lib::nereid::validWaterSphere(*body)&&w.amplitude>Fixed{}&&
            lib::nereid::validWaterSphereWaves(w);
    });
    if(!waves)seconds={};
    const auto fingerprint=cavePlanetEnvironmentFingerprint(world,policyRevision,seconds);
    const auto* old=world.resourceConst<CavePlanetEnvironmentEpoch>();
    if(old&&old->revision&&old->fingerprint==fingerprint&&old->policyRevision==policyRevision)return *old;
    if(old&&old->revision==UINT64_MAX)throw std::overflow_error("Caverns environment epoch exhausted");
    CavePlanetEnvironmentEpoch next{old?old->revision+1:1,fingerprint,policyRevision,seconds};
    world.setResource<CavePlanetEnvironmentEpoch>(next);return next;
}
// Uses the actual PHYSIS flat cell grid in simulation-anchor coordinates.
// Planet-specific PHYSIS cell owners can supply a different explicit mapper;
// never infer that chart X/Z are global PHYSIS coordinates.
inline CavePlanetNavigationInputSource canonicalCavePlanetNavigationInputs(
        std::shared_ptr<const game::Surface> surface,uint64_t policyRevision,
        CaveEnvironmentCostSettings settings={}){
    if(!surface||!policyRevision)throw std::invalid_argument("Caverns environment input config");
    return [surface=std::move(surface),policyRevision,settings](const World& world,uint64_t id)
        ->std::optional<CavePlanetNavigationInputs>{
        const auto* epoch=world.resourceConst<CavePlanetEnvironmentEpoch>();
        const auto* entry=findPlanetCaveRegion(world,id);
        if(!epoch||!epoch->revision||epoch->policyRevision!=policyRevision||!entry)return {};
        const auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
        const auto* state=world.resourceConst<physis::State>();
        const int32_t cell=state?state->settings.cellSizeMeters:0;
        CavePlanetNavigationInputs input;input.environmentRevision=epoch->revision;
        input.sphereSeconds=epoch->sphereSeconds;input.settings=settings;
        input.physisCell=[frame,cell](PointCm p)->std::optional<std::pair<int32_t,int32_t>>{
            if(cell<=0||!frame.contains(p))return {};
            const auto pos=frame.toSimulation(CavePlanetFrame::metres(p));
            const auto x=physis::floorDiv(pos.x.floorInt(),cell);
            const auto z=physis::floorDiv(pos.z.floorInt(),cell);
            if(x<INT32_MIN||x>INT32_MAX||z<INT32_MIN||z>INT32_MAX)return {};
            return std::pair{int32_t(x),int32_t(z)};
        };
        return input;
    };
}
} // namespace aiverse::terraweave::caverns
