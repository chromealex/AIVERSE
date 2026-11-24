// Actual GameCore map fixture proving native module ECS hydration via mapPlaced().
#include "aiverselib/world/WorldProcessesNativeModule.h"
#include "aiverselib/world/WorldProcessGravity.h"
#include "aiverselib/world/GranularPlanetTerrainTransfer.h"
#include "aiverselib/world/CircuitSurfaceWiring.h"
#include "apps/studio-native/StudioPlanetWorldProcesses.h"
#include "aiverselib/world/EvolveSurfaceEcology.h"
#include <array>
#include "engine/data/JsonCompose.h"
#include "tests/AlwaysCheck.h"
#include "examples/native/world_processes/planet-demo/native/PlanetDemoModule.h"
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
static void checkPlanetImprint(){
    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);
    planet.id="moon";planet.radius=Fixed::fromInt(20);
    surface.bodies.push_back(planet);surface.build();
    const int a=surface.gridCell(0,0,7,3),b=surface.neighbor(a,0);
    AIVERSE_TEST_CHECK(b>=0);
    int body=0,face=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(b,body,face,x,y)&&body==0&&face!=0);
    const int moon=surface.gridCell(1,face,x,y);
    World ecs;
    auto makeTrace=[&](int cell){
        const auto e=ecs.create();Transform t{};t.pos=surface.cellCenter(cell);
        ecs.add<Transform>(e,t);ecs.add<imprint::Cell>(e);return e;
    };
    const auto first=makeTrace(a),second=makeTrace(b),duplicate=makeTrace(b),other=makeTrace(moon);
    const auto source=ecs.create(),tracker=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(a);ecs.add<Transform>(source,tr);
    imprint::Tracker cfg{};cfg.source=source;cfg.cell=first;cfg.minimumDistance=1;
    cfg.footprint=50;cfg.scent=10;ecs.add<imprint::Tracker>(tracker,cfg);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,1));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(first).footprint==0);
    ecs.enableCowSnapshots();
    const World before=ecs;
    ecs.get<Transform>(source).pos=surface.cellCenter(b);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,2));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(second).footprint==50);
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(duplicate).footprint==0);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,2));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(second).footprint==50);
    ecs=before;
    ecs.get<Transform>(source).pos=surface.cellCenter(b);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,2));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(second).footprint==50);
    ecs.get<Transform>(source).pos=surface.cellCenter(moon);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,3));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(other).footprint==0);
    AIVERSE_TEST_CHECK(ecs.get<imprint::SurfaceTrailHistory>(tracker).body==1);
    ecs.add<game::Transit>(source);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,4));
    AIVERSE_TEST_CHECK(!ecs.get<imprint::SurfaceTrailHistory>(tracker).initialized);
    ecs.remove<game::Transit>(source);
    ecs.get<Transform>(source).pos=surface.cellCenter(a);
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,5));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(first).footprint==0);
    ecs.get<Transform>(source).pos=surface.cellCenter(b);
    ecs.get<imprint::Cell>(second).enabled=false;
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,6));
    AIVERSE_TEST_CHECK(ecs.get<imprint::Cell>(duplicate).footprint==50);
    ecs.get<imprint::Cell>(duplicate).enabled=false;
    AIVERSE_TEST_CHECK(imprint::stampSurfaceMotion(ecs,surface,7));
    AIVERSE_TEST_CHECK(!ecs.get<imprint::SurfaceTrailHistory>(tracker).initialized);
}
static void checkPlanetCrowdSlots(){
    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;
    surface.bodies.push_back(planet);planet.id="moon";surface.bodies.push_back(planet);
    surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    int body=0,face=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(seam,body,face,x,y)&&body==0&&face!=0);
    World ecs;
    auto at=[&](int cell){
        const auto e=ecs.create();Transform tr{};tr.pos=surface.cellCenter(cell);
        ecs.add<Transform>(e,tr);return e;
    };
    const auto a=at(start),b=at(start),c=at(surface.gridCell(1,face,x,y));
    for(auto e:{a,b,c})ecs.add<crowd::Agent>(e);
    const auto local=at(seam),remote=at(surface.gridCell(1,face,x,y));
    ecs.add<crowd::Slot>(local);ecs.add<crowd::Slot>(remote);
    for(auto e:{a,b,c}){
        crowd::Preference pref{};pref.agent=e;pref.options[0]=remote;pref.options[1]=local;
        ecs.add<crowd::Preference>(ecs.create(),pref);
    }
    const auto allow=[](Entity){return true;};
    const auto pass=[](Entity,const wp::SurfaceFrame&){return true;};
    const auto plan=crowd::planSurfaceSlots(ecs,surface,1,allow,pass);
    AIVERSE_TEST_CHECK(plan&&plan->intents.size()==2);
    AIVERSE_TEST_CHECK(plan->intents[0].agent==a&&plan->intents[0].slot==local);
    AIVERSE_TEST_CHECK(plan->intents[1].agent==c&&plan->intents[1].slot==remote);
    const auto original=ecs.get<Transform>(a).pos;
    ecs.enableCowSnapshots();const World before=ecs;
    AIVERSE_TEST_CHECK(crowd::commitSurfaceSlots(ecs,*plan));
    AIVERSE_TEST_CHECK(ecs.get<crowd::Agent>(b).allocated==kNullEntity);
    AIVERSE_TEST_CHECK(ecs.get<Transform>(a).pos==original);
    const auto repeated=crowd::planSurfaceSlots(ecs,surface,1,allow,pass);
    AIVERSE_TEST_CHECK(repeated&&repeated->agents.empty()&&repeated->intents.empty());
    ecs=before;
    const auto blocked=crowd::planSurfaceSlots(ecs,surface,1,allow,
        [&](Entity,const wp::SurfaceFrame& frame){return frame.cell!=seam;});
    AIVERSE_TEST_CHECK(blocked&&blocked->intents.size()==1&&blocked->intents[0].agent==c);
    const auto excluded=crowd::planSurfaceSlots(ecs,surface,1,
        [&](Entity e){return e!=a;},pass);
    AIVERSE_TEST_CHECK(excluded&&excluded->intents.size()==2&&excluded->intents[0].agent==b);
}
static void checkPlanetEvolveRegions(){
    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);planet.id="moon";surface.bodies.push_back(planet);
    surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    int body=0,face=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(seam,body,face,x,y)&&body==0&&face!=0);
    const auto origin=surface.cellCenter(start);
    const auto neighbor=surface.cellCenter(seam);
    const auto moon=surface.cellCenter(surface.gridCell(1,face,x,y));
    World ecs;
    const auto first=ecs.create(),second=ecs.create(),remote=ecs.create();
    evolve::Region r{};r.fertility=300000000;r.pollution=400000000;
    ecs.add<evolve::Region>(first,r);
    r.fertility=100000000;ecs.add<evolve::Region>(second,r);
    r.fertility=50000000;ecs.add<evolve::Region>(remote,r);
    evolve::Area local{0,0,3000,3000,true};
    AIVERSE_TEST_CHECK(evolve::setSurfaceArea(ecs,surface,first,origin,local));
    AIVERSE_TEST_CHECK(evolve::setSurfaceArea(ecs,surface,second,neighbor,local));
    AIVERSE_TEST_CHECK(evolve::setSurfaceArea(ecs,surface,remote,moon,local));
    AIVERSE_TEST_CHECK(!ecs.has<evolve::Area>(first));
    evolve::SurfaceRegionIndex index;
    AIVERSE_TEST_CHECK(index.build(ecs,surface));
    std::vector<Entity> ids;
    index.query(ecs,surface,neighbor,[&](Entity e,const evolve::Region&){ids.push_back(e);});
    AIVERSE_TEST_CHECK((ids==std::vector<Entity>{first,second}));
    ids.clear();
    index.query(ecs,surface,moon,[&](Entity e,const evolve::Region&){ids.push_back(e);});
    AIVERSE_TEST_CHECK((ids==std::vector<Entity>{remote}));
    terraweave::ForestEcologyClimate baseline{};
    baseline.sunPermille=900;baseline.nutrientsPermille=800;
    const auto climate=evolve::sampleSurfaceEcology(ecs,surface,index,neighbor,baseline);
    AIVERSE_TEST_CHECK(climate.nutrientsPermille==100&&climate.sunPermille==800);
    AIVERSE_TEST_CHECK(climate.waterPermille==baseline.waterPermille);
    ecs.get<evolve::Region>(second).enabled=false;
    AIVERSE_TEST_CHECK(evolve::sampleSurfaceEcology(ecs,surface,index,neighbor,baseline).nutrientsPermille==300);
    // Bounds are snapshotted; rebuild after structural edits/rollback.
    ecs.enableCowSnapshots();const World before=ecs;
    ecs.get<evolve::SurfaceArea>(first).local.enabled=false;
    AIVERSE_TEST_CHECK(index.build(ecs,surface));
    AIVERSE_TEST_CHECK(evolve::sampleSurfaceEcology(ecs,surface,index,neighbor,baseline).nutrientsPermille==800);
    ecs=before;AIVERSE_TEST_CHECK(index.build(ecs,surface));
    AIVERSE_TEST_CHECK(evolve::sampleSurfaceEcology(ecs,surface,index,neighbor,baseline).nutrientsPermille==300);
    auto f=wp::surfaceFrame(&surface,origin);AIVERSE_TEST_CHECK(f);
    const auto zero=evolve::surfaceRegionOffset(surface,*f,*f);
    AIVERSE_TEST_CHECK(zero&&zero->x.raw==0&&zero->y.raw==0);
}
// Exercise actual native hydration on a planet map, not only the standalone
// Surface index. Reuse known unit types from the existing host fixture.
static void checkPlanetEvolveHydration(){
    const Json source=baseMap();
    Json map=Json::parse(R"({"world":{"start":"terra","bodies":[
        {"id":"terra","kind":"planet","radius":64,"faceSize":32},
        {"id":"moon","kind":"moon","radius":32,"faceSize":16}]},
        "players":[],"entities":[]})");
    auto players=source["players"];
    for(size_t i=0;i<players.size();++i){
        Json at=Json::parse(R"({"body":"terra","face":0,"x":16,"y":16})");
        at.set("face",Json::number(int64_t(i%6)));
        players.at(i).set("start",at);
    }
    map.set("players",std::move(players));
    Json entity=source["entities"][1];
    entity.set("id",Json::string("planet_ecology"));
    entity.set("at",Json::parse(R"({"body":"terra","face":0,"x":28,"y":16})"));
    entity.set("components",Json::parse(R"({
        "EvolveRegion":{"durability":1000000000,"fertility":200000000,"succession":0,"pollution":0,"enabled":true},
        "EvolveArea":{"halfWidthCm":1000,"halfDepthCm":1000,"offsetXcm":0,"offsetZcm":0,"enabled":true}
    })"));
    map.member("entities").push(std::move(entity));
    auto sim=make(map,true);
    const auto e=placed(*sim,0);
    AIVERSE_TEST_CHECK(sim->world().has<evolve::Region>(e));
    AIVERSE_TEST_CHECK(sim->world().has<evolve::SurfaceArea>(e));
    AIVERSE_TEST_CHECK(!sim->world().has<evolve::Area>(e));
    const auto& area=sim->world().get<evolve::SurfaceArea>(e);
    AIVERSE_TEST_CHECK(area.body==0&&area.local.halfWidthCm==1000);
    const auto& host=static_cast<const game::GameCore&>(sim->module());
    const auto* surface=host.surface(*sim);
    AIVERSE_TEST_CHECK(surface);
    evolve::SurfaceRegionIndex index;
    AIVERSE_TEST_CHECK(index.build(sim->world(),*surface));
    AIVERSE_TEST_CHECK(evolve::sampleSurfaceEcology(sim->world(),*surface,index,
        area.origin,{}).nutrientsPermille==200);
    auto restored=make(map,true);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(sim->serialize(),&error));
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
}
// Test double for the terrain owner only. No production terrain state or
// mutation API is implied by this fixture.
struct PlanetSoilFixture {
    uint64_t revision=4;
    int64_t terra=1000,moon=2000;
};
static void checkPlanetSoilTransfer(){
    using namespace granular;
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    ecs.setResource<PlanetSoilFixture>(PlanetSoilFixture{});
    const auto field=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(0,0,7,3));
    ecs.add<Transform>(field,tr);
    Field grains;grains.width=3;grains.height=3;grains.capacity=100;
    grains.mass.assign(9,0);grains.solid.assign(9,0);grains.mass[8]=80;grains.sampledTick=7;
    ecs.add<Field>(field,grains);
    PlanetSoilRequest req{field,"terra",2,2,1000,PlanetSoilDirection::Fill,50,4};
    const auto target=planPlanetSoilTransfer(ecs,surface,req,7);
    AIVERSE_TEST_CHECK(target&&target->body==0&&target->face!=0&&target->volumeBudgetCm3==50);
    auto owner=[](World& staged,const PlanetSoilTarget& t)->std::optional<PlanetSoilOwnerChange>{
        auto* soil=staged.resource<PlanetSoilFixture>();
        if(soil->revision!=t.expectedTerrainRevision)return {};
        auto& amount=t.body==0?soil->terra:soil->moon;
        const auto moved=t.volumeBudgetCm3;
        if(t.direction==PlanetSoilDirection::Dig&&uint64_t(amount)<moved)return {};
        amount+=t.direction==PlanetSoilDirection::Fill?int64_t(moved):-int64_t(moved);
        const auto old=soil->revision++;
        return PlanetSoilOwnerChange{old,soil->revision,moved};
    };
    ecs.enableCowSnapshots();const World before=ecs;
    const auto fill=transferPlanetSoil(ecs,surface,req,7,owner);
    AIVERSE_TEST_CHECK(fill&&fill->afterMass==30&&fill->terrain.movedVolumeCm3==50);
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->terra==1050);
    AIVERSE_TEST_CHECK(before.resourceConst<PlanetSoilFixture>()->terra==1000);
    AIVERSE_TEST_CHECK(before.tryGetConst<Field>(field)->mass[8]==80);
    AIVERSE_TEST_CHECK(!transferPlanetSoil(ecs,surface,req,7,owner)); // stale revision
    req.direction=PlanetSoilDirection::Dig;req.maximumVolumeCm3=100;req.expectedTerrainRevision=5;
    const auto dig=transferPlanetSoil(ecs,surface,req,7,owner);
    AIVERSE_TEST_CHECK(dig&&dig->afterMass==100&&dig->terrain.movedVolumeCm3==70);
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->terra==980);
    ecs=before;req.direction=PlanetSoilDirection::Fill;req.maximumVolumeCm3=50;req.expectedTerrainRevision=4;
    auto invalidOwner=[](World& staged,const PlanetSoilTarget& t)->std::optional<PlanetSoilOwnerChange>{
        staged.resource<PlanetSoilFixture>()->terra=0;
        return PlanetSoilOwnerChange{4,5,t.volumeBudgetCm3+1};
    };
    AIVERSE_TEST_CHECK(!transferPlanetSoil(ecs,surface,req,7,invalidOwner));
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->terra==1000&&ecs.get<Field>(field).mass[8]==80);
    auto noop=[](World& staged,const PlanetSoilTarget&)->std::optional<PlanetSoilOwnerChange>{
        staged.resource<PlanetSoilFixture>()->terra=0;
        return PlanetSoilOwnerChange{4,4,0};
    };
    AIVERSE_TEST_CHECK(transferPlanetSoil(ecs,surface,req,7,noop));
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->terra==1000);
    AIVERSE_TEST_CHECK(!planPlanetSoilTransfer(ecs,surface,req,8));
    req.row=1;AIVERSE_TEST_CHECK(!planPlanetSoilTransfer(ecs,surface,req,7));req.row=2;
    ecs.get<Transform>(field).pos=tr.pos+surface.up(tr.pos)*Fixed::fromInt(2);
    AIVERSE_TEST_CHECK(!planPlanetSoilTransfer(ecs,surface,req,7));
    ecs.get<Transform>(field).pos=surface.cellCenter(surface.gridCell(1,0,7,3));
    AIVERSE_TEST_CHECK(!planPlanetSoilTransfer(ecs,surface,req,7)); // stale body
    req.expectedBody="moon";
    const auto lunar=transferPlanetSoil(ecs,surface,req,7,owner);
    AIVERSE_TEST_CHECK(lunar&&lunar->target.body==1&&lunar->target.face!=0);
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->terra==1000);
    AIVERSE_TEST_CHECK(ecs.resourceConst<PlanetSoilFixture>()->moon==2050);
    ecs=before;
    AIVERSE_TEST_CHECK(ecs.get<Field>(field).mass[8]==80&&ecs.resourceConst<PlanetSoilFixture>()->moon==2000);
}
static void checkPlanetGranular(){
    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);
    planet.id="moon";surface.bodies.push_back(planet);surface.build();
    World ecs;
    physis::GravitySettings settings;settings.baseline={};
    ecs.setResource<physis::GravitySettings>(settings);
    const auto gravity=ecs.create();
    physis::GravityField g;g.mode=physis::GravityMode::Attract;
    g.center=game::Surface::anchor(0);g.radiusMeters=Fixed::fromInt(200);
    g.radialMagnitude=Fixed::fromInt(10);g.overrideBaseline=true;
    ecs.add<physis::GravityField>(gravity,g);
    const auto field=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(0,0,7,3));
    ecs.add<Transform>(field,tr);
    granular::Field grains;grains.width=3;grains.height=3;grains.capacity=10;
    grains.mass.assign(9,0);grains.solid.assign(9,0);grains.mass[4]=10;
    ecs.add<granular::Field>(field,grains);
    granular::SurfaceFieldPlacement placement{tr.pos,1000,0};
    const auto patch=granular::sampleSurfaceGrainPatch(ecs,surface,field,placement,0,0,3,3);
    AIVERSE_TEST_CHECK(patch&&patch->size()==9);
    int body=0,face=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace((*patch)[2].surfaceCell,body,face,x,y));
    AIVERSE_TEST_CHECK(body==0&&face!=0);
    for(const auto& cell:*patch)AIVERSE_TEST_CHECK(cell.body==0);
    AIVERSE_TEST_CHECK(((*patch)[1].center-(*patch)[1].ground).length()>
                       ((*patch)[7].center-(*patch)[7].ground).length());
    AIVERSE_TEST_CHECK(!granular::sampleSurfaceGrainPatch(ecs,surface,field,placement,0,0,3,3,8));
    const auto local=granular::surfaceFieldGravity(ecs,surface,field);
    AIVERSE_TEST_CHECK(local&&wp::granularGravityAxis(*local)==wp::GravityAxis::Down);
    AIVERSE_TEST_CHECK(granular::advanceSurfaceFields(ecs,surface,1));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(field).mass[7]==10);
    ecs.enableCowSnapshots();const World before=ecs;
    ecs.get<physis::GravityField>(gravity).mode=physis::GravityMode::Repel;
    AIVERSE_TEST_CHECK(granular::advanceSurfaceFields(ecs,surface,2));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(field).mass[4]==10);
    ecs=before;
    ecs.get<physis::GravityField>(gravity).enabled=false;
    AIVERSE_TEST_CHECK(granular::advanceSurfaceFields(ecs,surface,2));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(field).mass[7]==10);
    ecs.get<Transform>(field).pos=game::Surface::anchor(0);
    AIVERSE_TEST_CHECK(!granular::advanceSurfaceFields(ecs,surface,3));
    AIVERSE_TEST_CHECK(ecs.get<granular::Field>(field).sampledTick==2);
}
static void checkMorphGravity(){
    World ecs;
    const auto owner=ecs.create(),node=ecs.create();
    Transform tr{};ecs.add<Transform>(owner,tr);
    morph::Node n{};n.drag=0;ecs.add<morph::Node>(node,n);
    physis::GravitySettings settings;settings.baseline={Fixed{},Fixed::fromInt(-10),Fixed{}};
    ecs.setResource<physis::GravitySettings>(settings);
    AIVERSE_TEST_CHECK(morph::bindGravity(ecs,node,{owner,100,1000,false,true}));
    const auto absolute=morph::physisNodeLoad(ecs,nullptr,node,n);
    AIVERSE_TEST_CHECK(absolute&&(*absolute)[1]==-10);
    ecs.get<morph::GravityBinding>(node).relative=true;
    const auto relative=morph::physisNodeLoad(ecs,nullptr,node,n);
    AIVERSE_TEST_CHECK(relative&&(*relative==std::array<morph::I,3>{}));
    ecs.get<morph::GravityBinding>(node).relative=false;
    WorldTransform wt{};
    const auto rotation=fxQuaternionAxisAngle({Fixed{},Fixed{},Fixed::fromInt(1)},Fixed::fromRatio(1,4));
    AIVERSE_TEST_CHECK(rotation);
    wt.rotation={rotation->x,rotation->y,rotation->z,rotation->w};
    ecs.add<WorldTransform>(owner,wt);
    const auto rotated=morph::physisNodeLoad(ecs,nullptr,node,n);
    AIVERSE_TEST_CHECK(rotated&&(*rotated)[0]<=-9&&(*rotated)[1]==0);
    ecs.remove<WorldTransform>(owner);

    game::Surface surface;
    game::BodyDef planet;planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);surface.build();
    ecs.resource<physis::GravitySettings>()->baseline={};
    const auto field=ecs.create();
    physis::GravityField g;g.mode=physis::GravityMode::Attract;
    g.center=game::Surface::anchor(0);g.radiusMeters=Fixed::fromInt(200);
    g.radialMagnitude=Fixed::fromInt(10);g.overrideBaseline=true;
    ecs.add<physis::GravityField>(field,g);
    for(int face=0;face<6;++face){
        ecs.get<Transform>(owner).pos=surface.cellCenter(surface.gridCell(0,face,3,3));
        const auto force=morph::physisNodeLoad(ecs,&surface,node,n);
        AIVERSE_TEST_CHECK(force&&(*force)[1]<0&&(*force)[0]==0&&(*force)[2]==0);
    }
    ecs.enableCowSnapshots();const World before=ecs;
    AIVERSE_TEST_CHECK(morph::advanceWithPhysisGravity(ecs,&surface,1));
    const auto moved=ecs.get<morph::Node>(node);
    AIVERSE_TEST_CHECK(moved.y<0&&moved.sampledTick==1);
    AIVERSE_TEST_CHECK(morph::advanceWithPhysisGravity(ecs,&surface,1));
    AIVERSE_TEST_CHECK(ecs.get<morph::Node>(node).y==moved.y);
    ecs=before;
    AIVERSE_TEST_CHECK(morph::advanceWithPhysisGravity(ecs,&surface,1));
    AIVERSE_TEST_CHECK(ecs.get<morph::Node>(node).y==moved.y);
    AIVERSE_TEST_CHECK(morph::enqueueImpact(ecs,node,2,1,0,0));
    ecs.destroy(owner);
    AIVERSE_TEST_CHECK(!morph::advanceWithPhysisGravity(ecs,&surface,2));
    AIVERSE_TEST_CHECK(ecs.get<morph::Node>(node).sampledTick==1);
    AIVERSE_TEST_CHECK(ecs.get<morph::Impact>(node).pending);

    Json map=baseMap();
    map.member("entities").at(1).set("components",Json::parse(R"({"MorphNode":{
        "restX":0,"restY":0,"restZ":0,"drag":0,"anchored":false,"enabled":true,
        "gravityImpulseMilli":1000,"gravityRelative":true}})"));
    auto sim=make(map,true);
    const auto authored=placed(*sim,1);
    AIVERSE_TEST_CHECK(sim->world().has<morph::GravityBinding>(authored));
    sim->step();
    auto restored=make(map,true);std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(sim->serialize(),&error));
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
}
static void checkAetherStratification(){
    World ecs;
    physis::GravitySettings gravity;gravity.baseline={Fixed{},Fixed::fromInt(-10),Fixed{}};
    ecs.setResource<physis::GravitySettings>(gravity);
    const auto low=ecs.create(),high=ecs.create(),duct=ecs.create();
    Transform tr{};ecs.add<Transform>(low,tr);tr.pos.y=Fixed::fromInt(2);ecs.add<Transform>(high,tr);
    for(auto e:{low,high})ecs.add<aether::GasCell>(e,aether::GasCell{1000,1000,1000});
    ecs.add<aether::GasDuct>(duct,aether::GasDuct{low,high,1000,false,true});
    ecs.enableCowSnapshots();const World initial=ecs;
    AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,nullptr,1));
    const auto a=ecs.get<aether::GasCell>(low),b=ecs.get<aether::GasCell>(high);
    AIVERSE_TEST_CHECK(a.smoke==825&&b.smoke==1175&&a.toxin==1175&&b.toxin==825);
    AIVERSE_TEST_CHECK(a.oxygen==1000&&b.oxygen==1000);
    AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,nullptr,1));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).smoke==a.smoke);
    ecs=initial;ecs.resource<physis::GravitySettings>()->baseline={};
    AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,nullptr,1));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).smoke==1000);
    ecs=initial;ecs.get<aether::GasDuct>(duct).oneWay=true;
    AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,nullptr,1));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).toxin==1000);
    ecs=initial;
    physis::Atmosphere hot{};hot.temperature=Fixed::fromInt(100);
    ecs.add<physis::Atmosphere>(low,hot);ecs.add<physis::Atmosphere>(high,hot);
    AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,nullptr,1));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(high).oxygen==1200);
    ecs=initial;ecs.remove<Transform>(high);
    AIVERSE_TEST_CHECK(!aether::advanceWithStratification(ecs,nullptr,1));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).sampledTick==UINT32_MAX);
    ecs=initial;
    game::Surface surface;game::BodyDef planet;
    planet.id="terra";planet.faceSize=8;planet.radius=Fixed::fromInt(64);
    surface.bodies.push_back(planet);planet.id="moon";surface.bodies.push_back(planet);surface.build();
    ecs.resource<physis::GravitySettings>()->baseline={};
    physis::GravityField g;g.mode=physis::GravityMode::Attract;
    g.center=game::Surface::anchor(0);g.radiusMeters=Fixed::fromInt(200);
    g.radialMagnitude=Fixed::fromInt(10);g.overrideBaseline=true;
    ecs.add<physis::GravityField>(ecs.create(),g);
    for(int face=0;face<6;++face){
        const auto p=surface.cellCenter(surface.gridCell(0,face,3,3));
        ecs.get<Transform>(low).pos=p;
        ecs.get<Transform>(high).pos=p+surface.up(p)*Fixed::fromInt(2);
        for(auto e:{low,high})ecs.get<aether::GasCell>(e)=aether::GasCell{1000,1000,1000};
        AIVERSE_TEST_CHECK(aether::advanceWithStratification(ecs,&surface,uint32_t(face+1)));
        AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(high).smoke>1000);
        AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(high).toxin<1000);
        AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(high).smoke+ecs.get<aether::GasCell>(low).smoke==2000);
    }
    ecs.get<Transform>(high).pos=surface.cellCenter(surface.gridCell(1,0,3,3));
    AIVERSE_TEST_CHECK(!aether::advanceWithStratification(ecs,&surface,7));
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).sampledTick==6);
}
static void checkCircuitSurfaceWiring(){
    game::Surface surface;game::BodyDef body;
    body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    World ecs;
    auto at=[&](int cell){
        const auto e=ecs.create();Transform t{};t.pos=surface.cellCenter(cell);
        ecs.add<Transform>(e,t);ecs.add<circuit::Node>(e);return e;
    };
    const auto source=at(start),first=at(seam),second=at(seam);
    const auto remote=at(surface.gridCell(1,0,7,3));
    (void)remote;
    const auto result=circuit::localWireCandidates(ecs,&surface,source,Fixed::fromInt(10000));
    AIVERSE_TEST_CHECK(result&&result->size()==2&&(*result)[0].node==first&&(*result)[1].node==second);
    const auto capped=circuit::localWireCandidates(ecs,&surface,source,Fixed::fromInt(10000),1);
    AIVERSE_TEST_CHECK(capped&&capped->size()==1&&capped->front().node==first);
    ecs.get<circuit::Node>(first).enabled=false;
    const auto enabled=circuit::localWireCandidates(ecs,&surface,source,Fixed::fromInt(10000));
    AIVERSE_TEST_CHECK(enabled&&enabled->size()==1&&enabled->front().node==second);
    AIVERSE_TEST_CHECK(ecs.countExisting<circuit::Wire>()==0);
}
static void checkPlanetStudioDiagnostics(){
    game::Surface surface;game::BodyDef body;
    body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(1000);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);
    surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    World ecs;
    const auto focus=ecs.create(),near=ecs.create(),remote=ecs.create(),unknown=ecs.create();
    const auto addAt=[&](Entity e,int cell){
        Transform t{};t.pos=surface.cellCenter(cell);ecs.add<Transform>(e,t);
    };
    addAt(focus,start);addAt(near,seam);addAt(remote,surface.gridCell(1,0,7,3));
    for(auto e:{near,remote,unknown})ecs.add<crowd::Agent>(e);
    physis::GravitySettings gravity;gravity.baseline={};
    ecs.setResource<physis::GravitySettings>(gravity);
    studio::StudioWorldProcessDiagnostics owner;
    const auto ref=std::to_string(focus.packed());
    const auto result=studio::planet_wp::sample(owner,ecs,surface,"1",1,64,ref);
    AIVERSE_TEST_CHECK(result.getStr("topology")=="planet");
    AIVERSE_TEST_CHECK(result.getBool("observerAvailable"));
    AIVERSE_TEST_CHECK(result["focusSurface"].getStr("body")=="terra");
    AIVERSE_TEST_CHECK(result["focusSurface"]["gravityLocal"][0].fx().raw==0);
    const auto& lane=result["lanes"]["crowd"];
    AIVERSE_TEST_CHECK(lane.getInt("cold")==1);
    AIVERSE_TEST_CHECK(lane.getInt("hot")>=1);
    AIVERSE_TEST_CHECK(lane.getInt("hot")+lane.getInt("warm")+lane.getInt("cold")==3);
    const auto same=studio::planet_wp::sample(owner,ecs,surface,"1",1,64,ref);
    AIVERSE_TEST_CHECK(same==result);
    const auto noFocus=studio::planet_wp::sample(owner,ecs,surface,"1",2,64,"");
    AIVERSE_TEST_CHECK(!noFocus.getBool("observerAvailable"));
    AIVERSE_TEST_CHECK(noFocus["lanes"]["crowd"].getInt("hot")==3);
    const auto plane=owner.sample(&ecs,"2",1,64,ref);
    AIVERSE_TEST_CHECK(!plane.has("topology"));
}
static void checkAuthoredPlanetDemo(){
    const std::string root=std::string(AIVERSE_SOURCE_DIR)+"/examples/native/world_processes/planet-demo";
    const Json map=Json::parseFile(root+"/maps/planet.json");
    const Json rules=Json::parseFile(root+"/rules.json");
    const auto create=[&](){
        std::vector<g::NativeDefinition> defs{examples::planet_processes::definition()};
        auto s=std::make_unique<Simulation>(SimConfig{1,20,0,true},
            std::make_unique<g::GameCore>(std::move(defs)));
        s->setup(rules,map);return s;
    };
    auto sim=create(),peer=create();
    const auto station=placed(*sim,0),receiver=placed(*sim,1),links=placed(*sim,2);
    const auto walker=placed(*sim,3),slot=placed(*sim,4),eco=placed(*sim,6),moon=placed(*sim,7);
    const auto& host=static_cast<const g::GameCore&>(sim->module());
    const auto* surface=host.surface(*sim);
    AIVERSE_TEST_CHECK(surface);
    auto& w=sim->world();
    AIVERSE_TEST_CHECK(w.countExisting<physis::GravityField>()==2);
    AIVERSE_TEST_CHECK(w.get<echo::AcousticPortal>(links).to==receiver);
    AIVERSE_TEST_CHECK(w.get<fracture::Bond>(links).first==station);
    AIVERSE_TEST_CHECK(w.get<aether::GasDuct>(links).to==receiver);
    AIVERSE_TEST_CHECK(w.has<morph::GravityBinding>(walker));
    AIVERSE_TEST_CHECK(w.has<crowd::Agent>(walker)&&w.has<crowd::Slot>(slot));
    AIVERSE_TEST_CHECK(w.has<imprint::Cell>(slot)&&w.has<circuit::Node>(receiver));
    AIVERSE_TEST_CHECK(w.has<granular::Field>(eco)&&w.has<granular::Field>(moon));
    AIVERSE_TEST_CHECK(w.has<evolve::SurfaceArea>(eco)&&!w.has<evolve::Area>(eco));
    AIVERSE_TEST_CHECK(w.has<society::Person>(receiver));
    const auto gravity=[&](Entity e,int body){
        const auto pos=w.get<Transform>(e).pos;
        const auto frame=wp::surfaceFrame(surface,pos);
        AIVERSE_TEST_CHECK(frame&&frame->body==body);
        const auto sample=wp::sampleGravity(w,*frame,pos);
        AIVERSE_TEST_CHECK(wp::granularGravityAxis(sample.local)==wp::GravityAxis::Down);
        return sample.local.y;
    };
    AIVERSE_TEST_CHECK(gravity(eco,0)<gravity(moon,1));
    AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    for(int i=0;i<5;++i){
        sim->step();peer->step();
        AIVERSE_TEST_CHECK(sim->checksum()==peer->checksum());
    }
    AIVERSE_TEST_CHECK(w.get<echo::AcousticHearing>(receiver).intensity==75);
    AIVERSE_TEST_CHECK(!w.get<fracture::Member>(receiver).detached);
    AIVERSE_TEST_CHECK(w.get<aether::GasCell>(receiver).smoke>0);
    AIVERSE_TEST_CHECK(w.get<aether::GasCell>(station).smoke+w.get<aether::GasCell>(receiver).smoke==250);
    AIVERSE_TEST_CHECK(w.get<morph::Node>(walker).y<0);
    AIVERSE_TEST_CHECK(w.get<crowd::Agent>(walker).allocated==slot);
    AIVERSE_TEST_CHECK(w.get<imprint::Cell>(slot).footprint>0);
    AIVERSE_TEST_CHECK(w.get<circuit::Node>(receiver).powered);
    AIVERSE_TEST_CHECK(w.get<granular::Field>(eco).mass[2]+w.get<granular::Field>(eco).mass[3]==400);
    AIVERSE_TEST_CHECK(w.get<granular::Field>(moon).mass[2]+w.get<granular::Field>(moon).mass[3]==400);
    AIVERSE_TEST_CHECK(w.get<evolve::Region>(eco).fertility>500000000);
    AIVERSE_TEST_CHECK((w.get<society::Person>(receiver).knowledge&1)!=0);
    auto restored=create();std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(sim->serialize(),&error));
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
    sim->world().enableCowSnapshots();
    SimSnapshot snapshot;sim->saveState(snapshot);
    sim->step();restored->step();
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
    sim->loadState(snapshot);sim->step();
    AIVERSE_TEST_CHECK(restored->checksum()==sim->checksum());
}
int main(){
    checkAuthoredPlanetDemo();
    checkPlanetStudioDiagnostics();
    checkCircuitSurfaceWiring();
    checkAetherStratification();
    checkMorphGravity();
    checkPlanetSoilTransfer();
    checkPlanetGranular();
    checkPlanetEvolveHydration();
    checkPlanetGravity();
    checkPlanetImprint();
    checkPlanetCrowdSlots();
    checkPlanetEvolveRegions();
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
