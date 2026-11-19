// PHYSIS Stage 5: generic Simulation integration, persistent ECS and render-neutral snapshots.
// No dependency on any genre module or rendering backend.
#pragma once
#include "PhysisDisasters.h"
#include "PhysisWind.h"
#include "PhysisGravity.h"
#include "PhysisStorm.h"
#include "PhysisForces.h"
#include "PhysisNatureSampling.h"
#include "PhysisNatureStress.h"
#include "PhysisFractureOutbox.h"
#include "PhysisFractureDelivery.h"
#include "../sim/Simulation.h"

namespace aiverse::physis {

// Stable field-wise serialization for Simulation::PersistentEcsRegistry.
inline void io(Archive& a, Cell& v) { a(v.x, v.z, v.elevation); }
inline void io(Archive& a, Atmosphere& v) {
    a(v.temperature, v.humidity, v.pressure, v.wind.x, v.wind.y, v.rain, v.snow);
}
inline void io(Archive& a, Surface& v) { a(v.wetness, v.snowpack, v.ice); }
inline void io(Archive& a, Settings& v) {
    a(v.originX, v.originZ, v.width, v.height, v.cellSizeMeters, v.seed,
      v.baseTemperature, v.baseHumidity);
}
inline void io(Archive& a, State& v) { a(v.settings, v.tick, v.cells); }
inline void io(Archive& a, ClimateZone& v) {
    a(v.temperatureOffset, v.humidityOffset, v.pressureOffset,
      v.prevailingWind.x, v.prevailingWind.y, v.seasonTemperatureAmplitude,
      v.seasonHumidityAmplitude, v.yearTicks, v.phaseTicks);
}
inline void io(Archive& a, WeatherFront& v) {
    a(v.center.x, v.center.y, v.velocity.x, v.velocity.y,
      v.radiusCells, v.temperatureDelta, v.humidityDelta,
      v.pressureDelta, v.windDelta.x, v.windDelta.y, v.precipitationBoost);
}
inline void io(Archive& a, SurfaceParameters& v) {
    a(v.wettingRate, v.dryingRate, v.snowAccumulationRate, v.meltRate,
      v.freezingRate, v.thawingRate, v.freezingPoint);
}
inline void io(Archive& a, SurfaceMaterial& v) {
    a(v.simulation, v.permeability, v.tractionDry);
}
inline void io(Archive& a, WindZone& v) {
    a(v.center, v.radiusMeters, v.velocity, v.gustAmplitude, v.seed,
      v.startTick, v.durationTicks, v.gustPeriodTicks);
}
inline void io(Archive& a, WindShelter& v) {
    a(v.center, v.radiusMeters, v.attenuation);
}
inline void io(Archive& a, GravitySettings& v) {
    a(v.baseline, v.maxAcceleration);
}
inline void io(Archive& a, GravityField& v) {
    a(v.mode, v.center, v.radiusMeters, v.acceleration, v.radialMagnitude,
      v.priority, v.overrideBaseline, v.enabled);
}
inline void io(Archive& a, StormEvent& v) {
    a(v.kind, v.center, v.driftPerTick, v.eyeRadiusMeters,
      v.outerRadiusMeters, v.heightMeters, v.tangentialWind, v.radialInflow,
      v.updraft, v.precipitationBoost, v.pressureDrop, v.startTick,
      v.durationTicks, v.clockwise, v.enabled);
}
inline void io(Archive& a, ForceReceiver& v) {
    a(v.position, v.velocity, v.massKg, v.dragCoefficient,
      v.areaSqMeters, v.airDensity, v.gravityScale, v.enabled);
}
inline void io(Archive& a, ForceResult& v) {
    a(v.windVelocity, v.gravityAcceleration, v.aerodynamicForce,
      v.gravityForce, v.totalForce);
}
inline void io(Archive& a, ForceOutput& v) {
    a(v.result, v.sampledTick, v.valid);
}
inline void io(Archive& a, HazardCell& v) {
    a(v.fuel, v.fire, v.waterDepth, v.quake);
}
inline void io(Archive& a, DisasterConfig& v) {
    a(v.fireSpreadRate, v.fireGrowthRate, v.fuelConsumptionRate,
      v.rainSuppressionRate, v.wetnessSuppressionRate, v.rainInflowRate,
      v.waterFlowFraction, v.evaporationRate);
}
inline void io(Archive& a, DisasterState& v) {
    a(v.config, v.lastProcessedTick);
}
inline void io(Archive& a, QuakeEvent& v) {
    a(v.centerX, v.centerZ, v.radiusCells, v.magnitude,
      v.startTick, v.durationTicks);
}

inline void registerPersistence(Simulation& simulation, bool disasters = false,
                                bool windZones = false, bool gravityFields = false,
                                bool storms = false, bool forces = false,
                                bool natureContacts = false,
                                bool natureStress = false,
                                bool fractureOutbox = false) {
    // Must be called during GameModule::setup before the registry is sealed.
    simulation.registerPersistentComponent<Cell>("physis.cell", 1);
    simulation.registerPersistentComponent<Atmosphere>("physis.atmosphere", 1);
    simulation.registerPersistentComponent<Surface>("physis.surface", 1);
    simulation.registerPersistentComponent<ClimateZone>("physis.climate_zone", 1);
    simulation.registerPersistentComponent<WeatherFront>("physis.weather_front", 1);
    simulation.registerPersistentComponent<SurfaceMaterial>("physis.material", 1);
    // Stage 1-5 save registries must remain byte-compatible by default.
    // New components are opt-in: native ECS saves include a strict schema
    // manifest and changing its count invalidates old save files.
    if (windZones) {
        simulation.registerPersistentComponent<WindZone>("physis.wind_zone", 1);
        simulation.registerPersistentComponent<WindShelter>("physis.wind_shelter", 1);
    }
    if (gravityFields) {
        simulation.registerPersistentComponent<GravityField>("physis.gravity_field", 1);
        simulation.registerPersistentResource<GravitySettings>("physis.gravity_settings", 1);
    }
    if (storms)
        simulation.registerPersistentComponent<StormEvent>("physis.storm_event", 1);
    if (forces) {
        simulation.registerPersistentComponent<ForceReceiver>("physis.force_receiver", 1);
        simulation.registerPersistentComponent<ForceOutput>("physis.force_output", 1);
    }
    // Additional per-entity nature sampling is opt-in. Do not alter
    // existing game persistence manifests when the feature is disabled.
    if (natureContacts) {
        simulation.registerPersistentComponent<NatureProbeComponent>(
            "physis.nature_probe", 1);
        simulation.registerPersistentComponent<NatureContactComponent>(
            "physis.nature_contact", 1);
    }
    if (natureStress) {
        simulation.registerPersistentComponent<NatureStressReceiverComponent>(
            "physis.nature_stress_receiver", 1);
        simulation.registerPersistentComponent<NatureStressOutputComponent>(
            "physis.nature_stress_output", 1);
    }
    // Fracture events are persistent only when native hosts explicitly opt in.
    if (fractureOutbox)
        simulation.registerPersistentComponent<NatureFractureOutboxComponent>(
            "physis.fracture_outbox", 1);
    simulation.registerPersistentResource<State>("physis.state", 1);
    if (disasters) {
        simulation.registerPersistentComponent<HazardCell>("physis.hazard", 1);
        simulation.registerPersistentComponent<QuakeEvent>("physis.quake_event", 1);
        simulation.registerPersistentResource<DisasterState>("physis.disaster_state", 1);
    }
}

struct InstallOptions {
    Settings climate{};
    DisasterConfig disasters{};
    bool enableDisasters = false;
    bool enableStorms = false;
    bool enableForces = false;
    // Publish read-only per-entity weather/gravity/storm snapshots after
    // each climate tick. Defaults off for strict older-save compatibility.
    bool enableNatureContacts = false;
    // Requires enableForces AND enableNatureContacts: derives deterministic
    // structural load/fatigue without calculating a second wind force.
    bool enableNatureStress = false;
    // Optional durable outbox for host-owned falling/collapse reactions.
    // Requires the NatureStress system and must be attached before fracture.
    bool enableFractureOutbox = false;
    // Enable these when a native host uses WindZone/WindShelter or custom
    // GravityField/GravitySettings and needs them in save/replay.
    bool enableWindZones = false;
    bool enableGravityFields = false;
    // Use ExternalAuthority when NEREID or another water simulation owns
    // water volumes; fire and quakes keep running without duplicate flooding.
    FloodMode floodMode = FloodMode::LocalShallow;
};

inline void install(Simulation& simulation, const InstallOptions& options = {}) {
    // Invalid policy must not partially initialize persistent ECS state.
    if (options.floodMode != FloodMode::LocalShallow &&
        options.floodMode != FloodMode::ExternalAuthority)
        throw std::invalid_argument("invalid PHYSIS flood authority");
    if (options.enableNatureStress &&
        (!options.enableForces || !options.enableNatureContacts))
        throw std::invalid_argument(
            "PHYSIS nature stress requires forces and nature contacts");
    if (options.enableFractureOutbox && !options.enableNatureStress)
        throw std::invalid_argument(
            "PHYSIS fracture outbox requires nature stress");
    // Universal one-line native project integration. Never call in render code.
    // Install during GameModule::setup; SimSnapshot and file saves use the same
    // authoritative World. The host controls the order of systems it registers.
    initialize(simulation.world(), options.climate);
    if (options.enableDisasters)
        initializeDisasters(simulation.world(), options.disasters);
    registerPersistence(simulation, options.enableDisasters,
                        options.enableWindZones, options.enableGravityFields,
                        options.enableStorms, options.enableForces,
                        options.enableNatureContacts, options.enableNatureStress,
                        options.enableFractureOutbox);
    simulation.addSystem("physis.environment",
        [hazards=options.enableDisasters, storms=options.enableStorms,
         flood=options.floodMode](Simulation& sim) {
            if (storms && hazards) advanceEnvironmentWithStormsAndDisasters(sim.world(), flood);
            else if (storms) advanceEnvironmentWithStorms(sim.world());
            else if (hazards) advanceEnvironmentWithDisasters(sim.world(), flood);
            else advanceEnvironment(sim.world());
        });
    // Optional published ECS force output; this does NOT integrate motion.
    if (options.enableForces)
        simulation.addSystem("physis.forces", [](Simulation& sim) {
            advanceForces(sim.world());
        });
    // The host creates NatureProbe + NatureContact on interested entities.
    // This runs only after the authoritative PHYSIS climate update and
    // deliberately does not integrate movement or modify water bodies.
    if (options.enableNatureContacts)
        simulation.addSystem("physis.nature_contacts", [](Simulation& sim) {
            advanceNatureContacts(sim.world(), sim.tick());
        });
    // Damage/fracture policy reads force + nature contact snapshots after
    // their barriers; host/Box3D/TerraWeave own the actual consequences.
    if (options.enableNatureStress)
        simulation.addSystem("physis.nature_stress", [](Simulation& sim) {
            advanceNatureStress(sim.world());
        });
    // Persist pending fracture signals until the authoritative game/physics
    // host has durably staged its own reaction and explicitly acknowledges.
    if (options.enableFractureOutbox)
        simulation.addSystem("physis.fracture_outbox", [](Simulation& sim) {
            advanceNatureFractureOutbox(sim.world());
        });
}

struct PresentationCell {
    Cell cell{};
    EnvironmentalResponse environment{};
    HazardCell hazards{};
    bool hasHazards = false;
};

// View / editor / audio / physics adapters supply their own sink. No handles
// enter the ECS; presentation reads immutable copies after a committed tick.
template<class Sink>
inline void forEachPresentationCell(const World& world, Sink&& sink) {
    const auto* state = world.resourceConst<State>();
    if (!state) return;
    for (const Entity entity : state->cells) {
        const auto* coord = world.tryGetConst<Cell>(entity);
        if (!coord) continue;
        PresentationCell result;
        result.cell = *coord;
        if (!environmentAt(world, coord->x, coord->z, result.environment)) continue;
        if (const auto* hazard = world.tryGetConst<HazardCell>(entity)) {
            result.hazards = *hazard;
            result.hasHazards = true;
        }
        sink(result);
    }
}

} // namespace aiverse::physis
