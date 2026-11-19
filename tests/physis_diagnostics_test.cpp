#include "engine/physis/PhysisDiagnostics.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS DIAGNOSTICS FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

void testRasterAndInvalidInputs() {
    World world;
    DiagnosticRequest req;
    DiagnosticRaster raster;
    raster.tick = 999;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.tick == 999);
    Settings settings;
    settings.originX = -2;
    settings.originZ = -1;
    settings.width = 4;
    settings.height = 3;
    settings.cellSizeMeters = 100;
    initialize(world, settings);
    req.originX = -3;
    req.originZ = -2;
    req.width = 6;
    req.height = 5;
    req.stride = 1;
    req.layer = DiagnosticLayer::TemperatureCelsius;
    CHECK(sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.width == 6 && raster.height == 5);
    CHECK(raster.sampleCount == 12);
    CHECK(!raster.valid[0]);
    CHECK(raster.valid[1 * 6 + 1]);
    CHECK(raster.tick == 0);
    CHECK(raster.maxRaw >= raster.minRaw);

    const auto before = raster;
    req.width = -1;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.sampleCount == before.sampleCount);
    req.width = 6;
    req.stride = 0;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    req.stride = 1;
    req.width = 100000;
    req.height = 100000;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
    req.width = 6;
    req.height = 5;
    req.originX = 2147483647;
    CHECK(!sampleDiagnosticRaster(world, req, raster));
}

