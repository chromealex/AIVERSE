// TerraWeave authoritative ECS resource for native games. Compatible with
// Simulation World COW snapshots, native persistence and rollback: all sparse
// mutable gameplay state lives in a persistent World resource, while RHI,
// NavMesh and Box3D are derived/presentation consumers on their owner threads.
// NEREID remains the ONLY authoritative water flow/erosion calculator.
#pragma once
#include "engine/world/TerraWeaveEcsBankBridge.h"
#include "engine/world/TerraWeavePersistence.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerraWeaveEcsAuthorityConfig {
    TerrainSettings generator{};
    int32_t soilGridCm=100,bedrockCm=-1000000;
    std::string immutableRecipeId; // exact procedural/asset version identity
};

inline void validateEcsAuthorityConfig(
        const TerraWeaveEcsAuthorityConfig& config){
    if(config.immutableRecipeId.empty() ||
       config.immutableRecipeId.size()>kTerraWeaveRecipeIdMax ||
       config.soilGridCm<10 || config.soilGridCm>10000 ||
       config.generator.basePeriodCm<100 ||
       config.generator.octaves<1 || config.generator.octaves>12 ||
       config.generator.reliefCm<0)
        throw std::invalid_argument("TerraWeave native ECS authority config");
}

struct TerraWeaveAuthorityResource {
    uint32_t version=2;
    uint64_t stateRevision=0; // includes ecology updates even without changing terrain
    std::string immutableRecipeId;
    EarthworksSnapshot earth;
    std::vector<AnchoredPlant> rooted;
    std::vector<GrassPatch> grass;
};

inline void io(Archive& a,TerraWeaveAuthorityResource& state){
    a(state.version);
    if(!a.ok() || state.version!=2u){a.fail();return;}
    a(state.stateRevision);
    save_detail::recipe(a,state.immutableRecipeId);
    a(state.earth.spacingCm,state.earth.bedrockCm,
      state.earth.revision);
    save_detail::array(a,state.earth.heights,1000000u,20);
    save_detail::array(a,state.rooted,RootSupportRegistry::kMaxAnchors,90);
    save_detail::array(a,state.grass,GrassCoverRegistry::kMaxPatches,75);
}

// A new authority world starts with an empty sparse overlay; its background
// procedural height source is immutable config captured by value.
inline TerraWeaveAuthorityResource initialTerraWeaveResource(
        const TerraWeaveEcsAuthorityConfig& cfg){
    validateEcsAuthorityConfig(cfg);
    TerraWeaveAuthorityResource out;
    out.immutableRecipeId=cfg.immutableRecipeId;
    out.earth.spacingCm=cfg.soilGridCm;
    out.earth.bedrockCm=cfg.bedrockCm;
    return out;
}

inline EarthworksLayer materializeEcsTerrain(
        const TerraWeaveAuthorityResource& state,
        const TerraWeaveEcsAuthorityConfig& cfg){
    validateEcsAuthorityConfig(cfg);
    if(state.version!=2u ||
       state.immutableRecipeId!=cfg.immutableRecipeId)
        throw std::invalid_argument("TerraWeave native ECS recipe mismatch");
    const TerrainSettings generated=cfg.generator;
    EarthworksLayer earth(cfg.soilGridCm,
        [generated](int64_t x,int64_t z){
            return heightAtCm(generated,x,z);
        },cfg.bedrockCm);
    earth.setSnapshot(state.earth);
    return earth;
}

inline void registerTerraWeaveAuthorityResource(
        Simulation& sim,const TerraWeaveEcsAuthorityConfig& cfg){
    const auto initial=initialTerraWeaveResource(cfg);
    sim.registerPersistentResource<TerraWeaveAuthorityResource>(
        "terraweave.authority",2);
    sim.world().setResource<TerraWeaveAuthorityResource>(initial);
}

// Native users may spawn authoritative plants/patches by supplying the
// resource's rooted/grass vectors during setup (before first tick).
// Game modules should edit them through a validated staging copy.
struct TerraWeaveAuthoritativeEcsChange {
    EarthworkChange earth;
    PlantSupportUpdate rooted;
    std::vector<GrassCoverChange> grass;
    SlopeFailurePlan slope;
    std::vector<std::pair<Entity,int32_t>> bankGroundCorrections;
    uint32_t tick=0;
};

