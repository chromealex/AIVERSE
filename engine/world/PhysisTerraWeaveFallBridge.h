// Stage 87: opt-in HOST bridge from PHYSIS fracture outbox to the canonical
// TerraWeave rooted plants and Box3D falling-body factory.
// This file belongs in engine/world, NEVER inside engine/physis.
// It requires the TerraWeave branch to be present in the integration build.
#pragma once
#include "engine/world/PhysisNatureFallHost.h"
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveFallenPhysics.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::world {

inline terraweave::AnchoredPlantId terraPlantId(NatureFallPlantId id) {
    return {id.speciesId, id.plantKey};
}

inline terraweave::PlantFallEvent terraFallEvent(
        const NatureFallRecord& record) {
    if (!natureFallValid(record))
        throw std::invalid_argument("invalid host fall record");
    terraweave::PlantFallEvent out;
    out.id = terraPlantId(record.plant);
    out.habit = static_cast<terraweave::PlantHabit>(record.habit);
    out.xCm = record.xCm;
    out.zCm = record.zCm;
    out.plantedHeightCm = record.plantedHeightCm;
    out.fallDirectionXPermille = record.directionXPermille;
    out.fallDirectionZPermille = record.directionZPermille;
    out.supportPermille = record.supportPermille;
    out.exposedCm = record.exposedCm;
    out.sourceSoilRevision = record.soilRevision;
    out.tick = record.climateTick;
    return out;
}

// PHYSIS force is Fixed Q47.16; direction is signed integer permille.
// Avoid floating-point and squared-magnitude overflow in authoritative code.
// A zero horizontal wind uses canonical root failure direction (if present).
inline std::pair<std::int32_t,std::int32_t> fractureFallDirection(
        const physis::NatureFractureEvent& event,
        const terraweave::RootState& root) {
    // PHYSIS force publisher clamps inputs at 200000 N; repeat the clamp
    // for trusted native callers that synthesize events directly.
    constexpr std::int64_t cap = 200000 * Fixed::kOne;
    const auto x = std::clamp(event.aerodynamicForceN.x.raw,-cap,cap);
    const auto z = std::clamp(event.aerodynamicForceN.z.raw,-cap,cap);
    const auto absX = x >= 0 ? x : -x;
    const auto absZ = z >= 0 ? z : -z;
    const auto maxAxis = std::max(absX, absZ);
    if (maxAxis == 0)
        return {root.fallDirectionXPermille, root.fallDirectionZPermille};
    // Divide before multiply: preserves 64-bit bounds for large Fixed loads.
    // These components have magnitude <=1000; Box3D normalizes the vector.
    const auto scaled = [maxAxis](std::int64_t raw) -> std::int32_t {
        return static_cast<std::int32_t>(
            (raw / maxAxis) * 1000 +
            ((raw % maxAxis) * 1000) / maxAxis);
    };
    return {scaled(x),scaled(z)};
}

inline std::optional<NatureFallRecord> makeNatureFallRecord(
        const physis::NatureFractureEvent& event,
        const terraweave::AnchoredPlant& plant,
        std::uint64_t soilRevision) {
    if (!event.entity.valid() || event.eventKey == 0 ||
        !plant.anchor.plantKey || !plant.anchor.speciesId ||
        plant.anchor.roots.habit == terraweave::PlantHabit::Grass)
        return std::nullopt;
    const auto [dx,dz] = fractureFallDirection(event, plant.support);
    NatureFallRecord result;
    result.entityPacked = event.entity.packed();
    result.eventKey = event.eventKey;
    result.climateTick = event.climateTick;
    result.plant = {plant.anchor.speciesId, plant.anchor.plantKey};
    result.habit = static_cast<std::uint8_t>(plant.anchor.roots.habit);
    result.xCm = plant.anchor.xCm;
    result.zCm = plant.anchor.zCm;
    result.plantedHeightCm = plant.anchor.plantedHeightCm;
    result.directionXPermille = dx;
    result.directionZPermille = dz;
    result.supportPermille = plant.support.supportPermille;
    result.exposedCm = plant.support.exposedCm;
    result.soilRevision = soilRevision;
    return natureFallValid(result) ?
        std::optional<NatureFallRecord>{result} : std::nullopt;
}

// Must run on the authoritative host barrier AFTER the PHYSIS stress/outbox
// systems and AFTER any authoritative TerraWeave terrain/root updates.
// On the PHYSIS branch alone this header is intentionally NOT compiled until
// TerraWeave is merged: the cross-system API is verified against its branch.
// Events are not physical bodies: sink only stages a persistent ECS record;
// the separate Box3D barrier reconstructs bodies from the committed ledger.
inline physis::NatureFractureDispatchStats
dispatchPhysisTerraWeaveFalls(World& world, std::size_t maxEvents = 64) {
    const auto* state = world.resourceConst<NatureFallHostState>();
    const auto* terrain =
        world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    if (!state || !terrain)
        throw std::logic_error("PHYSIS/Terra fall host resources not installed");

    // Avoid scanning up to a million TerraWeave rooted plants PER event.
    // Build a tiny requested-ID index, then make one linear pass over roots.
    // TerraWeave authority is immutable during this host barrier.
    const auto pending=physis::pendingNatureFractures(world);
    if (pending.empty()) return {};
    std::map<terraweave::AnchoredPlantId,
             const terraweave::AnchoredPlant*> requested;
    for (const auto& event : pending) {
        const auto it=std::lower_bound(
            state->bindings.begin(),state->bindings.end(),
            event.entity.packed(),
            [](const NatureFallBinding& b,std::uint64_t key){
                return b.entityPacked < key;
            });
        if (it!=state->bindings.end() &&
            it->entityPacked==event.entity.packed())
            requested.try_emplace(terraPlantId(it->plant),nullptr);
    }
    for (const auto& plant : terrain->rooted) {
        auto it=requested.find({plant.anchor.speciesId,plant.anchor.plantKey});
        if (it!=requested.end())it->second=&plant;
    }

    return physis::dispatchNatureFractures(
        world, [&](const physis::NatureFractureEvent& event) {
            using D = physis::NatureFractureDisposition;
            // Earlier sink calls replace the host ledger resource; re-read
            // the mapping, not a stale pointer retained into a COW write.
            const auto* host = world.resourceConst<NatureFallHostState>();
            const auto packed = event.entity.packed();
            const auto bound = std::lower_bound(
                host->bindings.begin(),host->bindings.end(),packed,
                [](const NatureFallBinding& b,std::uint64_t key){
                    return b.entityPacked < key;
                });
            if (bound == host->bindings.end() || bound->entityPacked != packed)
                return D::Retry;
            const auto plant=requested.find(terraPlantId(bound->plant));
            if (plant==requested.end() || !plant->second)
                return D::Retry;
            const auto record=makeNatureFallRecord(
                event,*plant->second,terrain->earth.revision);
            if (!record) return D::Retry;
            return stageNatureFall(world,event,*record);
        }, maxEvents);
}

struct NatureFallReconcileStats {
    std::size_t spawned = 0;
    std::size_t reused = 0;
    std::size_t failed = 0;
    std::size_t removed = 0;
};

// Box3D is a DERIVED cache of the authoritative ECS fall ledger.
// The game must separately persist/replay moving rigid-body poses if it needs
// exact mid-fall physics snapshots; this only recreates the START pose.
// Invoke after TerraWeave native scene binding, before scene render/physics.
// Never call from PHYSIS deterministic simulation jobs.
class NatureFallBox3DReconciler {
public:
    explicit NatureFallBox3DReconciler(physics::Scene& scene) : scene_(scene) {}
    template<class SpeciesSpec>
    NatureFallReconcileStats reconcile(
            const World& world, terraweave::FallenPlantPhysics& bodies,
            SpeciesSpec&& resolveSpec,
            terraweave::TerraWeaveForestScene* forest = nullptr) {
        const auto* ledger=world.resourceConst<NatureFallHostState>();
        if (!ledger)throw std::logic_error("nature fall ledger missing");
        NatureFallReconcileStats stats;
        std::set<terraweave::AnchoredPlantId> desired;
        for (const auto& fall : ledger->records) {
            if (!fall.active) continue;
            const auto id = terraPlantId(fall.plant);
            desired.insert(id);
            auto managed = owned_.find(id);
            if (managed != owned_.end() && managed->second != fall.eventKey) {
                // A repaired stem has a new PHYSIS event identity.
                bodies.erase(id,forest,false);
                owned_.erase(managed);
            }
            const auto currentBody = bodies.body(id);
            const bool hadLiveBody = currentBody != physics::kNoBody &&
                                     scene_.valid(currentBody);
            try {
                const auto spec = resolveSpec(id.speciesId);
                if (!spec) {++stats.failed;continue;}
                // spawn() is idempotent for a live body and repairs stale
                // handles after external Scene reset.
                const auto handle=bodies.spawn(terraFallEvent(fall),*spec,forest);
                if (handle==physics::kNoBody) {++stats.failed;continue;}
                if (!hadLiveBody)
                    owned_[id]=fall.eventKey;
                if (forest) forest->removeTree({id.key,id.speciesId});
                if (hadLiveBody) ++stats.reused;
                else ++stats.spawned;
            } catch (...) {
                // Leave authoritative fall record intact for a later retry.
                ++stats.failed;
            }
        }
        for (auto it=owned_.begin();it!=owned_.end();) {
            if (desired.contains(it->first)) {++it;continue;}
            const auto id=it->first;
            bool restore=true;
            if (const auto* authority =
                    world.resourceConst<terraweave::TerraWeaveAuthorityResource>()) {
                for (const auto& plant : authority->rooted) {
                    if (plant.anchor.speciesId==id.speciesId &&
                        plant.anchor.plantKey==id.key) {
                        restore=!plant.fallen;
                        break;
                    }
                }
            }
            bodies.erase(id,forest,restore);
            it=owned_.erase(it);
            ++stats.removed;
        }
        return stats;
    }
    // Call when an external scene resets or changes owner. This forgets
    // transient handles; next reconcile will rebuild from ECS authority.
    void forgetDerivedBodies() {owned_.clear();}
private:
    physics::Scene& scene_;
    std::map<terraweave::AnchoredPlantId,std::uint64_t> owned_;
};

} // namespace aiverse::world
