// Stage 87: opt-in HOST bridge from PHYSIS fracture outbox to the canonical
// TerraWeave rooted plants and Box3D falling-body factory.
// This file belongs in engine/world, NEVER inside engine/physis.
// It requires the TerraWeave branch to be present in the integration build.
#pragma once
#include "engine/world/PhysisNatureFallHost.h"
#include "engine/world/TerraWeaveEcsAuthority.h"
#include "engine/world/TerraWeaveFallenPhysics.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
// Stage 94: stage ALL selected events on a private World copy and publish
// only once. This keeps PHYSIS acks, fall ledger and TerraWeave rooted.fallen
// in the same rollback/persistence transaction (no intermediate save).
inline physis::NatureFractureDispatchStats
dispatchPhysisTerraWeaveFalls(World& world, std::size_t maxEvents = 64) {
    if (maxEvents == 0 || maxEvents > 4096)
        throw std::invalid_argument("PHYSIS/Terra fall host budget 1..4096");
    const auto* bindingState = world.resourceConst<NatureFallHostState>();
    const auto* terrain =
        world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    if (!bindingState || !terrain)
        throw std::logic_error("PHYSIS/Terra fall host resources not installed");

    const auto pending = physis::pendingNatureFractures(world);
    physis::NatureFractureDispatchStats stats;
    stats.pendingAtStart = pending.size();
    stats.pendingAtEnd = pending.size();
    if (pending.empty()) return stats;

    // One scan over canonical roots for up to maxEvents requested identities.
    const std::size_t count = std::min(maxEvents, pending.size());
    const std::size_t missing = std::numeric_limits<std::size_t>::max();
    std::map<terraweave::AnchoredPlantId, std::size_t> requested;
    for (std::size_t i = 0; i < count; ++i) {
        const auto packed = pending[i].entity.packed();
        const auto bound = std::lower_bound(
            bindingState->bindings.begin(), bindingState->bindings.end(),
            packed, [](const NatureFallBinding& b, std::uint64_t key) {
                return b.entityPacked < key;
            });
        if (bound != bindingState->bindings.end() &&
            bound->entityPacked == packed)
            requested.try_emplace(terraPlantId(bound->plant), missing);
    }
    for (std::size_t i = 0; i < terrain->rooted.size(); ++i) {
        const auto& plant = terrain->rooted[i];
        const auto it = requested.find(
            {plant.anchor.speciesId, plant.anchor.plantKey});
        if (it != requested.end()) {
            if (it->second != missing)
                throw std::invalid_argument("TerraWeave duplicate rooted plant identity");
            it->second = i;
        }
    }

    // COW shares unchanged pages when enabled. In legacy mode this is a
    // deliberate one-time deep clone on a batch with pending fractures.
    // Any exception before final move leaves original World intact.
    World staged = world;
    std::vector<std::size_t> fallenRoots;
    fallenRoots.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& event = pending[i];
        if (!staged.alive(event.entity)) {
            ++stats.stale;
            continue;
        }
        const auto* boundState = staged.resourceConst<NatureFallHostState>();
        const auto packed = event.entity.packed();
        const auto bound = std::lower_bound(
            boundState->bindings.begin(), boundState->bindings.end(),
            packed, [](const NatureFallBinding& b, std::uint64_t key) {
                return b.entityPacked < key;
            });
        if (bound == boundState->bindings.end() ||
            bound->entityPacked != packed) {
            ++stats.visited;
            ++stats.retry;
            continue;
        }
        const auto root = requested.find(terraPlantId(bound->plant));
        if (root == requested.end() || root->second == missing) {
            ++stats.visited;
            ++stats.retry;
            continue;
        }
        const auto record = makeNatureFallRecord(
            event, terrain->rooted[root->second], terrain->earth.revision);
        ++stats.visited;
        if (!record ||
            stageNatureFall(staged, event, *record) !=
                physis::NatureFractureDisposition::DurablyCommitted) {
            ++stats.retry;
            continue;
        }
        if (!physis::acknowledgeNatureFracture(
                staged, event.entity, event.eventKey, event.climateTick)) {
            ++stats.stale;
            continue;
        }
        ++stats.committed;
        if (!terrain->rooted[root->second].fallen)
            fallenRoots.push_back(root->second);
    }

    if (stats.committed) {
        if (!fallenRoots.empty()) {
            // One Terra authority revision publishes all wind-fallen roots,
            // so NativeTerraWeaveWorldBinding hides them after save/load.
            auto updated = *terrain;
            if (updated.stateRevision == UINT64_MAX)
                throw std::overflow_error("TerraWeave fall authority revision overflow");
            ++updated.stateRevision;
            for (std::size_t index : fallenRoots)
                updated.rooted[index].fallen = true;
            staged.setResource<terraweave::TerraWeaveAuthorityResource>(
                std::move(updated));
        }
        stats.pendingAtEnd = physis::pendingNatureFractures(staged).size();
        world = std::move(staged);
    }
    return stats;
}

