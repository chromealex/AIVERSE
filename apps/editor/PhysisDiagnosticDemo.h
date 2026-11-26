#pragma once
#include "engine/physis/PhysisStorm.h"
#include "engine/physis/PhysisPresentation.h"
#include <limits>
#include <memory>

namespace aiverse::editor {
inline bool samplePhysisCell(const World& world, int x, int z, physis::NatureFieldSample& sample) {
    const auto* state = world.resourceConst<physis::State>();
    if (!state) return false;
    const auto* cell = world.tryGetConst<physis::Cell>(physis::cellAt(world,x,z));
    if (!cell) return false;
    const int64_t meters = state->settings.cellSizeMeters;
    const int64_t mx = int64_t(x)*meters + meters/2;
    const int64_t mz = int64_t(z)*meters + meters/2;
    const auto limit = std::numeric_limits<int64_t>::max()/Fixed::kOne/2;
    if (mx < -limit || mx > limit || mz < -limit || mz > limit) return false;
    return physis::sampleNatureField(world,{Fixed::fromInt(mx),cell->elevation,Fixed::fromInt(mz)},sample);
}

inline std::unique_ptr<World> makePhysisDiagnosticDemo() {
    using namespace physis;
    auto world = std::make_unique<aiverse::World>();
    Settings config;
    config.originX = -16;
    config.originZ = -16;
    config.width = 32;
    config.height = 32;
    config.cellSizeMeters = 100;
    config.seed = 307;
    initialize(*world, config);
    initializeDisasters(*world);

    // Gradient provides a useful heatmap even without a weather tick.
    for (const auto entity : world->resourceConst<State>()->cells) {
        const auto* cell = world->tryGetConst<Cell>(entity);
        if (!cell) continue;
        ClimateZone zone;
        zone.temperatureOffset = Fixed::fromInt(cell->x);
        zone.humidityOffset = Fixed::fromRatio(cell->z, 64);
        world->add<ClimateZone>(entity, zone);
    }
    StormEvent storm;
    storm.kind = StormKind::Tornado;
    storm.center = {150_fx, 0_fx, 50_fx};
    storm.eyeRadiusMeters = 100_fx;
    storm.outerRadiusMeters = 900_fx;
    storm.heightMeters = 600_fx;
    storm.tangentialWind = 55_fx;
    storm.updraft = 35_fx;
    storm.pressureDrop = 45_fx;
    storm.precipitationBoost = Fixed::fromRatio(3, 4);
    world->add<StormEvent>(world->create(), storm);
    WindZone zone;
    zone.center = {-600_fx, 0_fx, -350_fx};
    zone.radiusMeters = 1100_fx;
    zone.velocity = {25_fx, 1_fx, -5_fx};
    zone.gustAmplitude = 6_fx;
    world->add<WindZone>(world->create(), zone);
    GravityField gravity;
    gravity.center = {-200_fx, 0_fx, 350_fx};
    gravity.radiusMeters = 600_fx;
    gravity.mode = GravityMode::Uniform;
    gravity.acceleration = {};
    gravity.overrideBaseline = true;
    world->add<GravityField>(world->create(), gravity);
    (void)ignite(*world, 2, 2);
    for (int i = 0; i < 25; ++i)
        advanceEnvironmentWithStormsAndDisasters(*world, FloodMode::ExternalAuthority);
    return world;
}
}
