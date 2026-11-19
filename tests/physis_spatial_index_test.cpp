#include "engine/physis/PhysisSpatialIndex.h"
#include <cstdio>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if(!(c)) { std::printf("PHYSIS INDEX FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++failures; } } while(0)

void parityAndSnapshots() {
    World world;
    Settings config;
    config.originX = -20;
    config.originZ = -20;
    config.width = 40;
    config.height = 40;
    config.cellSizeMeters = 100;
    initialize(world, config);
    initializeDisasters(world);

    // Mix overlapping wind and shelters; preserve source ECS iteration order.
    for (int i=0;i<130;++i) {
        WindZone wind;
        wind.center = {Fixed::fromInt((i%17-8)*120),
                       Fixed::fromInt(i%7*4),
                       Fixed::fromInt((i%11-5)*130)};
        wind.radiusMeters = Fixed::fromInt(i%5==0 ? 340 : 80);
        wind.velocity = {Fixed::fromInt(i%9-4),
                         Fixed::fromInt(i%4),
                         Fixed::fromInt(i%7-3)};
        wind.gustAmplitude = Fixed::fromRatio(i%13, 4);
        wind.seed = std::uint64_t(i+11);
        wind.startTick = 0;
        world.add<WindZone>(world.create(), wind);
    }
    for (int i=0;i<35;++i) {
        WindShelter shelter;
        shelter.center = {Fixed::fromInt((i-15)*70), 0_fx,
                          Fixed::fromInt((i%9-4)*90)};
        shelter.radiusMeters = 90_fx;
        shelter.attenuation = Fixed::fromRatio(i%8, 10);
        world.add<WindShelter>(world.create(), shelter);
    }
    GravityField global;
    global.acceleration = {0_fx, 2_fx, 0_fx};
    global.radiusMeters = Fixed{};
    world.add<GravityField>(world.create(), global);
    for (int i=0;i<20;++i) {
        GravityField gravity;
        gravity.mode = i%2 ? GravityMode::Attract : GravityMode::Repel;
        gravity.center = {Fixed::fromInt((i-12)*90), 60_fx,
                          Fixed::fromInt((i%6-2)*80)};
        gravity.radiusMeters = 250_fx;
        gravity.radialMagnitude = Fixed::fromInt(i%9+1);
        gravity.priority = i%4;
        gravity.overrideBaseline = (i%7==0);
        world.add<GravityField>(world.create(), gravity);
    }
    for(int i=0;i<14;++i) {
        StormEvent storm;
        storm.kind = i%2 ? StormKind::Hurricane : StormKind::Tornado;
        storm.center = {Fixed::fromInt((i-6)*110), 0_fx,
                        Fixed::fromInt((i%5-2)*130)};
        storm.eyeRadiusMeters = 30_fx;
        storm.outerRadiusMeters = Fixed::fromInt(i%3 ? 180 : 1100);
        storm.heightMeters = 300_fx;
        storm.tangentialWind = Fixed::fromInt(5+i);
        storm.driftPerTick = {1_fx, 0_fx, 0_fx};
        world.add<StormEvent>(world.create(),storm);
    }
    // A large-radius volume must use the bounded global fallback.
    WindZone huge;
    huge.radiusMeters = 100000_fx;
    huge.center = {0_fx,0_fx,0_fx};
    huge.velocity = {1_fx,0_fx,0_fx};
    world.add<WindZone>(world.create(),huge);

    FieldIndexSettings cfg;
    cfg.tileSizeMeters = 100;
    cfg.maxTilesPerVolume = 16;
    FieldSpatialIndex index(world,cfg);
    const auto stats = index.stats();
    CHECK(stats.windZones==131);
    CHECK(stats.shelters==35);
    CHECK(stats.gravityFields==21);
    CHECK(stats.storms==14);
    CHECK(stats.globalReferences>=2);
    CHECK(index.candidateCount({-1500_fx, 0_fx, -1500_fx}) <
          stats.windZones+stats.shelters+stats.gravityFields+stats.storms);

    // Compare the exact Fixed outputs of the existing unindexed policies.
    for(int z=-1200;z<=1200;z+=160)
        for(int x=-1400;x<=1400;x+=140) {
            const Vec3 position{Fixed::fromInt(x),Fixed::fromInt(x%7*20),
                                Fixed::fromInt(z)};
            CHECK(index.windAt(position)==aiverse::physis::windAt(world,position));
            CHECK(index.gravityAt(position)==aiverse::physis::gravityAt(world,position));
            const auto unindexed = aiverse::physis::stormsAt(
                world, position,world.resourceConst<State>()->tick);
            const auto indexed = index.stormsAt(position);
            CHECK(indexed.wind==unindexed.wind);
            CHECK(indexed.precipitationBoost==unindexed.precipitationBoost);
            CHECK(indexed.pressureOffset==unindexed.pressureOffset);
            CHECK(index.windWithStormsAt(position)==
                  aiverse::physis::windWithStormsAt(world,position));
            NatureFieldSample reference{}, accelerated{};
            CHECK(sampleNatureField(world,position,reference));
            CHECK(index.sample(position,accelerated));
            CHECK(reference.windVelocity==accelerated.windVelocity);
            CHECK(reference.gravityAcceleration==accelerated.gravityAcceleration);
            CHECK(reference.hasAtmosphere==accelerated.hasAtmosphere);
            CHECK(reference.hasHazards==accelerated.hasHazards);
            CHECK(reference.environment.temperature==accelerated.environment.temperature);
        }

    // The derived index is safely pinned to its own World snapshot. Host edits
    // and rollback cannot silently turn it into a stale dangling-pointer cache.
    const World old = world;
    const Vec3 compare{100_fx, 0_fx, 0_fx};
    const Vec3 pinned = index.windAt(compare);
    WindZone later;
    later.center = compare;
    later.radiusMeters = 50_fx;
    later.velocity = {40_fx, 0_fx, 0_fx};
    world.add<WindZone>(world.create(), later);
    CHECK(index.windAt(compare) == pinned);
    CHECK(index.windAt(compare) == aiverse::physis::windAt(old,compare));
    FieldSpatialIndex rebuilt(world,cfg);
    CHECK(rebuilt.windAt(compare) == aiverse::physis::windAt(world,compare));
}

