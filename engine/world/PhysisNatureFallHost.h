// Stage 87: host-owned, deterministic fracture -> rooted-plant fall ledger.
// Lives outside PHYSIS core. Simulation + World own save/rollback; Box3D is
// only a reconstructed consumer. This is not NEREID water or Terra soil logic.
#pragma once
#include "engine/physis/PhysisFractureDelivery.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <compare>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::world {

struct NatureFallPlantId {
    std::uint32_t speciesId = 0;
    std::uint64_t plantKey = 0;
    constexpr auto operator<=>(const NatureFallPlantId&) const = default;
};

struct NatureFallBinding {
    std::uint64_t entityPacked = 0;
    NatureFallPlantId plant{};
};

struct NatureFallRecord {
    std::uint64_t entityPacked = 0;
    std::uint64_t climateTick = 0;
    std::uint64_t eventKey = 0;
    NatureFallPlantId plant{};
    // TerraWeave PlantFallEvent payload, stored as exact integers. No float.
    std::uint8_t habit = 0; // 0 tree, 1 shrub, never 2 grass
    std::int64_t xCm = 0, zCm = 0;
    std::int32_t plantedHeightCm = 0;
    std::int32_t directionXPermille = 0, directionZPermille = 0;
    std::uint32_t supportPermille = 0, exposedCm = 0;
    std::uint64_t soilRevision = 0;
    bool active = true; // false after a trusted repair/replant
};

struct NatureFallHostState {
    static constexpr std::size_t kMaxBindings = 8192;
    static constexpr std::size_t kMaxRecords = 32768;
    std::uint64_t revision = 0;
    std::vector<NatureFallBinding> bindings; // sorted by entityPacked
    std::vector<NatureFallRecord> records;  // sorted by (entity,tick,key)
};

inline void io(Archive& a, NatureFallPlantId& value) {
    a(value.speciesId, value.plantKey);
}
inline void io(Archive& a, NatureFallBinding& value) {
    a(value.entityPacked, value.plant);
}
inline void io(Archive& a, NatureFallRecord& value) {
    a(value.entityPacked, value.climateTick, value.eventKey, value.plant,
      value.habit, value.xCm, value.zCm, value.plantedHeightCm,
      value.directionXPermille, value.directionZPermille,
      value.supportPermille, value.exposedCm, value.soilRevision, value.active);
}
inline bool natureFallValid(const NatureFallRecord& r) {
    return r.entityPacked != kNullEntity.packed() && r.eventKey != 0 &&
           r.plant.speciesId != 0 && r.plant.plantKey != 0 &&
           r.habit <= 1 && r.directionXPermille >= -1000 &&
           r.directionXPermille <= 1000 &&
           r.directionZPermille >= -1000 &&
           r.directionZPermille <= 1000 &&
           r.supportPermille <= 1000;
}
inline bool natureFallRecordLess(const NatureFallRecord& a,
                                 const NatureFallRecord& b) {
    if (a.entityPacked != b.entityPacked)
        return a.entityPacked < b.entityPacked;
    if (a.climateTick != b.climateTick)
        return a.climateTick < b.climateTick;
    return a.eventKey < b.eventKey;
}

// Dedicated bounded v1 resource. Defaults OFF: an opt-in game must explicitly
// register this before the persistent ECS registry is sealed in setup().
inline void io(Archive& a, NatureFallHostState& value) {
    a(value.revision);
    std::uint32_t binds = a.count(value.bindings.size(), 20);
    if (binds > NatureFallHostState::kMaxBindings) { a.fail(); return; }
    if (!a.writing()) value.bindings.resize(binds);
    for (auto& item : value.bindings) {
        if (!a.ok()) return;
        a(item);
    }
    std::uint32_t count = a.count(value.records.size(), 76);
    if (count > NatureFallHostState::kMaxRecords) { a.fail(); return; }
    if (!a.writing()) value.records.resize(count);
    for (auto& item : value.records) {
        if (!a.ok()) return;
        a(item);
    }
    if (!a.writing() && a.ok()) {
        for (std::size_t i = 0; i < value.bindings.size(); ++i) {
            const auto& b = value.bindings[i];
            if (b.entityPacked == kNullEntity.packed() ||
                !b.plant.speciesId || !b.plant.plantKey ||
                (i && value.bindings[i-1].entityPacked >= b.entityPacked)) {
                a.fail(); return;
            }
        }
        // Reject aliasing the same rooted plant to multiple ECS entities.
        // Corrupt imported state must never allow duplicate physical bodies.
        std::set<NatureFallPlantId> seenPlants;
        for (const auto& binding : value.bindings) {
            if (!seenPlants.insert(binding.plant).second) {
                a.fail(); return;
            }
        }
        for (std::size_t i = 0; i < value.records.size(); ++i) {
            if (!natureFallValid(value.records[i]) ||
                (i && !natureFallRecordLess(
                    value.records[i-1], value.records[i]))) {
                a.fail(); return;
            }
        }
    }
}

inline void installNatureFallHost(Simulation& sim) {
    sim.registerPersistentResource<NatureFallHostState>(
        "world.physis_nature_fall", 1);
    if (!sim.world().resourceConst<NatureFallHostState>())
        sim.world().setResource<NatureFallHostState>(NatureFallHostState{});
}

