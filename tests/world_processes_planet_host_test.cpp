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
static void checkStreamedTerrain(){
    auto surface=std::make_shared<game::Surface>();
    game::BodyDef def;def.id="terra";def.radius=Fixed::fromInt(120);def.faceSize=112;
    surface->bodies.push_back(def);surface->build();
    const auto at=[&](int face,int x,int y){return surface->cellCenter(surface->gridCell(0,face,x,y));};
    PlanetTerrainCollisionConfig cfg;cfg.enabled=true;cfg.streamed=true;cfg.maxResidentTiles=16;
    cfg.regions={{at(0,111,111),2}};
    const auto pins=planPlanetTerrainTiles(*surface,0,cfg);
    check(pins&&pins->size()>=3&&pins->size()<=16,"three-face streamed seam coverage");
    physics::Scene scene;PlanetTerrainCollision terrain;
    check(terrain.publish(scene,surface,0,0,cfg)&&terrain.covers(cfg.regions[0].position),"stream beyond legacy face limit");
    const auto old=terrain.handle(0,96,96);
    check(scene.valid(old)&&!terrain.covers(at(1,40,40)),"unresident ground rejected");
    cfg.regions.push_back({at(1,40,40),2});
    check(terrain.publish(scene,surface,0,0,cfg)&&terrain.handle(0,96,96)==old,"same revision residency expands");
    const auto retained=terrain.handle(1,32,32);
    cfg.regions.erase(cfg.regions.begin());
    check(terrain.publish(scene,surface,0,0,cfg)&&terrain.handle(1,32,32)==retained&&!scene.valid(old),"retire distant chunks");
    const auto count=scene.bodyCount();
    cfg.maxResidentTiles=1;cfg.regions.push_back({at(0,111,111),2});
    check(!terrain.publish(scene,surface,0,0,cfg)&&scene.bodyCount()==count&&scene.valid(retained),"budget rejection is atomic");
    cfg.maxResidentTiles=16;cfg.regions={{game::Surface::anchor(1)+Vec3{Fixed::fromInt(120),{},{}},2}};
    check(!terrain.publish(scene,surface,0,0,cfg),"foreign body rejected");
    check(terrain.reset(scene)&&scene.bodyCount()==0,"stream shutdown");
}
static void checkMorphCollisionPublication(){
    // Actual static mesh owner: changed geometry replaces the handle,
    // identical geometry retains it, failed capacity preserves old mesh.
    physics::SceneConfig cfg;cfg.maxBodies=2;
    physics::Scene scene(cfg);PlanetMorphCollisionPublisher publisher;
    PlanetMorphPreparedCollider mesh;mesh.id=1;
    mesh.desc.motion=physics::Motion::Static;mesh.desc.shape=physics::ShapeKind::Mesh;
    mesh.desc.vertices={{-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1}};
    mesh.desc.indices={0,1,2,0,2,3};
    check(publisher.publish(scene,{mesh}),"MORPH initial mesh");
    const auto old=publisher.handle(1);
    check(publisher.publish(scene,{mesh})&&publisher.handle(1)==old,"MORPH unchanged handle");
    const auto before=scene.raycast({0,3,0},{0,-5,0});
    check(before.body==old,"MORPH old collider hit");
    for(auto& p:mesh.desc.vertices)p.y=1;
    check(publisher.publish(scene,{mesh}),"MORPH replace mesh");
    const auto next=publisher.handle(1);
    check(next!=old&&!scene.valid(old)&&scene.raycast({0,3,0},{0,-5,0}).fraction<before.fraction,"MORPH deformed collider");
    auto other=mesh;other.id=2;
    check(publisher.publish(scene,{mesh,other}),"MORPH two colliders");
    mesh.desc.vertices[0].y=2;
    check(!publisher.publish(scene,{mesh,other})&&scene.valid(next)&&scene.bodyCount()==2,"MORPH atomic capacity reject");
    check(publisher.reset(scene)&&scene.bodyCount()==0,"MORPH rollback retire");
}
static void checkMorphNodeProxies(){
    physics::Scene scene;PlanetMorphNodePublisher nodes;
    World world;const auto owner=world.create(),node=world.create(),other=world.create();
    PlanetMorphNodeProxy proxy;proxy.request.instance=1;proxy.request.owner=owner;
    proxy.request.node=node;proxy.request.radius=.25f;proxy.target={0,2,0};
    check(nodes.publish(scene,{proxy}),"MORPH node proxy");
    const auto handle=nodes.handle(1,node);
    proxy.target={1,2,0};
    check(nodes.publish(scene,{proxy})&&nodes.handle(1,node)==handle,"stable moving proxy");
    scene.step();
    check(std::abs(scene.position(handle).x-1)<.05f,"proxy follows target");
    std::map<physics::BodyHandle,Entity> mapping;std::map<uint64_t,Entity> owners;
    check(nodes.appendContactBindings(mapping,owners)&&mapping.at(handle)==node&&owners.at(node.packed())==owner,"automatic contact binding");
    MorphContactDebounce debounce;
    std::vector<MorphContactInput> inputs{{node,handle,77,8,2,0,0},{node,handle,77,8,3,0,0}};
    check(debounce.filter(inputs,mapping,8)&&inputs.size()==1&&inputs[0].dx==3,"duplicate contact coalesced");
    inputs={{node,999,77,9,4,0,0}};
    check(debounce.filter(inputs,mapping,9)&&inputs.empty(),"replacement handle debounced");
    inputs={{node,999,77,11,4,0,0}};
    check(debounce.filter(inputs,mapping,11)&&inputs.size()==1,"later impact accepted");
    proxy.request.radius=.5f;
    check(!nodes.publish(scene,{proxy})&&scene.valid(handle),"shape change requires retirement");
    check(nodes.reset(scene)&&scene.bodyCount()==0,"proxy rollback retirement");
}
static void checkConsequenceOwner(){
    namespace wp=lib::worldprocess;
    game::Surface surface;game::BodyDef body;body.id="terra";body.radius=64_fx;body.faceSize=8;
    surface.bodies.push_back(body);surface.build();
    World world;world.setResource<wp::PlanetConsequenceResource>();
    const auto member=world.create();Transform tr{};tr.pos=surface.cellCenter(surface.gridCell(0,0,3,3));
    world.add<Transform>(member,tr);world.add<fracture::Member>(member,fracture::Member{false,true,true});
    fracture::Event event{member,fracture::collapseEventKey(member,7),7};
    world.add<fracture::Collapse>(member,fracture::Collapse{event.key,7,fracture::Delivery::Pending});
    physics::Scene scene;PlanetSourcePublisher sources;
    PlanetSourceCollider source;source.key={wp::ConsequenceKind::Structure,member.packed(),0};
    source.desc.motion=physics::Motion::Static;
    source.desc.position=physics::physisB3(tr.pos-game::Surface::anchor(0));
    const World original=world;
    check(sources.publish(scene,world,surface,"terra",std::span(&source,1))&&sources.count()==1,"source standing collider");
    const auto standing=sources.handle(source.key);
    const auto owner=[&](World&,const auto& delivery)->std::optional<wp::ConsequenceBody>{
        wp::ConsequenceBody record;record.motion.position=delivery.frame.origin+surface.up(tr.pos)*2_fx;
        return record; // fixture has no source collider
    };
    check(wp::deliverPlanetConsequences(world,surface,"terra",7,owner,
        [](World&,const auto&)->std::optional<wp::ConsequenceBody>{return {};}),"durable consequence");
    check(fracture::planetCollapseReceipt(world,event),"ack after record");
    const auto key=wp::consequenceKey(event);
    PlanetConsequencePublisher pub;
    check(pub.publish(scene,world,surface,"terra")&&pub.count()==1,"spawn durable body");
    const auto handle=pub.handle(key);
    check(sources.publish(scene,world,surface,"terra",std::span(&source,1))&&sources.count()==0&&!scene.valid(standing),"retire source on durable consequence");
    check(wp::planetSourceRetired(world,source.key,"terra"),"source visual suppressed");
    check(pub.publish(scene,world,surface,"terra")&&pub.handle(key)==handle,"idempotent body");
    scene.step();
    std::vector<wp::ConsequenceCheckpoint> state;
    check(pub.capture(scene,surface,"terra",state)&&state.size()==1,"capture motion");
    check(wp::commitConsequenceMotion(world,surface,"terra",7,state),"persist native checkpoint");
    check(!wp::commitConsequenceMotion(world,surface,"terra",7,state),"reject repeated checkpoint");
    check(pub.reset(scene)&&scene.bodyCount()==0,"reset body");
    check(pub.publish(scene,world,surface,"terra")&&pub.count()==1,"restore checkpoint");
    check(pub.reset(scene),"shutdown owner");
    check(sources.publish(scene,original,surface,"terra",std::span(&source,1))&&sources.count()==1,"rollback restores standing source");
    check(!wp::planetSourceRetired(original,source.key,"terra"),"rollback restores visibility");
    check(sources.reset(scene)&&scene.bodyCount()==0,"source shutdown");
}
int main(){
    try{
        checkTerrainCollision();
        checkStreamedTerrain();
        checkMorphCollisionPublication();
        checkMorphNodeProxies();
        checkConsequenceOwner();
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
