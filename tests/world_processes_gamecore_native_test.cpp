// Actual GameCore map fixture proving native module ECS hydration via mapPlaced().
#include "aiverselib/world/WorldProcessesNativeModule.h"
#include "aiverselib/world/WorldProcessGravity.h"
#include <array>
#include "engine/data/JsonCompose.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace aiverse;
namespace g=aiverse::game;
namespace wp=aiverse::lib::worldprocess;
static Json baseMap(){
    return Json::parseFile(std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/maps/duel.json");
}
static Json mapWithProcesses(){
    Json m=baseMap();
    auto& entities=m.member("entities");
    auto& first=entities.at(1);
    auto& second=entities.at(2);
    std::string id1=first.getStr("id"),id2=second.getStr("id");
    if(id1.empty()){id1="world_zone_a";first.set("id",Json::string(id1));}
    if(id2.empty()){id2="world_zone_b";second.set("id",Json::string(id2));}
    Json a=Json::parse(R"({"EchoZone":{"enabled":true},
        "FractureMember":{"anchored":true},
        "EchoEmitter":{"zone":"","intensity":100,"enabled":true}})");
    a.member("EchoEmitter").set("zone",Json::string(id1));
    first.set("components",std::move(a));
    Json b=Json::parse(R"({"EchoZone":{"enabled":true},
        "AetherGasCell":{"smoke":250,"toxin":50,"oxygen":500,"enabled":true},
        "EchoPortal":{"from":"","to":"","loss":25,"bidirectional":true,"enabled":true},
        "EchoReceiver":{"zone":"","threshold":30,"enabled":true}})");
    b.member("EchoPortal").set("from",Json::string(id1));
    b.member("EchoPortal").set("to",Json::string(id2));
    b.member("EchoReceiver").set("zone",Json::string(id2));
    second.set("components",std::move(b));
    return m;
}
static std::unique_ptr<Simulation> make(const Json& map,bool enable){
    Json rules=readComposedJson(std::string(AIVERSE_TEMPLATES_DIR)+
                                 "/mushrooms/rules.json",fileReader());
    std::vector<g::NativeDefinition> modules;
    if(enable){
        rules.set("nativeModules",Json::parse(R"([{"id":"world_processes"}])"));
        modules.push_back(wp::worldProcessesNativeDefinition());
    }
    auto sim=std::make_unique<Simulation>(SimConfig{1,20,0,true},
                                         std::make_unique<g::GameCore>(std::move(modules)));
    sim->setup(rules,map);
    return sim;
}
static Entity placed(const Simulation& sim,std::size_t i){
    auto& game=static_cast<const g::GameCore&>(sim.module());
    AIVERSE_TEST_CHECK(game.mapPlaced().size()>i);
    return game.mapPlaced()[i];
}
static void checkPlanetGravity(){
    namespace wp=aiverse::lib::worldprocess;
    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);
    planet.id="moon";planet.radius=Fixed::fromInt(20);
    surface.bodies.push_back(planet);surface.build();
    World ecs;
    physis::GravitySettings settings;settings.baseline={};
    ecs.setResource<physis::GravitySettings>(settings);
    const auto field=ecs.create();
    physis::GravityField g;g.mode=physis::GravityMode::Attract;
    g.center=game::Surface::anchor(0);g.radiusMeters=Fixed::fromInt(200);
    g.radialMagnitude=Fixed::fromInt(10);g.overrideBaseline=true;
    ecs.add<physis::GravityField>(field,g);
    for(int face=0;face<6;++face){
        const auto p=game::Surface::anchor(0)+surface.facePoint(0,face,Fixed::fromInt(4),Fixed::fromInt(4));
        const auto frame=wp::surfaceFrame(&surface,p);
        AIVERSE_TEST_CHECK(frame&&frame->body==0&&frame->cell>=0);
        const auto gravity=wp::sampleGravity(ecs,*frame,p);
        AIVERSE_TEST_CHECK(gravity.local.y<Fixed::fromInt(-1));
        AIVERSE_TEST_CHECK(fxAbs(gravity.local.x)<Fixed::fromRatio(1,10));
        AIVERSE_TEST_CHECK(fxAbs(gravity.local.z)<Fixed::fromRatio(1,10));
        AIVERSE_TEST_CHECK(wp::granularGravityAxis(gravity.local)==wp::GravityAxis::Down);
        const auto force=wp::morphGravityForce(field,gravity.local,Fixed::fromInt(10));
        AIVERSE_TEST_CHECK(force&&force->y<0);
    }
    const auto a=wp::surfaceFrame(&surface,surface.cellCenter(surface.gridCell(0,0,7,3)));
    AIVERSE_TEST_CHECK(a);
    const int neighbor=surface.neighbor(a->cell,0);
    AIVERSE_TEST_CHECK(neighbor>=0);
    const auto seam=wp::surfaceFrame(&surface,surface.cellCenter(neighbor));
    const auto moon=wp::surfaceFrame(&surface,surface.cellCenter(surface.gridCell(1,0,7,3)));
    AIVERSE_TEST_CHECK(a&&seam&&moon&&wp::sameSurfaceBody(*a,*seam));
    AIVERSE_TEST_CHECK(wp::surfaceGroundDistance(&surface,*a,*seam).has_value());
    AIVERSE_TEST_CHECK(!wp::surfaceGroundDistance(&surface,*a,*moon));
    AIVERSE_TEST_CHECK(!wp::surfaceFrame(&surface,game::Surface::anchor(0)));

    granular::Field grains;grains.width=3;grains.height=3;grains.capacity=10;
    grains.mass.assign(9,0);grains.solid.assign(9,0);
    grains.mass[4]=10;
    for(const auto& [accel,target]:std::array<std::pair<Vec3,size_t>,5>{{
        {{Fixed{},Fixed::fromInt(-1),Fixed{}},7},
        {{Fixed{},Fixed::fromInt(1),Fixed{}},1},
        {{Fixed::fromInt(-1),Fixed{},Fixed{}},3},
        {{Fixed::fromInt(1),Fixed{},Fixed{}},5},
        {{},4}}}){
        auto next=grains;
        AIVERSE_TEST_CHECK(wp::settleWithGravity(next,1,accel));
        AIVERSE_TEST_CHECK(next.mass[target]==10&&next.sampledTick==1);
        AIVERSE_TEST_CHECK(next.width==3&&next.height==3&&next.solid==grains.solid);
    }
    const auto loose=ecs.create();ecs.add<granular::Field>(loose,grains);
    AIVERSE_TEST_CHECK(!wp::advanceGranularWithGravity(ecs,2,
        [](const World&,Entity)->std::optional<Vec3>{return {};}));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(loose).mass==grains.mass);
    AIVERSE_TEST_CHECK(wp::advanceGranularWithGravity(ecs,2,
        [](const World&,Entity)->std::optional<Vec3>{return Vec3{Fixed{},Fixed::fromInt(1),Fixed{}};}));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(loose).mass[1]==10);
}
int main(){
    checkPlanetGravity();
    const Json map=mapWithProcesses();
    auto sim=make(map,true),peer=make(map,true);
    auto zone=placed(*sim,1),gas=placed(*sim,2);
    AIVERSE_TEST_CHECK(sim->world().alive(zone)&&sim->world().alive(gas));
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticZone>(zone).enabled);
    AIVERSE_TEST_CHECK(sim->world().get<fracture::Member>(zone).anchored);
    AIVERSE_TEST_CHECK(sim->world().get<aether::GasCell>(gas).oxygen==500);
    AIVERSE_TEST_CHECK(sim->world().get<aether::GasCell>(gas).smoke==250);
    AIVERSE_TEST_CHECK(sim->world().has<echo::AcousticReceiver>(gas));
    AIVERSE_TEST_CHECK(sim->world().has<echo::AcousticHearing>(gas));
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticPortal>(gas).from==zone);
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticEmitter>(zone).zone==zone);
    AIVERSE_TEST_CHECK(uint8_t(sim->serialize()[4])==10);
    AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    for(int i=0;i<5;++i){
        sim->step();peer->step();
        AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    }
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticHearing>(gas).audible);
    AIVERSE_TEST_CHECK(sim->world().get<echo::AcousticHearing>(gas).intensity==75);
    const std::string bytes=sim->serialize();
    auto restored=make(map,true);
    std::string err;
    AIVERSE_TEST_CHECK(restored->deserialize(bytes,&err));
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
    sim->world().enableCowSnapshots();
    SimSnapshot snap;sim->saveState(snap);
    sim->world().get<aether::GasCell>(gas).oxygen=1;
    sim->loadState(snap);
    AIVERSE_TEST_CHECK(sim->checksum()==restored->checksum());
    // Without the explicit project native module these authored configs do
    // not instantiate ECS component pools or alter the legacy game save.
    const auto original=make(map,false);
    AIVERSE_TEST_CHECK(!original->world().has<echo::AcousticZone>(placed(*original,1)));
    AIVERSE_TEST_CHECK(!original->world().has<aether::GasCell>(placed(*original,2)));
    AIVERSE_TEST_CHECK(uint8_t(original->serialize()[4])==9);
    Json invalid=map;
    invalid.member("entities").at(2).member("components")
        .member("AetherGasCell").set("oxygen",Json::number(int64_t(-5)));
    bool rejected=false;
    try{(void)make(invalid,true);}
    catch(const std::invalid_argument&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    std::cout<<"PASS native GameCore map World Processes hydration, opt-in, persistence, replay and rollback\n";
}
