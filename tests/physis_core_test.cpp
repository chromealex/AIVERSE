#include "engine/physis/Physis.h"
#include <cstdio>
#include <cstdint>

using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

bool same(const Sample& a, const Sample& b) {
    return a.cell.x == b.cell.x && a.cell.z == b.cell.z &&
           a.atmosphere.temperature == b.atmosphere.temperature &&
           a.atmosphere.humidity == b.atmosphere.humidity &&
           a.atmosphere.wind == b.atmosphere.wind &&
           a.atmosphere.rain == b.atmosphere.rain &&
           a.atmosphere.snow == b.atmosphere.snow &&
           a.surface.wetness == b.surface.wetness &&
           a.surface.snowpack == b.surface.snowpack;
}

void deterministicTicksAndSampling() {
    Settings config;
    config.originX = -2;
    config.originZ = -1;
    config.width = 4;
    config.height = 3;
    config.cellSizeMeters = 64;
    config.seed = 4567;
    World left, right;
    initialize(left, config);
    initialize(right, config);
    CHECK(left.countExisting<Cell>() == 12);
    CHECK(left.countExisting<Atmosphere>() == 12);
    CHECK(left.countExisting<Surface>() == 12);
    CHECK(!cellAt(left, -3, 0).valid());
    CHECK(!cellAt(left, 0, 3).valid());
    CHECK(cellAt(left, -2, -1).valid());
    CHECK(cellAtPosition(left, {Fixed::fromInt(-1), {}, Fixed::fromInt(-1)}) ==
          cellAt(left, -1, -1));
    CHECK(cellAtPosition(left, {Fixed::fromInt(-64), {}, Fixed::fromInt(-64)}) ==
          cellAt(left, -1, -1));
    CHECK(!cellAtPosition(left, {Fixed::fromInt(9000), {}, {}}).valid());
    for (int tick = 0; tick < 400; ++tick) {
        advance(left);
        advance(right);
    }
    CHECK(left.resourceConst<State>()->tick == 400);
    Sample a{}, b{};
    for (std::int32_t z = -1; z < 2; ++z) {
        for (std::int32_t x = -2; x < 2; ++x) {
            CHECK(sample(left, x, z, a));
            CHECK(sample(right, x, z, b));
            CHECK(same(a, b));
            CHECK(a.atmosphere.humidity >= Fixed{} &&
                  a.atmosphere.humidity <= Fixed::fromInt(1));
        }
    }
    World checkpoint = left; // World snapshots own both components and resource grid indices.
    advance(left);
    CHECK(sample(left, -2, -1, a));
    left = checkpoint; // rollback to tick 400
    CHECK(left.resourceConst<State>()->tick == 400);
    advance(left);
    CHECK(sample(left, -2, -1, b));
    CHECK(same(a, b));

    World different;
    config.seed = 4568;
    initialize(different, config);
    Sample changed{};
    CHECK(sample(different, -2, -1, changed));
    CHECK(changed.atmosphere.temperature != b.atmosphere.temperature ||
          changed.atmosphere.humidity != b.atmosphere.humidity);
}

void configurableGenreNeutralEcsAndCow() {
    struct CustomGameComponent { std::uint32_t custom = 123; };
    World world;
    Settings config;
    config.width = 1;
    config.height = 1;
    config.baseTemperature = Fixed::fromInt(-7);
    config.baseHumidity = Fixed::fromInt(1);
    initialize(world, config);
    const auto actor = world.create();
    world.add<CustomGameComponent>(actor, CustomGameComponent{});
    world.enableCowSnapshots();
    World snapshot = world;
    for (int i = 0; i < 80; ++i) advance(world);
    CHECK(world.has<CustomGameComponent>(actor));
    CHECK(world.get<CustomGameComponent>(actor).custom == 123);
    CHECK(snapshot.resourceConst<State>()->tick == 0);
    Sample before{}, after{};
    CHECK(sample(snapshot, 0, 0, before));
    CHECK(sample(world, 0, 0, after));
    CHECK(before.atmosphere.snow == Fixed{});
    CHECK(after.atmosphere.rain == Fixed{});
    CHECK(after.atmosphere.snow >= Fixed{});
    world = snapshot;
    CHECK(world.resourceConst<State>()->tick == 0);
    CHECK(sample(world, 0, 0, after));
    CHECK(same(before, after));
}

void guards() {
    World world;
    bool rejected = false;
    try { advance(world); } catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected);
    Settings config;
    config.width = 0;
    rejected = false;
    try { initialize(world, config); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    CHECK(world.countExisting<Cell>() == 0);
    config.width = 2;
    config.height = 1;
    initialize(world, config);
    rejected = false;
    try { initialize(world, config); } catch (const std::logic_error&) { rejected = true; }
    CHECK(rejected);
}
}
int main() {
    deterministicTicksAndSampling();
    configurableGenreNeutralEcsAndCow();
    guards();
    std::printf("PHYSIS core: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
