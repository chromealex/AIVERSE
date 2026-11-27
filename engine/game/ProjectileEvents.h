#pragma once
#include "Components.h"
#include "engine/sim/Simulation.h"

namespace aiverse::game {
// Adapter from authoritative projectile data to the generic motion lifecycle.
inline SimEvent projectileFinishedEvent(Entity entity, const Projectile& p, Vec3 endpoint) {
    auto event = SimEvent::motionFinished(entity, endpoint, p.owner);
    event.b = p.source;
    event.subjectType = p.sourceType;
    event.value = p.weapon;
    event.motion = std::make_shared<const MotionPathPresentation>(MotionPathPresentation{
        p.start, !p.program.enabled || p.program.origin == projectile::FlightOrigin::Emission});
    return event;
}
} // namespace aiverse::game