// Trusted authoritative GAME action: replace a previously broken rooted
// plant, clear PHYSIS stress and retire the active fall record in ONE World
// transaction. Box3D/forest/navmesh are repaired later by the host barrier.
// Caller supplies the complete re-established TerraWeave plant (including
// authored age, root support, species dimensions and location).
inline bool repairPhysisTerraWeavePlant(
        World& world, Entity entity, std::uint64_t acknowledgedKey,
        std::uint64_t acknowledgedTick,
        const terraweave::AnchoredPlant& repaired) {
    const auto* clock = world.resourceConst<physis::State>();
    const auto* ledger = world.resourceConst<NatureFallHostState>();
    const auto* terrain =
        world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    const auto* outbox = static_cast<const World&>(world)
        .tryGetConst<physis::NatureFractureOutboxComponent>(entity);
    if (!clock || !ledger || !terrain || !world.alive(entity) ||
        !outbox || outbox->delivery != physis::NatureFractureDelivery::Acknowledged ||
        outbox->eventKey != acknowledgedKey ||
        outbox->climateTick != acknowledgedTick ||
        clock->tick <= acknowledgedTick || repaired.fallen)
        return false;

    const auto packed = entity.packed();
    const auto bound = std::lower_bound(
        ledger->bindings.begin(), ledger->bindings.end(), packed,
        [](const NatureFallBinding& b, std::uint64_t id) {
            return b.entityPacked < id;
        });
    if (bound == ledger->bindings.end() || bound->entityPacked != packed ||
        bound->plant != NatureFallPlantId{
            repaired.anchor.speciesId,repaired.anchor.plantKey})
        return false;

    const auto record = std::find_if(
        ledger->records.begin(), ledger->records.end(),
        [&](const NatureFallRecord& item) {
            return item.entityPacked == packed &&
                   item.eventKey == acknowledgedKey &&
                   item.climateTick == acknowledgedTick && item.active;
        });
    if (record == ledger->records.end()) return false;

    const auto root = std::find_if(
        terrain->rooted.begin(), terrain->rooted.end(),
        [&](const terraweave::AnchoredPlant& plant) {
            return plant.anchor.speciesId == bound->plant.speciesId &&
                   plant.anchor.plantKey == bound->plant.plantKey;
        });
    if (root == terrain->rooted.end()) return false;

    // Validate the replacement before touching World. Ground/root support
    // validation is TerraWeave's job, not a second PHYSIS soil solver.
    terraweave::RootSupportRegistry validator;
    validator.setSnapshot({repaired});
    if (terrain->stateRevision == UINT64_MAX)
        throw std::overflow_error("TerraWeave repair authority revision overflow");

    World staged = world;
    const auto* priorStress = static_cast<const World&>(staged)
        .tryGetConst<physis::NatureStressOutputComponent>(entity);
    if (!priorStress || !priorStress->fractured) return false;
    staged.get<physis::NatureStressOutputComponent>(entity) =
        physis::NatureStressOutputComponent{};
    if (!physis::rearmNatureFractureAfterRepair(
            staged,entity,acknowledgedKey,acknowledgedTick))
        return false;
    if (!retireNatureFall(staged,entity,acknowledgedKey,acknowledgedTick))
        return false;

    auto updated = *terrain;
    updated.rooted[std::size_t(root-terrain->rooted.begin())] = repaired;
    ++updated.stateRevision;
    staged.setResource<terraweave::TerraWeaveAuthorityResource>(
        std::move(updated));
    world = std::move(staged);
    return true;
}

