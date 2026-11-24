// AIVERSE NEREID -- entry / exit / switch / surface-wake events for sphere water (planet seas), any genre.
// The same WaterInteraction components and output as plane water (NereidInteractions.h); positions are on the
// sphere surface, wakes measure the 3D distance moved. Schedule after addSphereWaterSamplingSystem.
#pragma once
#include "engine/world/nereid/NereidInteractions.h"
#include "engine/world/nereid/NereidSphereWater.h"

namespace aiverse::lib::nereid {

// Separate history for sphere contacts (the plane interaction system resets its own state on entities
// without a plane contact). Same fields; previousSurfaceY holds the surface radius.
struct WaterSphereInteractionStateComponent : WaterInteractionStateComponent {};
inline void io(Archive& a, WaterSphereInteractionStateComponent& s) { io(a, static_cast<WaterInteractionStateComponent&>(s)); }

// Pure policy. `state.previousSurfaceY` holds the previous surface radius; `wakeAnchor` the last wake point.
inline WaterInteractionOutputComponent evaluateSphereWaterInteraction(const SphereWaterSample& water, Vec3 probe,
        const WaterInteractionComponent& config, WaterInteractionStateComponent& state, uint32_t tick) {
    WaterInteractionOutputComponent output;
    output.tick = tick;
    const bool wet = water.found && water.submerged;
    if (!config.enabled || config.strength <= Fixed{} || config.strength > 16_fx ||
        config.wakeSpacing <= Fixed{} || config.wakeSpacing > 1000_fx ||
        config.surfaceWakeDepth <= Fixed{} || config.surfaceWakeDepth > 1000_fx ||
        config.wakeRadius <= Fixed{} || config.wakeRadius > 1000_fx ||
        config.splashRadius <= Fixed{} || config.splashRadius > 1000_fx) {
        state.initialized = false;
        state.previouslySubmerged = false;
        return output;
    }
    auto emit = [&](WaterInteractionKind kind, Entity body, Vec3 at, Fixed radius) {
        output.kind = kind; output.water = body; output.position = at;
        output.radius = radius; output.strength = config.strength;
        state.wakeAnchor = probe;
    };
    if (!state.initialized) {   // spawning / loading in water is no splash
        state.initialized = true;
    } else if (wet && !state.previouslySubmerged) {
        emit(WaterInteractionKind::Enter, water.body, water.surfacePoint, config.splashRadius);
    } else if (!wet && state.previouslySubmerged) {
        emit(WaterInteractionKind::Exit, state.previousBody, water.found ? water.surfacePoint : probe, config.splashRadius);
    } else if (wet && water.body != state.previousBody) {
        emit(WaterInteractionKind::SwitchBody, water.body, water.surfacePoint, config.splashRadius);
    } else if (wet && config.emitSurfaceWakes && water.depth <= config.surfaceWakeDepth) {
        const Vec3 d = probe - state.wakeAnchor;
        if (d.lengthSq() >= config.wakeSpacing * config.wakeSpacing)
            emit(WaterInteractionKind::SurfaceWake, water.body, water.surfacePoint, config.wakeRadius);
    }
    if (!wet || !state.previouslySubmerged) { if (output.kind == WaterInteractionKind::None) state.wakeAnchor = probe; }
    state.previouslySubmerged = wet;
    state.previousBody = wet ? water.body : kNullEntity;
    state.previousSurfaceY = wet ? water.surfaceRadius : Fixed{};
    state.previousPosition = probe;
    return output;
}

// Entities with a WaterSphereContactComponent (+ WaterInteractionComponent, WaterSphereInteractionStateComponent,
// WaterInteractionOutputComponent) get their output from it. With plane water too, run it after the plane system.
inline void addSphereWaterInteractionSystem(Simulation& sim, std::string name) {
    sim.addSystem(std::move(name), [](Simulation& current) {
        World& world = current.world();
        if (!world.countExisting<WaterSphereContactComponent>()) return;
        auto* outputs = world.prepareExistingWrite<WaterInteractionOutputComponent>();
        auto* states = world.prepareExistingWrite<WaterSphereInteractionStateComponent>();
        if (!outputs) return;
        const World& read = world;
        for (size_t i = 0; i < outputs->size(); ++i) {
            const Entity e = outputs->entityAt(i);
            if (!read.alive(e)) continue;
            const auto* contact = read.tryGetConst<WaterSphereContactComponent>(e);
            if (!contact) continue;
            auto& output = outputs->dataAt(i);
            output = {};
            output.tick = current.tick();
            auto* state = states ? states->tryGet(e.index) : nullptr;
            const auto* config = read.tryGetConst<WaterInteractionComponent>(e);
            const auto* probe = read.tryGetConst<WaterProbeComponent>(e);
            if (!state) continue;
            if (!config || !probe || contact->sampledTick != current.tick()) { state->initialized = false; continue; }
            output = evaluateSphereWaterInteraction(contact->sample, probe->position, *config, *state, current.tick());
        }
    });
}

// Give an entity with a Transform everything sphere water needs: probe, contact, optional buoyancy (host owns
// integration) and optional entry/exit/wake events. Existing components are kept.
struct SphereWaterActorOptions {
    bool buoyancy = false;
    WaterBuoyancyComponent buoyancyParams{};
    bool interactions = true;
    WaterInteractionComponent interaction{};
};
inline void attachSphereWaterActor(World& world, Entity e, const SphereWaterActorOptions& o = {}) {
    if (!world.has<WaterProbeComponent>(e)) world.add<WaterProbeComponent>(e);
    if (!world.has<WaterSphereContactComponent>(e)) world.add<WaterSphereContactComponent>(e);
    if (o.buoyancy) {
        if (!world.has<WaterBuoyancyComponent>(e)) world.add<WaterBuoyancyComponent>(e, o.buoyancyParams);
        if (!world.has<WaterVelocityInputComponent>(e)) world.add<WaterVelocityInputComponent>(e);
        if (!world.has<WaterForceComponent>(e)) world.add<WaterForceComponent>(e);
    }
    if (o.interactions) {
        if (!world.has<WaterInteractionComponent>(e)) world.add<WaterInteractionComponent>(e, o.interaction);
        if (!world.has<WaterSphereInteractionStateComponent>(e)) world.add<WaterSphereInteractionStateComponent>(e);
        if (!world.has<WaterInteractionOutputComponent>(e)) world.add<WaterInteractionOutputComponent>(e);
    }
}

// Call together with registerNereidInteractionPersistence (config + output components live there).
inline void registerNereidSphereInteractionPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterSphereInteractionStateComponent>("nereid.sphere_interaction_state", 1);
}

} // namespace aiverse::lib::nereid
