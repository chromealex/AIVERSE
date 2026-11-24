// Opt-in bounded native chamber demo. No legacy game module or second solver.
#include "examples/world/terraweave_native/PhysicsIntegration.h"
#include "engine/physis/PhysisIntegration.h"
#include "engine/platform/Platform.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>

using namespace aiverse;
using namespace aiverse::terraweave::caverns;
namespace {
constexpr uint64_t regionId=1;
constexpr int tickRate=60;
const CavePlanetAnchor anchor{"planet",2,120_fx,120_fx,1200};
const Vec3 localStart{-1_fx,-4_fx,0_fx},localGoal{1_fx,-4_fx,0_fx};
const render::float3 soil{.32f,.26f,.20f};
CavePlanetDemandConfig demand(){
    CavePlanetDemandConfig out;out.resolution=16;out.spacingCm=50;out.maxRegions=1;out.maxVolumeSlots=8;return out;
}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::shared_ptr<const game::Surface> makeSurface(){
    auto s=std::make_shared<game::Surface>();game::BodyDef body;
    body.id=anchor.bodyId;body.radius=100_fx;body.faceSize=256;
    s->bodies.push_back(body);s->build();return s;
}
Json authoredMap(){
    CaveVolumeSnapshot volume;
    volume.settings.tunnelChance=0;volume.settings.chamberChance=0;
    volume.settings.minYcm=-790;volume.settings.maxYcm=-100;
    volume.stamps.push_back({1,{0,-400,0},300,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(volume);
    Json doc=Json::object(),map=Json::object();
    doc.set("version",Json::number(int64_t(1)));
    doc.set("anchor",cavePlanetAnchorToJson(anchor));doc.set("volume",caveSnapshotToJson(volume));
    map.set(kPlanetCaveMapKey,std::move(doc));return map;
}
struct ActorPosition {Vec3 value{};};
void io(Archive& archive,ActorPosition& p){archive(p.value);}
std::optional<Vec3> position(const World& world,Entity e){
    const auto* p=world.tryGetConst<ActorPosition>(e);
    return p?std::optional<Vec3>{p->value}:std::nullopt;
}
struct DemoModule final:GameModule {
    std::shared_ptr<const game::Surface> surface;
    Entity actor{};
    std::function<void(Simulation&)> motor;
    explicit DemoModule(std::shared_ptr<const game::Surface> s):surface(std::move(s)){}
    std::string name()const override{return "native-caverns-chamber";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        physis::InstallOptions physisOptions;
        physisOptions.climate.width=1;physisOptions.climate.height=1;
        physisOptions.enableGravityFields=true;physis::install(sim,physisOptions);
        lib::nereid::registerNereidSphereWaterPersistence(sim);
        sim.registerPersistentComponent<ActorPosition>("demo.caverns.actor.position",1);
        registerCavePlanetTravel(sim);
        physis::GravitySettings zero;zero.baseline={};sim.world().setResource<physis::GravitySettings>(zero);
        const auto frame=CavePlanetFrame::make(surface,anchor);
        actor=sim.world().create();
        sim.world().add<ActorPosition>(actor,ActorPosition{frame.toSimulation(localStart)});
        sim.world().add<CavePlanetTravel>(actor,CavePlanetTravel{regionId,0,0,0});
        sim.addSystem("demo.motor",[this](Simulation& s){if(motor)motor(s);});
        auto router=std::make_shared<CavePathRouter>();
        CaveGraphSolverSettings solver;solver.cellCm=25;solver.regionMarginCells=2;
        registerCaveSharedGraphSolvers(*router,solver);
        require(bool(installPlanetCavesInNativeSimulation(sim,map[kPlanetCaveMapKey],surface,router,
            position,"demo.motor",1,demand())),"install cave authority");
        CaveAgentProfile agent;agent.traversal=CaveTraversal::Flying;agent.radiusCm=20;
        agent.heightCm=60;agent.sampleCm=25;
        require(assignCavePlanetMotorGoal(sim.world(),actor,surface,regionId,frame.toSimulation(localGoal),
            agent,"astar",3),"assign chamber route");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& archive)override{return archive.ok();}
};
struct Demo {
    gfx::Device* device=nullptr;
    std::shared_ptr<const game::Surface> surface;
    std::optional<CavePlanetFrame> frame;
    std::unique_ptr<Simulation> sim;
    DemoModule* module=nullptr;
    game::GameView view;
    render::Renderer3D renderer;
    render::GiGeometryScene giScene;
    render::GiMainCpuBridge gi;
    render::SphereViewFrame viewFrame;
    CavePlanetGiProject project;
    std::unique_ptr<physics::Scene> box;
    world::SpatialWorld spatial;
    CaveColliderLeasePool pool{1,1000000};
    std::unique_ptr<CavePlanetExteriorCollision> exterior;
    std::unique_ptr<CavePlanetCpuNativeHost> host;
    std::map<CavePlanetSurfaceSlotKey,uint64_t> surfaceIds;
    std::map<ChunkKey3,uint64_t,KeyLess> volumeIds;
    std::vector<CavePlanetSurfaceSlotKey> keys;
    physics::CharacterParams params;
    physics::CharacterState actorState;
    physics::BodyHandle proxy=physics::kNoBody;
    render::Mesh marker;
    bool exclusive=false,stopping=false,failed=false,initialized=false,arrived=false;
    unsigned frames=0,maxFrames=0;
    double accumulator=0;
    uint64_t lastProgress=0;
    void fail(const char* reason){
        if(!failed)std::fprintf(stderr,"[caverns-demo] FAIL %s\n",reason);
        failed=true;stopping=true;platform::requestQuit();
    }
    render::float3 renderPosition(Vec3 p)const{return project(anchor.bodyId,p-game::Surface::anchor(0));}
    void init(gfx::Device& dev){
        device=&dev;exclusive=true;
        surface=makeSurface();frame=CavePlanetFrame::make(surface,anchor);
        auto m=std::make_unique<DemoModule>(surface);module=m.get();
        SimConfig cfg;cfg.tickRate=tickRate;sim=std::make_unique<Simulation>(cfg,std::move(m));
        sim->setup(Json::object(),authoredMap());
        physics::SceneConfig physical;physical.tickRate=tickRate;physical.gravity={0,0,0};
        box=std::make_unique<physics::Scene>(physical);
        renderer.init(dev);
        game::TerrainLook look;look.smooth=false;
        for(size_t i=0;i<8;++i){look.hasColor[i]=true;look.color[i]=soil;}
        view.planets().init(dev,look,[](const std::string& name,std::string& out){return platform::readContentFile(name,out);},"engine/assets");
        game::PlanetWorld planet;planet.sim=surface;
        game::PlanetBody body;body.id=anchor.bodyId;body.radius=100;body.faceSize=256;body.style=0;
        body.corners=surface->corners;body.flags.resize(size_t(6)*256*256,game::kCellGround);
        planet.bodies.push_back(std::move(body));view.planets().setWorld(planet);
        const auto floatVec=[](Vec3 p){return render::float3{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};};
        viewFrame.origin=floatVec(frame->originBody());viewFrame.right=floatVec(frame->east());
        viewFrame.up=floatVec(frame->up());viewFrame.back=floatVec(frame->south());
        project=CavePlanetGiViewFrame::capture(planet,viewFrame,0).projector();
        // The demo intentionally owns just the bounded cave-area surface
        // patch. No other static terrain producer exists in this Scene.
        keys=view.planets().selectCaveSurfaceSlots(*frame,{-1,-1,1,1});
        uint64_t id=1;for(const auto& key:keys)surfaceIds.emplace(key,id++);
        for(const auto& key:planetInteriorKeys(anchor,demand()))volumeIds.emplace(key,id++);
        const auto cells=[this](const std::string& b,const CavePlanetSurfaceSlotKey& key){
            if(b!=anchor.bodyId)throw std::invalid_argument("foreign body");
            return world::Cell{int32_t(surfaceIds.at(key)),0,0};
        };
        const auto unowned=[this](const std::string& b,const CavePlanetSurfaceSlotKey& key){
            return exclusive&&b==anchor.bodyId&&surfaceIds.contains(key);
        };
        exterior=std::make_unique<CavePlanetExteriorCollision>(view.planets(),*box,spatial,pool,
            [this]{return exclusive;},cells,unowned,keys.size());
        require(exterior->ensure(anchor.bodyId,keys),"real exterior baseline");
        render::GiMainCpuConfig giConfig;giConfig.origin={-3,-7,-3};giConfig.spacing=1;
        giConfig.nx=7;giConfig.ny=7;giConfig.nz=7;giConfig.samples=16;
        require(gi.initialize(dev,giConfig),"initialize CPU GI");
        const auto originals=view.planets().captureCaveSurfaceSlots(0,keys);
        for(const auto& leaf:originals){
            std::vector<render::StaticShadowGeometry::Triangle> triangles;
            for(const auto& t:leaf.bodyTriangles)triangles.push_back({project(anchor.bodyId,t.a),
                project(anchor.bodyId,t.b),project(anchor.bodyId,t.c),0});
            giScene.replaceChunk(surfaceIds.at(leaf.key),std::move(triangles),{soil});
        }
        require(giScene.publish(gi).published,"publish actual baseline GI");
        render::GiMovingLight lamp;lamp.key=1;lamp.position={0,-3,0};lamp.radius=8;lamp.intensity=4;
        lamp.color={1,.8f,.55f};
        require(gi.publishLights({lamp})&&gi.publishEnvironment({}),"GI lights");
        CavePlanetGiBindings bindings;
        bindings.surfaceId=[this](const std::string& b,const auto& key)->std::optional<uint64_t>{
            if(b!=anchor.bodyId||!surfaceIds.contains(key))return {};return surfaceIds.at(key);
        };
        bindings.volumeId=[this](const std::string& b,uint64_t r,ChunkKey3 key)->std::optional<uint64_t>{
            if(b!=anchor.bodyId||r!=regionId||!volumeIds.contains(key))return {};return volumeIds.at(key);
        };
        bindings.project=project;bindings.albedo=[](const auto&,const auto&,const auto&){return soil;};
        bindings.ready=[this]{return exclusive&&device->waitForResourceIdle();};
        CavePlanetRuntimeHooks hooks;hooks.exclusive=[this]{return exclusive;};
        hooks.unownedLeaf=unowned;
        hooks.volumeCell=[this](uint64_t r,ChunkKey3 key){
            if(r!=regionId)throw std::invalid_argument("foreign region");
            return world::Cell{int32_t(volumeIds.at(key)),1,0};
        };
        hooks.mayReleaseCoverage=[this](const auto&){return exclusive&&stopping&&proxy==physics::kNoBody;};
        require(exterior->bind(hooks),"bind exterior owner");
        host=std::make_unique<CavePlanetCpuNativeHost>(*sim,dev,view,*box,spatial,pool,surface,
            giScene,gi,std::move(bindings),std::move(hooks),1,demand());
        params.radius=.18f;params.height=.55f;params.maxSpeed=2;
        actorState.position=physics::physisB3(*position(sim->world(),module->actor));
        proxy=box->createCharacter(params,actorState.position);require(proxy!=physics::kNoBody,"create real proxy");
        CavePlanetMotorIntent flightIntent;flightIntent.traversal=CaveTraversal::Flying;
        const auto orientation=cavePlanetVolumeOrientation(*frame,flightIntent);
        require(orientation&&box->moveCharacterOriented(proxy,actorState,*orientation,true),"initial proxy orientation");
        module->motor=[this](Simulation& s){
            CavePlanetMotorDispatchInput input;input.capabilities.ground=false;input.capabilities.flight=true;
            // The demo's existing flight controller requests velocity; the
            // engine owns collision, route cap and authoritative dispatch.
            input.flight=[](const CavePlanetMotorIntent& intent,Vec3,Fixed){return std::optional<Vec3>{intent.simulationDirection*1_fx};};
            input.position=position;
            input.publishPosition=[](World& w,Entity actor,const physics::CharacterState& state){
                Vec3 p;if(!physics::physisVec3(state.position,p))return false;
                w.get<ActorPosition>(actor).value=p;return true;
            };
            const auto result=host->dispatchMotor(s,module->actor,params,actorState,proxy,input);
            if(result==CavePlanetDispatchResult::Recovery||result==CavePlanetDispatchResult::Refused)
                throw std::runtime_error("native motor refused");
        };
        render::MeshBuilder mesh;mesh.color(render::rgba(255,180,65)).sphere({},.18f);
        marker=mesh.upload(dev);
        initialized=true;exclusive=false;
    }
    // All mutation outside render passes; platform afterFrame is the actual
    // completed submission boundary. No simulation update from draw.
    void afterFrame(double dt){
        if(!initialized||stopping)return;
        exclusive=true;
        try{
            const auto p=position(sim->world(),module->actor);
            require(bool(p),"missing actor position");
            const std::array<CavePlanetObserver,1> observers{{{0,*p}}};
            const auto progress=host->beforeFrame(sim->world(),renderer,observers,{regionId},4);
            if(progress.phase==CavePlanetRuntimePhase::Recovery||progress.phase==CavePlanetRuntimePhase::Refused)
                throw std::runtime_error(progress.diagnostic);
            if(progress.pinsReady&&exterior->covers(host->runtime(),sim->world(),anchor.bodyId,keys)){
                accumulator+=std::min(dt,.1);
                while(accumulator>=1./tickRate&&!arrived){
                    const auto tick=host->stepNativeTick({regionId});
                    if(tick==CavePlanetCpuNativeHost::TickResult::Recovery)throw std::runtime_error("native tick recovery");
                    if(tick!=CavePlanetCpuNativeHost::TickResult::Advanced)break;
                    accumulator-=1./tickRate;
                    const auto actual=position(sim->world(),module->actor);
                    const auto* route=sim->world().tryGetConst<nav::CaveRouteFollower>(module->actor);
                    if(actual&&route&&route->arrived&&(*actual-frame->toSimulation(localGoal)).length()<Fixed::fromRatio(35,100)){
                        arrived=true;std::fprintf(stdout,"[caverns-demo] ARRIVED tick=%u bodies=%zu\n",sim->tick(),box->bodyCount());
                    }
                }
            }
            (void)gi.updateFrame(renderPosition(*position(sim->world(),module->actor)));
            if(frames-lastProgress>=120){lastProgress=frames;
                std::fprintf(stdout,"[caverns-demo] frame=%u tick=%u regions=%zu phase=%d\n",
                    frames,sim->tick(),progress.residentRegions,int(progress.phase));}
            if(maxFrames&&frames>=maxFrames){
                if(!arrived)fail("route did not arrive within frame budget");
                else{stopping=true;platform::requestQuit();}
            }
        }catch(const std::exception& ex){fail(ex.what());}
        exclusive=false;
    }
    void draw(gfx::Device& dev,double){
        if(!initialized)return;
        ++frames;
        render::Camera cam;cam.target=renderPosition(*position(sim->world(),module->actor));
        cam.distance=1.4f;cam.pitch=.25f;cam.yaw=.8f;
        renderer.beginScene(cam,dev.framebufferWidth(),dev.framebufferHeight());
        view.planets().draw(renderer,viewFrame,0,0,0);
        view.drawPlanetCaves(renderer,viewFrame,0);
        render::PointLight lamp;lamp.pos={0,-3,0};lamp.radius=8;lamp.intensity=4;
        lamp.color={1,.8f,.55f};renderer.lights.add(lamp);
        (void)host->bindIndirect(renderer);
        renderer.drawMesh(marker,render::mat4::translate(cam.target),{1,1,1,1});
        gfx::PassDesc pass;pass.clearColor[0]=.04f;pass.clearColor[1]=.05f;pass.clearColor[2]=.08f;
        renderer.render(pass);dev.endPass();
    }
    void shutdown(){
        stopping=true;exclusive=true;
        if(!initialized)return;
        module->motor={};
        if(proxy!=physics::kNoBody){
            if(!box->destroy(proxy)){failed=true;return;}proxy=physics::kNoBody;
        }
        if(!device->waitForResourceIdle()||!host->shutdown(renderer)){failed=true;return;}
        host.reset();
        for(const auto& key:keys)if(!exterior->unload(anchor.bodyId,key,[this](const auto&,const auto&){return stopping&&proxy==physics::kNoBody;})){
            failed=true;return;
        }
        exterior.reset();
        if(!device->waitForResourceIdle()||!gi.shutdownAfterGpuIdle()){failed=true;return;}
        device->destroyBuffer(marker.vb);device->destroyBuffer(marker.ib);
        view.planets().shutdown(*device);renderer.shutdown();initialized=false;
    }
};
// Retain a failed/quarantined owner until process exit, not unsafe teardown.
std::unique_ptr<Demo> demo;
}
int aiverseMain(int argc,char** argv){
    demo=std::make_unique<Demo>();
    if(argc==3&&std::string(argv[1])=="--frames")demo->maxFrames=unsigned(std::strtoul(argv[2],nullptr,10));
    platform::AppConfig cfg;cfg.title="AIVERSE Caverns — native chamber";cfg.width=1280;cfg.height=800;
    double delta=0;platform::AppCallbacks cb;
    cb.init=[](gfx::Device& d){try{demo->init(d);}catch(const std::exception& e){demo->fail(e.what());}};
    cb.frame=[&](gfx::Device& d,double dt){delta=dt;demo->draw(d,dt);};
    cb.afterFrame=[&](gfx::Device&){demo->afterFrame(delta);};
    cb.shutdown=[](){demo->shutdown();};
    const int result=platform::run(cfg,cb);
    const bool failed=demo->failed;
    if(failed)(void)demo.release();else demo.reset();
    return failed?1:result;
}