void testLayersAndDeterminism() {
    World a, b;
    Settings settings;
    settings.originX = -1; settings.originZ = -1;
    settings.width = 2; settings.height = 2;
    initialize(a, settings);
    initialize(b, settings);
    initializeDisasters(a);
    initializeDisasters(b);
    CHECK(ignite(a, -1, -1));
    CHECK(ignite(b, -1, -1));
    DiagnosticRequest req;
    req.originX = -1; req.originZ = -1;
    req.width = 2; req.height = 2;
    req.layer = DiagnosticLayer::Fire;
    DiagnosticRaster ar, br;
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(sampleDiagnosticRaster(b, req, br));
    CHECK(ar.sampleCount == 4);
    CHECK(ar.rawValues == br.rawValues);
    CHECK(ar.minRaw == 0 && ar.maxRaw == Fixed::kOne);
    CHECK(diagnosticNormalized(ar, 0) == 65535);
    CHECK(diagnosticNormalized(ar, 1) == 0);
    CHECK(diagnosticNormalized(ar, 999) == 0);

    req.layer = DiagnosticLayer::GravityMagnitude;
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(ar.sampleCount == 4);
    CHECK(ar.minRaw == ar.maxRaw);
    CHECK(diagnosticNormalized(ar, 0) == 0);

    req.layer = DiagnosticLayer::VerticalWindMetersPerSecond;
    WindZone zone;
    zone.center = {-64_fx, 0_fx, -64_fx};
    zone.radiusMeters = 200_fx;
    zone.velocity = {0_fx, 10_fx, 0_fx};
    a.add<WindZone>(a.create(), zone);
    CHECK(sampleDiagnosticRaster(a, req, ar));
    CHECK(ar.maxRaw > 0);
    World checkpoint = a;
    advance(a);
    CHECK(sampleDiagnosticRaster(a, req, ar));
    a = checkpoint;
    CHECK(sampleDiagnosticRaster(a, req, br));
    CHECK(br.tick == 0);
    CHECK(br.rawValues != ar.rawValues || br.tick != ar.tick);
}
void testAllLayersMatchFullNatureSampling() {
    World world;
    Settings cfg;
    cfg.originX = -2; cfg.originZ = -1;
    cfg.width = 3; cfg.height = 2; cfg.cellSizeMeters = 100;
    initialize(world, cfg);
    initializeDisasters(world);
    CHECK(ignite(world, -2, -1));
    const Entity e = cellAt(world, -2, -1);
    world.get<Surface>(e).wetness = Fixed::fromRatio(1, 2);
    world.get<Surface>(e).snowpack = Fixed::fromRatio(1, 4);
    world.get<Atmosphere>(e).rain = Fixed::fromRatio(3, 4);
    WindZone zone;
    zone.center = {-150_fx, 0_fx, -50_fx};
    zone.radiusMeters = 200_fx;
    zone.velocity = {12_fx, 6_fx, -4_fx};
    world.add<WindZone>(world.create(), zone);
    GravityField field;
    field.mode = GravityMode::Attract;
    field.center = {-150_fx, 50_fx, -50_fx};
    field.radiusMeters = 300_fx;
    world.add<GravityField>(world.create(), field);
    StormEvent cyclone;
    cyclone.center = {-150_fx, 0_fx, -50_fx};
    cyclone.eyeRadiusMeters = 40_fx;
    cyclone.outerRadiusMeters = 500_fx;
    world.add<StormEvent>(world.create(), cyclone);

    const DiagnosticLayer layers[] = {
        DiagnosticLayer::TemperatureCelsius, DiagnosticLayer::RelativeHumidity,
        DiagnosticLayer::PressureHpa, DiagnosticLayer::Rain, DiagnosticLayer::Snow,
        DiagnosticLayer::SurfaceWetness, DiagnosticLayer::SnowpackMeters,
        DiagnosticLayer::SurfaceIce, DiagnosticLayer::WindSpeedMetersPerSecond,
        DiagnosticLayer::VerticalWindMetersPerSecond, DiagnosticLayer::GravityMagnitude,
        DiagnosticLayer::Fire, DiagnosticLayer::Quake, DiagnosticLayer::LocalFloodMeters
    };
    FieldSpatialIndex index(world, {.tileSizeMeters = 100, .maxTilesPerVolume = 16});
    for (auto layer : layers) {
        DiagnosticRequest request;
        request.originX = cfg.originX;
        request.originZ = cfg.originZ;
        request.width = cfg.width;
        request.height = cfg.height;
        request.layer = layer;
        DiagnosticRaster raster;
        CHECK(sampleDiagnosticRaster(world, request, raster));
        DiagnosticRaster indexed;
        CHECK(sampleDiagnosticRasterIndexed(index, request, indexed));
        CHECK(indexed.rawValues == raster.rawValues);
        CHECK(indexed.valid == raster.valid);
        CHECK(indexed.sampleCount == raster.sampleCount);
        CHECK(indexed.minRaw == raster.minRaw && indexed.maxRaw == raster.maxRaw);
        CHECK(raster.sampleCount == 6);
        for (int z = 0; z < cfg.height; ++z)
            for (int x = 0; x < cfg.width; ++x) {
                const auto index = size_t(z * cfg.width + x);
                const Entity cell = cellAt(world, cfg.originX + x, cfg.originZ + z);
                const auto* position = world.tryGetConst<Cell>(cell);
                CHECK(position != nullptr);
                if (!position) continue;
                NatureFieldSample full;
                const Vec3 center{Fixed::fromInt((cfg.originX + x) * 100 + 50),
                                  position->elevation,
                                  Fixed::fromInt((cfg.originZ + z) * 100 + 50)};
                CHECK(sampleNatureField(world, center, full));
                CHECK(full.hasAtmosphere);
                CHECK(raster.valid[index] == 1);
                CHECK(raster.rawValues[index] == diagnosticValue(full, layer).raw);
            }
    }
    CHECK(world.resourceConst<State>()->tick == 0);
}

void testStrideAndReadOnly() {
    World world;
    Settings s;
    s.width = 5; s.height = 5;
    initialize(world, s);
    DiagnosticRequest req;
    req.width = 5; req.height = 5; req.stride = 2;
    req.layer = DiagnosticLayer::SurfaceWetness;
    DiagnosticRaster raster;
    CHECK(sampleDiagnosticRaster(world, req, raster));
    CHECK(raster.width == 3 && raster.height == 3);
    CHECK(raster.sampleCount == 9);
    CHECK(world.resourceConst<State>()->tick == 0);
    CHECK(!raster.valid.empty());
}
}

int main() {
    testRasterAndInvalidInputs();
    testLayersAndDeterminism();
    testAllLayersMatchFullNatureSampling();
    testStrideAndReadOnly();
    std::printf("PHYSIS Diagnostics: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
