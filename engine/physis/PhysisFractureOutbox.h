// PHYSIS Stage 82 — optional deterministic fracture event outbox.
// Only publishes a persisted host-facing ECS event after NatureStress;
// PHYSIS does not spawn Box3D bodies, mutate TerraWeave vegetation or run VFX.
// A native host consumes and acknowledges the event at its own barrier.
#pragma once
#include "PhysisNatureStress.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::physis {

enum class NatureFractureDelivery : std::uint8_t {
    Empty = 0, Pending = 1, Acknowledged = 2
};

// Attach on interested objects or foliage CLUSTER proxies before they fracture.
// Pending remains across future ticks and native save/load until the host
// acknowledges; Acknowledged survives rollback/save to prevent repeat delivery
// on the same authoritative timeline.
struct NatureFractureOutboxComponent {
    std::uint64_t eventKey = 0;
    std::uint64_t climateTick = 0;
    Vec3 aerodynamicForceN{};
    Fixed bendingMomentNm{};
    Fixed accumulatedDamage{};
    NatureFractureDelivery delivery = NatureFractureDelivery::Empty;
};

inline void io(Archive& a, NatureFractureOutboxComponent& v) {
    a(v.eventKey, v.climateTick, v.aerodynamicForceN,
      v.bendingMomentNm, v.accumulatedDamage, v.delivery);
}

// Stable, source-entity generation-safe identity. This is a deterministic
// internal event marker, not a cryptographic ID or a network authentication
// token; consumer deduplication should use (Entity, climateTick, eventKey).
inline std::uint64_t natureFractureEventKey(
        Entity entity, std::uint64_t climateTick) {
    const std::uint64_t key =
        mix64(entity.packed() ^ mix64(climateTick) ^
              0x5048595349534652ull); // "PHYSISFR"
    return key ? key : 1;
}

// Read: stress output + PHYSIS authoritative climate clock.
// Write: outbox ONLY. No structural ECS changes or host side effects.
// Call after physis.nature_stress (the installer schedules this).
inline void advanceNatureFractureOutbox(World& world) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    auto* outboxes = world.prepareExistingWrite<NatureFractureOutboxComponent>();
    if (!outboxes) return;
    const World& read = world;
    for (std::size_t i = 0; i < outboxes->size(); ++i) {
        const Entity entity = outboxes->entityAt(i);
        if (!read.alive(entity)) continue;
        auto& outbox = outboxes->dataAt(i);
        // Pending must not disappear if delivery is delayed by several ticks;
        // acknowledged entities never republish the same fracture.
        if (outbox.delivery != NatureFractureDelivery::Empty) continue;
        // Keep the prior event identity as a tombstone after explicit repair.
        // A second fracture in the SAME climate tick must not reuse its key.
        if (outbox.eventKey != 0 && state->tick <= outbox.climateTick)
            continue;
        const auto* stress =
            read.tryGetConst<NatureStressOutputComponent>(entity);
        if (!stress || !stress->valid || !stress->fractured ||
            !stress->newlyFractured ||
            stress->sampledClimateTick != state->tick) continue;

        outbox.eventKey = natureFractureEventKey(entity, state->tick);
        outbox.climateTick = state->tick;
        outbox.aerodynamicForceN = stress->aerodynamicForceN;
        outbox.bendingMomentNm = stress->bendingMomentNm;
        outbox.accumulatedDamage = stress->accumulatedDamage;
        outbox.delivery = NatureFractureDelivery::Pending;
    }
}

struct NatureFractureEvent {
    Entity entity = kNullEntity;
    std::uint64_t eventKey = 0;
    std::uint64_t climateTick = 0;
    Vec3 aerodynamicForceN{};
    Fixed bendingMomentNm{};
    Fixed accumulatedDamage{};
};

// Stable ordering regardless of ECS pool insertion order or thread lanes.
// Read-only collection: the host may map entity -> TerraWeave AnchoredPlantId
// and process pending events at its own authoritative gameplay/physics barrier.
// Do NOT include TerraWeave or Box3D in this PHYSIS core header.
inline std::vector<NatureFractureEvent> pendingNatureFractures(
        const World& world) {
    std::vector<NatureFractureEvent> events;
    world.eachExistingConst<NatureFractureOutboxComponent>(
        [&](Entity entity, const NatureFractureOutboxComponent& out) {
            if (!world.alive(entity) ||
                out.delivery != NatureFractureDelivery::Pending ||
                out.eventKey == 0) return;
            events.push_back({entity, out.eventKey, out.climateTick,
                              out.aerodynamicForceN, out.bendingMomentNm,
                              out.accumulatedDamage});
        });
    std::sort(events.begin(), events.end(),
        [](const NatureFractureEvent& a, const NatureFractureEvent& b) {
            return a.entity.packed() < b.entity.packed();
        });
    return events;
}

// Call ONLY after the host has durably staged/applied its authoritative
// reaction and deduplication state. Wrong/stale/duplicate acknowledgements
// are rejected, and failed reactions leave Pending intact for retry.
// Native Box3D pose itself is float/server-authoritative and must be
// reconciled by the host if rewound to a previous ECS snapshot.
inline bool acknowledgeNatureFracture(
        World& world, Entity entity, std::uint64_t eventKey,
        std::uint64_t climateTick) {
    if (!world.alive(entity)) return false;
    const auto* prior = static_cast<const World&>(world)
        .tryGetConst<NatureFractureOutboxComponent>(entity);
    if (!prior || prior->delivery != NatureFractureDelivery::Pending ||
        prior->eventKey != eventKey ||
        prior->climateTick != climateTick) return false;
    world.get<NatureFractureOutboxComponent>(entity).delivery =
        NatureFractureDelivery::Acknowledged;
    return true;
}

// Call only after a trusted host has durably repaired/replanted the object,
// reconciled its external physical/vegetation state, and cleared the PHYSIS
// stress latch (fractured=false, newlyFractured=false, damage=0).
// This is an explicit lifecycle transition, never an automatic weather pass.
// The acknowledged event key/tick remain as a persistent tombstone to prevent
// duplicate emission if repair and a new fracture happen in the same tick.
// A new fracture in a LATER tick gets a new identity, and old acknowledgements
// can never acknowledge it.
inline bool rearmNatureFractureAfterRepair(World& world, Entity entity,
                                          std::uint64_t acknowledgedKey,
                                          std::uint64_t acknowledgedTick) {
    if (!world.alive(entity)) return false;
    const auto* climate = static_cast<const World&>(world).resourceConst<State>();
    const auto* outbox = static_cast<const World&>(world)
        .tryGetConst<NatureFractureOutboxComponent>(entity);
    const auto* stress = static_cast<const World&>(world)
        .tryGetConst<NatureStressOutputComponent>(entity);
    if (!climate || !outbox || !stress ||
        outbox->delivery != NatureFractureDelivery::Acknowledged ||
        outbox->eventKey == 0 ||
        outbox->eventKey != acknowledgedKey ||
        outbox->climateTick != acknowledgedTick ||
        climate->tick <= outbox->climateTick ||
        stress->fractured || stress->newlyFractured ||
        stress->accumulatedDamage != Fixed{})
        return false;
    auto& writable = world.get<NatureFractureOutboxComponent>(entity);
    writable.delivery = NatureFractureDelivery::Empty;
    writable.aerodynamicForceN = Vec3{};
    writable.bendingMomentNm = Fixed{};
    writable.accumulatedDamage = Fixed{};
    return true;
}

} // namespace aiverse::physis