// A binding is a generation-safe ECS Entity -> TerraWeave stable plant key.
// One active entity may not claim a plant already bound to another entity.
inline bool bindNatureFallPlant(World& world, Entity entity,
                               NatureFallPlantId plant) {
    if (!world.alive(entity) || !plant.speciesId || !plant.plantKey)
        return false;
    const auto* original = world.resourceConst<NatureFallHostState>();
    if (!original) throw std::logic_error("nature fall host not installed");
    const auto packed = entity.packed();
    const auto it = std::lower_bound(original->bindings.begin(),
        original->bindings.end(), packed,
        [](const NatureFallBinding& a, std::uint64_t id) {
            return a.entityPacked < id;
        });
    if (it != original->bindings.end() && it->entityPacked == packed)
        return it->plant == plant;
    if (original->bindings.size() >= NatureFallHostState::kMaxBindings)
        return false;
    for (const auto& b : original->bindings)
        if (b.plant == plant) return false;
    auto updated = *original; // staged before touching World
    auto insertAt = std::lower_bound(updated.bindings.begin(),
        updated.bindings.end(), packed,
        [](const NatureFallBinding& a, std::uint64_t id) {
            return a.entityPacked < id;
        });
    updated.bindings.insert(insertAt, NatureFallBinding{packed, plant});
    if (updated.revision == UINT64_MAX)
        throw std::overflow_error("nature fall host revision overflow");
    ++updated.revision;
    world.setResource<NatureFallHostState>(std::move(updated));
    return true;
}

// Returns true only when the record is already stored in the authoritative
// World resource. Invoke within the PHYSIS dispatcher sink. The enclosing
// committed World save/snapshot is the durable record, NOT a Box3D body.
// Physical reconciliation must run in a separate post-tick owner barrier.
inline physis::NatureFractureDisposition stageNatureFall(
        World& world, const physis::NatureFractureEvent& source,
        const NatureFallRecord& record) {
    using physis::NatureFractureDisposition;
    if (!world.alive(source.entity) || !natureFallValid(record) ||
        record.entityPacked != source.entity.packed() ||
        record.eventKey != source.eventKey ||
        record.climateTick != source.climateTick || !record.active)
        return NatureFractureDisposition::Retry;
    const auto* box = static_cast<const World&>(world)
        .tryGetConst<physis::NatureFractureOutboxComponent>(source.entity);
    const auto* state = world.resourceConst<NatureFallHostState>();
    if (!state) throw std::logic_error("nature fall host not installed");
    if (!box || box->delivery != physis::NatureFractureDelivery::Pending ||
        box->eventKey != source.eventKey ||
        box->climateTick != source.climateTick)
        return NatureFractureDisposition::Retry;
    const auto binding = std::lower_bound(state->bindings.begin(),
        state->bindings.end(), source.entity.packed(),
        [](const NatureFallBinding& b, std::uint64_t key) {
            return b.entityPacked < key;
        });
    if (binding == state->bindings.end() ||
        binding->entityPacked != source.entity.packed() ||
        binding->plant != record.plant)
        return NatureFractureDisposition::Retry;

    const auto it = std::lower_bound(state->records.begin(),
        state->records.end(), record, natureFallRecordLess);
    if (it != state->records.end() &&
        !natureFallRecordLess(record, *it) &&
        !natureFallRecordLess(*it, record)) {
        // A replay must match the original payload byte-for-byte.
        // Collision or changed terrain is not a new authoritative event.
        const auto& old = *it;
        if (old.plant != record.plant || old.habit != record.habit ||
            old.xCm != record.xCm || old.zCm != record.zCm ||
            old.plantedHeightCm != record.plantedHeightCm ||
            old.directionXPermille != record.directionXPermille ||
            old.directionZPermille != record.directionZPermille ||
            old.supportPermille != record.supportPermille ||
            old.exposedCm != record.exposedCm ||
            old.soilRevision != record.soilRevision || !old.active)
            return NatureFractureDisposition::Retry;
        return NatureFractureDisposition::DurablyCommitted;
    }
    if (state->records.size() >= NatureFallHostState::kMaxRecords)
        return NatureFractureDisposition::Retry;
    for (const auto& prior : state->records)
        if (prior.plant == record.plant && prior.active)
            return NatureFractureDisposition::Retry;
    auto updated = *state;
    auto pos = std::lower_bound(updated.records.begin(),
        updated.records.end(), record, natureFallRecordLess);
    updated.records.insert(pos, record);
    if (updated.revision == UINT64_MAX)
        throw std::overflow_error("nature fall host revision overflow");
    ++updated.revision;
    world.setResource<NatureFallHostState>(std::move(updated));
    return NatureFractureDisposition::DurablyCommitted;
}

// Call after PHYSIS rearm succeeded and the game has committed the repaired
// rooted-plant state. Tombstones remain for replay/deduplication; Box3D's
// derived body is removed by reconcile, not by authoritative simulation.
inline bool retireNatureFall(World& world, Entity entity,
                             std::uint64_t key, std::uint64_t tick) {
    const auto* original = world.resourceConst<NatureFallHostState>();
    if (!original) throw std::logic_error("nature fall host not installed");
    const NatureFallRecord lookup{entity.packed(), tick, key};
    const auto it = std::lower_bound(original->records.begin(),
        original->records.end(), lookup, natureFallRecordLess);
    if (it == original->records.end() ||
        natureFallRecordLess(lookup, *it) || natureFallRecordLess(*it, lookup) ||
        !it->active) return false;
    const auto* box = static_cast<const World&>(world)
        .tryGetConst<physis::NatureFractureOutboxComponent>(entity);
    if (!world.alive(entity) || !box ||
        box->delivery != physis::NatureFractureDelivery::Empty ||
        box->eventKey != key || box->climateTick != tick)
        return false;
    auto updated = *original;
    updated.records[std::size_t(it - original->records.begin())].active = false;
    if (updated.revision == UINT64_MAX)
        throw std::overflow_error("nature fall host revision overflow");
    ++updated.revision;
    world.setResource<NatureFallHostState>(std::move(updated));
    return true;
}

} // namespace aiverse::world
