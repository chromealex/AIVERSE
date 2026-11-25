// Natural mortality uses the existing TerraWeave root transition and EVOLVE fall outbox.
// No separate plant authority, physics integration or organic-mass ledger.
#pragma once
#include "EvolvePlanetDeadwood.h"
#include "PlanetConsequenceOwner.h"
namespace aiverse::evolve {
// Opt-in policy: standing dead wood topples once it reaches the configured
// remaining mass fraction. Fully spent wood needs no physical consequence.
inline std::optional<PlanetRootSupportOutcome> reconcilePlanetNaturalFalls(
        World& world,const game::Surface& surface,uint32_t tick,uint32_t remainingThreshold){
    using namespace terraweave;
    namespace wp=lib::worldprocess;
    const auto* authority=world.resourceConst<TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<PlanetRootDeliveryResource>();
    const auto* ecology=world.resourceConst<ForestEcologyResource>();
    const auto* wood=world.resourceConst<PlanetDeadwoodResource>();
    if(!authority||!delivery||!ecology||!wood||!surface.tab||tick==UINT32_MAX||
       remainingThreshold>1000||authority->rooted.size()!=delivery->bindings.size()||
       authority->rooted.size()>kMaxPlanetRootBindings||!validPlanetDeadwood(*wood,surface))return {};
    PlanetRootSupportOutcome result;result.authorityRevision=authority->stateRevision;
    if(!remainingThreshold)return result;
    try{
        validateForestEcologyResource(*ecology);
        RootSupportRegistry roots;roots.setSnapshot(authority->rooted);
        std::map<AnchoredPlantId,const PlanetRootBinding*> bindings;
        for(const auto& b:delivery->bindings)
            if(!roots.find(b.id)||!bindings.emplace(b.id,&b).second)return {};
        std::map<AnchoredPlantId,RootState> candidates;
        for(const auto& history:ecology->plants){
            if(!history.dead)continue;
            const auto* plant=roots.find(history.id);
            if(!plant||plant->fallen||plant->anchor.roots.habit==PlantHabit::Grass)continue;
            const auto it=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),history.id,
                [](const auto& record,auto id){return record.id<id;});
            if(it==wood->ledger.logs.end()||it->id!=history.id||!it->remainingUnits||!it->initialUnits)continue;
            // Exact threshold without multiplying a potentially large uint64.
            const uint64_t limit=(it->initialUnits/1000)*remainingThreshold+
                (it->initialUnits%1000)*remainingThreshold/1000;
            if(it->remainingUnits>limit)continue;
            const auto& binding=*bindings.at(history.id);
            const auto frame=wp::surfaceFrame(&surface,binding.plantedPosition);
            if(!frame||surface.bodies[size_t(frame->body)].id!=binding.bodyId)return {};
            auto state=plant->support;
            state.plantKey=history.id.key;state.speciesId=history.id.speciesId;
            state.habit=plant->anchor.roots.habit;state.plantedHeightCm=plant->anchor.plantedHeightCm;
            state.supportPermille=0;state.uprooted=true;
            // Prefer actual tangential acceleration. Symmetric/radial/zero
            // gravity leaves zero direction for the existing stable azimuth.
            const auto gravity=physis::gravityAt(world,binding.plantedPosition);
            const auto tangent=gravity-frame->up*gravity.dot(frame->up);
            state.fallDirectionXPermille=state.fallDirectionZPermille=0;
            if(tangent.lengthSq()>Fixed::fromRatio(1,10000)){
                const auto direction=fxNormalize(tangent);
                state.fallDirectionXPermille=int32_t(std::clamp<int64_t>((direction.dot(frame->east)*1000).toInt(),-1000,1000));
                state.fallDirectionZPermille=int32_t(std::clamp<int64_t>((direction.dot(frame->south)*1000).toInt(),-1000,1000));
            }
            candidates.emplace(history.id,state);
        }
        if(candidates.empty())return result;
        if(authority->stateRevision==UINT64_MAX)return {};
        // sourceSoilRevision is the legacy fall receipt field. For natural
        // mortality it carries the upcoming rooted authority revision, not
        // a fictitious terrain edit; tick and identity still scope the key.
        const auto revision=authority->stateRevision+1;
        result.support=roots.applySelectedSupport([&](const AnchoredPlant& plant)->std::optional<RootState>{
            const auto it=candidates.find({plant.anchor.speciesId,plant.anchor.plantKey});
            if(it==candidates.end())return {};
            return it->second;
        },revision,tick);
        if(result.support.fell.size()!=candidates.size())return {};
        for(const auto& event:result.support.fell){
            const auto& binding=*bindings.at(event.id);
            const auto frame=wp::surfaceFrame(&surface,binding.plantedPosition);
            if(!frame)return {};
            const auto tangent=fxNormalize(frame->east*Fixed::fromRatio(event.fallDirectionXPermille,1000)+
                frame->south*Fixed::fromRatio(event.fallDirectionZPermille,1000));
            result.falls.push_back({event,binding.bodyId,binding.plantedPosition,tangent,
                physis::gravityAt(world,binding.plantedPosition)});
        }
        auto nextDelivery=*delivery;
        if(!appendPlanetPlantFalls(nextDelivery,result.falls))return {};
        auto nextAuthority=*authority;nextAuthority.rooted=roots.snapshot();
        nextAuthority.stateRevision=revision;result.authorityRevision=revision;
        World staged=world;
        staged.setResource<TerraWeaveAuthorityResource>(std::move(nextAuthority));
        staged.setResource<PlanetRootDeliveryResource>(std::move(nextDelivery));
        world=std::move(staged);return result;
    }catch(const std::exception&){return {};}
}
}
namespace aiverse::lib::worldprocess {
// Explicit project-authored physical geometry, independent of renderer assets.
// Capsule halfHeight is the distance from centre to each cap centre.
struct PlanetForestCapsuleRecipe {
    Fixed radius=Fixed::fromRatio(15,100),halfHeight=2_fx;
    Fixed density=600_fx,friction=Fixed::fromRatio(1,2);
};
inline bool validPlanetForestCapsule(const PlanetForestCapsuleRecipe& r){
    const auto min=Fixed::fromRatio(1,50);
    return r.radius>=min&&r.radius<=1000_fx&&r.halfHeight>=min&&r.halfHeight<=1000_fx&&
        r.density>=1_fx&&r.density<=Fixed::fromInt(1000000)&&r.friction>=Fixed{}&&r.friction<=10_fx;
}
// Native owner may use this helper for natural or terrain-induced falls.
// Persist the actual shape and receipt together; existing native publisher
// switches static source and projects it into Box3D/PHYSIS on its next frame.
inline bool deliverPlanetForestCapsule(World& world,const game::Surface& surface,
        const evolve::PlanetPlantFall& fall,uint32_t tick,const PlanetForestCapsuleRecipe& recipe){
    if(!validPlanetForestCapsule(recipe))return false;
    return evolve::commitPlanetPlantFall(world,fall,tick,[&](World& staged,const auto& event){
        const auto frame=surfaceFrame(&surface,event.plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=event.bodyId)return false;
        const auto tangent=event.tangentDirection-frame->up*event.tangentDirection.dot(frame->up);
        if(tangent.lengthSq()<=Fixed::fromRatio(1,10000))return false;
        const auto heading=fxNormalize(tangent);
        // Small deterministic lean, not a synthesized velocity or gravity solver.
        const auto up=fxNormalize(frame->up+heading*Fixed::fromRatio(1,8));
        const auto forward=fxNormalize(heading-up*heading.dot(up));
        const auto right=fxNormalize(fxCross(up,forward));
        const auto rotation=fxQuaternionFromBasis({right,up,forward});
        if(!rotation)return false;
        ConsequenceBody body;body.key=consequenceKey(event);body.body=event.bodyId;
        body.shape=ConsequenceShape::Capsule;body.dimensions={recipe.radius,recipe.halfHeight,recipe.radius};
        body.density=recipe.density;body.friction=recipe.friction;
        body.motion.position=surface.project(event.plantedPosition)+up*(recipe.halfHeight+recipe.radius);
        body.motion.rotation=*rotation;
        return stageConsequence(staged,surface,std::move(body));
    });
}
} // namespace aiverse::lib::worldprocess
