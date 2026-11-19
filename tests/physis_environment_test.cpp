#include "engine/physis/PhysisEnvironment.h"
#include <cstdio>

using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS ENV FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)
void wettingAndMaterialDifferences() {
    World world;
    Settings settings;
    settings.width = 2; settings.height = 1;
    settings.baseTemperature = Fixed::fromInt(20);
    settings.baseHumidity = Fixed::fromInt(1);
    initialize(world, settings);
    SurfaceMaterial rainproof;
    rainproof.simulation.wettingRate = Fixed{};
    rainproof.simulation.dryingRate = Fixed{};
    rainproof.permeability = Fixed::fromInt(1);
    world.add<SurfaceMaterial>(cellAt(world, 1, 0), rainproof);
    for (int i = 0; i < 32; ++i) advanceEnvironment(world);
    EnvironmentalResponse wet{}, dry{};
    CHECK(environmentAt(world, 0, 0, wet));
    CHECK(environmentAt(world, 1, 0, dry));
    CHECK(wet.wetness > dry.wetness);
    CHECK(dry.wetness == Fixed{});
    CHECK(wet.tractionMultiplier < Fixed::fromInt(1));
    CHECK(dry.runoffPotential == Fixed{});
    CHECK(!environmentAt(world, 999, 999, wet));
}
void frozenWorldAndRollback() {
    World world;
    Settings settings;
    settings.width = 1;
    settings.height = 1;
    settings.baseTemperature = Fixed::fromInt(-15);
    settings.baseHumidity = Fixed::fromInt(1);
    initialize(world, settings);
    world.get<Surface>(cellAt(world, 0, 0)).wetness = Fixed::fromInt(1);
    World snapshot = world;
    for (int i = 0; i < 40; ++i) advanceEnvironment(world);
    EnvironmentalResponse cold{};
    CHECK(environmentAt(world, 0, 0, cold));
    CHECK(cold.rain == Fixed{});
    CHECK(cold.snow > Fixed{});
    CHECK(cold.snowpack > Fixed{});
    CHECK(cold.ice > Fixed{});
    CHECK(cold.tractionMultiplier < Fixed::fromInt(1));
    World replay = snapshot;
    for (int i = 0; i < 40; ++i) advanceEnvironment(replay);
    EnvironmentalResponse again{};
    CHECK(environmentAt(replay, 0, 0, again));
    CHECK(cold.wetness == again.wetness);
    CHECK(cold.ice == again.ice);
    CHECK(cold.snowpack == again.snowpack);
}
void nativeEcsComposition() {
    struct ArbitraryProjectComponent { std::uint64_t value = 987; };
    World world;
    Settings settings; settings.width = 1; settings.height = 1;
    initialize(world, settings);
    auto e = world.create();
    world.add<ArbitraryProjectComponent>(e, ArbitraryProjectComponent{});
    advanceEnvironment(world);
    CHECK(world.get<ArbitraryProjectComponent>(e).value == 987);
    CHECK(world.resourceConst<State>()->tick == 1);
}
}
int main() {
    wettingAndMaterialDifferences();
    frozenWorldAndRollback();
    nativeEcsComposition();
    std::printf("PHYSIS environment: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
