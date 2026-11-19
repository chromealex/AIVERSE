// PHYSIS Stage 79: generic structural exposure to nature.
// Reuses the canonical PHYSIS aerodynamic ForceOutput and NatureContact,
// never resimulates wind, integrates rigid bodies or modifies terrain/foliage.
// A host/Box3D/TerraWeave adapter consumes the fracture transition at its
// authoritative barrier; visual-only instances may share one cluster proxy.
#pragma once
#include "PhysisNatureSampling.h"
#include "PhysisForces.h"
#include <cstdint>

namespace aiverse::physis {

// Optional authored per-entity (or per-foliage-cluster) material properties.
// Force limit is shear/uplift Newtons; bending limit is Newton-metres.
// Rate coefficients apply per committed PHYSIS climate tick, not per second.
struct NatureStressReceiverComponent {
    Fixed leverArmMeters = Fixed::fromInt(1);
    Fixed forceLimitN = Fixed::fromInt(1000);
    Fixed bendingLimitNm = Fixed::fromInt(2000);
    Fixed fatigueRate = Fixed::fromRatio(1, 64);
    Fixed recoveryRate = Fixed::fromRatio(1, 512);
    Fixed fireDamageRate = Fixed::fromRatio(1, 128);
    Fixed quakeDamageRate = Fixed::fromRatio(1, 128);
    Fixed catastrophicLoadRatio = Fixed::fromInt(4);
    bool enabled = true;
};

// Cumulative damage is authoritative ECS output with rollback/persistence.
// Consumers must act only on newlyFractured once, never directly from FX.
// A true fracture is latched until a gameplay repair/replacement explicitly
// changes this component; PHYSIS itself does not spawn falling rigid bodies.
struct NatureStressOutputComponent {
    Vec3 aerodynamicForceN{};
    Fixed windLoadN{};
    Fixed bendingMomentNm{};
    Fixed normalizedLoad{};
    Fixed accumulatedDamage{}; // 0..1
    std::uint64_t sampledClimateTick = 0;
    bool fractured = false;
    bool newlyFractured = false;
    bool valid = false;
};

inline void io(Archive& archive, NatureStressReceiverComponent& v) {
    archive(v.leverArmMeters, v.forceLimitN, v.bendingLimitNm,
            v.fatigueRate, v.recoveryRate, v.fireDamageRate,
            v.quakeDamageRate, v.catastrophicLoadRatio, v.enabled);
}
inline void io(Archive& archive, NatureStressOutputComponent& v) {
    archive(v.aerodynamicForceN, v.windLoadN, v.bendingMomentNm,
            v.normalizedLoad, v.accumulatedDamage, v.sampledClimateTick,
            v.fractured, v.newlyFractured, v.valid);
}

inline bool validNatureStressReceiver(const NatureStressReceiverComponent& v) {
    return v.enabled && v.leverArmMeters >= Fixed{} &&
        v.forceLimitN > Fixed{} && v.bendingLimitNm > Fixed{} &&
        v.fatigueRate >= Fixed{} && v.recoveryRate >= Fixed{} &&
        v.fireDamageRate >= Fixed{} && v.quakeDamageRate >= Fixed{} &&
        v.catastrophicLoadRatio > Fixed{};
}

// Read: ForceReceiver/ForceOutput, NatureProbe/NatureContact and immutable
// climate State. Write: NatureStressOutput only. No movement, no geometry,
// no fluids, no Box3D stepping and no genre-specific ownership.
// Per-entity update is idempotent within the same committed climate tick.
inline void advanceNatureStress(World& world) {
    const auto* climate = world.resourceConst<State>();
    if (!climate) throw std::logic_error("PHYSIS not initialized");
    auto* outputs = world.prepareExistingWrite<NatureStressOutputComponent>();
    if (!outputs) return;
    const World& read = world;
    const Fixed forceCap = Fixed::fromInt(200000); // safe for lengthSq()
    const Fixed leverCap = Fixed::fromInt(1000);
    const Fixed strengthCap = Fixed::fromInt(10000000);
    const Fixed ratioCap = Fixed::fromInt(16);
    const Fixed one = Fixed::fromInt(1);
    for (std::size_t i = 0; i < outputs->size(); ++i) {
        const Entity entity = outputs->entityAt(i);
        if (!read.alive(entity)) continue;
        auto& output = outputs->dataAt(i);
        const Fixed previousDamage =
            fxClamp(output.accumulatedDamage, Fixed{}, one);
        const bool previouslyFractured = output.fractured;
        // Never advance an already resolved sample twice (fatigue, fire and
        // quake rates must not accidentally double on duplicate scheduling).
        if (output.valid && output.sampledClimateTick == climate->tick) continue;
        output = NatureStressOutputComponent{};
        output.accumulatedDamage = previousDamage;
        output.fractured = previouslyFractured;
        const auto* config = read.tryGetConst<NatureStressReceiverComponent>(entity);
        const auto* forces = read.tryGetConst<ForceOutput>(entity);
        const auto* forceReceiver = read.tryGetConst<ForceReceiver>(entity);
        const auto* nature = read.tryGetConst<NatureContactComponent>(entity);
        const auto* natureProbe = read.tryGetConst<NatureProbeComponent>(entity);
        if (!config || !validNatureStressReceiver(*config) ||
            !forces || !forces->valid || !forceReceiver || !forceReceiver->enabled ||
            !nature || !nature->valid || !natureProbe || !natureProbe->enabled ||
            forces->sampledTick != climate->tick ||
            nature->climateTick != climate->tick ||
            nature->position != natureProbe->position ||
            nature->position != forceReceiver->position)
            continue;

        // Use only aerodynamic force. Gravity is already accounted for
        // by a host's rigid-body integration and must not double-load here.
        const Vec3 windForce{
            fxClamp(forces->result.aerodynamicForce.x, -forceCap, forceCap),
            fxClamp(forces->result.aerodynamicForce.y, -forceCap, forceCap),
            fxClamp(forces->result.aerodynamicForce.z, -forceCap, forceCap)};
        const Fixed forceMagnitude = windForce.length();
        const Fixed horizontal = Vec2{windForce.x, windForce.z}.length();
        const Fixed arm = fxClamp(config->leverArmMeters, Fixed{}, leverCap);
        const Fixed forceLimit = fxClamp(config->forceLimitN, one, strengthCap);
        const Fixed momentLimit = fxClamp(config->bendingLimitNm, one, strengthCap);
        output.aerodynamicForceN = windForce;
        output.windLoadN = forceMagnitude;
        output.bendingMomentNm = horizontal * arm;
        output.normalizedLoad = fxClamp(
            fxMax(forceMagnitude / forceLimit,
                  output.bendingMomentNm / momentLimit),
            Fixed{}, ratioCap);
        output.sampledClimateTick = climate->tick;
        output.valid = true;

        if (output.fractured) continue; // preserve latched fracture
        const Fixed over = fxMax(output.normalizedLoad - one, Fixed{});
        const Fixed fatigue = fxClamp(config->fatigueRate, Fixed{}, one);
        const Fixed fireRate = fxClamp(config->fireDamageRate, Fixed{}, one);
        const Fixed quakeRate = fxClamp(config->quakeDamageRate, Fixed{}, one);
        const Fixed fire = nature->hasHazards ?
            fxClamp(nature->fire, Fixed{}, one) : Fixed{};
        const Fixed quake = nature->hasHazards ?
            fxClamp(nature->quake, Fixed{}, one) : Fixed{};
        const Fixed increase = fxClamp(over * fatigue +
            fire * fireRate + quake * quakeRate, Fixed{}, one);
        const Fixed recovery = (over == Fixed{} && increase == Fixed{})
            ? fxClamp(config->recoveryRate, Fixed{}, one) : Fixed{};
        output.accumulatedDamage = fxClamp(
            previousDamage + increase - recovery, Fixed{}, one);
        const Fixed snap = fxClamp(config->catastrophicLoadRatio,
                                   one, ratioCap);
        if (output.normalizedLoad >= snap ||
            output.accumulatedDamage >= one) {
            output.accumulatedDamage = one;
            output.fractured = true;
            output.newlyFractured = true;
        }
    }
}

} // namespace aiverse::physis
