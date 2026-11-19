// Optional PHYSIS -> AIVERSE Box3D Scene bridge. Physics owns integration.
// PHYSIS engine/physis core has NO dependency on physics or a game genre.
#pragma once
#include "Scene.h"
#include "../physis/PhysisForces.h"
#include <cmath>
#include <cstdint>

namespace aiverse::physics {

struct PhysisBodyMaterial {
    Fixed dragCoefficient = Fixed::fromRatio(1, 2);
    Fixed areaSqMeters = Fixed::fromInt(1);
    Fixed airDensity = Fixed::fromRatio(1225, 1000);
};

// Convert between Box3D float world and fixed PHYSIS sample space. This is
// a boundary adapter only: Box3D state is NOT a deterministic Fixed ECS clock.
inline bool physisFixed(float value, Fixed& result) {
    if (!std::isfinite(value) || std::abs(value) > 1000000.0f) return false;
    const double scaled = std::round(static_cast<double>(value) * Fixed::kOne);
    result = Fixed::fromRaw(static_cast<std::int64_t>(scaled));
    return true;
}
inline bool physisVec3(b3Vec3 value, Vec3& result) {
    return physisFixed(value.x, result.x) &&
           physisFixed(value.y, result.y) &&
           physisFixed(value.z, result.z);
}
inline b3Vec3 physisB3(Vec3 value) {
    return {static_cast<float>(value.x.toDouble()),
            static_cast<float>(value.y.toDouble()),
            static_cast<float>(value.z.toDouble())};
}

// Call on the *physics owner thread* once BEFORE each Scene::step(), after the
// game's canonical motion/input updates. Do not call from frame rendering.
// Scene uses its original gravity for normal Box3D integration; to avoid
// doubling it, apply only mass * (PHYSIS gravity - Box3D gravity) * scale.
// Wind drag is in Newtons and uses PHYSIS storms/gusts at current positions.
inline bool applyPhysisToBody(Scene& scene, const aiverse::World& physisWorld,
                              BodyHandle handle,
                              const PhysisBodyMaterial& material = {}) {
    if (!scene.valid(handle) || scene.motion(handle) != Motion::Dynamic)
        return false;
    const b3BodyId id = scene.native(handle);
    if (!b3Body_IsValid(id)) return false;
    const float bodyMass = b3Body_GetMass(id);
    const float gravityScale = b3Body_GetGravityScale(id);
    if (!std::isfinite(bodyMass) || bodyMass <= 0 ||
        !std::isfinite(gravityScale)) return false;
    physis::ForceReceiver receiver;
    if (!physisVec3(scene.position(handle), receiver.position) ||
        !physisVec3(scene.linearVelocity(handle), receiver.velocity) ||
        !physisFixed(bodyMass, receiver.massKg) ||
        !physisFixed(gravityScale, receiver.gravityScale))
        return false;
    receiver.dragCoefficient = material.dragCoefficient;
    receiver.areaSqMeters = material.areaSqMeters;
    receiver.airDensity = material.airDensity;
    const auto load = physis::evaluateForces(physisWorld, receiver);
    Vec3 nativeGravity{};
    if (!physisVec3(b3World_GetGravity(scene.world()), nativeGravity))
        return false;
    const Fixed mass = fxClamp(receiver.massKg, Fixed{}, Fixed::fromInt(100000));
    const Fixed scale = fxClamp(receiver.gravityScale,
                                 Fixed::fromInt(-100), Fixed::fromInt(100));
    const Vec3 deltaGravity = (load.gravityAcceleration - nativeGravity) *
                              mass * scale;
    const Fixed cap = Fixed::fromInt(10000000);
    const Vec3 force = load.aerodynamicForce + deltaGravity;
    const Vec3 bounded{fxClamp(force.x, -cap, cap),
                       fxClamp(force.y, -cap, cap),
                       fxClamp(force.z, -cap, cap)};
    return scene.applyForce(handle, physisB3(bounded));
}

} // namespace aiverse::physics