void totalReferenceBudgetAndParity() {
    World world;
    Settings settings;
    settings.originX = -5;
    settings.originZ = -5;
    settings.width = 10;
    settings.height = 10;
    initialize(world, settings);

    // Many individually tiny fields: each is under the old per-volume cap,
    // but together they previously allocated unbounded bucket references.
    for (int i = 0; i < 200; ++i) {
        WindZone w;
        w.center = {Fixed::fromInt((i % 20 - 10) * 35), 0_fx,
                    Fixed::fromInt((i / 20 - 5) * 35)};
        w.radiusMeters = 45_fx;
        w.velocity = {Fixed::fromInt(i % 4 - 2), 0_fx, 1_fx};
        world.add<WindZone>(world.create(), w);
        WindShelter shelter;
        shelter.center = w.center;
        shelter.radiusMeters = w.radiusMeters;
        shelter.attenuation = Fixed::fromRatio(1, 10);
        world.add<WindShelter>(world.create(), shelter);
        GravityField gravity;
        gravity.center = w.center;
        gravity.radiusMeters = w.radiusMeters;
        gravity.acceleration = {0_fx, Fixed::fromRatio(i % 5, 10), 0_fx};
        world.add<GravityField>(world.create(), gravity);
        StormEvent storm;
        storm.center = w.center;
        storm.eyeRadiusMeters = 4_fx;
        storm.outerRadiusMeters = 45_fx;
        storm.heightMeters = 50_fx;
        storm.tangentialWind = 2_fx;
        world.add<StormEvent>(world.create(), storm);
    }
    constexpr std::uint32_t budget = 8;
    FieldIndexSettings config;
    config.tileSizeMeters = 25;
    config.maxTilesPerVolume = 1024;
    config.maxTotalBucketReferences = budget;
    FieldSpatialIndex index(world, config);
    const auto stats = index.stats();
    CHECK(stats.windZones == 200);
    CHECK(stats.shelters == 200);
    CHECK(stats.gravityFields == 200);
    CHECK(stats.storms == 200);
    // Four independent typed tables, each with its own total budget.
    CHECK(stats.bucketReferences <= budget * 4);
    CHECK(stats.globalReferences > 0);

    for (int z = -300; z <= 300; z += 75)
        for (int x = -300; x <= 300; x += 75) {
            const Vec3 pos{Fixed::fromInt(x), 0_fx, Fixed::fromInt(z)};
            CHECK(index.windAt(pos) == aiverse::physis::windAt(world, pos));
            CHECK(index.gravityAt(pos) == aiverse::physis::gravityAt(world, pos));
            const auto slowStorm = aiverse::physis::stormsAt(world, pos, 0);
            const auto fastStorm = index.stormsAt(pos);
            CHECK(slowStorm.wind == fastStorm.wind);
            CHECK(slowStorm.precipitationBoost == fastStorm.precipitationBoost);
            CHECK(slowStorm.pressureOffset == fastStorm.pressureOffset);
            CHECK(index.windWithStormsAt(pos) ==
                  aiverse::physis::windWithStormsAt(world, pos));
        }

    bool rejected = false;
    try { FieldSpatialIndex invalid(world, {.maxTotalBucketReferences = 0}); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { FieldSpatialIndex invalid(world, {.maxTotalBucketReferences = 1048577}); }
    catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

void invalidConfigurationAndRebuild() {
    World world;
    Settings s;
    s.width = 1; s.height = 1;
    initialize(world,s);
    bool rejected=false;
    try { FieldSpatialIndex bad(world, {.tileSizeMeters=0}); }
    catch(const std::invalid_argument&) { rejected=true; }
    CHECK(rejected);

    FieldSpatialIndex before(world);
    const Vec3 position{0_fx,0_fx,0_fx};
    const Vec3 original = before.windAt(position);
    const World snapshot = world;
    WindZone wind;
    wind.center = position;
    wind.radiusMeters = 20_fx;
    wind.velocity = {10_fx,0_fx,0_fx};
    world.add<WindZone>(world.create(),wind);
    CHECK(before.windAt(position)==original);
    FieldSpatialIndex after(world);
    CHECK(after.windAt(position).x == original.x+10_fx);
    CHECK(after.windAt(position)==aiverse::physis::windAt(world,position));
    world = snapshot;
    CHECK(before.windAt(position)==aiverse::physis::windAt(world,position));
}
}
int main() {
    parityAndSnapshots();
    invalidConfigurationAndRebuild();
    totalReferenceBudgetAndParity();
    std::printf("PHYSIS SpatialIndex: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
