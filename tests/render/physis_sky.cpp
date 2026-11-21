#include "engine/world/PhysisSkyBridge.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace aiverse;
using namespace aiverse::world;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#c);std::exit(1);}}while(0)
int main(){
    render::PhysicalSkySettings authored;authored.enabled=true;
    physis::NatureFieldSample snow;snow.hasAtmosphere=true;snow.environment.snow=1_fx;
    CHECK(physisSkySettings(snow,authored).coverage>.89f);
    snow.environment.snow=0_fx;snow.storms.precipitationBoost=1_fx;
    CHECK(physisSkySettings(snow,authored).coverage>.89f);
    PhysisSkyPolicy invalid;invalid.humidCoverage=std::numeric_limits<float>::quiet_NaN();
    CHECK(std::isfinite(physisSkySettings(snow,authored,invalid).coverage));
    PhysisSkyBinding binding;World world;
    CHECK(!binding.update(world,{},1,.1,authored).hasAtmosphere);
    physis::Settings cfg;cfg.width=cfg.height=1;cfg.cellSizeMeters=100;cfg.originX=cfg.originZ=-1;
    physis::initialize(world,cfg);
    const Vec3 position{-50_fx,0_fx,-50_fx};
    const auto cell=physis::cellAtPosition(world,position);
    auto& atmosphere=world.get<physis::Atmosphere>(cell);
    atmosphere.humidity=0_fx;atmosphere.rain=0_fx;atmosphere.wind={10_fx,-2_fx};
    auto dry=binding.update(world,position,1,.1,authored);
    CHECK(dry.hasAtmosphere&&std::abs(dry.settings.coverage-.15f)<.001f);
    CHECK(dry.settings.windX==10&&dry.settings.windZ==-2);
    const World snapshot=world;
    world.resource<physis::State>()->tick=10;
    auto moving=binding.update(world,position,1,.1,authored);
    CHECK(moving.displacementMeters.x==10&&moving.displacementMeters.y==-2);
    CHECK(binding.update(world,position,1,.1,authored).displacementMeters.x==10);
    world.pool<physis::Atmosphere>();
    auto& storm=world.get<physis::Atmosphere>(cell);
    storm.rain=1_fx;storm.humidity=1_fx;storm.wind={-4_fx,0_fx};
    auto wet=binding.update(world,position,1,.1,authored);
    CHECK(wet.settings.coverage>dry.settings.coverage&&wet.settings.haze>dry.settings.haze);
    CHECK(wet.settings.extinction>dry.settings.extinction&&wet.displacementMeters.x==10);
    CHECK(wet.settings.cloudBase==authored.cloudBase&&wet.settings.enabled);
    world.resource<physis::State>()->tick=20;
    CHECK(binding.update(world,position,1,.1,authored).displacementMeters.x==6);
    CHECK(world.get<physis::Atmosphere>(cell).rain==1_fx); // consumer never changes ECS
    CHECK(snapshot.tryGetConst<physis::Atmosphere>(cell)->rain==0_fx);
    world=snapshot;
    auto rollback=binding.update(world,position,1,.1,authored);
    CHECK(rollback.tick==0&&rollback.displacementMeters.x==0&&rollback.settings.coverage==dry.settings.coverage);
    CHECK(!binding.update(world,{999_fx,0_fx,999_fx},1,.1,authored).hasAtmosphere);
    CHECK(binding.update(world,position,1,.1,authored).displacementMeters.x==0);
    CHECK(!binding.update(world,position,0,.1,authored).hasAtmosphere);
    CHECK(!binding.update(world,position,1,std::numeric_limits<double>::quiet_NaN(),authored).hasAtmosphere);
    binding.update(world,position,1,.1,authored);
    world.resource<physis::State>()->tick=1000;
    CHECK(binding.update(world,position,1,.1,authored).displacementMeters.x==0);
    world.resource<physis::State>()->tick=1001;
    CHECK(binding.update(world,position,2,.1,authored).displacementMeters.x==0);
    PhysisSkyBinding otherHost;
    CHECK(otherHost.update(world,position,8,.1,authored).displacementMeters.x==0);
    authored.enabled=false;
    CHECK(!binding.update(world,position,2,.1,authored).settings.enabled);
    std::puts("PHYSIS sky read-only mapping, pause, wind changes, rollback and source isolation passed");
}
