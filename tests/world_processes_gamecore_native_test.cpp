#include "apps/studio-native/StudioPlanetLiveBudget.h"
#include "apps/studio-native/StudioWorldProcessLiveOptions.h"
#include "apps/studio-native/StudioPlanetLiveSnapshot.h"
#include "apps/studio-native/StudioPlanetAetherSnapshot.h"
#include "apps/studio-native/StudioPlanetFractureSnapshot.h"
#include "apps/studio-native/StudioPlanetMorphSnapshot.h"
#include "apps/studio-native/StudioPlanetEchoSnapshot.h"
#include "apps/studio-native/StudioPlanetCrowdSnapshot.h"
#include "apps/studio-native/StudioPlanetCircuitSnapshot.h"
#include "apps/studio-native/StudioPlanetSocietySnapshot.h"
// Actual GameCore map fixture proving native module ECS hydration via mapPlaced().
#include "aiverselib/world/WorldProcessesNativeModule.h"
#include "aiverselib/world/WorldProcessGravity.h"
#include "aiverselib/world/PlanetProcessGeometry.h"
#include "aiverselib/world/PlanetGranularPhysicalFrame.h"
#include "aiverselib/world/MorphPlanetModelFrame.h"
#include "aiverselib/world/PlanetFractureDelivery.h"
#include "aiverselib/world/FracturePlanetFoundation.h"
#include "aiverselib/world/SurfaceGravitySupport.h"
#include "aiverselib/world/PlanetCharacterGravityFrame.h"
#include "aiverselib/world/GranularPlanetTerrainTransfer.h"
#include "aiverselib/world/GranularPlanetSoilGeometry.h"
#include "aiverselib/world/GranularGameCorePlanetSoil.h"
#include "aiverselib/world/CircuitSurfaceWiring.h"
#include "apps/studio-native/StudioPlanetWorldProcesses.h"
#include "aiverselib/world/EvolveSurfaceEcology.h"
#include "aiverselib/world/EvolvePlanetEcologySampling.h"
#include "aiverselib/world/EvolvePlanetRootClimate.h"
#include "aiverselib/world/EvolvePlanetRootSupport.h"
#include "engine/game/PlanetTerrainWater.h"
#include "aiverselib/world/NereidPlanetBankErosion.h"
#include "aiverselib/world/NereidPlanetSedimentTransport.h"
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
static void checkPlanetCharacterGravityFrame(){
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;physis::GravitySettings settings;settings.baseline={-10_fx,0_fx,0_fx};
    ecs.setResource<physis::GravitySettings>(settings);
    const auto at=surface.cellCenter(surface.gridCell(1,0,3,3));
    const auto frame=wp::planetCharacterGravityFrame(ecs,surface,at,"moon",{0_fx,0_fx,1_fx});
    AIVERSE_TEST_CHECK(frame&&frame->body==1&&frame->allowGround&&frame->up.x>Fixed::fromRatio(99,100));
    const auto basis=fxQuaternionBasis(frame->rotation);
    AIVERSE_TEST_CHECK(basis&&basis->up.dot(frame->up)>Fixed::fromRatio(99,100));
    AIVERSE_TEST_CHECK(!wp::planetCharacterGravityFrame(ecs,surface,at,"terra",{}));
    ecs.resource<physis::GravitySettings>()->baseline={};
    const auto zero=wp::planetCharacterGravityFrame(ecs,surface,at,"moon",{});
    AIVERSE_TEST_CHECK(zero&&!zero->allowGround&&zero->magnitude==Fixed{});
    ecs.resource<physis::GravitySettings>()->baseline={10_fx,0_fx,0_fx};
    const auto reverse=wp::planetCharacterGravityFrame(ecs,surface,at,"moon",{1_fx,0_fx,0_fx});
    AIVERSE_TEST_CHECK(reverse&&reverse->allowGround&&reverse->up.x<Fixed::fromRatio(-99,100));
}
static void checkPlanetGravitySupport(){
    const Vec3 up{Fixed{},Fixed::fromInt(1),Fixed{}};
    const auto down=wp::classifyGravitySupport({Fixed{},Fixed::fromInt(-10),Fixed{}},up,{});
    AIVERSE_TEST_CHECK(down&&down->supported()&&down->inwardAcceleration==Fixed::fromInt(10));
    const auto none=wp::classifyGravitySupport({},up,{});
    AIVERSE_TEST_CHECK(none&&none->status==wp::GravitySupportStatus::WeakGravity);
    const auto reverse=wp::classifyGravitySupport(up,up,{});
    AIVERSE_TEST_CHECK(reverse&&reverse->status==wp::GravitySupportStatus::Outward);
    const auto slope=wp::classifyGravitySupport({Fixed::fromInt(10),Fixed::fromInt(-1),Fixed{}},up,{});
    AIVERSE_TEST_CHECK(slope&&slope->status==wp::GravitySupportStatus::TooSteep);
    const auto air=wp::classifyGravitySupport(down->acceleration,up,Fixed::fromInt(2));
    AIVERSE_TEST_CHECK(air&&air->status==wp::GravitySupportStatus::Airborne);
    AIVERSE_TEST_CHECK(!wp::classifyGravitySupport(up,{},{}));
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;physis::GravitySettings settings;settings.baseline={};
    ecs.setResource<physis::GravitySettings>(settings);
    const auto e=ecs.create();
    physis::GravityField gravity;gravity.mode=physis::GravityMode::Attract;
    gravity.center=game::Surface::anchor(0);gravity.radiusMeters=Fixed::fromInt(200);
    gravity.radialMagnitude=Fixed::fromInt(10);gravity.overrideBaseline=true;
    ecs.add<physis::GravityField>(e,gravity);
    for(int face=0;face<6;++face){
        const auto pos=surface.cellCenter(surface.gridCell(0,face,7,3));
        const auto support=wp::samplePlanetGravitySupport(ecs,surface,pos);
        AIVERSE_TEST_CHECK(support&&support->body==0&&support->supported());
    }
    const auto p=surface.cellCenter(surface.gridCell(0,0,7,3));
    const auto lunar=surface.cellCenter(surface.gridCell(1,0,7,3));
    AIVERSE_TEST_CHECK(wp::samplePlanetGravitySupport(ecs,surface,lunar)->status==
        wp::GravitySupportStatus::WeakGravity);
    ecs.enableCowSnapshots();const World saved=ecs;
    ecs.get<physis::GravityField>(e).mode=physis::GravityMode::Repel;
    AIVERSE_TEST_CHECK(wp::samplePlanetGravitySupport(ecs,surface,p)->status==
        wp::GravitySupportStatus::Outward);
    ecs=saved;
    AIVERSE_TEST_CHECK(wp::samplePlanetGravitySupport(ecs,surface,p)->supported());
}
static void checkPlanetLiveGravity(){
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;physis::GravitySettings settings;settings.baseline={};ecs.setResource<physis::GravitySettings>(settings);
    const auto empty=aiverse::studio::samplePlanetGravity(ecs,surface,7);
    AIVERSE_TEST_CHECK(empty.getBool("available")&&empty["bodies"].size()==2);
    AIVERSE_TEST_CHECK(empty["bodies"][size_t(0)].getInt("zero")==96);
    physis::GravityField gravity;gravity.mode=physis::GravityMode::Attract;
    gravity.center=game::Surface::anchor(0);gravity.radiusMeters=Fixed::fromInt(200);
    gravity.radialMagnitude=Fixed::fromInt(10);gravity.overrideBaseline=true;
    const auto field=ecs.create();ecs.add<physis::GravityField>(field,gravity);
    const auto radial=aiverse::studio::samplePlanetGravity(ecs,surface,8);
    AIVERSE_TEST_CHECK(radial["bodies"][size_t(0)].getInt("inward")==96);
    AIVERSE_TEST_CHECK(radial["bodies"][size_t(1)].getStr("body")=="moon");
    AIVERSE_TEST_CHECK(radial["bodies"][size_t(1)].getInt("zero")==96);
    physis::GravityField additive;additive.acceleration={Fixed::fromInt(2),{},{}};
    ecs.add<physis::GravityField>(ecs.create(),additive);
    const auto combined=aiverse::studio::samplePlanetGravity(ecs,surface,9);
    const auto& row=combined["bodies"][size_t(0)]["samples"][size_t(0)];
    const auto position=game::Surface::anchor(0)+surface.facePoint(0,0,Fixed::fromInt(1),Fixed::fromInt(1));
    const auto actual=physis::gravityAt(ecs,position);
    AIVERSE_TEST_CHECK(std::abs(row["acceleration"][size_t(0)].toDouble()-actual.x.toDouble())<.001);
    AIVERSE_TEST_CHECK(combined["bodies"][size_t(1)]["samples"][size_t(0)]["magnitude"].toDouble()==2.);
    ecs.get<physis::GravityField>(field).mode=physis::GravityMode::Repel;
    const auto reversed=aiverse::studio::samplePlanetGravity(ecs,surface,10);
    AIVERSE_TEST_CHECK(reversed["bodies"][size_t(0)].getInt("outward")==96);
    const auto cap=aiverse::studio::samplePlanetGravity(ecs,surface,10,1);
    AIVERSE_TEST_CHECK(!cap.getBool("available")&&cap.getBool("truncated")&&cap["bodies"].size()==0);
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
static void checkPlanetLiveCrowd(){
    using aiverse::studio::samplePlanetCrowd;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    const int first=surface.gridCell(0,0,7,3),seam=surface.neighbor(first,0);
    AIVERSE_TEST_CHECK(seam>=0);
    World ecs;
    const auto actor=ecs.create(),other=ecs.create(),slot=ecs.create(),remote=ecs.create(),pref=ecs.create();
    const auto at=[&](Entity e,int cell){Transform t{};t.pos=surface.cellCenter(cell);ecs.add<Transform>(e,t);};
    at(actor,first);at(other,first);at(slot,seam);at(remote,surface.gridCell(1,0,3,3));
    crowd::Agent a{};a.sampledTick=7;a.allocated=slot;ecs.add<crowd::Agent>(actor,a);
    a.allocated=remote;ecs.add<crowd::Agent>(other,a);
    ecs.add<crowd::Slot>(slot);ecs.add<crowd::Slot>(remote);
    crowd::Preference p{};p.agent=actor;p.options[0]=slot;ecs.add<crowd::Preference>(pref,p);
    game::Mobile m{};m.moving=true;m.goal=surface.cellCenter(seam);
    m.path={surface.cellCenter(first),surface.cellCenter(seam),surface.cellCenter(surface.gridCell(1,0,3,3))};
    m.pathPos=1;ecs.add<game::Mobile>(actor,m);
    ecs.add<game::Orders>(actor);
    physis::GravitySettings gravity;gravity.baseline={};ecs.setResource<physis::GravitySettings>(gravity);
    const auto snapshot=samplePlanetCrowd(ecs,surface,7,true);
    AIVERSE_TEST_CHECK(snapshot.getBool("available")&&snapshot["agents"].size()==2&&snapshot["slots"].size()==2);
    AIVERSE_TEST_CHECK(snapshot.getInt("invalidReservation")==1&&snapshot.getInt("crossBody")==2);
    const auto& row=snapshot["agents"][size_t(0)];
    AIVERSE_TEST_CHECK(row.getStr("id")==std::to_string(actor.packed())&&row.getBool("reservationValid"));
    AIVERSE_TEST_CHECK(row.getInt("pathCursor")==1&&row["route"].size()==2&&row["route"][size_t(1)].isNull());
    AIVERSE_TEST_CHECK(row["support"].getStr("status")=="weak");
    AIVERSE_TEST_CHECK(snapshot["slots"][size_t(0)].getInt("occupancy")==1);
    AIVERSE_TEST_CHECK(snapshot["slots"][size_t(1)].getInt("occupancy")==0);
    const auto stale=samplePlanetCrowd(ecs,surface,8,true);
    AIVERSE_TEST_CHECK(stale.getInt("stale")==2&&stale["agents"].size()==0);
    const auto capped=samplePlanetCrowd(ecs,surface,7,false,nullptr,1,512,0);
    AIVERSE_TEST_CHECK(capped.getBool("truncated")&&capped["agents"].size()==1&&capped.getInt("routePoints")==0);
    AIVERSE_TEST_CHECK(ecs.get<crowd::Agent>(actor).allocated==slot&&ecs.get<game::Mobile>(actor).pathPos==1);
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
static void checkPlanetRootClimate(){
    using namespace evolve;
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    int b=0,f=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(seam,b,f,x,y)&&b==0&&f!=0);
    const auto origin=surface.cellCenter(start),point=surface.cellCenter(seam);
    const auto lunar=surface.cellCenter(surface.gridCell(1,f,x,y));
    World ecs;
    physis::Settings cfg;cfg.width=3;cfg.height=1;physis::initialize(ecs,cfg);
    const auto a=physis::cellAt(ecs,0,0),c=physis::cellAt(ecs,1,0),m=physis::cellAt(ecs,2,0);
    ecs.get<physis::Surface>(a).wetness=Fixed::fromRatio(1,4);
    ecs.get<physis::Surface>(c).wetness=Fixed::fromRatio(1,2);
    ecs.get<physis::Surface>(m).wetness=Fixed::fromRatio(3,4);
    const auto region=ecs.create();Region r{};r.fertility=300000000;
    ecs.add<Region>(region,r);
    AIVERSE_TEST_CHECK(setSurfaceArea(ecs,surface,region,origin,Area{0,0,3000,3000,true}));
    std::vector<PlanetWeatherMapping> sources{
        {c,"terra",origin,Fixed::fromInt(40)},
        {a,"terra",origin,Fixed::fromInt(40)},
        {m,"moon",lunar,Fixed::fromInt(40)}};
    std::vector<PlanetRootClimateSite> sites{
        {"terra",point,{900,0,800,700},true,true},
        {"moon",lunar,{900,0,800,700},true,true}};
    const auto first=samplePlanetRootClimate(ecs,surface,sources,sites,0);
    AIVERSE_TEST_CHECK(first&&first->sites.size()==2&&first->climateTick==0);
    const auto winner=a.packed()<c.packed()?a:c;
    AIVERSE_TEST_CHECK(first->sites[0].weather->cell==winner&&first->sites[0].climate.nutrientsPermille==300);
    AIVERSE_TEST_CHECK(first->sites[0].climate.waterPermille==(winner==a?250u:500u));
    AIVERSE_TEST_CHECK(first->sites[1].weather->cell==m&&first->sites[1].climate.waterPermille==750);
    std::reverse(sources.begin(),sources.end());
    const auto repeat=samplePlanetRootClimate(ecs,surface,sources,sites,0);
    AIVERSE_TEST_CHECK(repeat&&repeat->sites[0].weather->cell==winner);
    AIVERSE_TEST_CHECK(!samplePlanetRootClimate(ecs,surface,sources,sites,1));
    const auto saved=sources;
    sources.push_back(sources.front());
    AIVERSE_TEST_CHECK(!samplePlanetRootClimate(ecs,surface,sources,sites,0));
    sources=saved;sources.erase(std::remove_if(sources.begin(),sources.end(),[&](const auto& v){return v.cell==m;}),sources.end());
    AIVERSE_TEST_CHECK(!samplePlanetRootClimate(ecs,surface,sources,sites,0));
    sites[1].requireWeather=false;
    const auto uncovered=samplePlanetRootClimate(ecs,surface,sources,sites,0);
    AIVERSE_TEST_CHECK(uncovered&&!uncovered->sites[1].weather&&uncovered->sites[1].climate.waterPermille==800);
    const auto orphan=ecs.create();ecs.add<physis::Cell>(orphan,physis::Cell{});
    ecs.add<physis::Atmosphere>(orphan,physis::Atmosphere{});ecs.add<physis::Surface>(orphan,physis::Surface{});
    sources.push_back({orphan,"terra",origin,Fixed::fromInt(40)});
    AIVERSE_TEST_CHECK(!samplePlanetRootClimate(ecs,surface,sources,sites,0));
}
static void checkPlanetEcologySampling(){
    using namespace evolve;
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    const int start=surface.gridCell(0,0,7,3),seam=surface.neighbor(start,0);
    AIVERSE_TEST_CHECK(seam>=0);
    int b=0,f=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(seam,b,f,x,y)&&b==0&&f!=0);
    const auto origin=surface.cellCenter(start),point=surface.cellCenter(seam);
    const auto lunar=surface.cellCenter(surface.gridCell(1,f,x,y));
    World ecs;
    const auto region=ecs.create();
    Region r{};r.fertility=300000000;r.pollution=400000000;
    ecs.add<Region>(region,r);
    AIVERSE_TEST_CHECK(setSurfaceArea(ecs,surface,region,origin,Area{0,0,3000,3000,true}));
    const auto weather=ecs.create();
    physis::Atmosphere air{};air.temperature=Fixed::fromInt(12);
    air.rain=Fixed::fromRatio(1,2);ecs.add<physis::Atmosphere>(weather,air);
    physis::Surface soil{};soil.wetness=Fixed::fromRatio(1,4);
    ecs.add<physis::Surface>(weather,soil);
    PlanetEcologyRequest first{"terra",point,{900,0,800,700},
        PlanetWeatherSource{weather,origin,Fixed::fromInt(40),true}};
    PlanetEcologyRequest second{"moon",lunar,{900,0,800,700},{}};
    std::array<PlanetEcologyRequest,2> requests{first,second};
    const auto result=samplePlanetEcology(ecs,surface,requests);
    AIVERSE_TEST_CHECK(result&&result->size()==2);
    AIVERSE_TEST_CHECK((*result)[0].body==0&&(*result)[0].face==f);
    AIVERSE_TEST_CHECK((*result)[0].contributingRegions==1);
    AIVERSE_TEST_CHECK((*result)[0].climate.nutrientsPermille==300);
    AIVERSE_TEST_CHECK((*result)[0].climate.sunPermille==800);
    AIVERSE_TEST_CHECK((*result)[0].climate.waterPermille==250);
    AIVERSE_TEST_CHECK((*result)[0].weather&&(*result)[0].weather->temperature==Fixed::fromInt(12));
    AIVERSE_TEST_CHECK((*result)[1].body==1&&(*result)[1].contributingRegions==0);
    AIVERSE_TEST_CHECK((*result)[1].climate.nutrientsPermille==700&&(*result)[1].climate.waterPermille==800);
    ecs.enableCowSnapshots();const World saved=ecs;
    ecs.get<Region>(region).enabled=false;
    auto changed=samplePlanetEcology(ecs,surface,requests);
    AIVERSE_TEST_CHECK(changed&&(*changed)[0].climate.nutrientsPermille==700);
    ecs=saved;
    changed=samplePlanetEcology(ecs,surface,requests);
    AIVERSE_TEST_CHECK(changed&&(*changed)[0].climate.nutrientsPermille==300);
    requests[0].weather->capWaterBySurfaceWetness=false;
    changed=samplePlanetEcology(ecs,surface,requests);
    AIVERSE_TEST_CHECK(changed&&(*changed)[0].climate.waterPermille==800);
    requests[0].weather->maxGroundDistance=Fixed{};
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests));
    requests[0]=first;requests[1].weather=first.weather;
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests)); // wrong body
    requests[1]=second;requests[1].expectedBody="terra";
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests));
    requests[1]=second;requests[1].baseline.sunPermille=1001;
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests));
    requests[1]=second;requests[0].ground=point+surface.up(point)*Fixed::fromInt(2);
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests));
    requests[0]=first;ecs.destroy(weather);
    AIVERSE_TEST_CHECK(!samplePlanetEcology(ecs,surface,requests));
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
static void checkPlanetTerrainFootprint(){
    game::Surface surface;game::BodyDef body;
    body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    using lib::worldprocess::planetTerrainFootprint;
    const auto interior=planetTerrainFootprint(surface,0,surface.gridCell(0,0,3,3));
    AIVERSE_TEST_CHECK(interior&&interior->affectedCells.size()==9);
    for(const auto& c:interior->corners)AIVERSE_TEST_CHECK(c.aliasCount==1);
    const auto edge=planetTerrainFootprint(surface,0,surface.gridCell(0,0,7,3));
    AIVERSE_TEST_CHECK(edge&&edge->affectedCells.size()==9);
    size_t doubles=0;
    for(const auto& c:edge->corners)if(c.aliasCount==2)++doubles;
    AIVERSE_TEST_CHECK(doubles==2);
    const int cornerCell=surface.gridCell(0,0,7,7);
    const auto triple=planetTerrainFootprint(surface,0,cornerCell);
    AIVERSE_TEST_CHECK(triple&&triple->bodyId=="terra");
    int alias=-1;
    for(const auto& c:triple->corners)if(c.aliasCount==3)alias=c.aliases[2];
    AIVERSE_TEST_CHECK(alias>=0);
    for(int cell:triple->affectedCells)AIVERSE_TEST_CHECK(surface.bodyOfCell(cell)==0);
    AIVERSE_TEST_CHECK(!planetTerrainFootprint(surface,1,cornerCell));
    const auto moon=planetTerrainFootprint(surface,1,surface.gridCell(1,0,7,7));
    AIVERSE_TEST_CHECK(moon&&moon->bodyId=="moon"&&moon->corners[0].canonical==triple->corners[0].canonical);
    AIVERSE_TEST_CHECK(!planetTerrainFootprint(surface,0,-1));
    surface.corners[surface.bodies[0].cornerBase+size_t(alias)]=1;
    AIVERSE_TEST_CHECK(!planetTerrainFootprint(surface,0,cornerCell));
    AIVERSE_TEST_CHECK(planetTerrainFootprint(surface,1,surface.gridCell(1,0,7,7)));
}
static void checkPlanetSoilGeometry(){
    using namespace granular;
    game::Surface surface;
    game::BodyDef def;def.id="terra";def.radius=Fixed::fromInt(64);def.faceSize=8;
    surface.bodies.push_back(def);def.id="moon";surface.bodies.push_back(def);surface.build();
    const auto original=surface.corners;
    for(const auto xy:std::array<std::array<int,2>,3>{{{3,3},{7,3},{7,7}}}){
        PlanetSoilTarget target;
        target.body=0;target.bodyId="terra";target.surfaceCell=surface.gridCell(0,0,xy[0],xy[1]);
        const auto footprint=wp::planetTerrainFootprint(surface,0,target.surfaceCell);
        AIVERSE_TEST_CHECK(footprint);
        target.footprint=*footprint;target.volumeBudgetCm3=UINT64_MAX;
        const auto fill=previewPlanetSoilGeometry(surface,target,1,-100,100);
        AIVERSE_TEST_CHECK(fill&&fill->movedVolumeCm3>0&&fill->surface->tab==surface.tab);
        AIVERSE_TEST_CHECK(surface.corners==original&&fill->affectedCells==footprint->affectedCells);
        for(const auto& corner:footprint->corners)
            for(int i=0;i<corner.aliasCount;++i)
                AIVERSE_TEST_CHECK(fill->surface->corners[size_t(corner.aliases[size_t(i)])]==1);
        for(size_t i=surface.bodies[1].cornerBase;i<original.size();++i)
            AIVERSE_TEST_CHECK(fill->surface->corners[i]==original[i]);
        AIVERSE_TEST_CHECK(!previewPlanetSoilGeometry(*fill->surface,target,1,-100,100)); // stale footprint
        target.volumeBudgetCm3=fill->movedVolumeCm3-1;
        AIVERSE_TEST_CHECK(!previewPlanetSoilGeometry(surface,target,1,-100,100));
        target.volumeBudgetCm3=fill->movedVolumeCm3;
        AIVERSE_TEST_CHECK(!previewPlanetSoilGeometry(surface,target,1,-100,0));
        const auto next=wp::planetTerrainFootprint(*fill->surface,0,target.surfaceCell);
        AIVERSE_TEST_CHECK(next);
        target.footprint=*next;target.direction=PlanetSoilDirection::Dig;
        const auto dig=previewPlanetSoilGeometry(*fill->surface,target,1,-100,100);
        AIVERSE_TEST_CHECK(dig&&dig->movedVolumeCm3==fill->movedVolumeCm3&&dig->surface->corners==original);
    }
}
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
    AIVERSE_TEST_CHECK(target->footprint.bodyId==target->bodyId&&target->footprint.surfaceCell==target->surfaceCell&&!target->footprint.affectedCells.empty());
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
static void checkPlanetLiveFracture(){
    using aiverse::studio::samplePlanetFracture;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto root=ecs.create(),child=ecs.create(),moon=ecs.create(),bond=ecs.create(),cross=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(0,0,3,3));ecs.add<Transform>(root,tr);
    tr.pos=surface.cellCenter(surface.gridCell(0,0,4,3));ecs.add<Transform>(child,tr);
    tr.pos=surface.cellCenter(surface.gridCell(1,0,3,3));ecs.add<Transform>(moon,tr);
    ecs.add<fracture::Member>(root,fracture::Member{true,false,false});
    ecs.add<fracture::Member>(child,fracture::Member{});
    ecs.add<fracture::Member>(moon,fracture::Member{true,false,false});
    ecs.add<fracture::Collapse>(child,fracture::Collapse{});
    ecs.add<fracture::Bond>(bond,fracture::Bond{root,child,100,25,true});
    ecs.add<fracture::Bond>(cross,fracture::Bond{root,moon,100,0,true});
    AIVERSE_TEST_CHECK(fracture::advance(ecs,7));
    const auto before=samplePlanetFracture(ecs,surface,7);
    AIVERSE_TEST_CHECK(before.getBool("available")&&before["members"].size()==3);
    AIVERSE_TEST_CHECK(before["bonds"].size()==1&&before.getInt("crossBody")==1);
    AIVERSE_TEST_CHECK(before["bonds"][size_t(0)].getInt("damage")==25&&before["bonds"][size_t(0)].getBool("activeSupportEdge"));
    ecs.get<fracture::Bond>(bond).damage=100;
    AIVERSE_TEST_CHECK(fracture::advance(ecs,8));
    physis::NatureStressOutputComponent stress;stress.valid=true;stress.sampledClimateTick=123;
    stress.normalizedLoad=Fixed::fromInt(2);stress.accumulatedDamage=Fixed::fromRatio(1,2);
    ecs.add<physis::NatureStressOutputComponent>(child,stress);
    const auto after=samplePlanetFracture(ecs,surface,8);
    AIVERSE_TEST_CHECK(after.getInt("detached")==1&&after.getInt("pending")==1);
    AIVERSE_TEST_CHECK(!after["bonds"][size_t(0)].getBool("activeSupportEdge"));
    bool found=false;
    for(const auto& row:after["members"].items())if(row.getStr("id")==std::to_string(child.packed())){
        found=true;AIVERSE_TEST_CHECK(row.getStr("body")=="terra"&&row.getBool("detached"));
        AIVERSE_TEST_CHECK(row["collapse"].getStr("delivery")=="pending"&&row["collapse"].getInt("tick")==8);
        AIVERSE_TEST_CHECK(row["stress"].getStr("climateTick")=="123");
    }
    AIVERSE_TEST_CHECK(found);
    AIVERSE_TEST_CHECK(ecs.get<fracture::Collapse>(child).delivery==fracture::Delivery::Pending);
    const auto cap=samplePlanetFracture(ecs,surface,8,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["members"].size()==1&&cap.getInt("pending")==1);
    AIVERSE_TEST_CHECK(!samplePlanetFracture(ecs,surface,UINT32_MAX).getBool("available"));
}
static void checkPlanetSedimentTransport(){
    namespace nw=aiverse::lib::nereid;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;const auto sea=ecs.create();
    const int cell=surface.gridCell(0,0,7,3),target=surface.neighbor(cell,0);
    int b=-1,f=-1,x=-1,y=-1;
    AIVERSE_TEST_CHECK(surface.cellFace(target,b,f,x,y)&&b==0&&f!=0);
    const auto up=surface.cellDir(cell),next=surface.cellDir(target);
    const auto tangent=fxNormalize(next-up*next.dot(up));
    nw::WaterSphereComponent ocean;ocean.center=game::Surface::anchor(0);
    ocean.surfaceRadius=Fixed::fromInt(70);ocean.floorRadius=Fixed::fromInt(50);
    ocean.axis=fxCross(up,tangent);ocean.currentSpeed=Fixed::fromInt(10);
    ecs.add<nw::WaterSphereComponent>(sea,ocean);
    const auto source=ecs.create(),dest=ecs.create();
    nw::PlanetBankSoilComponent bank;bank.bodyId="terra";bank.waterBody=sea;
    bank.surfaceCell=cell;bank.lastTick=7;bank.suspendedCm3=1000000000000ull;
    ecs.add<nw::PlanetBankSoilComponent>(source,bank);
    ecs.add<nw::PlanetBankSedimentTransportComponent>(source,{});
    bank.surfaceCell=target;bank.suspendedCm3=0;
    ecs.add<nw::PlanetBankSoilComponent>(dest,bank);
    ecs.add<nw::PlanetBankSedimentTransportComponent>(dest,{});
    ecs.enableCowSnapshots();const World before=ecs;
    const auto result=nw::transportPlanetSediment(ecs,surface,7,100,Fixed{});
    AIVERSE_TEST_CHECK(result&&result->transfers.size()==1&&result->transfers[0].target==dest);
    AIVERSE_TEST_CHECK(result->movedCm3>0&&result->beforeCm3==result->afterCm3);
    AIVERSE_TEST_CHECK(ecs.get<nw::PlanetBankSoilComponent>(dest).suspendedCm3==result->movedCm3);
    AIVERSE_TEST_CHECK(!nw::transportPlanetSediment(ecs,surface,7,100,Fixed{}));
    ecs=before;
    ecs.get<nw::PlanetBankSoilComponent>(dest).suspendedCm3=nw::kPlanetSedimentCapacityCm3;
    const auto full=nw::transportPlanetSediment(ecs,surface,7,100,Fixed{});
    AIVERSE_TEST_CHECK(full&&full->movedCm3==0&&full->beforeCm3==full->afterCm3);
    ecs=before;
    ecs.get<nw::PlanetBankSoilComponent>(dest).surfaceCell=cell;
    AIVERSE_TEST_CHECK(!nw::transportPlanetSediment(ecs,surface,7,100,Fixed{}));
    AIVERSE_TEST_CHECK(ecs.get<nw::PlanetBankSoilComponent>(source).suspendedCm3==1000000000000ull);
}
static void checkPlanetBankErosion(){
    namespace nw=aiverse::lib::nereid;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);surface.build();
    World ecs;const auto sea=ecs.create();
    nw::WaterSphereComponent ocean;ocean.center=game::Surface::anchor(0);
    ocean.surfaceRadius=Fixed::fromInt(70);ocean.floorRadius=Fixed::fromInt(50);ocean.currentSpeed=Fixed::fromInt(20);
    ecs.add<nw::WaterSphereComponent>(sea,ocean);
    nw::PlanetBankSoilComponent bank;bank.bodyId="terra";bank.surfaceCell=surface.gridCell(0,0,7,7);
    bank.waterBody=sea;bank.soil=nw::ShoreSoil::Sand;bank.demand100=-100;
    const auto cut=nw::planPlanetBankErosion(ecs,surface,bank,0,7,16,Fixed{});
    AIVERSE_TEST_CHECK(cut&&cut->geometry&&cut->geometry->heightDelta16<0);
    AIVERSE_TEST_CHECK(cut->bank.suspendedCm3==cut->geometry->movedVolumeCm3);
    AIVERSE_TEST_CHECK(!nw::planPlanetBankErosion(ecs,*cut->geometry->surface,cut->bank,1,7,16,Fixed{}));
    auto deposition=cut->bank;deposition.demand100=-cut->geometry->heightDelta16*100;
    deposition.erosionCarryMilliCm=deposition.depositionCarryMilliCm=0;
    ecs.get<nw::WaterSphereComponent>(sea).currentSpeed={};
    const auto fill=nw::planPlanetBankErosion(ecs,*cut->geometry->surface,deposition,1,8,16,Fixed{});
    AIVERSE_TEST_CHECK(fill&&fill->geometry&&fill->geometry->surface->corners==surface.corners);
    AIVERSE_TEST_CHECK(fill->bank.suspendedCm3==0&&fill->geometry->movedVolumeCm3==cut->geometry->movedVolumeCm3);
    deposition.suspendedCm3=1;
    const auto limited=nw::planPlanetBankErosion(ecs,*cut->geometry->surface,deposition,1,8,16,Fixed{});
    AIVERSE_TEST_CHECK(limited&&!limited->geometry&&limited->bank.suspendedCm3==1);
}
static void checkPlanetTerrainWater(){
    namespace nw=aiverse::lib::nereid;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto sea=ecs.create(),other=ecs.create();
    nw::WaterSphereComponent ocean;ocean.center=game::Surface::anchor(0);
    ocean.surfaceRadius=Fixed::fromInt(65);ocean.floorRadius=Fixed::fromInt(50);
    ocean.currentSpeed=Fixed::fromInt(2);
    ecs.add<nw::WaterSphereComponent>(sea,ocean);
    ocean.center=game::Surface::anchor(1);ocean.priority=100;
    ecs.add<nw::WaterSphereComponent>(other,ocean);
    const auto dir=surface.cellDir(surface.gridCell(0,0,7,7));
    const auto pos=game::Surface::anchor(0)+dir*Fixed::fromRatio(129,2);
    const auto water=g::samplePlanetTerrainWater(ecs,surface,pos);
    AIVERSE_TEST_CHECK(water.found&&water.body==sea&&water.submerged&&water.floorRadius==Fixed::fromInt(64));
    auto raised=surface;
    std::fill(raised.corners.begin(),raised.corners.begin()+ptrdiff_t(surface.bodies[1].cornerBase),int16_t(32));
    AIVERSE_TEST_CHECK(!g::samplePlanetTerrainWater(ecs,raised,pos).found);
    const auto moonPos=game::Surface::anchor(1)+dir*Fixed::fromRatio(129,2);
    AIVERSE_TEST_CHECK(g::samplePlanetTerrainWater(ecs,raised,moonPos).body==other);
    auto lower=surface;
    std::fill(lower.corners.begin(),lower.corners.begin()+ptrdiff_t(surface.bodies[1].cornerBase),int16_t(-16));
    const auto shore=g::samplePlanetShore(ecs,lower,pos,Fixed{},500,200,100);
    AIVERSE_TEST_CHECK(shore&&shore->water.body==sea&&shore->forcing.wetContactPermille==1000);
    AIVERSE_TEST_CHECK(shore->forcing.flow.y==Fixed{}&&nw::shoreFlowSpeedCmPerSec(shore->forcing.flow)>0);
    const auto dry=g::samplePlanetShore(ecs,raised,pos,Fixed{});
    AIVERSE_TEST_CHECK(dry&&dry->forcing.wetContactPermille==0);
    AIVERSE_TEST_CHECK(g::samplePlanetTerrainWater(ecs,surface,pos).submerged); // restored geometry
}
static void checkPlanetRootSupport(){
    using namespace terraweave;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    auto lowered=surface;
    std::fill(lowered.corners.begin(),lowered.corners.begin()+ptrdiff_t(surface.bodies[1].cornerBase),int16_t(-32));
    AnchoredPlant tree;tree.anchor.speciesId=1;tree.anchor.plantKey=10;
    tree.anchor.roots=defaultRootProfile(PlantHabit::Tree);
    auto moonTree=tree;moonTree.anchor.plantKey=11;
    World ecs;TerraWeaveAuthorityResource authority;authority.rooted={tree,moonTree};
    ecs.setResource<TerraWeaveAuthorityResource>(authority);ecs.enableCowSnapshots();
    const World before=ecs;
    std::vector<evolve::PlanetRootBinding> bindings{
        {{1,10},"terra",surface.cellCenter(surface.gridCell(0,0,7,7))},
        {{1,11},"moon",surface.cellCenter(surface.gridCell(1,0,7,7))}
    };
    auto wrong=bindings;wrong[1].bodyId="terra";
    AIVERSE_TEST_CHECK(!evolve::reconcilePlanetRoots(ecs,lowered,wrong,1,7));
    const auto result=evolve::reconcilePlanetRoots(ecs,lowered,bindings,1,7);
    AIVERSE_TEST_CHECK(result&&result->falls.size()==1&&result->falls[0].bodyId=="terra");
    AIVERSE_TEST_CHECK(result->falls[0].event.id.key==10&&result->falls[0].event.supportPermille==0);
    const auto* current=ecs.resourceConst<TerraWeaveAuthorityResource>();
    AIVERSE_TEST_CHECK(current->rooted[0].fallen&&!current->rooted[1].fallen);
    AIVERSE_TEST_CHECK(current->stateRevision==1&&!before.resourceConst<TerraWeaveAuthorityResource>()->rooted[0].fallen);
    const auto retry=evolve::reconcilePlanetRoots(ecs,lowered,bindings,1,7);
    AIVERSE_TEST_CHECK(retry&&retry->falls.empty()&&retry->authorityRevision==1);
    ecs=before;
    AIVERSE_TEST_CHECK(evolve::reconcilePlanetRoots(ecs,lowered,bindings,1,7)->falls.size()==1);
}
static void checkPlanetFoundationLoss(){
    using namespace fracture;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    const int cell=surface.gridCell(0,0,7,7);
    const auto footprint=wp::planetTerrainFootprint(surface,0,cell);
    AIVERSE_TEST_CHECK(footprint);
    auto lowered=surface;
    for(const auto& corner:footprint->corners)for(int k=0;k<corner.aliasCount;++k)
        lowered.corners[size_t(corner.aliases[size_t(k)])]=-16;
    World ecs;
    const auto root=ecs.create(),child=ecs.create(),other=ecs.create(),moon=ecs.create(),bond=ecs.create();
    const auto pos=surface.cellCenter(cell);
    const auto safe=surface.cellCenter(surface.gridCell(0,1,3,3));
    const auto lunar=surface.cellCenter(surface.gridCell(1,0,7,7));
    for(const auto& item:std::array<std::pair<Entity,Vec3>,4>{{{root,pos},{child,pos},{other,safe},{moon,lunar}}}){
        Transform tr{};tr.pos=item.second;ecs.add<Transform>(item.first,tr);
        ecs.add<Member>(item.first,Member{item.first!=child,false,false});
        ecs.add<Collapse>(item.first,Collapse{});
    }
    ecs.add<Bond>(bond,Bond{root,child,100,0,true});
    std::vector<PlanetFoundation> supports{{root,"terra",{pos},1,Fixed::fromRatio(1,8)},
        {other,"terra",{safe},1,Fixed::fromRatio(1,8)},
        {moon,"moon",{lunar},1,Fixed::fromRatio(1,8)}};
    ecs.enableCowSnapshots();const World before=ecs;
    auto malformed=supports;malformed.push_back(supports.front());
    AIVERSE_TEST_CHECK(!reconcilePlanetFoundations(ecs,surface,lowered,malformed,7));
    AIVERSE_TEST_CHECK(ecs.get<Member>(root).anchored);
    const auto result=reconcilePlanetFoundations(ecs,surface,lowered,supports,7);
    AIVERSE_TEST_CHECK(result&&result->lostAnchors.size()==1&&result->lostAnchors[0]==root&&result->newlyDetached.size()==2);
    AIVERSE_TEST_CHECK(!ecs.get<Member>(root).anchored&&ecs.get<Member>(root).detached&&ecs.get<Member>(child).detached);
    AIVERSE_TEST_CHECK(!ecs.get<Member>(other).detached&&!ecs.get<Member>(moon).detached);
    AIVERSE_TEST_CHECK(ecs.get<Transform>(root).pos==pos&&ecs.get<Transform>(child).pos==pos);
    const auto events=pending(ecs);
    AIVERSE_TEST_CHECK(events.size()==2);
    AIVERSE_TEST_CHECK(projectPlanetCollapse(ecs,lowered,events.front()));
    const auto again=reconcilePlanetFoundations(ecs,surface,lowered,supports,7);
    AIVERSE_TEST_CHECK(again&&again->lostAnchors.empty()&&pending(ecs).size()==2);
    ecs=before;
    AIVERSE_TEST_CHECK(ecs.get<Member>(root).anchored&&pending(ecs).empty());
    // A redundant remaining foundation contact prevents loss.
    supports[0].feet.push_back(safe);
    const auto redundant=reconcilePlanetFoundations(ecs,surface,lowered,supports,7);
    AIVERSE_TEST_CHECK(redundant&&redundant->lostAnchors.empty()&&!ecs.get<Member>(child).detached);
}
static void checkPlanetCollapseDelivery(){
    game::Surface surface;game::BodyDef body;
    body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;const auto member=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(1,4,3,3));
    ecs.add<Transform>(member,tr);ecs.add<fracture::Member>(member,fracture::Member{false,true,true});
    const auto key=fracture::collapseEventKey(member,7);
    ecs.add<fracture::Collapse>(member,fracture::Collapse{key,7,fracture::Delivery::Pending});
    const fracture::Event event{member,key,7};
    const auto projected=fracture::projectPlanetCollapse(ecs,surface,event);
    AIVERSE_TEST_CHECK(projected&&projected->frame.body==1&&projected->frame.origin==tr.pos);
    AIVERSE_TEST_CHECK(ecs.get<fracture::Collapse>(member).delivery==fracture::Delivery::Pending);
    AIVERSE_TEST_CHECK(!fracture::projectPlanetCollapse(ecs,surface,{member,key,8}));
    physis::NatureFractureOutboxComponent nature{};
    nature.delivery=physis::NatureFractureDelivery::Pending;
    ecs.add<physis::NatureFractureOutboxComponent>(member,nature);
    AIVERSE_TEST_CHECK(!fracture::projectPlanetCollapse(ecs,surface,event));
    ecs.remove<physis::NatureFractureOutboxComponent>(member);
    AIVERSE_TEST_CHECK(!fracture::commitPlanetCollapse(ecs,surface,event,6,
        [](World&,const auto&){return true;}));
    ecs.enableCowSnapshots();const World before=ecs;
    const auto failed=fracture::commitPlanetCollapse(ecs,surface,event,7,
        [&](World& staged,const auto&){staged.get<Transform>(member).pos={};return false;});
    AIVERSE_TEST_CHECK(!failed&&ecs.get<Transform>(member).pos==tr.pos);
    AIVERSE_TEST_CHECK(!fracture::planetCollapseReceipt(ecs,event));
    // Fixture owner explicitly stages a durable consequence on the same
    // entity. Production owners must persist shape/pose and support removal.
    const auto committed=fracture::commitPlanetCollapse(ecs,surface,event,7,
        [&](World& staged,const auto& delivery){
            staged.get<fracture::Member>(member).broken=true;
            return delivery.frame.body==1;
        });
    AIVERSE_TEST_CHECK(committed&&fracture::planetCollapseReceipt(ecs,event));
    AIVERSE_TEST_CHECK(!fracture::projectPlanetCollapse(ecs,surface,event));
    AIVERSE_TEST_CHECK(!fracture::commitPlanetCollapse(ecs,surface,event,8,
        [](World&,const auto&){return true;}));
    AIVERSE_TEST_CHECK(!fracture::planetCollapseReceipt(before,event));
    ecs=before;AIVERSE_TEST_CHECK(fracture::projectPlanetCollapse(ecs,surface,event));
}
static void checkPlanetMorphModelFrame(){
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;const auto owner=ecs.create(),node=ecs.create(),other=ecs.create();
    WorldTransform wt;wt.position=surface.cellCenter(surface.gridCell(1,0,3,3));
    wt.affine.m[3]=wt.position.x;wt.affine.m[7]=wt.position.y;wt.affine.m[11]=wt.position.z;
    ecs.add<WorldTransform>(owner,wt);
    morph::Node n;n.sampledTick=7;ecs.add<morph::Node>(node,n);
    morph::GravityBinding binding;binding.frameOwner=owner;binding.unitsPerMetre=1000;
    AIVERSE_TEST_CHECK(morph::bindGravity(ecs,node,binding));
    const auto frame=morph::resolvePlanetModelFrame(ecs,surface,owner,"moon",1000);
    AIVERSE_TEST_CHECK(frame&&frame->frame.body==1&&frame->frame.origin==wt.position);
    AIVERSE_TEST_CHECK(morph::planetModelNodeCurrent(ecs,*frame,node,7));
    AIVERSE_TEST_CHECK(!morph::planetModelNodeCurrent(ecs,*frame,node,8));
    AIVERSE_TEST_CHECK(!morph::resolvePlanetModelFrame(ecs,surface,owner,"terra",1000));
    ecs.get<morph::GravityBinding>(node).unitsPerMetre=100;
    AIVERSE_TEST_CHECK(!morph::planetModelNodeCurrent(ecs,*frame,node,7));
    ecs.get<morph::GravityBinding>(node).unitsPerMetre=1000;
    ecs.get<morph::GravityBinding>(node).frameOwner=other;
    AIVERSE_TEST_CHECK(!morph::planetModelNodeCurrent(ecs,*frame,node,7));
    ecs.get<WorldTransform>(owner).affine.m[1]=Fixed::fromRatio(1,2);
    AIVERSE_TEST_CHECK(!morph::resolvePlanetModelFrame(ecs,surface,owner,"moon",1000));
    ecs.get<WorldTransform>(owner)=wt;
    ecs.get<WorldTransform>(owner).scale.x=Fixed::fromInt(2);
    AIVERSE_TEST_CHECK(!morph::resolvePlanetModelFrame(ecs,surface,owner,"moon",1000));
}
static void checkPlanetMorphContactFrame(){
    game::Surface surface;game::BodyDef body;
    body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);surface.build();
    World ecs;const auto owner=ecs.create();
    Transform t{};ecs.add<Transform>(owner,t);
    for(int face=0;face<6;++face){
        const auto pos=surface.cellCenter(surface.gridCell(0,face,3,3));
        ecs.get<Transform>(owner).pos=pos;
        const auto up=surface.up(pos);
        const auto local=morph::projectMorphContactDirection(ecs,&surface,owner,up);
        AIVERSE_TEST_CHECK(local&&local->y>Fixed::fromRatio(99,100));
        AIVERSE_TEST_CHECK(fxAbs(local->x)<Fixed::fromRatio(1,100));
        AIVERSE_TEST_CHECK(fxAbs(local->z)<Fixed::fromRatio(1,100));
    }
    AIVERSE_TEST_CHECK(!morph::projectMorphContactDirection(ecs,&surface,owner,{}));
    ecs.destroy(owner);
    AIVERSE_TEST_CHECK(!morph::projectMorphContactDirection(ecs,&surface,owner,
        {Fixed{},Fixed::fromInt(1),Fixed{}}));
}
static void checkPlanetProcessGeometry(){
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.faceSize=8;body.radius=Fixed::fromInt(64);
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;const auto field=ecs.create(),trace=ecs.create();
    Transform t{};t.pos=surface.cellCenter(surface.gridCell(0,0,7,3));
    ecs.add<Transform>(field,t);ecs.add<Transform>(trace,t);
    granular::Field f;f.width=3;f.height=2;f.capacity=100;
    f.mass={0,0,0,100,100,100};f.solid.assign(6,0);f.sampledTick=7;
    ecs.add<granular::Field>(field,f);
    imprint::Cell c;c.footprint=50;c.sampledTick=7;ecs.add<imprint::Cell>(trace,c);
    granular::SurfaceFieldPlacement place{t.pos,1000,0};
    const auto tops=wp::planetGranularTops(ecs,surface,field,place,200,0,0,3,2,7);
    AIVERSE_TEST_CHECK(tops&&tops->size()==3);
    const wp::PlanetGranularScale scale{1000,200,0};
    const auto physical=wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,7,"terra");
    const auto canonical=wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,7);
    AIVERSE_TEST_CHECK(physical&&canonical&&physical->body==0);
    AIVERSE_TEST_CHECK(physical->publicationOffset==game::Surface::anchor(0));
    AIVERSE_TEST_CHECK(physical->patchOrigin+physical->publicationOffset==canonical->patchOrigin);
    AIVERSE_TEST_CHECK(!wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,7,"moon"));
    AIVERSE_TEST_CHECK(!wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",{0,200,0},7));
    AIVERSE_TEST_CHECK(!wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,8));
    ecs.get<Transform>(field).pos=t.pos+surface.up(t.pos)*Fixed::fromInt(2);
    AIVERSE_TEST_CHECK(!wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,7));
    ecs.get<Transform>(field)=t;
    int b=0,face=0,x=0,y=0;
    AIVERSE_TEST_CHECK(surface.cellFace(tops->back().cell,b,face,x,y)&&b==0&&face!=0);
    for(const auto& q:*tops){
        AIVERSE_TEST_CHECK(q.body==0&&q.row==1);
        for(const auto& p:q.vertices)AIVERSE_TEST_CHECK(surface.bodyAt(p)==0);
        AIVERSE_TEST_CHECK(q.normal.dot(surface.up(q.vertices[0])).raw>0);
    }
    size_t shared=0;
    for(auto a:(*tops)[0].vertices)for(auto b:(*tops)[1].vertices)if(a==b)++shared;
    AIVERSE_TEST_CHECK(shared==2);
    const auto overlay=wp::planetImprintQuad(ecs,surface,trace,7);
    AIVERSE_TEST_CHECK(overlay&&overlay->footprint==50&&overlay->geometry.body==0);
    AIVERSE_TEST_CHECK(!wp::planetImprintQuad(ecs,surface,trace,8));
    AIVERSE_TEST_CHECK(!wp::planetGranularTops(ecs,surface,field,place,200,0,0,3,2,8));
    t.pos=surface.cellCenter(surface.gridCell(1,0,7,3));
    ecs.get<Transform>(field)=t;ecs.get<Transform>(trace)=t;
    AIVERSE_TEST_CHECK(!wp::planetGranularTops(ecs,surface,field,place,200,0,0,3,2,7));
    place.origin=t.pos;
    const auto moon=wp::planetGranularTops(ecs,surface,field,place,200,0,0,3,2,7);
    AIVERSE_TEST_CHECK(moon&&moon->size()==3&&moon->front().body==1);
    const auto lunarPhysical=wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"moon",scale,7,"moon");
    AIVERSE_TEST_CHECK(lunarPhysical&&lunarPhysical->publicationOffset==game::Surface::anchor(1));
    AIVERSE_TEST_CHECK(lunarPhysical->patchOrigin==physical->patchOrigin);
    AIVERSE_TEST_CHECK(!wp::resolvePlanetGranularPhysicalFrame(ecs,surface,field,"terra",scale,7,"terra"));
    AIVERSE_TEST_CHECK(wp::planetImprintQuad(ecs,surface,trace,7)->geometry.body==1);
    const auto live=aiverse::studio::samplePlanetImprint(ecs,surface,7);
    AIVERSE_TEST_CHECK(live["cells"].size()==1&&!live.getBool("truncated"));
    const auto& row=live["cells"][size_t(0)];
    AIVERSE_TEST_CHECK(row.getStr("body")=="moon"&&row.getInt("footprint")==50);
    const auto canonical=wp::planetImprintQuad(ecs,surface,trace,7);
    const auto expected=canonical->geometry.vertices[0]-game::Surface::anchor(1);
    AIVERSE_TEST_CHECK(std::abs(row["vertices"][size_t(0)][size_t(0)].toDouble()-expected.x.toDouble())<.001);
    AIVERSE_TEST_CHECK(aiverse::studio::samplePlanetImprint(ecs,surface,8)["cells"].size()==0);
    const auto capped=aiverse::studio::samplePlanetImprint(ecs,surface,7,0);
    AIVERSE_TEST_CHECK(capped.getBool("truncated")&&capped["cells"].size()==0);
    const auto grains=aiverse::studio::samplePlanetGranular(ecs,surface,7,1000);
    AIVERSE_TEST_CHECK(grains.getBool("available")&&grains["cells"].size()==3);
    AIVERSE_TEST_CHECK(grains["cells"][size_t(0)].getStr("body")=="moon");
    AIVERSE_TEST_CHECK(grains["cells"][size_t(0)].getInt("mass")==100);
    AIVERSE_TEST_CHECK(grains["cells"][size_t(0)].getInt("row")==1);
    AIVERSE_TEST_CHECK(aiverse::studio::samplePlanetGranular(ecs,surface,8,1000)["cells"].size()==0);
    AIVERSE_TEST_CHECK(!aiverse::studio::samplePlanetGranular(ecs,surface,7,0).getBool("available"));
    const auto grainCap=aiverse::studio::samplePlanetGranular(ecs,surface,7,1000,1);
    AIVERSE_TEST_CHECK(grainCap.getBool("truncated")&&grainCap["cells"].size()==1);
    const auto region=ecs.create();
    evolve::Region ecology;ecology.fertility=800000000;ecology.pollution=200000000;ecology.sampledTick=7;
    ecs.add<evolve::Region>(region,ecology);
    AIVERSE_TEST_CHECK(evolve::setSurfaceArea(ecs,surface,region,t.pos,{100,-200,300,400,true}));
    const auto frame=wp::surfaceFrame(&surface,t.pos);AIVERSE_TEST_CHECK(frame);
    const auto dir=evolve::surfaceRegionDirection(frame->up,frame->east,frame->south,body.radius,
        Fixed::fromInt(3),Fixed::fromInt(-2));
    AIVERSE_TEST_CHECK(dir);
    const auto target=wp::surfaceFrame(&surface,game::Surface::anchor(1)+*dir*body.radius);
    AIVERSE_TEST_CHECK(target);
    const auto offset=evolve::surfaceRegionOffset(surface,*frame,*target);
    AIVERSE_TEST_CHECK(offset&&fxAbs(offset->x-Fixed::fromInt(3))<Fixed::fromRatio(1,10));
    AIVERSE_TEST_CHECK(fxAbs(offset->y-Fixed::fromInt(-2))<Fixed::fromRatio(1,10));
    const auto regions=aiverse::studio::samplePlanetEvolve(ecs,surface,7);
    AIVERSE_TEST_CHECK(regions["regions"].size()==1&&!regions.getBool("truncated"));
    const auto& eco=regions["regions"][size_t(0)];
    AIVERSE_TEST_CHECK(eco.getStr("body")=="moon"&&eco.getInt("fertility")==800000000);
    AIVERSE_TEST_CHECK(!eco.getBool("chartClipped")&&eco["edges"].size()==4);
    AIVERSE_TEST_CHECK(eco["edges"][size_t(0)].size()==17);
    AIVERSE_TEST_CHECK(aiverse::studio::samplePlanetEvolve(ecs,surface,8)["regions"].size()==0);
    AIVERSE_TEST_CHECK(aiverse::studio::samplePlanetEvolve(ecs,surface,7,0).getBool("truncated"));
    auto& area=ecs.get<evolve::SurfaceArea>(region);
    area.local.centerXcm=100000;
    const auto clipped=aiverse::studio::samplePlanetEvolve(ecs,surface,7);
    AIVERSE_TEST_CHECK(clipped.getInt("clipped")==1&&clipped["regions"][size_t(0)].getBool("chartClipped"));
    area.local.enabled=false;
    AIVERSE_TEST_CHECK(aiverse::studio::samplePlanetEvolve(ecs,surface,7)["regions"].size()==0);
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
static void checkPlanetLiveEcho(){
    using aiverse::studio::samplePlanetEcho;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto a=ecs.create(),b=ecs.create(),moon=ecs.create(),portal=ecs.create(),cross=ecs.create();
    const auto emitter=ecs.create(),receiver=ecs.create(),reverse=ecs.create(),unplaced=ecs.create();
    const auto place=[&](Entity e,int bi,int x){
        Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(bi,0,x,3));ecs.add<Transform>(e,tr);
    };
    place(a,0,2);place(b,0,4);place(moon,1,2);place(emitter,0,2);place(receiver,0,4);place(reverse,0,2);
    for(auto e:{a,b,moon})ecs.add<echo::AcousticZone>(e,echo::AcousticZone{});
    ecs.add<echo::AcousticPortal>(portal,echo::AcousticPortal{a,b,25,false,true});
    ecs.add<echo::AcousticPortal>(cross,echo::AcousticPortal{b,moon,10,false,true});
    ecs.add<echo::AcousticEmitter>(emitter,echo::AcousticEmitter{a,100,true});
    ecs.add<echo::AcousticReceiver>(receiver,echo::AcousticReceiver{b,50,true});
    ecs.add<echo::AcousticReceiver>(reverse,echo::AcousticReceiver{a,101,true});
    ecs.add<echo::AcousticReceiver>(unplaced,echo::AcousticReceiver{moon,50,true});
    for(auto e:{receiver,reverse,unplaced})ecs.add<echo::AcousticHearing>(e,echo::AcousticHearing{});
    AIVERSE_TEST_CHECK(echo::propagate(ecs,7));
    const auto snapshot=samplePlanetEcho(ecs,surface,7);
    AIVERSE_TEST_CHECK(snapshot.getBool("available")&&snapshot["zones"].size()==3&&snapshot["portals"].size()==2);
    AIVERSE_TEST_CHECK(snapshot.getInt("crossBody")==1&&snapshot.getInt("unplaced")==1);
    AIVERSE_TEST_CHECK(snapshot.getInt("audible")==2&&snapshot.getInt("staleHearing")==0);
    bool found=false;
    for(const auto& row:snapshot["receivers"].items())if(row.getStr("id")==std::to_string(receiver.packed())){
        found=true;AIVERSE_TEST_CHECK(row.getBool("audible")&&row.getInt("intensity")==75);
        AIVERSE_TEST_CHECK(row.getStr("source")==std::to_string(emitter.packed())&&row["sourcePosition"].size()==3);
    }
    AIVERSE_TEST_CHECK(found);
    AIVERSE_TEST_CHECK(!snapshot["portals"][size_t(1)].getBool("placed"));
    const auto stale=samplePlanetEcho(ecs,surface,8);
    AIVERSE_TEST_CHECK(stale.getInt("staleHearing")==3&&stale.getInt("audible")==0);
    ecs.get<echo::AcousticPortal>(portal).enabled=false;
    AIVERSE_TEST_CHECK(echo::propagate(ecs,8));
    const auto disabled=samplePlanetEcho(ecs,surface,8);
    AIVERSE_TEST_CHECK(disabled.getInt("audible")==0&&!disabled["portals"][size_t(0)].getBool("active"));
    const auto cap=samplePlanetEcho(ecs,surface,8,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["zones"].size()==1);
    AIVERSE_TEST_CHECK(ecs.get<echo::AcousticHearing>(receiver).sampledTick==8);
}
static void checkPlanetLiveMorph(){
    using aiverse::studio::samplePlanetMorph;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto owner=ecs.create(),moonOwner=ecs.create();
    const auto a=ecs.create(),b=ecs.create(),moon=ecs.create(),unbound=ecs.create();
    const auto spring=ecs.create(),cross=ecs.create(),unknown=ecs.create(),force=ecs.create();
    Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(0,0,3,3));ecs.add<Transform>(owner,tr);
    tr.pos=surface.cellCenter(surface.gridCell(1,0,3,3));ecs.add<Transform>(moonOwner,tr);
    morph::Node n{};n.sampledTick=7;n.anchored=true;ecs.add<morph::Node>(a,n);
    n.anchored=false;n.x=150;n.restX=100;n.vx=10;ecs.add<morph::Node>(b,n);
    ecs.add<morph::Node>(moon,n);ecs.add<morph::Node>(unbound,n);
    AIVERSE_TEST_CHECK(morph::bindGravity(ecs,a,{owner,100,1000,false,true}));
    AIVERSE_TEST_CHECK(morph::bindGravity(ecs,b,{owner,100,1000,false,true}));
    AIVERSE_TEST_CHECK(morph::bindGravity(ecs,moon,{moonOwner,100,1000,false,true}));
    ecs.add<morph::Spring>(spring,morph::Spring{a,b,100,0,0,400,true});
    ecs.add<morph::Spring>(cross,morph::Spring{a,moon,100,0,0,400,true});
    ecs.add<morph::Spring>(unknown,morph::Spring{a,unbound,100,0,0,400,true});
    ecs.add<morph::Force>(force,morph::Force{b,5,0,-2,true});
    AIVERSE_TEST_CHECK(morph::enqueueImpact(ecs,b,8,10,20,30));
    physis::GravitySettings settings;settings.baseline={Fixed{},Fixed::fromInt(-10),Fixed{}};
    ecs.setResource<physis::GravitySettings>(settings);
    const auto snapshot=samplePlanetMorph(ecs,surface,7,true);
    AIVERSE_TEST_CHECK(snapshot.getBool("available")&&snapshot["nodes"].size()==4);
    AIVERSE_TEST_CHECK(snapshot.getInt("unprojected")==1&&snapshot.getInt("crossBody")==1&&snapshot.getInt("frameMismatch")==1);
    const auto frame=morph::gravity_detail::ownerFrame(ecs,&surface,owner);
    AIVERSE_TEST_CHECK(frame);
    const auto expected=frame->origin+frame->simulationVector({Fixed::fromRatio(3,2),{},{} })-game::Surface::anchor(0);
    bool found=false;
    for(const auto& row:snapshot["nodes"].items())if(row.getStr("id")==std::to_string(b.packed())){
        found=true;AIVERSE_TEST_CHECK(row.getBool("projected")&&row.getStr("body")=="terra");
        AIVERSE_TEST_CHECK(row["displacement"][size_t(0)].integer()==50);
        AIVERSE_TEST_CHECK(std::abs(row["position"][size_t(0)].toDouble()-expected.x.toDouble())<.001);
        AIVERSE_TEST_CHECK(row["configuredForce"][size_t(2)].integer()==-2);
        AIVERSE_TEST_CHECK(row["pendingImpact"].getInt("tick")==8);
        const auto load=morph::physisNodeLoad(ecs,&surface,b,ecs.get<morph::Node>(b));
        AIVERSE_TEST_CHECK(load&&row["currentGravityLoad"][size_t(1)].integer()==(*load)[1]);
    }
    AIVERSE_TEST_CHECK(found);
    const auto& edge=snapshot["springs"][size_t(0)];
    AIVERSE_TEST_CHECK(edge.getBool("projected")&&edge["residual"][size_t(0)].integer()==50);
    AIVERSE_TEST_CHECK(ecs.get<morph::Impact>(b).pending&&ecs.get<morph::Node>(b).x==150);
    AIVERSE_TEST_CHECK(samplePlanetMorph(ecs,surface,8,true)["nodes"].size()==0);
    const auto cap=samplePlanetMorph(ecs,surface,7,false,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["nodes"].size()==1);
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
static void checkPlanetLiveAether(){
    using aiverse::studio::AetherDiagnosticPolicy;
    using aiverse::studio::samplePlanetAether;
    game::Surface surface;
    game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto low=ecs.create(),high=ecs.create(),moon=ecs.create(),duct=ecs.create(),cross=ecs.create(),source=ecs.create();
    Transform tr{};tr.pos=game::Surface::anchor(0)+surface.facePoint(0,0,Fixed::fromInt(4),Fixed::fromInt(4));
    const auto frame=wp::surfaceFrame(&surface,tr.pos);AIVERSE_TEST_CHECK(frame);
    physis::GravitySettings gravity;gravity.baseline=frame->up*Fixed::fromInt(-10);
    ecs.setResource<physis::GravitySettings>(gravity);
    ecs.add<Transform>(low,tr);
    tr.pos=tr.pos+frame->up*Fixed::fromInt(2);ecs.add<Transform>(high,tr);
    tr.pos=game::Surface::anchor(1)+surface.facePoint(1,0,Fixed::fromInt(4),Fixed::fromInt(4));
    ecs.add<Transform>(moon,tr);
    for(auto e:{low,high,moon}){aether::GasCell c{1000,1000,1000};c.sampledTick=7;ecs.add<aether::GasCell>(e,c);}
    ecs.add<aether::GasDuct>(duct,aether::GasDuct{low,high,1000,false,true});
    ecs.add<aether::GasDuct>(cross,aether::GasDuct{low,moon,1000,false,true});
    ecs.add<aether::GasFlux>(source,aether::GasFlux{low,5,-2,0,true});
    const auto diffusion=samplePlanetAether(ecs,surface,7,AetherDiagnosticPolicy::Diffusion);
    AIVERSE_TEST_CHECK(diffusion.getBool("available")&&diffusion["cells"].size()==3);
    AIVERSE_TEST_CHECK(diffusion["ducts"].size()==1&&diffusion.getInt("crossBody")==1);
    AIVERSE_TEST_CHECK(diffusion["ducts"][size_t(0)]["potential"][size_t(0)].integer()==0);
    const auto strat=samplePlanetAether(ecs,surface,7,AetherDiagnosticPolicy::Stratified);
    const auto& d=strat["ducts"][size_t(0)];
    AIVERSE_TEST_CHECK(d.getBool("potentialAvailable"));
    AIVERSE_TEST_CHECK(d["potential"][size_t(0)].integer()>0&&d["potential"][size_t(1)].integer()<0);
    AIVERSE_TEST_CHECK(d["potential"][size_t(2)].integer()==0);
    for(const auto& c:strat["cells"].items())if(c.getStr("id")==std::to_string(low.packed())){
        AIVERSE_TEST_CHECK(c["sourceRate"][size_t(0)].integer()==5&&c["sourceRate"][size_t(1)].integer()==-2);
    }
    ecs.get<aether::GasDuct>(duct).oneWay=true;
    const auto valve=samplePlanetAether(ecs,surface,7,AetherDiagnosticPolicy::Stratified);
    AIVERSE_TEST_CHECK(valve["ducts"][size_t(0)]["potential"][size_t(1)].integer()==0);
    const auto unknown=samplePlanetAether(ecs,surface,7,AetherDiagnosticPolicy::Unknown);
    AIVERSE_TEST_CHECK(!unknown["ducts"][size_t(0)].getBool("potentialAvailable"));
    AIVERSE_TEST_CHECK(samplePlanetAether(ecs,surface,8,AetherDiagnosticPolicy::Diffusion)["cells"].size()==0);
    const auto cap=samplePlanetAether(ecs,surface,7,AetherDiagnosticPolicy::Diffusion,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["cells"].size()==1);
    AIVERSE_TEST_CHECK(ecs.get<aether::GasCell>(low).smoke==1000);
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
static void checkPlanetLiveSociety(){
    using aiverse::studio::samplePlanetSociety;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto a=ecs.create(),b=ecs.create(),c=ecs.create(),ab=ecs.create(),bc=ecs.create(),blocked=ecs.create();
    const auto message=ecs.create(),trigger=ecs.create();
    const auto place=[&](Entity e,int bi,int x){
        Transform t{};t.pos=surface.cellCenter(surface.gridCell(bi,0,x,3));ecs.add<Transform>(e,t);
    };
    place(a,0,2);place(b,0,4);place(c,1,2);place(message,0,2);place(trigger,1,2);
    for(auto e:{a,b,c})ecs.add<society::Person>(e,society::Person{});
    ecs.add<society::Contact>(ab,society::Contact{a,b,1000,true});
    ecs.add<society::Contact>(bc,society::Contact{b,c,1000,true});
    ecs.add<society::Contact>(blocked,society::Contact{a,c,100,true});
    constexpr society::Mask topic=(society::Mask(1)<<63)|society::Mask(1);
    ecs.add<society::Message>(message,society::Message{a,topic,true});
    society::NpcTrigger t{};t.person=c;t.topics=topic;t.actionId=42;
    ecs.add<society::NpcTrigger>(trigger,t);
    AIVERSE_TEST_CHECK(society::advance(ecs,7)&&society::updateNpcTriggers(ecs,7));
    const auto first=samplePlanetSociety(ecs,surface,7);
    AIVERSE_TEST_CHECK(first.getBool("available")&&first["people"].size()==3);
    AIVERSE_TEST_CHECK(first.getInt("eligibleContacts")==2&&first.getInt("crossBody")==2);
    AIVERSE_TEST_CHECK(first["people"][size_t(1)].getStr("knowledge")==std::to_string(topic));
    AIVERSE_TEST_CHECK(first["people"][size_t(1)].getInt("topicCount")==2);
    AIVERSE_TEST_CHECK(first["people"][size_t(2)].getStr("knowledge")=="0");
    AIVERSE_TEST_CHECK(!first["contacts"][size_t(2)].getBool("eligibleNow"));
    AIVERSE_TEST_CHECK(first["contacts"][size_t(1)].getStr("sourceOnlyTopics")==std::to_string(topic));
    AIVERSE_TEST_CHECK(!first["contacts"][size_t(1)].getBool("placed"));
    AIVERSE_TEST_CHECK(first["messages"][size_t(0)].getStr("topics")==std::to_string(topic));
    AIVERSE_TEST_CHECK(first.getInt("activeTriggers")==0);
    AIVERSE_TEST_CHECK(society::advance(ecs,8)&&society::updateNpcTriggers(ecs,8));
    const auto second=samplePlanetSociety(ecs,surface,8);
    AIVERSE_TEST_CHECK(second["people"][size_t(2)].getStr("knowledge")==std::to_string(topic));
    AIVERSE_TEST_CHECK(second.getInt("activeTriggers")==1&&second.getInt("firedTriggers")==1);
    AIVERSE_TEST_CHECK(second["triggers"][size_t(0)].getInt("transition")==1);
    AIVERSE_TEST_CHECK(second["triggers"][size_t(0)].getInt("changedTick")==8);
    const auto stale=samplePlanetSociety(ecs,surface,9);
    AIVERSE_TEST_CHECK(stale.getInt("staleOutputs")==4&&!stale["triggers"][size_t(0)].getBool("outputAvailable"));
    AIVERSE_TEST_CHECK(society::advance(ecs,9)&&society::updateNpcTriggers(ecs,9));
    const auto third=samplePlanetSociety(ecs,surface,9);
    AIVERSE_TEST_CHECK(third.getInt("activeTriggers")==0&&third.getInt("firedTriggers")==1);
    AIVERSE_TEST_CHECK(ecs.get<society::NpcTrigger>(trigger).transition==1);
    const auto cap=samplePlanetSociety(ecs,surface,9,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["people"].size()==1);
}
static void checkPlanetLiveCircuit(){
    using aiverse::studio::samplePlanetCircuit;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=Fixed::fromInt(64);body.faceSize=8;
    surface.bodies.push_back(body);body.id="moon";surface.bodies.push_back(body);surface.build();
    World ecs;
    const auto gen=ecs.create(),load=ecs.create(),remote=ecs.create(),wire=ecs.create(),cross=ecs.create();
    const auto sw=ecs.create(),device=ecs.create(),gate=ecs.create();
    const auto place=[&](Entity e,int bi,int x){
        Transform t{};t.pos=surface.cellCenter(surface.gridCell(bi,0,x,3));ecs.add<Transform>(e,t);
    };
    place(gen,0,2);place(load,0,4);place(remote,1,2);place(sw,0,2);place(device,0,4);place(gate,0,3);
    circuit::Node n{};n.supply=10;ecs.add<circuit::Node>(gen,n);
    n.supply=0;n.demand=3;ecs.add<circuit::Node>(load,n);ecs.add<circuit::Node>(remote,n);
    ecs.add<circuit::Wire>(wire,circuit::Wire{gen,load,false,true});
    ecs.add<circuit::Wire>(cross,circuit::Wire{gen,remote,false,true});
    ecs.add<circuit::Switch>(sw,circuit::Switch{gen,true,true});
    ecs.add<circuit::Gate>(gate,circuit::Gate{gen,kNullEntity,load,circuit::Op::Not,true});
    circuit::Device d{};d.node=load;d.kind=circuit::DeviceKind::Light;d.requireSignal=true;
    ecs.add<circuit::Device>(device,d);
    AIVERSE_TEST_CHECK(circuit::synchronizeSwitches(ecs));
    AIVERSE_TEST_CHECK(circuit::advance(ecs,7));
    AIVERSE_TEST_CHECK(circuit::updateDevices(ecs,7));
    const auto before=samplePlanetCircuit(ecs,surface,7);
    AIVERSE_TEST_CHECK(before.getBool("available")&&before["nodes"].size()==3);
    AIVERSE_TEST_CHECK(before.getInt("powered")==3&&before.getInt("high")==2);
    AIVERSE_TEST_CHECK(before["wires"].size()==2&&before.getInt("crossBody")==1);
    AIVERSE_TEST_CHECK(before["wires"][size_t(0)].getBool("bothPowered"));
    AIVERSE_TEST_CHECK(!before["wires"][size_t(1)].getBool("placed"));
    AIVERSE_TEST_CHECK(before["devices"][size_t(0)].getBool("active"));
    AIVERSE_TEST_CHECK(before["devices"][size_t(0)].getInt("transition")==1);
    AIVERSE_TEST_CHECK(before["gates"][size_t(0)].getBool("outputSignal"));
    const auto stale=samplePlanetCircuit(ecs,surface,8);
    AIVERSE_TEST_CHECK(stale.getInt("staleOutputs")==4&&!stale["devices"][size_t(0)].getBool("outputAvailable"));
    AIVERSE_TEST_CHECK(circuit::advance(ecs,8)&&circuit::updateDevices(ecs,8));
    const auto after=samplePlanetCircuit(ecs,surface,8);
    AIVERSE_TEST_CHECK(!after["devices"][size_t(0)].getBool("active"));
    AIVERSE_TEST_CHECK(after["devices"][size_t(0)].getInt("transition")==2);
    AIVERSE_TEST_CHECK(after["devices"][size_t(0)].getInt("changedTick")==8);
    AIVERSE_TEST_CHECK(!after["gates"][size_t(0)].getBool("outputSignal"));
    const auto cap=samplePlanetCircuit(ecs,surface,8,1);
    AIVERSE_TEST_CHECK(cap.getBool("truncated")&&cap["nodes"].size()==1);
    AIVERSE_TEST_CHECK(ecs.get<circuit::Device>(device).transition==2);
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
static void checkGameCorePlanetTerrainPersistence(){
    const std::string root=std::string(AIVERSE_SOURCE_DIR)+"/examples/native/world_processes/planet-demo";
    const Json map=Json::parseFile(root+"/maps/planet.json");
    Json rules=Json::parseFile(root+"/rules.json");
    rules.set("terrain",Json::parse(R"({"model":"heightfield"})"));
    const auto create=[&](){
        std::vector<g::NativeDefinition> defs{examples::planet_processes::definition()};
        auto s=std::make_unique<Simulation>(SimConfig{1,20,0,true},
            std::make_unique<g::GameCore>(std::move(defs)));
        s->setup(rules,map);return s;
    };
    auto sim=create(),peer=create();
    auto& core=static_cast<g::GameCore&>(sim->module());
    auto& w=sim->world();
    const auto* gs=w.resourceConst<g::GameState>();
    AIVERSE_TEST_CHECK(gs&&gs->grid.hf&&gs->surface&&gs->planetTerrainRevision==0);
    const auto base=gs->surface;
    const auto legacy=sim->serialize();
    AIVERSE_TEST_CHECK(!legacy.empty()&&uint8_t(legacy[4])==10);
    const auto initialHash=sim->checksum();
    SimSnapshot rollback;sim->saveState(rollback);
    granular::PlanetSoilTarget target;
    target.body=0;target.bodyId=base->bodies[0].id;
    const auto station=placed(*sim,0);
    const auto oldStation=w.get<Transform>(station).pos;
    target.surfaceCell=base->cellAt(oldStation);
    const auto fp=wp::planetTerrainFootprint(*base,0,target.surfaceCell);
    AIVERSE_TEST_CHECK(fp);target.footprint=*fp;target.volumeBudgetCm3=UINT64_MAX;
    const auto edit=granular::previewPlanetSoilGeometry(*base,target,1,-100,100);
    AIVERSE_TEST_CHECK(edit);
    AIVERSE_TEST_CHECK(core.stagePlanetTerrain(w,*base,0,*edit->surface));
    AIVERSE_TEST_CHECK(core.reconcilePlanetGroundUnits(w,*base));
    const auto newStation=w.get<Transform>(station).pos;
    AIVERSE_TEST_CHECK(newStation!=oldStation&&edit->surface->cellAt(newStation)==target.surfaceCell);
    gs=w.resourceConst<g::GameState>();
    AIVERSE_TEST_CHECK(gs->planetTerrainRevision==1&&gs->surface==gs->grid.surf&&gs->surface==gs->hash.surf);
    AIVERSE_TEST_CHECK(gs->surface->corners==edit->surface->corners&&sim->checksum()!=initialHash);
    AIVERSE_TEST_CHECK(!core.stagePlanetTerrain(w,*base,0,*edit->surface));
    const auto saved=sim->serialize();
    AIVERSE_TEST_CHECK(!saved.empty()&&uint8_t(saved[4])==11);
    std::string error;
    AIVERSE_TEST_CHECK(peer->deserialize(saved,&error)&&peer->checksum()==sim->checksum());
    const auto* loaded=peer->world().resourceConst<g::GameState>();
    AIVERSE_TEST_CHECK(loaded->planetTerrainRevision==1&&loaded->surface->corners==edit->surface->corners&&loaded->surface==loaded->grid.surf&&loaded->surface==loaded->hash.surf);
    AIVERSE_TEST_CHECK(peer->world().get<Transform>(station).pos==newStation);
    AIVERSE_TEST_CHECK(peer->deserialize(legacy,&error)&&peer->checksum()==initialHash);
    sim->loadState(rollback);
    AIVERSE_TEST_CHECK(sim->checksum()==initialHash&&w.resourceConst<g::GameState>()->surface==base);
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
    const std::array<evolve::PlanetEcologyRequest,2> sites{{
        {"terra",w.get<Transform>(eco).pos,{},{}},
        {"moon",w.get<Transform>(moon).pos,{},{}}
    }};
    const auto climate=evolve::samplePlanetEcology(w,*surface,sites);
    AIVERSE_TEST_CHECK(climate&&(*climate)[0].contributingRegions==1);
    AIVERSE_TEST_CHECK((*climate)[0].climate.nutrientsPermille==
        evolve::permille(w.get<evolve::Region>(eco).fertility));
    AIVERSE_TEST_CHECK((*climate)[1].contributingRegions==0&&
        (*climate)[1].climate.nutrientsPermille==1000);
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
    // Native opt-in CROWD must release an unsupported allocation on the
    // next reservation tick; movement authority remains with GameCore.
    std::vector<Entity> fields;
    w.eachExistingConst<physis::GravityField>([&](Entity e,const physis::GravityField&){
        fields.push_back(e);
    });
    for(Entity e:fields)w.get<physis::GravityField>(e).enabled=false;
    sim->step();
    AIVERSE_TEST_CHECK(w.get<crowd::Agent>(walker).allocated==kNullEntity);
    for(Entity e:fields)w.get<physis::GravityField>(e).enabled=true;
    sim->step();
    AIVERSE_TEST_CHECK(w.get<crowd::Agent>(walker).allocated==slot);
}
static void checkWorldProcessLiveOptions(){
    const studio::StudioWorldProcessLiveOptions defaults;
    AIVERSE_TEST_CHECK(defaults.layer=="combined"&&!defaults.live&&defaults.anyPlanetLayer());
    const auto next=defaults.patched(Json::parse(R"({"layer":"off","live":true,"liveSociety":true,"granularCellCm":100,"aetherLayer":"oxygen"})"));
    AIVERSE_TEST_CHECK(next.live&&next.society&&next.granularCellCm==100&&next.aetherLayer=="oxygen"&&next.anyPlanetLayer());
    AIVERSE_TEST_CHECK(next.patched(Json::object())==next);
    AIVERSE_TEST_CHECK(defaults.patched(defaults.json())==defaults&&defaults.patched(next.json())==next);
    const auto disabled=next.patched(Json::parse(R"({"liveSociety":false,"aetherLayer":"off"})"));
    AIVERSE_TEST_CHECK(!disabled.anyPlanetLayer()&&disabled.live&&disabled.granularCellCm==100);
    for(const char* bad:{R"({"live":false,"liveEcho":1})",R"({"layer":3})",R"({"layer":""})",R"({"granularCellCm":10.5})",R"({"granularCellCm":9})",R"({"aetherLayer":"water"})"}){
        bool rejected=false;
        try{(void)next.patched(Json::parse(bad));}catch(const std::runtime_error&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected&&next.live&&next.society);
    }
}
static void checkPlanetLiveBudget(){
    AIVERSE_TEST_CHECK(studio::planetLiveShare(4096,11)*11<=4096);
    AIVERSE_TEST_CHECK(studio::planetLiveOwnerLimit(256,1)==256&&studio::planetLiveOwnerLimit(256,11)==23);
    studio::PlanetLiveLineBudget lines(5);
    lines.beginLayer(2);AIVERSE_TEST_CHECK(lines.take()&&lines.take()&&!lines.take());
    lines.beginLayer(4);AIVERSE_TEST_CHECK(lines.take()&&lines.take()&&lines.take()&&!lines.take()&&lines.dropped==2);
    Json layer=Json::object(),rows=Json::array();
    for(int i=0;i<12;++i){Json row=Json::object();row.set("id",Json::number(int64_t(i)));row.set("text",Json::string(std::string(80,'x')));rows.push(std::move(row));}
    layer.set("first",rows);layer.set("second",rows);layer.set("count",Json::number(int64_t(24)));
    const auto original=layer;
    AIVERSE_TEST_CHECK(studio::fitPlanetLiveLayer(layer,480));
    AIVERSE_TEST_CHECK(layer.dump(-1).size()<=480&&layer.getBool("payloadTruncated")&&layer.getInt("count")==24);
    AIVERSE_TEST_CHECK(layer["first"].size()>0&&layer["second"].size()>0);
    AIVERSE_TEST_CHECK(layer["first"][size_t(0)].getInt("id")==0&&layer["second"][size_t(0)].getInt("id")==0);
    auto repeat=original;AIVERSE_TEST_CHECK(studio::fitPlanetLiveLayer(repeat,480)&&repeat==layer);
    auto tiny=original;AIVERSE_TEST_CHECK(!studio::fitPlanetLiveLayer(tiny,1));
}
int main(){
    checkPlanetLiveBudget();
    checkWorldProcessLiveOptions();
    checkAuthoredPlanetDemo();
    checkGameCorePlanetTerrainPersistence();
    checkPlanetStudioDiagnostics();
    checkCircuitSurfaceWiring();
    checkPlanetLiveCircuit();
    checkPlanetLiveSociety();
    checkAetherStratification();
    checkPlanetLiveAether();
    checkMorphGravity();
    checkPlanetLiveMorph();
    checkPlanetLiveEcho();
    checkPlanetTerrainFootprint();
    checkPlanetSoilTransfer();
    checkPlanetSoilGeometry();
    checkPlanetCollapseDelivery();
    checkPlanetFoundationLoss();
    checkPlanetRootSupport();
    checkPlanetTerrainWater();
    checkPlanetBankErosion();
    checkPlanetSedimentTransport();
    checkPlanetLiveFracture();
    checkPlanetMorphModelFrame();
    checkPlanetMorphContactFrame();
    checkPlanetProcessGeometry();
    checkPlanetGranular();
    checkPlanetEvolveHydration();
    checkPlanetCharacterGravityFrame();
    checkPlanetGravitySupport();
    checkPlanetGravity();
    checkPlanetLiveGravity();
    checkPlanetImprint();
    checkPlanetCrowdSlots();
    checkPlanetLiveCrowd();
    checkPlanetEvolveRegions();
    checkPlanetRootClimate();
    checkPlanetEcologySampling();
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
