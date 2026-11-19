// TerraWeave Stage66 — rollback-safe, genre-independent native harvesting.
// Harvest hides stable procedural identities AND removes interactable roots
// from the canonical ECS resource. No Box3D, GPU or inventory side effects.
// Only a host structural/authoritative barrier may call these operations.
#pragma once
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveForestEcology.h"
#include "engine/world/TerraWeaveForestDeadwood.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ForestHarvestRecord {
    AnchoredPlantId id{};
    uint64_t harvestedTick=0;
    bool wasRecruited=false; // explicit native sapling needs restoration state
};

// Separate from ecological death and fallen roots: a harvested procedural
// tree must stay hidden when its forest chunk streams out and back in.
// The bounded tombstones live in ECS persistent World/COW snapshots.
struct ForestHarvestResource {
    uint32_t version=1;
    uint64_t revision=0;
    std::vector<ForestHarvestRecord> records; // sorted strictly by species/key
    static constexpr size_t kMaxRecords=1000000;
};

inline void validateForestHarvestResource(const ForestHarvestResource& state) {
    if(state.version!=1 || state.records.size()>
       ForestHarvestResource::kMaxRecords)
        throw std::invalid_argument("TerraWeave invalid harvest resource");
    AnchoredPlantId previous{};
    bool first=true;
    for(const auto& record:state.records) {
        if(!record.id.key || !record.id.speciesId ||
           (!first && !(previous<record.id)))
            throw std::invalid_argument(
                "TerraWeave duplicate/invalid harvested identity");
        previous=record.id;
        first=false;
    }
}
inline void io(Archive& a,ForestHarvestRecord& record) {
    a(record.id.speciesId,record.id.key,record.harvestedTick,
      record.wasRecruited);
}
inline void io(Archive& a,ForestHarvestResource& state) {
    a(state.version,state.revision);
    if(!a.ok() || state.version!=1u){a.fail();return;}
    uint32_t count=a.count(state.records.size(),21);
    if(!a.ok() || count>ForestHarvestResource::kMaxRecords){
        a.fail();return;
    }
    if(!a.writing())state.records.assign(count,{});
    for(auto& record:state.records)if(a.ok())a(record);
    if(!a.writing() && a.ok()){
        try{validateForestHarvestResource(state);}
        catch(const std::exception&){a.fail();}
    }
}

inline void registerTerraWeaveForestHarvestPersistence(Simulation& sim) {
    sim.registerPersistentResource<ForestHarvestResource>(
        "terraweave.forest_harvest",1);
    sim.world().setResource<ForestHarvestResource>(ForestHarvestResource{});
}
inline bool isForestHarvested(const ForestHarvestResource& state,
                             AnchoredPlantId id) {
    const auto it=std::lower_bound(
        state.records.begin(),state.records.end(),id,
        [](const ForestHarvestRecord& record,AnchoredPlantId key){
            return record.id<key;
        });
    return it!=state.records.end() && it->id==id;
}

struct NativeForestHarvestOutcome {
    bool changed=false;
    bool removedRoot=false;
    bool removedEcology=false;
    bool wasRecruited=false;
    uint64_t exportedDeadwoodUnits=0; // remaining organic matter carried away
    AnchoredPlantId id{};
    uint64_t tick=0;
    uint64_t authoritativeStateRevision=0;
    std::optional<AnchoredPlant> uprooted; // host may remove collider/award wood
};

