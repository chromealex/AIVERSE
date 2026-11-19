#include "engine/physis/PhysisGravity.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(v) do { if (!(v)) { std::printf("PHYSIS GRAVITY FAIL %s:%d %s\n", __FILE__, __LINE__, #v); ++failures; } } while(0)
void testGravity() {
    World world;
    const Vec3 origin{0_fx, 0_fx, 0_fx};
    CHECK(gravityAt(world, origin).y == Fixed::fromRatio(-981,100));
    const Entity global = world.create();
    GravityField f;
    f.acceleration = {0_fx, 1_fx, 0_fx};
    world.add<GravityField>(global, f);
    CHECK(gravityAt(world, origin).y == Fixed::fromRatio(-881,100));

    const Entity lunar = world.create();
    GravityField low;
    low.center = {0_fx, 0_fx, 0_fx};
    low.radiusMeters = 100_fx;
    low.acceleration = {0_fx, Fixed::fromRatio(-162,100), 0_fx};
    low.overrideBaseline = true;
    low.priority = 10;
    world.add<GravityField>(lunar, low);
    // Local override replaces baseline at the exact centre. Additive +1 remains.
    CHECK(gravityAt(world, origin).y == Fixed::fromRatio(-62,100));
    CHECK(gravityAt(world, {100_fx, 0_fx, 0_fx}).y == Fixed::fromRatio(-881,100));
    world.get<GravityField>(lunar).enabled=false;
    CHECK(gravityAt(world, origin).y == Fixed::fromRatio(-881,100));
}
void testLargePlanetField() {
    World world;
    GravitySettings settings; settings.baseline = {};
    world.setResource<GravitySettings>(settings);
    GravityField planet;
    planet.mode = GravityMode::Attract;
    planet.radiusMeters = Fixed::fromInt(20000000);
    planet.radialMagnitude = Fixed::fromInt(9);
    world.add<GravityField>(world.create(), planet);
    CHECK(gravityAt(world, {10000000_fx, 0_fx, 0_fx}).x < Fixed{});
}
void testRadialAndWorldCopy() {
    World world;
    GravitySettings settings;
    settings.baseline = {};
    world.setResource<GravitySettings>(settings);
    const Entity e=world.create();
    GravityField field;
    field.mode = GravityMode::Attract;
    field.center = {0_fx,0_fx,0_fx};
    field.radiusMeters = 100_fx;
    field.radialMagnitude = 20_fx;
    world.add<GravityField>(e, field);
    const Vec3 positive{10_fx, 0_fx, 0_fx};
    const Vec3 g = gravityAt(world, positive);
    CHECK(g.x < Fixed{});
    CHECK(g.y == Fixed{});
    CHECK(gravityAt(world, {100_fx,0_fx,0_fx}) == Vec3{});
    CHECK(gravityAt(world, field.center) == Vec3{});
    World checkpoint=world;
    world.get<GravityField>(e).mode=GravityMode::Repel;
    CHECK(gravityAt(world, positive).x > Fixed{});
    world=checkpoint;
    CHECK(gravityAt(world, positive) == g);
}
}
int main() {
    testGravity();testRadialAndWorldCopy();testLargePlanetField();
    std::printf("PHYSIS Gravity: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
