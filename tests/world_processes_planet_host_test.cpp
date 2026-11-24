// Real CPU/Box3D composition fixture. No GPU or automatic GameCore wiring.
#include "examples/world/world_processes_native/PlanetProcessHost.h"
#include <stdexcept>
#include <cstdio>

using namespace aiverse;
using namespace aiverse::examples::world_processes_native;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){
    try{
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
        check(host.prepare(scene,world,surface,stamp,tiles,placements),"prepare");
        check(!host.stepPhysics({}),"physics requires publication");
        check(host.publishPhysics()&&host.physicsTiles()==1,"real granular collider");
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
        check(!host.recordContacts([](const auto&,auto){return false;}),"record retry");
        check(host.recordContacts([](const auto& s,auto contacts){return s.completedTick==7&&contacts.empty();}),"record");
        check(host.skipRender()&&host.phase()==PlanetProcessHost::Phase::Complete,"headless finish");
        check(!host.prepare(scene,world,surface,stamp,tiles,placements),"duplicate completed tick");
        world.get<granular::Field>(field).sampledTick=8;stamp.completedTick=8;
        check(host.prepare(scene,world,surface,stamp,tiles,placements),"next frame");
        check(host.publishPhysics(),"next collision revision");
        check(host.resetAfterRollback()&&scene.bodyCount()==0,"retire projected physics");
        stamp.epoch=2;stamp.completedTick=7;world.get<granular::Field>(field).sampledTick=7;
        check(host.prepare(scene,world,surface,stamp,tiles,placements),"restored epoch");
        check(host.shutdown()&&host.phase()==PlanetProcessHost::Phase::Idle,"shutdown");
        std::puts("planet process host fixture PASS");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
