// Opt-in spherical root support projection into the EXISTING TerraWeave
// authoritative rooted records. The owner supplies persistent placement
// identities; planar x/z in RootAnchor are never interpreted as planet coords.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/core/FixedMath.h"
#include "engine/physis/PhysisGravity.h"
#include <map>
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
        world.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    }
    return result;
}
} // namespace aiverse::evolve
