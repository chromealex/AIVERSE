#include "engine/physis/PhysisWind.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("PHYSIS WIND FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)
void testZonesAndShelter() {
    World world;
    Settings cfg;
    cfg.width = 4; cfg.height = 4; cfg.cellSizeMeters = 100;
    initialize(world, cfg);
    const Entity zoneEntity = world.create();
    WindZone zone;
    zone.center = {50_fx, 10_fx, 50_fx};
    zone.radiusMeters = 50_fx;
    zone.velocity = {20_fx, 2_fx, 0_fx};
    world.add<WindZone>(zoneEntity, zone);
    const Vec3 p{50_fx, 10_fx, 50_fx};
    auto result = windAt(world, p);
    CHECK(result.x == 20_fx);
    CHECK(result.y == 2_fx);
    CHECK(windAt(world, {250_fx, 10_fx, 250_fx}) == Vec3{});
    const Entity shelterEntity = world.create();
    WindShelter shelter;
    shelter.center = p;
    shelter.radiusMeters = 20_fx;
    shelter.attenuation = 1_fx;
    world.add<WindShelter>(shelterEntity, shelter);
    CHECK(windAt(world, p) == Vec3{});
    CHECK(windAt(world, {60_fx, 10_fx, 50_fx}).x > Fixed{});
}
void testLargeRadiusNoOverflow() {
    // > 10 km used to overflow squared Q47.16 distance in radialWeight().
    const Fixed large = Fixed::fromInt(20000000);
    CHECK(radialWeight({10000000_fx, 0_fx, 0_fx}, large) > Fixed{});
    CHECK(radialWeight({20000000_fx, 0_fx, 0_fx}, large) == Fixed{});
}
void testSeededGustAndCopy() {
    World a, b;
    Settings cfg;
    cfg.width = 2; cfg.height = 2; cfg.seed = 123;
    initialize(a, cfg);
    initialize(b, cfg);
    WindZone gust;
    gust.radiusMeters = 40_fx;
    gust.center = {0_fx, 0_fx, 0_fx};
    gust.gustAmplitude = 10_fx;
    gust.gustPeriodTicks = 2;
    gust.seed = 77;
    a.add<WindZone>(a.create(), gust);
    b.add<WindZone>(b.create(), gust);
    const Vec3 query{1_fx, 0_fx, 1_fx};
    for (int i = 0; i < 5; ++i) {
        CHECK(windAt(a, query) == windAt(b, query));
        advance(a); advance(b);
    }
    World snapshot = a;
    Vec3 before = windAt(a, query);
    advance(a);
    a = snapshot;
    CHECK(windAt(a, query) == before);
    CHECK(!windActive(1, 2, 5));
    CHECK(windActive(2, 2, 5));
    CHECK(!windActive(7, 2, 5));
}
}
int main() {
    testZonesAndShelter();
    testLargeRadiusNoOverflow();
    testSeededGustAndCopy();
    std::printf("PHYSIS Wind: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
