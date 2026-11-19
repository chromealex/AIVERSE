#include "engine/physis/PhysisDebugPalette.h"
#include <cstdio>

using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS VIEW FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } } while (0)

void testPaletteAndInvalid() {
    const auto empty = DebugColor{};
    DiagnosticRaster raster;
    raster.width = 2;
    raster.height = 2;
    raster.rawValues = {0, Fixed::fromInt(1).raw, Fixed::fromInt(2).raw, Fixed::fromInt(3).raw};
    raster.valid = {1, 0, 1, 1};
    raster.minRaw = 0;
    raster.maxRaw = Fixed::fromInt(3).raw;
    CHECK(diagnosticColor(raster, 1) == empty);
    CHECK(diagnosticColor(raster, 50) == empty);
    CHECK((debugColor(0) == DebugColor{27, 33, 103, 255}));
    CHECK((debugColor(65535) == DebugColor{228, 45, 63, 255}));
    CHECK(debugColor(0, DebugPalette::Monochrome).a == 255);
    CHECK(debugColor(65535, DebugPalette::Ocean).a == 255);
    CHECK(diagnosticNormalized(raster, 0) == 0);
    CHECK(diagnosticNormalized(raster, 3) == 65535);
    CHECK(diagnosticColor(raster, 0).a == 255);
    CHECK(diagnosticColor(raster, 3).a == 255);
}
void testReadOnlyRasterForEditor() {
    World world;
    Settings settings;
    settings.width = 4; settings.height = 4;
    settings.originX = -2; settings.originZ = -2;
    settings.cellSizeMeters = 100;
    initialize(world, settings);
    initializeDisasters(world);
    CHECK(ignite(world, -2, -2));
    const World snapshot = world;
    DiagnosticRequest request;
    request.originX = -3;
    request.originZ = -3;
    request.width = 6;
    request.height = 6;
    request.layer = DiagnosticLayer::Fire;
    FieldSpatialIndex index(world);
    DiagnosticRaster raster;
    CHECK(sampleDiagnosticRasterIndexed(index, request, raster));
    CHECK(raster.width == 6 && raster.height == 6);
    CHECK(raster.sampleCount == 16);
    CHECK(diagnosticColor(raster, 0).a == 0);
    CHECK(diagnosticColor(raster, 7).a == 255);
    CHECK(raster.minRaw == 0 && raster.maxRaw == Fixed::kOne);
    CHECK(world.resourceConst<State>()->tick == 0);
    CHECK(world.get<HazardCell>(cellAt(world, -2,-2)).fire == 1_fx);
    const auto first = raster.rawValues;
    // New live changes do not bleed into a pinned index / editor preview.
    world.get<HazardCell>(cellAt(world,-2,-2)).fire = Fixed{};
    CHECK(sampleDiagnosticRasterIndexed(index,request,raster));
    CHECK(raster.rawValues == first);
    FieldSpatialIndex rebuilt(world);
    CHECK(sampleDiagnosticRasterIndexed(rebuilt,request,raster));
    CHECK(raster.rawValues != first);
    world = snapshot;
    CHECK(world.get<HazardCell>(cellAt(world,-2,-2)).fire == 1_fx);
}
}
int main() {
    testPaletteAndInvalid();
    testReadOnlyRasterForEditor();
    std::printf("PHYSIS Editor View: %s\n",failures?"FAIL":"PASS");
    return failures ? 1 : 0;
}
