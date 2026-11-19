#include "engine/physis/PhysisDisasters.h"
#include <cstdio>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS DISASTER FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

void fireAndDeterminism() {
    Settings settings;
    settings.width = 3; settings.height = 1;
    settings.seed = 71;
    settings.baseTemperature = Fixed::fromInt(22);
    settings.baseHumidity = Fixed::fromRatio(1, 5); // dry climate
    World a, b;
    initialize(a, settings); initialize(b, settings);
    initializeDisasters(a); initializeDisasters(b);
    CHECK(ignite(a, 1, 0));
    CHECK(ignite(b, 1, 0));
    for (int i = 0; i < 20; ++i) {
        advanceEnvironmentWithDisasters(a);
        advanceEnvironmentWithDisasters(b);
    }
    HazardCell center{}, left{}, copy{};
    CHECK(hazardAt(a, 1, 0, center));
    CHECK(hazardAt(a, 0, 0, left));
    CHECK(hazardAt(b, 0, 0, copy));
    CHECK(left.fire > Fixed{});
    CHECK(center.fuel < Fixed::fromInt(1));
    CHECK(left.fire == copy.fire);
    CHECK(!ignite(a, 99, 99));
    World checkpoint = a;
    advanceEnvironmentWithDisasters(a);
    HazardCell after{};
    CHECK(hazardAt(a, 1, 0, after));
    a = checkpoint;
    advanceEnvironmentWithDisasters(a);
    CHECK(hazardAt(a, 1, 0, center));
    CHECK(after.fire == center.fire && after.fuel == center.fuel);
}

void floodConservesWaterWithoutRain() {
    Settings settings;
    settings.width = 3; settings.height = 1;
    settings.baseHumidity = Fixed{};
    World world;
    initialize(world, settings);
    DisasterConfig config;
    config.rainInflowRate = Fixed{};
    config.evaporationRate = Fixed{};
    initializeDisasters(world, config);
    world.get<HazardCell>(cellAt(world, 0, 0)).waterDepth = Fixed::fromInt(3);
    auto totalRaw = [&]() {
        std::int64_t total = 0;
        for (int x = 0; x < 3; ++x) {
            HazardCell cell{};
            CHECK(hazardAt(world, x, 0, cell));
            total += cell.waterDepth.raw;
        }
        return total;
    };
    const auto before = totalRaw();
    for (int i = 0; i < 15; ++i) advanceEnvironmentWithDisasters(world);
    const auto after = totalRaw();
    CHECK(before == after);
    HazardCell downstream{};
    CHECK(hazardAt(world, 1, 0, downstream));
    CHECK(downstream.waterDepth > Fixed{});
}
void externalAuthorityKeepsFireAndQuakeWithoutFlooding() {
    Settings settings;
    settings.width = 3;
    settings.height = 1;
    settings.seed = 19;
    settings.baseHumidity = Fixed{}; // keep fire burning
    World world;
    initialize(world, settings);
    initializeDisasters(world);
    world.get<HazardCell>(cellAt(world, 0, 0)).waterDepth = Fixed::fromInt(4);
    CHECK(ignite(world, 1, 0));

    const Entity quakeEntity = world.create();
    QuakeEvent quake;
    quake.centerX = 1;
    quake.centerZ = 0;
    quake.startTick = 1;
    quake.durationTicks = 5;
    world.add<QuakeEvent>(quakeEntity, quake);

    advanceEnvironmentWithDisasters(world, FloodMode::ExternalAuthority);
    CHECK(world.resourceConst<State>()->tick == 1);
    for (int x = 0; x < 3; ++x) {
        HazardCell h{};
        CHECK(hazardAt(world, x, 0, h));
        CHECK(h.waterDepth == Fixed{});
    }
    HazardCell burning{};
    CHECK(hazardAt(world, 1, 0, burning));
    CHECK(burning.fire > Fixed{});
    CHECK(burning.quake > Fixed{});

    const World checkpoint = world;
    advanceEnvironmentWithDisasters(world, FloodMode::ExternalAuthority);
    HazardCell expected{};
    CHECK(hazardAt(world, 1, 0, expected));
    world = checkpoint;
    advanceEnvironmentWithDisasters(world, FloodMode::ExternalAuthority);
    HazardCell replayed{};
    CHECK(hazardAt(world, 1, 0, replayed));
    CHECK(replayed.fire == expected.fire);
    CHECK(replayed.fuel == expected.fuel);
    CHECK(replayed.quake == expected.quake);
    CHECK(replayed.waterDepth == Fixed{});

    // Bad host policy must fail before any authoritative climate/tick writes.
    const auto beforeTick = world.resourceConst<State>()->tick;
    bool invalidRejected = false;
    try {
        advanceEnvironmentWithDisasters(world, static_cast<FloodMode>(255));
    } catch (const std::invalid_argument&) { invalidRejected = true; }
    CHECK(invalidRejected);
    CHECK(world.resourceConst<State>()->tick == beforeTick);
}

void quakesAndTickGuard() {
    World world;
    Settings settings; settings.width = 2; settings.height = 1;
    initialize(world, settings);
    initializeDisasters(world);
    Entity event = world.create();
    QuakeEvent quake;
    quake.centerX = 0;
    quake.centerZ = 0;
    quake.radiusCells = Fixed::fromInt(3);
    quake.startTick = 1;
    quake.durationTicks = 2;
    world.add<QuakeEvent>(event, quake);
    advanceEnvironmentWithDisasters(world); // tick 1
    HazardCell center{}, neighbor{};
    CHECK(hazardAt(world, 0, 0, center));
    CHECK(hazardAt(world, 1, 0, neighbor));
    CHECK(center.quake == Fixed::fromInt(1));
    CHECK(neighbor.quake > Fixed{} && neighbor.quake < center.quake);
    bool rejected = false;
    try { advanceDisasters(world); } catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected);
    advanceEnvironmentWithDisasters(world); // tick 2
    advanceEnvironmentWithDisasters(world); // tick 3
    CHECK(hazardAt(world, 0, 0, center));
    CHECK(center.quake == Fixed{});
}
}
int main() {
    fireAndDeterminism();
    floodConservesWaterWithoutRain();
    quakesAndTickGuard();
    externalAuthorityKeepsFireAndQuakeWithoutFlooding();
    std::printf("PHYSIS disasters: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
