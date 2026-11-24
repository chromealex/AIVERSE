// AIVERSE NEREID — deterministic water interaction events for all game genres.
// A pure ECS component/system stage, not an integrator or renderer.
// The host chooses how to present splash, foam, ripples, audio and wake VFX.
#pragma once
#include "NereidWaterSystems.h"
#include "NereidWakeDirection.h"
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

enum class WaterInteractionKind : uint8_t {
    None=0, Enter=1, Exit=2, SurfaceWake=3, SwitchBody=4
};

// Host-authored settings, persisted alongside the actor and independent of
// physics mass/colliders. Only horizontal movement creates surface wakes.
struct WaterInteractionComponent {
    Fixed wakeSpacing=2_fx;       // metres of horizontal motion per emitted wake
    Fixed surfaceWakeDepth=1_fx;  // only bodies this close to the surface create wakes
    Fixed wakeRadius=1_fx;        // render/audio hint, metres
    Fixed splashRadius=2_fx;      // render/audio hint, metres
    Fixed strength=1_fx;          // normalized effect multiplier, 0..16
    bool emitSurfaceWakes=true;
    bool enabled=true;
};

// Historical state is ECS-owned to preserve deterministic save/rollback.
// Does not cache native pointers or world indices.
struct WaterInteractionStateComponent {
    bool initialized=false;
    bool previouslySubmerged=false;
    Entity previousBody=kNullEntity;
    Fixed previousSurfaceY{};
    Vec3 previousPosition{};
    Vec3 wakeAnchor{};
};

// One bounded event per actor per tick. Consumers check
// output.tick == current.tick() and output.kind != None.
// A native host can copy it into a presentation event queue at the
// post-simulation read barrier; no mutable global event bus is needed.
struct WaterInteractionOutputComponent {
    WaterInteractionKind kind=WaterInteractionKind::None;
    Entity water=kNullEntity;
    Vec3 position{};
    Fixed radius{};
    Fixed strength{};
    uint32_t tick=0;
};

// Pure classification policy. The caller ensures that contact.sampledTick
// belongs to this tick and provides the current canonical probe position.
// Contact and probe are read-only. The returned state is authoritative ECS.
inline WaterInteractionOutputComponent evaluateWaterInteraction(
        const WaterContactComponent& contact,
        Vec3 probePosition,
        const WaterInteractionComponent& config,
        WaterInteractionStateComponent& state,
        uint32_t tick) {
    WaterInteractionOutputComponent output;
    output.tick=tick;
    const auto& water=contact.sample;
    const bool wet=water.found && water.submerged;
    if(!config.enabled || config.strength<=Fixed{} ||
       config.wakeSpacing<=Fixed{} || config.wakeSpacing>1000_fx ||
       config.surfaceWakeDepth<=Fixed{} || config.surfaceWakeDepth>1000_fx ||
       config.wakeRadius<=Fixed{} || config.wakeRadius>1000_fx ||
       config.splashRadius<=Fixed{} || config.splashRadius>1000_fx ||
       config.strength>16_fx) {
        // Disabled config is not an implicit surface-exit event.
        state.initialized=false;
        state.previouslySubmerged=false;
        return output;
    }
    if(!state.initialized) {
        state.initialized=true;
        state.previouslySubmerged=wet;
        state.previousBody=wet?water.body:kNullEntity;
        state.previousSurfaceY=wet?water.surfaceY:Fixed{};
        state.previousPosition=probePosition;
        state.wakeAnchor=probePosition;
        // Initializing an already submerged object does not fake an
        // entry splash during scene load, spawn or rollback.
        return output;
    }
    const bool switched=wet && state.previouslySubmerged &&
                        water.body!=state.previousBody;
    if(wet && !state.previouslySubmerged) {
        output.kind=WaterInteractionKind::Enter;
        output.water=water.body;
        output.position={probePosition.x,water.surfaceY,probePosition.z};
        output.radius=config.splashRadius;
        output.strength=config.strength;
        state.wakeAnchor=probePosition;
    } else if(!wet && state.previouslySubmerged) {
        output.kind=WaterInteractionKind::Exit;
        output.water=state.previousBody;
        output.position={probePosition.x,state.previousSurfaceY,probePosition.z};
        output.radius=config.splashRadius;
        output.strength=config.strength;
        state.wakeAnchor=probePosition;
    } else if(switched) {
        output.kind=WaterInteractionKind::SwitchBody;
        output.water=water.body;
        output.position={probePosition.x,water.surfaceY,probePosition.z};
        output.radius=config.splashRadius;
        output.strength=config.strength;
        state.wakeAnchor=probePosition;
    } else if(wet && config.emitSurfaceWakes &&
              water.depth<=config.surfaceWakeDepth) {
        const Fixed dx=probePosition.x-state.wakeAnchor.x;
        const Fixed dz=probePosition.z-state.wakeAnchor.z;
        // Compare squared distances to avoid nondeterministic float sqrt.
        if(dx*dx+dz*dz>=config.wakeSpacing*config.wakeSpacing) {
            output.kind=WaterInteractionKind::SurfaceWake;
            output.water=water.body;
            output.position={probePosition.x,water.surfaceY,probePosition.z};
            output.radius=config.wakeRadius;
            output.strength=config.strength;
            state.wakeAnchor=probePosition;
        }
    }
    state.previouslySubmerged=wet;
    state.previousBody=wet?water.body:kNullEntity;
    state.previousSurfaceY=wet?water.surfaceY:Fixed{};
    state.previousPosition=probePosition;
    return output;
}

