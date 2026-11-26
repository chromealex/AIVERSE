#pragma once
#include "engine/physis/PhysisStorm.h"
#include <memory>

namespace aiverse::editor {
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