// Idempotent for duplicated network commands; authority and optional ecology
// history are updated in one staged World publication. If an identity is only
// decorative, the persistent tombstone still hides it across streaming.
// Inventory rewards / Box3D / navmesh changes must happen AFTER ECS commit;
// callers should use changed to avoid awarding twice.
inline NativeForestHarvestOutcome harvestNativeForestTree(
        World& ecs,AnchoredPlantId id,uint64_t completedTick) {
    if(!id.speciesId || !id.key)
        throw std::invalid_argument("TerraWeave harvested plant identity");
    const auto* original=ecs.resourceConst<TerraWeaveAuthorityResource>();
    const auto* harvest=ecs.resourceConst<ForestHarvestResource>();
    if(!original || !harvest)
        throw std::logic_error(
            "TerraWeave harvest needs authority and harvest resources");
    validateForestHarvestResource(*harvest);
    NativeForestHarvestOutcome out;
    out.id=id;out.tick=completedTick;
    out.authoritativeStateRevision=original->stateRevision;
    if(isForestHarvested(*harvest,id))return out;
    if(harvest->records.size()>=ForestHarvestResource::kMaxRecords)
        throw std::length_error("TerraWeave harvested tree budget");
    if(original->stateRevision==UINT64_MAX ||
       harvest->revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave harvest revision overflow");

    TerraWeaveAuthorityResource updated=*original;
    ForestHarvestResource updatedHarvest=*harvest;
    const auto insertAt=std::lower_bound(
        updatedHarvest.records.begin(),updatedHarvest.records.end(),id,
        [](const ForestHarvestRecord& record,AnchoredPlantId key){
            return record.id<key;
        });
    auto inserted=updatedHarvest.records.insert(
        insertAt,{id,completedTick,false});
    ++updatedHarvest.revision;

    // A harvested tree must not compete for canopy light, seed, erode soil
    // with roots or fall again. Never mutate per-tree renderer state here.
    const auto oldRoot=std::find_if(updated.rooted.begin(),
        updated.rooted.end(),[id](const AnchoredPlant& plant){
            return plant.anchor.speciesId==id.speciesId &&
                   plant.anchor.plantKey==id.key;
        });
    if(oldRoot!=updated.rooted.end()) {
        out.uprooted=*oldRoot;
        updated.rooted.erase(oldRoot);
        out.removedRoot=true;
    }

    std::optional<ForestEcologyResource> updatedEcology;
    if(const auto* ecology=ecs.resourceConst<ForestEcologyResource>()){
        updatedEcology=*ecology;
        validateForestEcologyResource(*updatedEcology);
        const auto oldRecord=std::lower_bound(
            updatedEcology->plants.begin(),updatedEcology->plants.end(),id,
            [](const ForestEcologyPlantState& record,AnchoredPlantId key){
                return record.id<key;
            });
        if(oldRecord!=updatedEcology->plants.end() &&
           oldRecord->id==id) {
            if(updatedEcology->revision==UINT64_MAX)
                throw std::overflow_error(
                    "TerraWeave harvest ecology revision overflow");
            out.wasRecruited=oldRecord->recruited;
            updatedEcology->plants.erase(oldRecord);
            ++updatedEcology->revision;
            out.removedEcology=true;
        }
    }
    inserted->wasRecruited=out.wasRecruited;
    std::optional<ForestDeadwoodResource> updatedDeadwood;
    if(const auto* wood=ecs.resourceConst<ForestDeadwoodResource>()){
        updatedDeadwood=*wood;
        out.exportedDeadwoodUnits=extractForestDeadwood(
            *updatedDeadwood,id);
    }
    ++updated.stateRevision;
    out.authoritativeStateRevision=updated.stateRevision;

    World staged=ecs; // exceptions before publication leave live ECS intact
    staged.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    staged.setResource<ForestHarvestResource>(std::move(updatedHarvest));
    if(updatedEcology)
        staged.setResource<ForestEcologyResource>(
            std::move(*updatedEcology));
    if(updatedDeadwood)
        staged.setResource<ForestDeadwoodResource>(
            std::move(*updatedDeadwood));
    ecs=std::move(staged);
    out.changed=true;
    return out;
}

// Explicit gameplay replant/restore. No automatic resurrection of natural
// deaths or fallen trees. A supplied new rooted plant is verified against the
// identity and normal RootSupportRegistry constraints before mutation.
// Leaving planted empty merely unhides an existing cosmetic scatter identity.
inline bool restoreHarvestedForestTree(
        World& ecs,AnchoredPlantId id,
        std::optional<AnchoredPlant> planted=std::nullopt) {
    if(!id.speciesId || !id.key)
        throw std::invalid_argument("TerraWeave restored plant identity");
    const auto* original=ecs.resourceConst<TerraWeaveAuthorityResource>();
    const auto* harvest=ecs.resourceConst<ForestHarvestResource>();
    if(!original || !harvest)
        throw std::logic_error("TerraWeave missing restore resources");
    validateForestHarvestResource(*harvest);
    if(!isForestHarvested(*harvest,id))return false;
    const auto found=std::lower_bound(
        harvest->records.begin(),harvest->records.end(),id,
        [](const ForestHarvestRecord& record,AnchoredPlantId key){
            return record.id<key;
        });
    if(found->wasRecruited && !planted)
        throw std::invalid_argument(
            "TerraWeave native recruit requires explicit replanted roots");
    if(original->stateRevision==UINT64_MAX ||
       harvest->revision==UINT64_MAX)
        throw std::overflow_error("TerraWeave restore revision overflow");

    TerraWeaveAuthorityResource updated=*original;
    if(planted){
        if(planted->anchor.speciesId!=id.speciesId ||
           planted->anchor.plantKey!=id.key || planted->fallen)
            throw std::invalid_argument("TerraWeave invalid replanted identity");
        RootSupportRegistry validation;
        validation.setSnapshot({*planted});
        if(std::any_of(updated.rooted.begin(),updated.rooted.end(),
            [id](const AnchoredPlant& old){
                return old.anchor.speciesId==id.speciesId &&
                       old.anchor.plantKey==id.key;
            }))
            throw std::invalid_argument(
                "TerraWeave replant already has authoritative roots");
        if(updated.rooted.size()>=RootSupportRegistry::kMaxAnchors)
            throw std::length_error("TerraWeave replant root budget");
        updated.rooted.push_back(*planted);
    }
    // A native recruited sapling is not part of procedural background
    // scatter. Rebuild its sparse ecology presentation marker on replant.
    std::optional<ForestEcologyResource> updatedEcology;
    if(found->wasRecruited){
        const auto* ecology=ecs.resourceConst<ForestEcologyResource>();
        if(!ecology)
            throw std::logic_error("TerraWeave restore needs ecology state");
        updatedEcology=*ecology;
        validateForestEcologyResource(*updatedEcology);
        if(updatedEcology->revision==UINT64_MAX)
            throw std::overflow_error("TerraWeave restore ecology overflow");
        const auto pos=std::lower_bound(
            updatedEcology->plants.begin(),updatedEcology->plants.end(),id,
            [](const ForestEcologyPlantState& record,AnchoredPlantId key){
                return record.id<key;
            });
        if(pos!=updatedEcology->plants.end() && pos->id==id)
            throw std::invalid_argument(
                "TerraWeave restored recruit already has ecology record");
        updatedEcology->plants.insert(pos,{
            id,updatedEcology->lastStepTick,0,0,0,0,false,true});
        ++updatedEcology->revision;
    }
    // Replanting an identity begins a NEW organic life cycle. Keep the
    // historical nutrient/export/respiration ledgers intact while removing
    // only a fully spent old deadwood identity, so its future death can be
    // registered again without double-counting prior wood.
    std::optional<ForestDeadwoodResource> updatedDeadwood;
    if(const auto* wood=ecs.resourceConst<ForestDeadwoodResource>()){
        updatedDeadwood=*wood;
        validateForestDeadwoodResource(*updatedDeadwood);
        const auto log=std::lower_bound(
            updatedDeadwood->logs.begin(),updatedDeadwood->logs.end(),id,
            [](const ForestDeadwoodRecord& record,AnchoredPlantId key){
                return record.id<key;
            });
        if(log!=updatedDeadwood->logs.end() && log->id==id){
            if(log->remainingUnits)
                throw std::logic_error(
                    "TerraWeave replant needs deadwood export/cleanup");
            if(updatedDeadwood->revision==UINT64_MAX)
                throw std::overflow_error(
                    "TerraWeave replant deadwood revision overflow");
            updatedDeadwood->logs.erase(log);
            ++updatedDeadwood->revision;
        }
    }
    ForestHarvestResource updatedHarvest=*harvest;
    const auto it=std::lower_bound(
        updatedHarvest.records.begin(),updatedHarvest.records.end(),id,
        [](const ForestHarvestRecord& record,AnchoredPlantId key){
            return record.id<key;
        });
    updatedHarvest.records.erase(it);
    ++updatedHarvest.revision;
    ++updated.stateRevision;
    World staged=ecs;
    staged.setResource<TerraWeaveAuthorityResource>(std::move(updated));
    staged.setResource<ForestHarvestResource>(std::move(updatedHarvest));
    if(updatedEcology)
        staged.setResource<ForestEcologyResource>(
            std::move(*updatedEcology));
    if(updatedDeadwood)
        staged.setResource<ForestDeadwoodResource>(
            std::move(*updatedDeadwood));
    ecs=std::move(staged);
    return true;
}

} // namespace aiverse::terraweave