// Schedule AFTER NEREID sampling, BEFORE the presentation read phase.
// Missing/stale inputs yield no output and reset state safely; never mutate
// water body ECS data or the host movement/physics components.
inline void addWaterInteractionSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* outputs=world.prepareExistingWrite<WaterInteractionOutputComponent>();
        auto* states=world.prepareExistingWrite<WaterInteractionStateComponent>();
        auto* directionPool=world.prepareExistingWrite<WaterWakeDirectionComponent>();
        if(!outputs)return;
        const World& read=world;
        for(size_t i=0;i<outputs->size();++i) {
            const Entity entity=outputs->entityAt(i);
            if(!read.alive(entity))continue;
            auto& output=outputs->dataAt(i);
            output={};
            output.tick=current.tick();
            auto* motion=directionPool?directionPool->tryGet(entity.index):nullptr;
            if(motion)*motion=WaterWakeDirectionComponent{};
            auto* state=states?states->tryGet(entity.index):nullptr;
            const auto* config=read.tryGetConst<WaterInteractionComponent>(entity);
            const auto* probe=read.tryGetConst<WaterProbeComponent>(entity);
            const auto* contact=read.tryGetConst<WaterContactComponent>(entity);
            if(!state)continue;
            if(!config || !probe || !contact || contact->sampledTick!=current.tick()) {
                state->initialized=false;
                continue;
            }
            const Vec3 previous=state->previousPosition;
            const bool hadHistory=state->initialized;
            output=evaluateWaterInteraction(*contact,probe->position,*config,
                                            *state,current.tick());
            if(motion && hadHistory &&
               output.kind==WaterInteractionKind::SurfaceWake)
                *motion=waterWakeDirection(previous,probe->position,current.tick());
        }
    });
}

inline void registerNereidInteractionPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterInteractionComponent>("nereid.interaction",1);
    sim.registerPersistentComponent<WaterInteractionStateComponent>("nereid.interaction_state",1);
    sim.registerPersistentComponent<WaterInteractionOutputComponent>("nereid.interaction_output",1);
    sim.registerPersistentComponent<WaterWakeDirectionComponent>("nereid.wake_direction",1);
}

inline void io(Archive& a,WaterInteractionComponent& c) {
    a(c.wakeSpacing,c.surfaceWakeDepth,c.wakeRadius,c.splashRadius,c.strength,
      c.emitSurfaceWakes,c.enabled);
}
inline void io(Archive& a,WaterInteractionStateComponent& s) {
    a(s.initialized,s.previouslySubmerged,s.previousBody,s.previousSurfaceY,
      s.previousPosition,s.wakeAnchor);
}
inline void io(Archive& a,WaterInteractionOutputComponent& o) {
    a(o.kind,o.water,o.position,o.radius,o.strength,o.tick);
}

} // namespace aiverse::lib::nereid