// A single native fixed-step action. The ECS World resource is canonical.
// Both the user's dig and optional induced slope slides are merged into ONE
// Earthworks transaction/soil revision (delta replay/rollback compatible).
// Existing NEREID bank soil height is synchronized on the same ECS barrier.
inline TerraWeaveAuthoritativeEcsChange
excavateAuthoritativeEcs(World& world,
                        const TerraWeaveEcsAuthorityConfig& cfg,
                        EarthworkBrush brush,uint32_t tick,
                        bool enableSlopeFailures=false,
                        SlopeFailureSettings slopeSettings={},
                        SlopeSoilSampler slopeMaterial={}) {
    if(enableSlopeFailures)validateSlopeFailureSettings(slopeSettings);
    const auto* old=world.resourceConst<TerraWeaveAuthorityResource>();
    if(!old)throw std::logic_error("TerraWeave ECS resource not initialized");
    const EarthworksLayer original=materializeEcsTerrain(*old,cfg);
    EarthworksLayer ground=original;
    RootSupportRegistry roots;
    GrassCoverRegistry grass;
    roots.setSnapshot(old->rooted);
    grass.setSnapshot(old->grass);

    TerraWeaveAuthoritativeEcsChange result;
    result.tick=tick;
    const auto dug=ground.sculpt(brush);
    if(dug.vertices.empty())return result;
    std::map<EarthVertex,int32_t> finalHeights;
    std::vector<EarthVertex> seeds;
    seeds.reserve(dug.vertices.size());
    for(const auto& edit:dug.vertices) {
        finalHeights.emplace(edit.vertex,edit.afterCm);
        seeds.push_back(edit.vertex);
    }

    std::map<EarthVertex,SlopeSoil> soils;
    std::map<EarthVertex,std::pair<Entity,int32_t>> banks;
    world.eachExistingConst<BankEarthVertexComponent>(
        [&](Entity e,const BankEarthVertexComponent& bind){
            const auto* bank=world.tryGetConst<
                lib::nereid::WaterBankSoilComponent>(e);
            if(!bank)return;
            if(!banks.emplace(bind.vertex,std::make_pair(
                    e,bank->soil.groundCm)).second)
                throw std::invalid_argument(
                    "TerraWeave duplicate bank vertex during excavation");
            soils.emplace(bind.vertex,SlopeSoil{
                bank->soil.soil,
                bank->enabled?bank->wetContactPermille:0u,
                bank->soil.rootBindingPermille});
        });
    if(enableSlopeFailures){
        const SlopeSoilSampler material=
            [&soils,&slopeMaterial](EarthVertex key){
                if(slopeMaterial)return slopeMaterial(key);
                const auto it=soils.find(key);
                return it==soils.end()?SlopeSoil{}:it->second;
            };
        result.slope=planSlopeFailures(
            ground,seeds,material,slopeSettings);
        for(const auto& edit:result.slope.edits)
            finalHeights.insert_or_assign(edit.vertex,edit.afterCm);
    }
    std::vector<EarthVertexEdit> combined;
    combined.reserve(finalHeights.size());
    for(const auto& [vertex,newHeight]:finalHeights){
        const int32_t before=original.heightAt(vertex);
        if(before!=newHeight)
            combined.push_back({vertex,before,newHeight});
        const auto bank=banks.find(vertex);
        if(bank==banks.end())continue;
        if(bank->second.second!=before)
            throw std::invalid_argument(
                "TerraWeave excavated bank ground conflicts with NEREID");
        if(bank->second.second!=newHeight)
            result.bankGroundCorrections.push_back({
                bank->second.first,newHeight});
    }
    if(combined.empty())return result;
    EarthworksLayer next=original;
    result.earth=next.applyEdits(combined,original.revision());
    result.grass=grass.applyEarthwork(
        result.earth,next.spacingCm());
    const SoilHeightSampler sample=[&next](int64_t x,int64_t z)
            ->std::optional<int32_t>{
        const int64_t edge=kWorldLimitCm-next.spacingCm();
        if(x<=-edge||x>=edge||z<=-edge||z>=edge)
            return std::nullopt;
        return next.sampleHeightCm(x,z);
    };
    result.rooted=roots.applyGroundChange(
        sample,result.earth,tick);

    TerraWeaveAuthorityResource updated=*old;
    updated.earth=next.snapshot();
    if(updated.stateRevision==UINT64_MAX)
        throw std::overflow_error("TerraWeave ECS state revision overflow");
    ++updated.stateRevision;
    updated.rooted=roots.snapshot();
    updated.grass=grass.snapshot();

    // Validate/detach bank pages BEFORE swapping the authoritative
    // resource. Subsequent existing component writes cannot allocate.
    using Bank=lib::nereid::WaterBankSoilComponent;
    Pool<Bank>* mutableBanks=nullptr;
    if(!result.bankGroundCorrections.empty()){
        mutableBanks=&world.pool<Bank>();
        mutableBanks->prepareParallelWrite();
        for(const auto& [entity,height]:result.bankGroundCorrections){
            (void)height;
            if(!world.alive(entity) ||
               !mutableBanks->tryGet(entity.index))
                throw std::invalid_argument(
                    "TerraWeave bank cell disappeared during dig");
        }
    }
    world.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    for(const auto& [entity,height]:result.bankGroundCorrections)
        mutableBanks->tryGet(entity.index)->soil.groundCm=height;
    return result;
}