struct NatureFallReconcileStats {
    std::size_t spawned = 0;
    std::size_t reused = 0;
    std::size_t failed = 0;
    std::size_t removed = 0;
};


struct NatureFallPhysicsPose {
    terraweave::AnchoredPlantId plant{};
    std::uint64_t eventKey = 0;
    b3Vec3 position{};
    b3Quat rotation = b3Quat_identity;
    b3Vec3 linearVelocity{};
    b3Vec3 angularVelocity{};
};

struct NatureFallPhysicsFrame {
    static constexpr std::size_t kMaxEntries = 8192;
    std::uint64_t sceneTick = 0;
    std::vector<NatureFallPhysicsPose> poses; // sorted by plant identity
};

// Optional native host physics checkpoints, outside deterministic ECS authority.
// Store exact IEEE-754 bits for same-backend Box3D replay; never feed this
// float representation into PHYSIS Fixed or network lockstep hashing.
inline void natureFallFloatIo(Archive& archive, float& value) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    archive(bits);
    if (!archive.writing()) {
        value = std::bit_cast<float>(bits);
        if (!std::isfinite(value)) archive.fail();
    }
}
inline void io(Archive& a, NatureFallPhysicsPose& pose) {
    a(pose.plant.speciesId, pose.plant.key, pose.eventKey);
    natureFallFloatIo(a, pose.position.x);
    natureFallFloatIo(a, pose.position.y);
    natureFallFloatIo(a, pose.position.z);
    natureFallFloatIo(a, pose.rotation.v.x);
    natureFallFloatIo(a, pose.rotation.v.y);
    natureFallFloatIo(a, pose.rotation.v.z);
    natureFallFloatIo(a, pose.rotation.s);
    natureFallFloatIo(a, pose.linearVelocity.x);
    natureFallFloatIo(a, pose.linearVelocity.y);
    natureFallFloatIo(a, pose.linearVelocity.z);
    natureFallFloatIo(a, pose.angularVelocity.x);
    natureFallFloatIo(a, pose.angularVelocity.y);
    natureFallFloatIo(a, pose.angularVelocity.z);
}
inline void io(Archive& a, NatureFallPhysicsFrame& frame) {
    a(frame.sceneTick);
    const auto count = a.count(frame.poses.size(), 72);
    if (count > NatureFallPhysicsFrame::kMaxEntries) {
        a.fail();
        return;
    }
    if (!a.writing()) frame.poses.resize(count);
    for (auto& pose : frame.poses) {
        if (!a.ok()) return;
        io(a, pose);
    }
    if (!a.writing()) {
        for (std::size_t i = 0; i < frame.poses.size(); ++i) {
            const auto& pose = frame.poses[i];
            if (!pose.plant.speciesId || !pose.plant.key || !pose.eventKey ||
                (i && !(frame.poses[i-1].plant < pose.plant))) {
                a.fail();
                return;
            }
        }
    }
}

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
    // Snapshot only bodies created by this PHYSIS host. External Terra
    // erosion bodies are NOT owned by us, so we cannot accidentally roll
    // their transforms back from a PHYSIS-only checkpoint.
    NatureFallPhysicsFrame captureFrame(
            const terraweave::FallenPlantPhysics& bodies) const {
        NatureFallPhysicsFrame frame;
        frame.sceneTick = scene_.tick();
        for (const auto& [plant, eventKey] : owned_) {
            const auto handle = bodies.body(plant);
            if (handle == physics::kNoBody || !scene_.valid(handle)) continue;
            NatureFallPhysicsPose pose;
            pose.plant = plant;
            pose.eventKey = eventKey;
            pose.position = scene_.position(handle);
            pose.rotation = scene_.rotation(handle);
            pose.linearVelocity = scene_.linearVelocity(handle);
            pose.angularVelocity = scene_.angularVelocity(handle);
            frame.poses.push_back(pose);
        }
        return frame;
    }

    // Restore after reconcile() has rebuilt any missing derived bodies.
    // Snapshot validity is checked first, so malformed data cannot apply a
    // partial set of velocities/poses. The host owns Scene tick/contacts.
    bool restoreFrame(const NatureFallPhysicsFrame& frame,
                      const terraweave::FallenPlantPhysics& bodies) {
        if (frame.poses.size() > NatureFallPhysicsFrame::kMaxEntries)
            return false;
        terraweave::AnchoredPlantId previous{};
        bool first = true;
        for (const auto& pose : frame.poses) {
            if (!first && !(previous < pose.plant)) return false;
            first = false;
            previous = pose.plant;
            auto it = owned_.find(pose.plant);
            if (it == owned_.end() || it->second != pose.eventKey)
                return false;
            const auto handle = bodies.body(pose.plant);
            if (handle == physics::kNoBody || !scene_.valid(handle))
                return false;
            const float values[] = {
                pose.position.x,pose.position.y,pose.position.z,
                pose.rotation.v.x,pose.rotation.v.y,pose.rotation.v.z,
                pose.rotation.s,
                pose.linearVelocity.x,pose.linearVelocity.y,
                pose.linearVelocity.z,
                pose.angularVelocity.x,pose.angularVelocity.y,
                pose.angularVelocity.z
            };
            for (float v : values) if (!std::isfinite(v)) return false;
            const float q2 = pose.rotation.v.x*pose.rotation.v.x +
                pose.rotation.v.y*pose.rotation.v.y +
                pose.rotation.v.z*pose.rotation.v.z +
                pose.rotation.s*pose.rotation.s;
            if (q2 < 0.95f || q2 > 1.05f) return false;
        }
        for (const auto& pose : frame.poses) {
            const auto handle = bodies.body(pose.plant);
            if (!scene_.setTransform(handle,pose.position,pose.rotation) ||
                !scene_.setLinearVelocity(handle,pose.linearVelocity) ||
                !scene_.setAngularVelocity(handle,pose.angularVelocity))
                return false;
        }
        return true;
    }

    // Call when an external scene resets or changes owner. This forgets
    // transient handles; next reconcile will rebuild from ECS authority.
    void forgetDerivedBodies() {owned_.clear();}
private:
    physics::Scene& scene_;
    std::map<terraweave::AnchoredPlantId,std::uint64_t> owned_;
};

// Convenience entry point for native game modules. Call at a committed
// authoritative post-simulation barrier, AFTER all PHYSIS and TerraWeave
// ECS systems, before Box3D::Scene::step(). No genre-specific dependencies.
// Physics output is derived and must not feed float data back into PHYSIS.
struct NatureFallHostBarrierResult {
    physis::NatureFractureDispatchStats delivery{};
    NatureFallReconcileStats physics{};
};
template<class SpeciesSpec>
inline NatureFallHostBarrierResult advanceNatureFallHostBarrier(
        World& world,
        terraweave::FallenPlantPhysics& bodies,
        NatureFallBox3DReconciler& reconciler,
        SpeciesSpec&& species,
        std::size_t maxEvents = 64,
        terraweave::TerraWeaveForestScene* forest = nullptr) {
    NatureFallHostBarrierResult result;
    result.delivery = dispatchPhysisTerraWeaveFalls(world,maxEvents);
    result.physics = reconciler.reconcile(
        static_cast<const World&>(world),bodies,
        std::forward<SpeciesSpec>(species),forest);
    return result;
}

} // namespace aiverse::world
