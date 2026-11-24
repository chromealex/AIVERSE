// Real CPU/Box3D composition fixture. No GPU or automatic GameCore wiring.
#include "examples/world/world_processes_native/PlanetProcessHost.h"
#include <stdexcept>
#include <cstdio>

using namespace aiverse;
using namespace aiverse::examples::world_processes_native;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void checkTerrainCollision(){
    auto base=std::make_shared<game::Surface>();
    game::BodyDef def;def.id="terra";def.radius=Fixed::fromInt(64);def.faceSize=8;
    base->bodies.push_back(def);def.id="moon";base->bodies.push_back(def);base->build();
    auto edited=std::make_shared<game::Surface>(*base);
    // Interior quad on +X; other faces and moon stay unchanged.
    const auto& cube=base->cubeOf(0);
    for(int y=3;y<=4;++y)for(int x=3;x<=4;++x)
        edited->corners[size_t(cube.cornerId(0,x,y))]=16;
    physics::Scene scene;PlanetTerrainCollision terrain;
    check(terrain.publish(scene,base,0,0)&&terrain.count()==6,"initial spherical collision coverage");
    const auto old=terrain.handle(0,0,0),other=terrain.handle(1,0,0);
    const auto dir=cube.cellCenterDir(cube.cellId(0,3,3));
    const auto start=physics::physisB3(dir*Fixed::fromInt(80));
    const auto ray=physics::physisB3(dir*Fixed::fromInt(-30));
    const auto before=scene.raycast(start,ray);
    check(before.body==old,"ray hits outward terrain");
    check(!terrain.publish(scene,edited,0,0),"reject changed geometry with same revision");
    check(terrain.publish(scene,edited,0,1),"replace edited face");
    const auto after=scene.raycast(start,ray);
    check(after.body==terrain.handle(0,0,0)&&after.fraction<before.fraction,"raised physical terrain");
    check(!scene.valid(old)&&terrain.handle(1,0,0)==other&&scene.valid(other),"unchanged tile retained");
    check(terrain.reset(scene)&&scene.bodyCount()==0,"rollback retirement");
    check(terrain.publish(scene,base,0,0),"rollback republish");
    const auto restored=scene.raycast(start,ray);
    check(restored.body!=physics::kNoBody&&std::abs(restored.fraction-before.fraction)<.0001f,"restored physical ground");
    check(terrain.reset(scene),"shutdown terrain");
    physics::SceneConfig limits;limits.maxBodies=6;
    physics::Scene small(limits);PlanetTerrainCollision bounded;
    check(bounded.publish(small,base,0,0),"bounded initial coverage");
    const auto retained=bounded.handle(0,0,0);
    check(!bounded.publish(small,edited,0,1)&&small.valid(retained)&&small.bodyCount()==6,"no replacement capacity preserves old geometry");
    check(bounded.reset(small),"bounded shutdown");
}
int main(){
    try{
        checkTerrainCollision();
        auto surface=std::make_shared<game::Surface>();
        game::BodyDef def;def.id="terra";def.radius=Fixed::fromInt(64);def.faceSize=8;
        surface->bodies.push_back(def);def.id="moon";surface->bodies.push_back(def);surface->build();
        World world;world.enableCowSnapshots();
        physis::GravitySettings gravity;gravity.baseline={};
        world.setResource<physis::GravitySettings>(gravity);
        physis::Settings weather;weather.width=1;weather.height=1;physis::initialize(world,weather);
        const auto ground=surface->cellCenter(surface->gridCell(0,0,3,3));
        const auto member=world.create();
        Transform tr{};tr.pos=ground;world.add<Transform>(member,tr);
        world.add<fracture::Member>(member,fracture::Member{false,true,true});
        const auto key=fracture::collapseEventKey(member,7);
        world.add<fracture::Collapse>(member,fracture::Collapse{key,7,fracture::Delivery::Acknowledged});
        const auto field=world.create();world.add<Transform>(field,tr);
        granular::Field grains;grains.width=1;grains.height=1;grains.capacity=100;
        grains.mass={100};grains.solid={0};grains.sampledTick=7;world.add<granular::Field>(field,grains);
        std::vector<GranularResidentTile> tiles{{field,0,0}};
        std::map<uint64_t,PlanetGranularPlacement> placements{{field.packed(),{"terra",200,200,0}}};
        physics::Scene scene;
        PlanetProcessHost host;
        PlanetHostStamp stamp{1,0,7,"terra"};
        check(host.prepare(scene,world,surface,stamp,tiles,placements,{},PlanetTerrainCollisionConfig{true}),"prepare");
        check(!host.stepPhysics({}),"physics requires publication");
        check(host.publishPhysics()&&host.physicsTiles()==1&&host.terrainPhysicsTiles()==6,"real granular collider");
        check(!host.publishPhysics(),"duplicate publication");
        PlanetDebrisProjection debris;debris.event={member,key,7};debris.bodyId="terra";
        debris.simulationPosition=ground+surface->up(ground)*Fixed::fromInt(5);
        debris.description.size={.5f,.5f,.5f};
        check(host.publishDebris(debris),"real debris body");
        const auto handle=host.debrisHandle(member);
        check(scene.valid(handle)&&host.publishDebris(debris)&&host.debrisHandle(member)==handle,"idempotent projection");
        const auto source=physis::cellAt(world,0,0);
        std::vector<evolve::PlanetWeatherMapping> weatherMap{{source,"terra",ground,Fixed::fromInt(10)}};
        std::vector<evolve::PlanetRootClimateSite> sites{{"terra",ground,{},true,false}};
        check(bool(host.rootClimate(weatherMap,sites,0)),"frozen climate");
        const auto tick=scene.tick();
        check(host.stepPhysics({})&&scene.tick()==tick+1,"single physical tick");
        check(!host.stepPhysics({}),"duplicate step rejected");
        check(!host.recordContacts([](const auto&,auto){return true;}),"cannot discard debris motion");
        PlanetDebrisState saved;
        check(!host.recordPhysicsOutputs([&](const auto& s,auto contacts,auto states){
            check(s.completedTick==7&&contacts.empty()&&states.size()==1,"output batch");
            saved=states[0];return false;
        }),"owner transaction retry");
        check(host.recordPhysicsOutputs([&](const auto& s,auto contacts,auto states){
            return s.completedTick==7&&contacts.empty()&&states.size()==1&&
                states[0].event.key==key&&states[0].simulationPosition==saved.simulationPosition&&
                states[0].linearVelocity.x==saved.linearVelocity.x;
        }),"record frozen motion");
        check(!host.recordPhysicsOutputs([](const auto&,auto,auto){return true;}),"duplicate record");
        check(saved.bodyId=="terra"&&validPlanetDebrisMotion(saved),"valid owner checkpoint");
        auto restored=debris;
        auto invalid=saved;invalid.bodyId="moon";
        check(!restorePlanetDebrisMotion(restored,invalid),"reject cross-body restore");
        check(restorePlanetDebrisMotion(restored,saved),"merge checkpoint into owner shape");
        check(host.skipRender()&&host.phase()==PlanetProcessHost::Phase::Complete,"headless finish");
        check(!host.prepare(scene,world,surface,stamp,tiles,placements,{},PlanetTerrainCollisionConfig{true}),"duplicate completed tick");
        world.get<granular::Field>(field).sampledTick=8;stamp.completedTick=8;
        check(host.prepare(scene,world,surface,stamp,tiles,placements,{},PlanetTerrainCollisionConfig{true}),"next frame");
        check(host.publishPhysics(),"next collision revision");
        check(host.resetAfterRollback()&&scene.bodyCount()==0,"retire projected physics");
        stamp.epoch=2;stamp.completedTick=7;world.get<granular::Field>(field).sampledTick=7;
        check(host.prepare(scene,world,surface,stamp,tiles,placements,{},PlanetTerrainCollisionConfig{true}),"restored epoch");
        check(host.publishPhysics()&&host.publishDebris(restored),"recreate from owner checkpoint");
        const auto restoredHandle=host.debrisHandle(member);
        Vec3 restoredLocal{};
        check(physics::physisVec3(scene.position(restoredHandle),restoredLocal)&&
            restoredLocal+game::Surface::anchor(0)==saved.simulationPosition,"restored native position");
        check(host.shutdown()&&host.phase()==PlanetProcessHost::Phase::Idle,"shutdown");
        std::puts("planet process host fixture PASS");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