// Install this system AFTER registerNativeWaterPipeline(sim,config) with
// enableBankErosion=true and AFTER registerTerraWeaveBankVertexPersistence.
// NEREID's bank ECS components already hold current, sediment, carry, wetness.
// Only the corresponding terrain/root/grass ECS resource is updated here.
// Callback should enqueue output in the native host's post-tick presentation
// queue; no graphics/Box3D work or non-deterministic side effects in callback.
template<class OnCommit>
inline void addTerraWeaveAuthoritativeBankSystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg,OnCommit&& callback,
        bool enableSlopeFailures=false,
        SlopeFailureSettings slopeSettings={}){
    validateEcsAuthorityConfig(cfg);
    if(enableSlopeFailures)validateSlopeFailureSettings(slopeSettings);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),enableSlopeFailures,slopeSettings,
         onCommit=std::forward<OnCommit>(callback)]
        (Simulation& current)mutable{
            World& ecs=current.world();
            const auto* existing=
                ecs.resourceConst<TerraWeaveAuthorityResource>();
            if(!existing)
                throw std::logic_error("TerraWeave ECS resource not initialized");
            // Most bank ticks only accumulate a fractional erosion carry
            // inside NEREID ECS. Avoid rematerializing up to a million saved
            // vegetation records when no actual ground vertex was changed.
            bool anyGroundEdit=false;
            static_cast<const World&>(ecs)
                .eachExistingConst<
                    lib::nereid::WaterBankErosionOutputComponent>(
                    [&](Entity,
                        const lib::nereid::WaterBankErosionOutputComponent& out){
                        if(out.sampledTick==current.tick() && out.changed)
                            anyGroundEdit=true;
                    });
            if(!anyGroundEdit)return;
            auto ground=materializeEcsTerrain(*existing,cfg);
            RootSupportRegistry roots;
            GrassCoverRegistry grass;
            roots.setSnapshot(existing->rooted);
            grass.setSnapshot(existing->grass);
            auto change=commitNereidBankTerrain(
                ecs,current.tick(),ground,&roots,&grass,
                enableSlopeFailures,slopeSettings);
            if(change.earth.vertices.empty() &&
               change.bankGroundCorrections.empty())return;
            TerraWeaveAuthorityResource updated=*existing;
            updated.earth=ground.snapshot();
            if(updated.stateRevision==UINT64_MAX)
                throw std::overflow_error(
                    "TerraWeave ECS state revision overflow");
            ++updated.stateRevision;
            updated.rooted=roots.snapshot();
            updated.grass=grass.snapshot();
            // Ensure every affected existing bank cell can be updated
            // without COW detachment/allocation AFTER the canonical ECS
            // terrain resource has already committed.
            using Bank=lib::nereid::WaterBankSoilComponent;
            Pool<Bank>* writableBanks=nullptr;
            if(!change.bankGroundCorrections.empty()){
                writableBanks=&ecs.pool<Bank>();
                writableBanks->prepareParallelWrite();
                for(const auto& [entity,height]:
                    change.bankGroundCorrections){
                    (void)height;
                    if(!ecs.alive(entity) ||
                       !writableBanks->tryGet(entity.index))
                        throw std::invalid_argument(
                            "TerraWeave bank disappeared before slope commit");
                }
            }
            ecs.setResource<TerraWeaveAuthorityResource>(
                std::move(updated));
            // NEREID's own local bank-erosion ECS stage already updated
            // its soil and sediment. Apply only EXTRA slope displacement
            // at bank vertices; preserve suspended sediment and carry.
            for(const auto& [entity,height]:
                change.bankGroundCorrections)
                writableBanks->tryGet(entity.index)->soil.groundCm=height;
            // Host callback must not mutate the ECS or access gfx/Box3D
            // directly, and must avoid throwing after the ECS commit.
            onCommit(change);
        });
}

} // namespace aiverse::terraweave
