#include "engine/physis/PhysisForces.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(v) do { if(!(v)){std::printf("PHYSIS FORCES FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;} } while(0)
void testWindAndGravity() {
    World world;
    Settings settings;
    settings.width=2;settings.height=2;
    initialize(world,settings);
    GravitySettings gravity;gravity.baseline={};
    world.setResource<GravitySettings>(gravity);
    WindZone zone;
    zone.center={0_fx,0_fx,0_fx};
    zone.radiusMeters=100_fx;
    zone.velocity={20_fx,0_fx,0_fx};
    world.add<WindZone>(world.create(),zone);
    ForceReceiver body;
    body.position={0_fx,0_fx,0_fx};
    body.massKg=10_fx;
    body.dragCoefficient=1_fx;
    body.areaSqMeters=2_fx;
    body.airDensity=1_fx;
    ForceResult sampled=evaluateForces(world,body);
    CHECK(sampled.gravityForce==Vec3{});
    CHECK(sampled.aerodynamicForce.x==400_fx);
    CHECK(sampled.aerodynamicForce.y==Fixed{});
    CHECK(sampled.totalForce.x==400_fx);
    gravity.baseline={0_fx,Fixed::fromRatio(-981,100),0_fx};
    world.setResource<GravitySettings>(gravity);
    sampled=evaluateForces(world,body);
    // Q47.16 rounds the baseline -9.81 m/s^2 BEFORE multiplying by
    // 10 kg. This differs by two raw ticks from separately rounding
    // the ideal -98.1 N; do not compare those two quantization paths
    // bit-for-bit. The actual deterministic force policy is F = m * g.
    CHECK(sampled.gravityAcceleration.y==gravity.baseline.y);
    CHECK(sampled.gravityForce.y==gravity.baseline.y * body.massKg);
    CHECK(fxAbs(sampled.gravityForce.y-Fixed::fromRatio(-981,10))<=
          Fixed::fromRaw(2));
    CHECK(sampled.totalForce.x==400_fx);
}
void testSystemOnlyPublishes() {
    World world;
    Settings settings;settings.width=1;settings.height=1;
    initialize(world,settings);
    const Entity e=world.create();
    ForceReceiver receiver;
    receiver.position={0_fx,0_fx,0_fx};
    receiver.massKg=2_fx;
    world.add<ForceReceiver>(e,receiver);
    world.add<ForceOutput>(e,ForceOutput{});
    advance(world);
    advanceForces(world);
    const auto output=world.get<ForceOutput>(e);
    CHECK(output.valid);
    CHECK(output.sampledTick==1);
    CHECK(output.result.gravityForce.y==Fixed::fromRatio(-981,50));
    CHECK(world.get<ForceReceiver>(e).position==receiver.position);
    World checkpoint=world;
    world.get<ForceReceiver>(e).velocity={0_fx,10_fx,0_fx};
    advanceForces(world);
    world=checkpoint;
    CHECK(world.get<ForceOutput>(e).result.gravityForce.y==Fixed::fromRatio(-981,50));
    world.get<ForceReceiver>(e).enabled=false;
    advanceForces(world);
    CHECK(!world.get<ForceOutput>(e).valid);
}
void testHighVelocityBounds() {
    World world;
    initialize(world);
    ForceReceiver body;
    body.velocity = {100000000_fx, -100000000_fx, 100000000_fx};
    body.massKg = 100_fx;
    const auto sample = evaluateForces(world, body);
    const Fixed cap = Fixed::fromInt(10000000);
    CHECK(fxAbs(sample.totalForce.x) <= cap);
    CHECK(fxAbs(sample.totalForce.y) <= cap);
    CHECK(fxAbs(sample.totalForce.z) <= cap);
}
void testTornadoForceUpdraft() {
    World world;
    Settings settings;settings.width=2;settings.height=2;
    initialize(world,settings);
    GravitySettings g;g.baseline={};
    world.setResource<GravitySettings>(g);
    StormEvent storm;
    storm.kind=StormKind::Tornado;
    storm.center={0_fx,0_fx,0_fx};
    storm.eyeRadiusMeters=10_fx;
    storm.outerRadiusMeters=100_fx;
    storm.heightMeters=200_fx;
    storm.updraft=70_fx;
    storm.radialInflow=0_fx;
    world.add<StormEvent>(world.create(),storm);
    ForceReceiver body;
    body.position={10_fx,0_fx,0_fx};
    body.velocity={};
    const auto result=evaluateForces(world,body);
    CHECK(result.windVelocity.y>Fixed{});
    CHECK(result.aerodynamicForce.y>Fixed{});
}
}
int main(){testWindAndGravity();testSystemOnlyPublishes();testTornadoForceUpdraft();testHighVelocityBounds();
std::printf("PHYSIS Forces: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
