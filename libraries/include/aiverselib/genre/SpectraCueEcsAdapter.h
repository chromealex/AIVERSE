// SpectraFX: optional read-only ECS adapter for CueScheduler.
// Native games of any genre provide one render-pose resolver. Gameplay ECS
// never stores particle handles; all mutable VFX state stays in presentation.
#pragma once

#include "EcsVfxPresentation.h" // exact existing SpectraSourceComponent type
#include "engine/fx/SpectraCueScheduler.h"
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::spectra {

using SourcePoseResolver = lib::genre::SpectraPresentationSystem::ResolvePose;

// Sync all sources, including generation-safe Entity identity. Missing/disabled
// components are omitted and are detached by CueScheduler::syncAttachments.
// This reads World but never mutates simulation components or the tick clock.
inline void syncEcsCueSources(const World& world, CueScheduler& scheduler,
                              const SourcePoseResolver& resolve) {
    std::vector<AttachedCue> sources;
    world.eachExistingConst<lib::genre::SpectraSourceComponent>(
        [&](Entity entity, const lib::genre::SpectraSourceComponent& source) {
            if(!world.alive(entity) || !source.enabled || source.effect.empty())return;
            fx::float3 position{},direction{0,1,0};
            if(!resolve || !resolve(world,entity,position,direction))return;
            AttachedCue cue;
            cue.key=entity.packed(); // generation avoids recycled-entity collisions
            cue.effect=source.effect;
            cue.position=position;
            cue.direction=direction;
            cue.scale=float(source.scale.toDouble());
            cue.revision=source.revision;
            cue.killOnDetach=source.killOnDetach;
            sources.push_back(std::move(cue));
        });
    scheduler.syncAttachments(sources);
}

// Adapts module-defined SimEvent types and fixed-point positions into
// presentation-only CueEvents. Bind type->effect names once with
// scheduler.bindCue(); use scheduler.acceptTick directly for non-default
// direction/scale/tint or custom event semantics.
// Call ONLY after a committed simulation tick. On rollback call
// scheduler.resetForRollback() BEFORE replaying restored ticks.
inline bool acceptSimCueTick(CueScheduler& scheduler, uint64_t tick,
                             std::span<const SimEvent> events) {
    std::vector<CueEvent> cues;
    cues.reserve(events.size());
    for(const SimEvent& event:events) {
        CueEvent cue;
        cue.type=event.type;
        cue.position={float(event.pos.x.toDouble()),
                      float(event.pos.y.toDouble()),
                      float(event.pos.z.toDouble())};
        cues.push_back(cue);
    }
    return scheduler.acceptTick(tick,cues);
}

} // namespace aiverse::spectra
