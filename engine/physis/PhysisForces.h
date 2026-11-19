// PHYSIS Stage 11: generic environmental force policy.
// No Box3D, renderer, water service, or gameplay genre dependency.
// Units: positions metres, velocities metres/second, acceleration m/s^2,
// area m^2, mass kg, force Newtons; Y is up.
#pragma once
#include "PhysisStorm.h"

namespace aiverse::physis {

// Optional native ECS input. The host (Physics, movement, automation) owns the
// canonical motion and writes this proxy before a PHYSIS force-sampling pass.
struct ForceReceiver {
    Vec3 position{};
    Vec3 velocity{};
    Fixed massKg = Fixed::fromInt(1);
    Fixed dragCoefficient = Fixed::fromRatio(1, 2);
    Fixed areaSqMeters = Fixed::fromInt(1);
    Fixed airDensity = Fixed::fromRatio(1225, 1000);
    Fixed gravityScale = Fixed::fromInt(1);
    bool enabled = true;
};

struct ForceResult {
    Vec3 windVelocity{};
    Vec3 gravityAcceleration{};
    Vec3 aerodynamicForce{};
    Vec3 gravityForce{}; // FULL gravity; host subtracts built-in gravity if any
    Vec3 totalForce{};
};

struct ForceOutput {
    ForceResult result{};
    std::uint64_t sampledTick = 0;
    bool valid = false;
};

// Pure policy: quadratic drag F = 0.5 * rho * Cd * A * |wind - velocity| *
// (wind - velocity), plus total environmental gravity. Optional Box3D adapter
// subtracts its world's gravity before applying force to avoid double gravity.
inline ForceResult evaluateForces(const World& world, const ForceReceiver& receiver) {
    ForceResult out;
    if (!receiver.enabled || receiver.massKg <= Fixed{}) return out;
    const Fixed mass = fxClamp(receiver.massKg, Fixed{}, Fixed::fromInt(100000));
    out.gravityAcceleration = gravityAt(world, receiver.position);
    out.windVelocity = windWithStormsAt(world, receiver.position);
    // Fixed-point squared length must not see untrusted/unbounded velocity.
    // Clamp individual source components *before* subtraction and squaring:
    // max |relative| <= 400 m/s per axis, well inside Q47.16 multiplication.
    const Fixed velocityCap = Fixed::fromInt(200);
    const auto bounded = [velocityCap](Vec3 v) {
        return Vec3{fxClamp(v.x, -velocityCap, velocityCap),
                    fxClamp(v.y, -velocityCap, velocityCap),
                    fxClamp(v.z, -velocityCap, velocityCap)};
    };
    const Vec3 relative = bounded(out.windVelocity) - bounded(receiver.velocity);
    const Fixed speed = fxMin(fxSqrt(relative.lengthSq()), velocityCap);
    const Fixed cd = fxClamp(receiver.dragCoefficient, Fixed{}, Fixed::fromInt(10));
    const Fixed area = fxClamp(receiver.areaSqMeters, Fixed{}, Fixed::fromInt(1000));
    const Fixed density = fxClamp(receiver.airDensity, Fixed{}, Fixed::fromInt(10));
    const Fixed gain = density * cd * area * speed / 2;
    out.aerodynamicForce = relative * gain;
    out.gravityForce = out.gravityAcceleration * mass *
        fxClamp(receiver.gravityScale, Fixed::fromInt(-100), Fixed::fromInt(100));
    const auto clampForces=[](Vec3 v) {
        const Fixed cap = Fixed::fromInt(10000000);
        return Vec3{fxClamp(v.x,-cap,cap),fxClamp(v.y,-cap,cap),fxClamp(v.z,-cap,cap)};
    };
    out.aerodynamicForce = clampForces(out.aerodynamicForce);
    out.gravityForce = clampForces(out.gravityForce);
    out.totalForce = clampForces(out.aerodynamicForce + out.gravityForce);
    return out;
}

// This ECS system *only publishes outputs*. Never integrates movement and
// never steps Box3D; consumer physics chooses when/how to apply the result.
inline void advanceForces(World& world) {
    const auto* state = world.resourceConst<State>();
    if (!state) throw std::logic_error("PHYSIS not initialized");
    auto* outputs = world.prepareExistingWrite<ForceOutput>();
    if (!outputs) return;
    const World& read = world;
    for (std::size_t i = 0; i < outputs->size(); ++i) {
        const Entity entity = outputs->entityAt(i);
        if (!world.alive(entity)) continue;
        auto& output = outputs->dataAt(i);
        const auto* receiver = read.tryGetConst<ForceReceiver>(entity);
        output.result = receiver ? evaluateForces(read, *receiver) : ForceResult{};
        output.sampledTick = state->tick;
        output.valid = receiver && receiver->enabled && receiver->massKg > Fixed{};
    }
}

} // namespace aiverse::physis
