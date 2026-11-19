#include "engine/physis/PhysisPresentation.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("PHYSIS PRESENTATION FAIL %s:%d %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

void testNatureFieldReadOnlyAndNegativeCoordinates() {
    World world;
    NatureFieldSample output;
    output.tick = 999;
    CHECK(!sampleNatureField(world, {-50_fx, 0_fx, -50_fx}, output));
    CHECK(output.tick == 999);

    Settings cfg;
    cfg.originX = -1;
    cfg.originZ = -1;
    cfg.width = 2;
    cfg.height = 2;
    cfg.cellSizeMeters = 100;
    initialize(world, cfg);
    initializeDisasters(world);
    CHECK(ignite(world, -1, -1));
    GravitySettings g;
    g.baseline = {};
    world.setResource<GravitySettings>(g);

    const Entity windEntity = world.create();
    WindZone gust;
    gust.center = {-50_fx, 0_fx, -50_fx};
    gust.radiusMeters = 50_fx;
    gust.velocity = {15_fx, 0_fx, 0_fx};
    world.add<WindZone>(windEntity, gust);

    const Entity stormEntity = world.create();
    StormEvent storm;
    storm.kind = StormKind::Tornado;
    storm.center = {-50_fx, 0_fx, -50_fx};
    storm.eyeRadiusMeters = 10_fx;
    storm.outerRadiusMeters = 100_fx;
    storm.heightMeters = 200_fx;
    storm.precipitationBoost = 1_fx;
    world.add<StormEvent>(stormEntity, storm);

    const Vec3 queried{-50_fx, 0_fx, -50_fx};
    CHECK(sampleNatureField(world, queried, output));
    CHECK(output.position == queried);
    CHECK(output.tick == 0);
    CHECK(output.hasAtmosphere);
    CHECK(output.hasHazards);
    CHECK(output.hazards.fire == 1_fx);
    CHECK(output.windVelocity.x == 15_fx);
    CHECK(output.storms.precipitationBoost > Fixed{});
    CHECK(output.gravityAcceleration == Vec3{});
    CHECK(output.pressureHpa == 1013_fx);
    CHECK(output.environment.tractionMultiplier > Fixed{});

    const World snapshot = world;
    advanceEnvironmentWithStormsAndDisasters(world, FloodMode::ExternalAuthority);
    NatureFieldSample updated;
    CHECK(sampleNatureField(world, queried, updated));
    CHECK(updated.tick == 1);
    CHECK(updated.hasHazards);
    CHECK(updated.hazards.waterDepth == Fixed{});

    world = snapshot;
    NatureFieldSample replay;
    CHECK(sampleNatureField(world, queried, replay));
    CHECK(replay.tick == 0);
    CHECK(replay.windVelocity == output.windVelocity);

    CHECK(sampleNatureField(world, {10000_fx, 0_fx, 10000_fx}, replay));
    CHECK(!replay.hasAtmosphere);
    CHECK(!replay.hasHazards);
    CHECK(replay.gravityAcceleration == Vec3{});
}
}
int main() {
    testNatureFieldReadOnlyAndNegativeCoordinates();
    std::printf("PHYSIS Presentation: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
