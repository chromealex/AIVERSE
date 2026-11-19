#include "engine/physis/PhysisStorm.h"
#include <cstdio>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures=0;
#define CHECK(v) do { if(!(v)){std::printf("PHYSIS STORM FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;} } while(0)
void testVorticesAndTracks() {
    World world, peer;
    Settings cfg;
    cfg.width=2; cfg.height=2; cfg.cellSizeMeters=100;
    initialize(world,cfg); initialize(peer,cfg);
    StormEvent event;
    event.kind=StormKind::Tornado;
    event.center={50_fx,0_fx,50_fx};
    event.driftPerTick={1_fx,0_fx,0_fx};
    event.eyeRadiusMeters=10_fx;
    event.outerRadiusMeters=80_fx;
    event.heightMeters=500_fx;
    event.tangentialWind=60_fx;
    event.updraft=25_fx;
    event.radialInflow=0_fx;
    event.precipitationBoost=1_fx;
    world.add<StormEvent>(world.create(),event);
    peer.add<StormEvent>(peer.create(),event);
    const auto right=stormsAt(world,{60_fx,0_fx,50_fx},0);
    const auto left=stormsAt(world,{40_fx,0_fx,50_fx},0);
    CHECK(right.wind.z>Fixed{});
    CHECK(left.wind.z<Fixed{});
    CHECK(stormsAt(world,event.center,0).wind.y>Fixed{});
    CHECK(stormsAt(world,{300_fx,0_fx,0_fx},0).wind==Vec3{});
    for(int i=0;i<5;++i) {
        advanceEnvironmentWithStorms(world);
        advanceEnvironmentWithStorms(peer);
        CHECK(windWithStormsAt(world,{60_fx,0_fx,50_fx})==
              windWithStormsAt(peer,{60_fx,0_fx,50_fx}));
    }
    const auto* state=world.resourceConst<State>();
    CHECK(state && state->tick==5);
    StormEvent moved{};
    world.eachExistingConst<StormEvent>([&](Entity,const StormEvent& e){moved=e;});
    CHECK(moved.center.x==55_fx);
    Sample sample{};
    CHECK(aiverse::physis::sample(world,0,0,sample)); // below: no dependency on any renderer
    CHECK(sample.atmosphere.rain>Fixed{});
    CHECK(sample.atmosphere.pressure<1013_fx);
    World snapshot=world;
    advanceEnvironmentWithStorms(world);
    const Vec3 next=windWithStormsAt(world,{60_fx,0_fx,50_fx});
    world=snapshot;
    advanceEnvironmentWithStorms(world);
    CHECK(windWithStormsAt(world,{60_fx,0_fx,50_fx})==next);
}
void testLargeRadiusStorm() {
    StormEvent storm;
    storm.center = {};
    storm.eyeRadiusMeters = Fixed::fromInt(100000);
    storm.outerRadiusMeters = Fixed::fromInt(10000000);
    storm.heightMeters = Fixed::fromInt(10000000);
    const auto effect = stormContribution(
        storm, {2000000_fx, 0_fx, 0_fx}, 0);
    CHECK(effect.pressureOffset < Fixed{});
    CHECK(effect.wind.z > Fixed{});
}
void testHurricaneAndDisasters() {
    World world;
    Settings cfg; cfg.width=1;cfg.height=1;cfg.cellSizeMeters=100;
    initialize(world,cfg);
    initializeDisasters(world);
    StormEvent hurricane;
    hurricane.center={50_fx,0_fx,50_fx};
    hurricane.eyeRadiusMeters=20_fx;
    hurricane.outerRadiusMeters=2000_fx;
    hurricane.heightMeters=20000_fx;
    hurricane.precipitationBoost=1_fx;
    hurricane.pressureDrop=70_fx;
    world.add<StormEvent>(world.create(),hurricane);
    CHECK(ignite(world,0,0));
    advanceEnvironmentWithStormsAndDisasters(world,FloodMode::ExternalAuthority);
    HazardCell hazard{};
    CHECK(hazardAt(world,0,0,hazard));
    CHECK(hazard.waterDepth==Fixed{});
    CHECK(world.resourceConst<State>()->tick==1);
    CHECK(stormsAt(world,hurricane.center,1).pressureOffset<Fixed{});
}
}
int main(){testVorticesAndTracks();testHurricaneAndDisasters();testLargeRadiusStorm();
std::printf("PHYSIS Storm: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
