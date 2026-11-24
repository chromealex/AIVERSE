// Opt-in spherical root support projection into the EXISTING TerraWeave
// authoritative rooted records. The owner supplies persistent placement
// identities; planar x/z in RootAnchor are never interpreted as planet coords.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/core/FixedMath.h"
#include "engine/physis/PhysisGravity.h"
#include <map>
#include <set>
#include <tuple>
#include <optional>
#include <span>

namespace aiverse::evolve {
struct PlanetRootBinding {
    terraweave::AnchoredPlantId id{};
    std::string bodyId;
    Vec3 plantedPosition{}; // historic root crown, canonical simulation metres
};
struct PlanetPlantFall {
    terraweave::PlantFallEvent event;
    std::string bodyId;
    Vec3 plantedPosition{},tangentDirection{},gravity{};
};
struct PlanetRootSupportOutcome {
    terraweave::PlantSupportUpdate support;
    std::vector<PlanetPlantFall> falls;
    uint64_t authorityRevision=0;
};
inline constexpr size_t kMaxPlanetRootBindings=4096;
inline constexpr size_t kMaxPlanetPlantFallEvents=16384;
struct PlanetPlantFallRecord {
    PlanetPlantFall fall;
    bool acknowledged=false;
};
struct PlanetRootDeliveryResource {
    // Existing TerraWeave owns rooted support/fallen; this resource stores
    // only spherical placement bindings and durable delivery receipts.
    std::vector<PlanetRootBinding> bindings;
    std::vector<PlanetPlantFallRecord> events;
};
inline auto planetFallKey(const PlanetPlantFall& f){
    return std::tuple{f.event.id.speciesId,f.event.id.key,f.event.tick,f.event.sourceSoilRevision};
}
inline bool validPlanetPlantFall(const PlanetPlantFall& f){
    const auto& e=f.event;
    return e.id.speciesId&&e.id.key&&!f.bodyId.empty()&&f.bodyId.size()<=128&&
        e.habit!=terraweave::PlantHabit::Grass&&e.habit<=terraweave::PlantHabit::Grass&&
        e.supportPermille<=1000&&e.exposedCm<=10000&&e.sourceSoilRevision&&e.tick<UINT32_MAX&&
        e.fallDirectionXPermille>=-1000&&e.fallDirectionXPermille<=1000&&
        e.fallDirectionZPermille>=-1000&&e.fallDirectionZPermille<=1000;
}
inline void io(Archive& a,PlanetRootBinding& b){
    a(b.id.speciesId,b.id.key,b.bodyId,b.plantedPosition);
    if(!b.id.speciesId||!b.id.key||b.bodyId.empty()||b.bodyId.size()>128)a.fail();
}
inline void io(Archive& a,PlanetPlantFallRecord& r){
    auto& f=r.fall;auto& e=f.event;
    a(e.id.speciesId,e.id.key,e.habit,e.xCm,e.zCm,e.plantedHeightCm,
      e.fallDirectionXPermille,e.fallDirectionZPermille,e.supportPermille,e.exposedCm,
      e.sourceSoilRevision,e.tick,f.bodyId,f.plantedPosition,f.tangentDirection,f.gravity,r.acknowledged);
    if(!validPlanetPlantFall(f))a.fail();
}
inline void io(Archive& a,PlanetRootDeliveryResource& r){
    auto bindings=a.count(r.bindings.size(),40);
    if(!a.ok()||bindings>kMaxPlanetRootBindings){a.fail();return;}
    if(!a.writing())r.bindings.resize(bindings);
    for(auto& b:r.bindings)a(b);
    auto events=a.count(r.events.size(),80);
    if(!a.ok()||events>kMaxPlanetPlantFallEvents){a.fail();return;}
    if(!a.writing())r.events.resize(events);
    for(auto& e:r.events)a(e);
    for(size_t i=1;i<r.bindings.size();++i)
        if(!(r.bindings[i-1].id<r.bindings[i].id))a.fail();
    for(size_t i=1;i<r.events.size();++i)
        if(!(planetFallKey(r.events[i-1].fall)<planetFallKey(r.events[i].fall)))a.fail();
}
inline void registerPlanetRootDeliveryPersistence(Simulation& sim){
    sim.registerPersistentResource<PlanetRootDeliveryResource>("evolve.planet_root_delivery",1);
    if(!sim.world().resourceConst<PlanetRootDeliveryResource>())
        sim.world().setResource<PlanetRootDeliveryResource>();
}
// Owner registers the complete authoritative rooted placement population.
// Updating mappings never clears unacknowledged events or tombstones.
inline bool setPlanetRootBindings(World& world,const game::Surface& surface,
        std::span<const PlanetRootBinding> bindings){
    const auto* delivery=world.resourceConst<PlanetRootDeliveryResource>();
    const auto* authority=world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    if(!delivery||!authority||bindings.size()>kMaxPlanetRootBindings||
       bindings.size()!=authority->rooted.size())return false;
    auto next=*delivery;next.bindings.assign(bindings.begin(),bindings.end());
    std::sort(next.bindings.begin(),next.bindings.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::set<terraweave::AnchoredPlantId> expected;
    for(const auto& plant:authority->rooted)
        if(!expected.insert({plant.anchor.speciesId,plant.anchor.plantKey}).second)return false;
    for(const auto& binding:next.bindings){
        const auto frame=lib::worldprocess::surfaceFrame(&surface,binding.plantedPosition);
        if(!expected.erase(binding.id)||!frame||
           surface.bodies[size_t(frame->body)].id!=binding.bodyId)return false;
    }
    if(!expected.empty())return false;
    world.setResource<PlanetRootDeliveryResource>(std::move(next));return true;
}
inline bool appendPlanetPlantFalls(PlanetRootDeliveryResource& delivery,
        std::span<const PlanetPlantFall> falls){
    if(delivery.events.size()>kMaxPlanetPlantFallEvents||
       falls.size()>kMaxPlanetPlantFallEvents-delivery.events.size())return false;
    auto staged=delivery.events;
    for(const auto& fall:falls){
        if(!validPlanetPlantFall(fall))return false;
        staged.push_back({fall,false});
    }
    std::sort(staged.begin(),staged.end(),[](const auto& a,const auto& b){return planetFallKey(a.fall)<planetFallKey(b.fall);});
    for(size_t i=1;i<staged.size();++i)
        if(planetFallKey(staged[i-1].fall)==planetFallKey(staged[i].fall))return false;
    delivery.events=std::move(staged);return true;
}
inline std::vector<PlanetPlantFall> pendingPlanetPlantFalls(const World& world){
    std::vector<PlanetPlantFall> result;
    if(const auto* r=world.resourceConst<PlanetRootDeliveryResource>())
        for(const auto& e:r->events)if(!e.acknowledged)result.push_back(e.fall);
    return result;
}
// Commit the actual owner's durable physical consequence in the same
// World transaction as the acknowledgement. No native handles persisted.
template<class StageOwner>
inline bool commitPlanetPlantFall(World& world,const PlanetPlantFall& requested,
        uint32_t completedTick,StageOwner&& stageOwner){
    if(completedTick==UINT32_MAX||requested.event.tick>completedTick)return false;
    const auto* source=world.resourceConst<PlanetRootDeliveryResource>();
    if(!source)return false;
    const auto key=planetFallKey(requested);
    const auto find=[&](const PlanetRootDeliveryResource& resource){
        return std::lower_bound(resource.events.begin(),resource.events.end(),key,
            [](const auto& r,const auto& k){return planetFallKey(r.fall)<k;});
    };
    const auto found=find(*source);
    if(found==source->events.end()||planetFallKey(found->fall)!=key||found->acknowledged)return false;
    const auto event=found->fall;
    World staged=world;
    if(!std::forward<StageOwner>(stageOwner)(staged,event))return false;
    const auto* updated=staged.resourceConst<PlanetRootDeliveryResource>();
    if(!updated)return false;
    auto next=*updated;const auto receipt=find(next);
    if(receipt==next.events.end()||planetFallKey(receipt->fall)!=key||receipt->acknowledged)return false;
    next.events[size_t(receipt-next.events.begin())].acknowledged=true;
    staged.setResource<PlanetRootDeliveryResource>(std::move(next));
    world=std::move(staged);return true;
}
inline std::optional<terraweave::RootState> evaluatePlanetRootSupport(
        const game::Surface& surface,const terraweave::AnchoredPlant& plant,
        const PlanetRootBinding& binding){
    using namespace terraweave;
    if(binding.id!=AnchoredPlantId{plant.anchor.speciesId,plant.anchor.plantKey})return {};
    const auto frame=lib::worldprocess::surfaceFrame(&surface,binding.plantedPosition);
    if(!frame||surface.bodies[size_t(frame->body)].id!=binding.bodyId)return {};
    const auto radius=surface.bodies[size_t(frame->body)].radius;
    if(radius<=Fixed{}||Fixed::fromRatio(plant.anchor.roots.radiusCm,100)>=radius*game::kFxTwoPi/4)return {};
    // Preserve the historic planting level, not current height. Use a
    // tangent-local chart only for the existing 8-point root support policy.
    auto local=plant.anchor;local.xCm=local.zCm=0;
    const auto historic=surface.altitude(binding.plantedPosition);
    const auto heightCm=[](Fixed height)->int32_t{
        const auto raw=height.raw*100;
        return int32_t(raw>=0?(raw+Fixed::kOne/2)/Fixed::kOne:-((-raw+Fixed::kOne/2)/Fixed::kOne));
    };
    if(heightCm(historic)!=local.plantedHeightCm)return {};
    const auto origin=game::Surface::anchor(frame->body);
    return evaluateRootSupport(local,[&](int64_t x,int64_t z)->std::optional<int32_t>{
        const auto tangent=frame->east*Fixed::fromRatio(x,100)+frame->south*Fixed::fromRatio(z,100);
        const auto distance=tangent.length();
        const auto turns=distance/(radius*game::kFxTwoPi);
        if(turns>=Fixed::fromRatio(1,4))return {};
        const auto dir=distance.raw>0?fxNormalize(frame->up*fxCosTurns(turns)+
            (tangent/distance)*fxSinTurns(turns)):frame->up;
        const auto sample=origin+dir*radius;
        const auto resolved=lib::worldprocess::surfaceFrame(&surface,sample);
        if(!resolved||resolved->body!=frame->body)return {};
        return heightCm(surface.heightAt(sample));
    });
}

// Called on staged World after a terrain edit, before physics publication.
// Existing TerraWeave rooted records retain support/fallen identity. The
// supplied mapping is explicit owner input; never run planar root/ecology
// systems over the same spherical records. No new authoritative plant store.
// Fall outputs are owner events: record them durably before publishing bodies.
inline std::optional<PlanetRootSupportOutcome> reconcilePlanetRoots(
        World& world,const game::Surface& surface,
        std::span<const PlanetRootBinding> bindings,uint64_t terrainRevision,uint32_t tick){
    using namespace terraweave;
    if(bindings.size()>kMaxPlanetRootBindings||tick==UINT32_MAX)return {};
    const auto* authority=world.resourceConst<TerraWeaveAuthorityResource>();
    if(bindings.empty())return PlanetRootSupportOutcome{{},{},authority?authority->stateRevision:0};
    if(!authority||!terrainRevision)return {};
    RootSupportRegistry roots;roots.setSnapshot(authority->rooted);
    std::map<AnchoredPlantId,PlanetRootBinding> mapped;
    std::map<AnchoredPlantId,RootState> states;
    for(const auto& binding:bindings){
        const auto* plant=roots.find(binding.id);
        if(!plant||!mapped.emplace(binding.id,binding).second)return {};
        const auto state=evaluatePlanetRootSupport(surface,*plant,binding);
        if(!state)return {};
        states.emplace(binding.id,*state);
    }
    PlanetRootSupportOutcome result;result.authorityRevision=authority->stateRevision;
    result.support=roots.applySelectedSupport([&](const AnchoredPlant& plant)->std::optional<RootState>{
        const auto it=states.find({plant.anchor.speciesId,plant.anchor.plantKey});
        if(it==states.end())return {};
        return it->second;
    },terrainRevision,tick);
    for(const auto& event:result.support.fell){
        const auto& binding=mapped.at(event.id);
        const auto frame=lib::worldprocess::surfaceFrame(&surface,binding.plantedPosition);
        if(!frame)return {};
        const auto tangent=fxNormalize(frame->east*Fixed::fromRatio(event.fallDirectionXPermille,1000)+
            frame->south*Fixed::fromRatio(event.fallDirectionZPermille,1000));
        result.falls.push_back({event,binding.bodyId,binding.plantedPosition,tangent,
            physis::gravityAt(world,binding.plantedPosition)});
    }
    if(!result.support.changed.empty()){
        if(authority->stateRevision==UINT64_MAX)return {};
        auto updated=*authority;updated.rooted=roots.snapshot();
        result.authorityRevision=++updated.stateRevision;
        World staged=world;
        if(const auto* outbox=world.resourceConst<PlanetRootDeliveryResource>()){
            auto delivery=*outbox;
            if(!appendPlanetPlantFalls(delivery,result.falls))return {};
            staged.setResource<PlanetRootDeliveryResource>(std::move(delivery));
        }
        staged.setResource<TerraWeaveAuthorityResource>(std::move(updated));
        world=std::move(staged);
    }
    return result;
}
} // namespace aiverse::evolve
