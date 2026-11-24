// Opt-in bounded native chamber demo. No legacy game module or second solver.
#include "examples/world/terraweave_native/PhysicsIntegration.h"
#include "engine/physis/PhysisIntegration.h"
#include "engine/platform/Platform.h"
#include "engine/world/TerraWeaveCavernsGraphSolvers.h"
#include "engine/physics/TerraWeaveCavernsNativeCheckpoint.h"
#include <algorithm>
#include <array>
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
constexpr int planetBody=1;
constexpr int starBody=0;
constexpr uint64_t mouthId=1;
enum class Scenario:uint8_t { Chamber,Portal,Swim };
enum class Journey:uint8_t { Enter,Explore,Return,Exit,Done };
const CavePlanetAnchor anchor{"planet",2,120_fx,120_fx,1200};
const Vec3 localStart{-1_fx,-4_fx,0_fx},localGoal{1_fx,-4_fx,0_fx};
const Vec3 portalStart{0_fx,2_fx,0_fx},portalGoal{1_fx,-5_fx,0_fx},portalReturn{0_fx,-4_fx,0_fx};
const render::float3 soil{.32f,.26f,.20f};
CavePlanetDemandConfig demand(){
    CavePlanetDemandConfig out;out.resolution=16;out.spacingCm=50;out.maxRegions=1;out.maxVolumeSlots=8;return out;
}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::shared_ptr<const game::Surface> makeSurface(){
    auto s=std::make_shared<game::Surface>();game::BodyDef body;
    game::BodyDef star;star.id="star";star.kind=game::BodyKind::Star;star.radius=8_fx;
    body.id=anchor.bodyId;body.radius=100_fx;body.faceSize=256;
    body.parent=starBody;body.orbitRadius=1000_fx;body.orbitPeriod=60_fx;
    s->bodies.push_back(star);s->bodies.push_back(body);s->startBody=planetBody;
    s->build();return s;
}
Json authoredMap(Scenario scenario){
    CaveVolumeSnapshot volume;
    volume.settings.tunnelChance=0;volume.settings.chamberChance=0;
    volume.settings.minYcm=-790;volume.settings.maxYcm=-100;
    if(scenario==Scenario::Portal){
        volume.stamps.push_back({1,{0,-500,0},220,CaveStampMode::Carve});
        volume.entrances.push_back({mouthId,0,0,0,-400,125});
    }else volume.stamps.push_back({1,{0,-400,0},300,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(volume);
    Json doc=Json::object(),map=Json::object();
    doc.set("version",Json::number(int64_t(1)));
    doc.set("anchor",cavePlanetAnchorToJson(anchor));doc.set("volume",caveSnapshotToJson(volume));
    map.set(kPlanetCaveMapKey,std::move(doc));return map;
}
struct ActorPosition {Vec3 value{};Journey journey=Journey::Explore;};
void io(Archive& archive,ActorPosition& p){archive(p.value,p.journey);if(!archive.writing()&&uint8_t(p.journey)>uint8_t(Journey::Done))archive.fail();}
struct DemoSwimState {lib::mechanics::LocomotionState value;};
void io(Archive& a,DemoSwimState& s){a(s.value);if(!a.writing()&&!lib::mechanics::valid(s.value))a.fail();}
CaveAgentProfile agentFor(Scenario scenario){
    CaveAgentProfile a;a.traversal=scenario==Scenario::Swim?CaveTraversal::Swimming:CaveTraversal::Flying;
    a.radiusCm=20;a.heightCm=60;a.sampleCm=25;return a;
}
std::optional<Vec3> position(const World& world,Entity e){
    const auto* p=world.tryGetConst<ActorPosition>(e);
    return p?std::optional<Vec3>{p->value}:std::nullopt;
}
struct DemoModule final:GameModule {
    std::shared_ptr<const game::Surface> surface;
    Entity actor{};
    std::function<void(Simulation&)> motor;
    Scenario scenario;
    DemoModule(std::shared_ptr<const game::Surface> s,Scenario mode):surface(std::move(s)),scenario(mode){}
    std::string name()const override{return "native-caverns-chamber";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        physis::InstallOptions physisOptions;
        physisOptions.climate.width=1;physisOptions.climate.height=1;
        physisOptions.enableGravityFields=true;physis::install(sim,physisOptions);
        lib::nereid::registerNereidSphereWaterPersistence(sim);
        sim.registerPersistentComponent<ActorPosition>("demo.caverns.actor.position",2);
        sim.registerPersistentComponent<DemoSwimState>("demo.caverns.actor.swim",1);
        registerCavePlanetTravel(sim);
        const auto frame=CavePlanetFrame::make(surface,anchor);
        physis::GravitySettings gravity;gravity.baseline=scenario==Scenario::Swim?frame.up()*(-10_fx):Vec3{};
        sim.world().setResource<physis::GravitySettings>(gravity);
        if(scenario==Scenario::Swim){
            lib::nereid::WaterSphereComponent sea;sea.center=game::Surface::anchor(planetBody);
            sea.surfaceRadius=100_fx;sea.floorRadius=90_fx;
            sim.world().add<lib::nereid::WaterSphereComponent>(sim.world().create(),sea);
        }
        actor=sim.world().create();
        const bool portal=scenario==Scenario::Portal;
        sim.world().add<ActorPosition>(actor,ActorPosition{frame.toSimulation(portal?portalStart:localStart),portal?Journey::Enter:Journey::Explore});
        sim.world().add<DemoSwimState>(actor,DemoSwimState{});
        sim.world().add<CavePlanetTravel>(actor,CavePlanetTravel{portal?0:regionId,0,0,0});
        sim.addSystem("demo.motor",[this](Simulation& s){if(motor)motor(s);});
        auto router=std::make_shared<CavePathRouter>();
        CaveGraphSolverSettings solver;solver.cellCm=25;solver.regionMarginCells=2;
        registerCaveSharedGraphSolvers(*router,solver);
        require(bool(installPlanetCavesInNativeSimulation(sim,map[kPlanetCaveMapKey],surface,router,
            position,"demo.motor",1,demand())),"install cave authority");
        if(portal)require(requestCavePlanetCrossing(sim.world(),actor,regionId,mouthId,true,sim.tick()),"request portal entry");
        else require(assignCavePlanetMotorGoal(sim.world(),actor,surface,regionId,frame.toSimulation(localGoal),
            agentFor(scenario),"astar",3),"assign chamber route");
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
    std::map<uint64_t,CavePlanetGiCanonicalChunk> baselineSources;
    double renderSeconds=0;
    uint32_t lastRebaseTick=0,rebaseCount=0;
    bool movingView=false,forceRebase=false;
    bool restoreScenario=false,restoreCompleted=false;
    std::string checkpointBytes;
    uint32_t checkpointTick=0;
    uint64_t checkpointChecksum=0;
    bool restoredProxyReady=false;
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
    Scenario scenario=Scenario::Chamber;
    lib::mechanics::LocomotionModeConfig swimConfig;
    unsigned frames=0,maxFrames=0;
    double accumulator=0;
    uint64_t lastProgress=0;
    void fail(const char* reason){
        if(!failed)std::fprintf(stderr,"[caverns-demo] FAIL %s\n",reason);
        failed=true;stopping=true;platform::requestQuit();
    }
    render::float3 renderPosition(Vec3 p)const{return project(anchor.bodyId,p-game::Surface::anchor(planetBody));}
    static render::float3 floatVec(Vec3 p){return {float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};}
    render::SphereViewFrame makeViewFrame(double seconds,uint32_t serial)const{
        const auto& planet=view.planets().world();
        render::SphereViewFrame result;
        const auto right=floatVec(frame->east()),up=floatVec(frame->up()),back=floatVec(frame->south());
        const float angle=(serial%2)?0.15f:0.f;
        const auto cosine=std::cos(angle),sine=std::sin(angle);
        result.right=right*cosine+back*sine;result.up=up;result.back=back*cosine-right*sine;
        // Follow the actual orbit center and periodically rebase the local
        // frame origin within the CPU probe lattice. Simulation never moves.
        result.origin=planet.center(planetBody,seconds)+floatVec(frame->originBody())+
            right*((serial%2)?0.5f:0.f);
        return result;
    }
    render::float3 lampPosition(const CavePlanetGiProject& p)const{
        return p(anchor.bodyId,frame->toBody({0_fx,-3_fx,0_fx}));
    }
    bool publishFrameLighting(const CavePlanetGiProject& p,const CavePlanetGiViewFrame& captured){
        render::GiMovingLight lamp;lamp.key=1;lamp.position=lampPosition(p);lamp.radius=8;lamp.intensity=4;
        lamp.color={1,.8f,.55f};
        render::GiEnvironmentLight environment;
        environment.sunDirection=render::normalize(captured.centers.at("star")-captured.centers.at(anchor.bodyId));
        environment.sunIntensity=.7f;
        return gi.publishLights({lamp})&&gi.publishEnvironment(environment);
    }
    void updateMovingFrame(){
        if(!movingView||!host->runtime().regionReady(sim->world(),regionId)||
           (!forceRebase&&sim->tick()>=lastRebaseTick&&sim->tick()-lastRebaseTick<30))return;
        const auto hash=sim->checksum(),physicsTick=box->tick(),spatialRevision=spatial.mutationVersion();
        const auto nextSerial=rebaseCount+1;
        const double nextTime=double(sim->tick())/tickRate;
        const auto nextView=makeViewFrame(nextTime,nextSerial);
        const auto captured=CavePlanetGiViewFrame::capture(view.planets().world(),nextView,nextTime);
        auto nextProject=captured.projector();
        auto remap=exactCavePlanetGiBaselineRemap(project,nextProject,baselineSources);
        require(host->reprojectGi(renderer,nextProject,remap),"reproject shared GI frame");
        // Geometry is already published: a later lighting failure must stop
        // the world, not present a mixed frame or retry against stale inputs.
        try{
            require(publishFrameLighting(nextProject,captured),"publish rebased lights");
            require(sim->checksum()==hash&&box->tick()==physicsTick&&
                spatial.mutationVersion()==spatialRevision,"render rebase mutated gameplay");
        }catch(...){host->runtime().quarantineNativeMotion();throw;}
        viewFrame=nextView;project.swap(nextProject);renderSeconds=nextTime;
        lastRebaseTick=sim->tick();rebaseCount=nextSerial;forceRebase=false;
        std::fprintf(stdout,"[caverns-demo] REBASE serial=%u seconds=%.3f tick=%u\n",
            rebaseCount,renderSeconds,sim->tick());
    }
    void init(gfx::Device& dev){
        device=&dev;exclusive=true;
        surface=makeSurface();frame=CavePlanetFrame::make(surface,anchor);
        auto m=std::make_unique<DemoModule>(surface,scenario);module=m.get();
        SimConfig cfg;cfg.tickRate=tickRate;sim=std::make_unique<Simulation>(cfg,std::move(m));
        sim->setup(Json::object(),authoredMap(scenario));
        physics::SceneConfig physical;physical.tickRate=tickRate;physical.gravity={0,0,0};
        box=std::make_unique<physics::Scene>(physical);
        renderer.init(dev);
        game::TerrainLook look;look.smooth=false;
        for(size_t i=0;i<8;++i){look.hasColor[i]=true;look.color[i]=soil;}
        view.planets().init(dev,look,[](const std::string& name,std::string& out){return platform::readContentFile(name,out);},"engine/assets");
        game::PlanetWorld planet;planet.sim=surface;planet.start=planetBody;
        game::PlanetBody star;star.id="star";star.kind=game::PlanetBody::Kind::Star;star.radius=8;
        planet.bodies.push_back(star);
        game::PlanetBody body;body.id=anchor.bodyId;body.radius=100;body.faceSize=256;body.style=0;
        body.parent=starBody;body.orbitRadius=1000;body.period=60;
        body.corners=surface->corners;body.flags.resize(size_t(6)*256*256,game::kCellGround);
        planet.bodies.push_back(std::move(body));view.planets().setWorld(planet);
        viewFrame=makeViewFrame(0,0);
        const auto initialFrame=CavePlanetGiViewFrame::capture(view.planets().world(),viewFrame,0);
        project=initialFrame.projector();
        // The demo intentionally owns just the bounded cave-area surface
        // patch. No other static terrain producer exists in this Scene.
        keys=view.planets().selectCaveSurfaceSlots(*frame,scenario==Scenario::Portal?SurfaceRectCm{-250,-250,250,250}:SurfaceRectCm{-1,-1,1,1});
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
        const auto originals=view.planets().captureCaveSurfaceSlots(planetBody,keys);
        for(const auto& leaf:originals){
            std::vector<render::StaticShadowGeometry::Triangle> triangles;
            for(const auto& t:leaf.bodyTriangles)triangles.push_back({project(anchor.bodyId,t.a),
                project(anchor.bodyId,t.b),project(anchor.bodyId,t.c),0});
            const auto id=surfaceIds.at(leaf.key);
            baselineSources.emplace(id,CavePlanetGiCanonicalChunk{anchor.bodyId,leaf.bodyTriangles});
            giScene.replaceChunk(id,std::move(triangles),{soil});
        }
        require(giScene.publish(gi).published,"publish actual baseline GI");
        require(publishFrameLighting(project,initialFrame),"GI lights");
        CavePlanetGiBindings bindings;
        bindings.surfaceId=[this](const std::string& b,const auto& key)->std::optional<uint64_t>{
            if(b!=anchor.bodyId||!surfaceIds.contains(key))return {};
            return surfaceIds.at(key);
        };
        bindings.volumeId=[this](const std::string& b,uint64_t r,ChunkKey3 key)->std::optional<uint64_t>{
            if(b!=anchor.bodyId||r!=regionId||!volumeIds.contains(key))return {};
            return volumeIds.at(key);
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
        CavePlanetMotorIntent flightIntent;flightIntent.traversal=agentFor(scenario).traversal;
        flightIntent.basis=cavePlanetMotionBasis(*frame,sim->world(),*position(sim->world(),module->actor));
        const auto orientation=cavePlanetVolumeOrientation(*frame,flightIntent);
        require(orientation&&box->moveCharacterOriented(proxy,actorState,*orientation,true),"initial proxy orientation");
        require(host->setNativeRestoreProof([this](const Simulation& state){
            return restoredProxyReady&&&state==sim.get()&&state.world().alive(module->actor)&&
                caveNativeProxyPositionMatches(*box,proxy,actorState)&&
                cavePlanetDispatchPositionMatches(position,state.world(),module->actor,actorState);
        }),"bind native restore proof");
        // A deeply immersed swimmer must deliberately steer vertically;
        // neutral buoyancy would otherwise rise toward the water surface.
        swimConfig.buoyancy=Fixed{};
        module->motor=[this](Simulation& s){
            CavePlanetMotorDispatchInput input;input.capabilities.ground=false;
            input.capabilities.flight=scenario!=Scenario::Swim;input.capabilities.swim=scenario==Scenario::Swim;
            auto stagedSwim=s.world().get<DemoSwimState>(module->actor).value;
            input.swimConfig=&swimConfig;input.swimState=&stagedSwim;
            const auto journey=s.world().get<ActorPosition>(module->actor).journey;
            if(scenario==Scenario::Portal&&(journey==Journey::Enter||journey==Journey::Exit)){
                const auto current=position(s.world(),module->actor);
                require(bool(current),"portal position missing");
                // Existing flight controller flies along the certified shaft
                // axis. Native crossing adapter performs the real sweep.
                const auto target=frame->toSimulation(journey==Journey::Enter?portalReturn:portalStart);
                const auto delta=target-*current;
                input.crossing.velocity=fxNormalize(delta)*fxMin(1_fx,delta.length()/s.dt());
                if(journey==Journey::Enter)input.crossingGoal=CavePlanetCrossingGoal{
                    frame->toSimulation(portalGoal),agentFor(scenario),"astar",3,8192};
                input.crossingCoverage=[this](const World& w,Entity actor,uint64_t region,Vec3 a,Vec3 b,Fixed extent){
                    if(actor!=module->actor||region!=regionId||!exclusive||
                       !host->runtime().regionReady(w,region)||extent<=Fixed{})return false;
                    try{
                        const auto from=frame->fromSimulation(a),to=frame->fromSimulation(b);
                        const auto cap=Fixed::fromRatio(8,1);
                        for(const auto p:{from,to})if(fxAbs(p.x)+extent>=cap||
                            fxAbs(p.y)+extent>=cap||fxAbs(p.z)+extent>=cap)return false;
                        // Complete swept envelope against finest terrain
                        // leaves, not a camera-dependent residency flag.
                        const auto lo=[](Fixed x){return floorDiv(x.raw*100,Fixed::kOne);};
                        const auto hi=[&](Fixed x){return -lo(-x);};
                        const SurfaceRectCm rect{lo(fxMin(from.x,to.x)-extent),lo(fxMin(from.z,to.z)-extent),
                            hi(fxMax(from.x,to.x)+extent)+1,hi(fxMax(from.z,to.z)+extent)+1};
                        const auto needed=view.planets().selectCaveSurfaceSlots(*frame,rect);
                        return exterior->covers(host->runtime(),w,anchor.bodyId,needed);
                    }catch(const std::exception&){return false;}
                };
            }
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
            if(result==CavePlanetDispatchResult::Interior&&scenario==Scenario::Swim)
                s.world().get<DemoSwimState>(module->actor).value=stagedSwim;
            if(result==CavePlanetDispatchResult::Held||result==CavePlanetDispatchResult::Repath){
                require(box->setLinearVelocity(proxy,{0,0,0})&&box->setAngularVelocity(proxy,{0,0,0}),
                    "hold actual native proxy");
            }
        };
        render::MeshBuilder mesh;mesh.color(render::rgba(255,180,65)).sphere({},.18f);
        marker=mesh.upload(dev);
        initialized=true;exclusive=false;
    }
    // Save at a completed simulation/physics boundary, then deliberately
    // advance before loading. Portal checkpoint is taken while straddling
    // a committed rim, not before the travel request starts.
    bool exerciseRestore(){
        if(!restoreScenario||restoreCompleted||arrived)return false;
        const auto* travel=sim->world().tryGetConst<CavePlanetTravel>(module->actor);
        require(bool(travel),"checkpoint travel missing");
        if(checkpointBytes.empty()){
            const bool eligible=scenario==Scenario::Portal?
                (travel->pendingRegion&&travel->phase==CavePlanetCrossingPhase::Clearance):
                sim->tick()>=24;
            if(!eligible)return false;
            const auto saved=captureCaveNativeCheckpoint(*sim,*box,module->actor,proxy,actorState,position);
            require(bool(saved),"capture actual native checkpoint");
            checkpointBytes=encodeCaveNativeCheckpoint(*saved);
            require(!checkpointBytes.empty(),"encode native checkpoint");
            checkpointTick=saved->simulationTick;checkpointChecksum=saved->checksum;
            std::fprintf(stdout,"[caverns-demo] SAVED tick=%u bytes=%zu\n",checkpointTick,checkpointBytes.size());
            return false;
        }
        if(sim->tick()-checkpointTick<12)return false;
        const auto saved=decodeCaveNativeCheckpoint(checkpointBytes);
        require(saved&&saved->actor==module->actor&&saved->simulationTick==checkpointTick&&
            saved->checksum==checkpointChecksum,"decode checkpoint identity");
        restoredProxyReady=false;
        renderer.clearDynamicGiOverlay();
        try{
            std::string error;
            require(sim->deserialize(saved->simulation,&error),"restore simulation checkpoint");
            require(sim->tick()==checkpointTick&&sim->checksum()==checkpointChecksum&&
                sim->world().alive(saved->actor),"restore ECS identity/checksum");
            require(cavePlanetDispatchPositionMatches(position,sim->world(),saved->actor,saved->controller),
                "saved proxy does not match restored ECS position");
            require(restoreCaveNativeAirborneProxy(*box,proxy,*saved,actorState),"restore actual native proxy");
            require(!host->admitNativeTick(sim->world(),{regionId}),"restore must close native admission");
            restoredProxyReady=true;
        }catch(...){host->runtime().quarantineNativeMotion();throw;}
        restoreCompleted=true;forceRebase=movingView;accumulator=0;
        std::fprintf(stdout,"[caverns-demo] RESTORED tick=%u checksum=%llu\n",sim->tick(),
            static_cast<unsigned long long>(checkpointChecksum));
        return true;
    }
    // All mutation outside render passes; platform afterFrame is the actual
    // completed submission boundary. No simulation update from draw.
    void afterFrame(double dt){
        if(!initialized||stopping)return;
        exclusive=true;
        try{
            const auto p=position(sim->world(),module->actor);
            require(bool(p),"missing actor position");
            const std::array<CavePlanetObserver,1> observers{{{planetBody,*p}}};
            const auto progress=host->beforeFrame(sim->world(),renderer,observers,{regionId},4);
            if(progress.phase==CavePlanetRuntimePhase::Recovery||progress.phase==CavePlanetRuntimePhase::Refused)
                throw std::runtime_error(progress.diagnostic);
            if(progress.pinsReady&&exterior->covers(host->runtime(),sim->world(),anchor.bodyId,keys)){
                updateMovingFrame();
                accumulator+=std::min(dt,.1);
                while(accumulator>=1./tickRate&&!arrived){
                    const auto tick=host->stepNativeTick({regionId});
                    if(tick==CavePlanetCpuNativeHost::TickResult::Recovery)throw std::runtime_error("native tick recovery");
                    if(tick!=CavePlanetCpuNativeHost::TickResult::Advanced)break;
                    accumulator-=1./tickRate;
                    const auto actual=position(sim->world(),module->actor);
                    const auto* route=sim->world().tryGetConst<nav::CaveRouteFollower>(module->actor);
                    auto& actor=sim->world().get<ActorPosition>(module->actor);
                    const auto* travel=sim->world().tryGetConst<CavePlanetTravel>(module->actor);
                    require(actual&&travel,"missing journey state");
                    const auto at=[&](Vec3 goal){return (*actual-frame->toSimulation(goal)).length()<Fixed::fromRatio(35,100);};
                    if(scenario==Scenario::Portal){
                        if(actor.journey==Journey::Enter&&travel->occupiedRegion==regionId&&!travel->pendingRegion){
                            actor.journey=Journey::Explore;
                            std::fprintf(stdout,"[caverns-demo] ENTERED tick=%u\n",sim->tick());
                        }else if(actor.journey==Journey::Explore&&route&&route->arrived&&at(portalGoal)){
                            require(assignCavePlanetMotorGoal(sim->world(),module->actor,surface,regionId,
                                frame->toSimulation(portalReturn),agentFor(scenario),"astar",3),"assign return route");
                            actor.journey=Journey::Return;
                        }else if(actor.journey==Journey::Return&&route&&route->arrived&&at(portalReturn)){
                            require(requestCavePlanetCrossing(sim->world(),module->actor,regionId,mouthId,false,sim->tick()),"request exit");
                            actor.journey=Journey::Exit;
                        }else if(actor.journey==Journey::Exit&&!travel->occupiedRegion&&!travel->pendingRegion){
                            actor.journey=Journey::Done;arrived=true;
                            std::fprintf(stdout,"[caverns-demo] EXITED tick=%u\n",sim->tick());
                        }
                    }else if(route&&route->arrived&&at(localGoal)){
                        actor.journey=Journey::Done;arrived=true;
                    }
                    if(arrived)std::fprintf(stdout,"[caverns-demo] ARRIVED tick=%u bodies=%zu\n",sim->tick(),box->bodyCount());
                    if(exerciseRestore())break;
                }
            }
            (void)gi.updateFrame(renderPosition(*position(sim->world(),module->actor)));
            if(frames-lastProgress>=120){lastProgress=frames;
                std::fprintf(stdout,"[caverns-demo] frame=%u tick=%u regions=%zu phase=%d\n",
                    frames,sim->tick(),progress.residentRegions,int(progress.phase));}
            if(maxFrames&&frames>=maxFrames){
                if(!arrived)fail("route did not arrive within frame budget");
                else if(movingView&&!rebaseCount)fail("moving view did not publish a rebase");
                else if(restoreScenario&&(!restoreCompleted||sim->tick()<=checkpointTick))fail("restore/replay did not complete");
                else{stopping=true;platform::requestQuit();}
            }
        }catch(const std::exception& ex){fail(ex.what());}
        exclusive=false;
    }
    void draw(gfx::Device& dev,double){
        if(!initialized)return;
        ++frames;
        render::Camera cam;cam.target=renderPosition(*position(sim->world(),module->actor));
        cam.distance=1.4f;cam.pitch=.25f;cam.yaw=.8f+(movingView?float(renderSeconds)*.15f:0.f);
        renderer.beginScene(cam,dev.framebufferWidth(),dev.framebufferHeight());
        view.planets().draw(renderer,viewFrame,renderSeconds,planetBody,0);
        view.drawPlanetCaves(renderer,viewFrame,renderSeconds);
        render::PointLight lamp;lamp.pos=lampPosition(project);lamp.radius=8;lamp.intensity=4;
        lamp.color={1,.8f,.55f};renderer.lights.add(lamp);
        if(!host->recoveryRequired())(void)host->bindIndirect(renderer);
        else renderer.clearDynamicGiOverlay();
        renderer.drawMesh(marker,render::mat4::translate(cam.target),{1,1,1,1});
        gfx::PassDesc pass;pass.clearColor[0]=.04f;pass.clearColor[1]=.05f;pass.clearColor[2]=.08f;
        renderer.render(pass);dev.endPass();
    }
    void shutdown(){
        stopping=true;exclusive=true;
        if(!initialized){if(failed)std::fprintf(stderr,"[caverns-demo] partial initialization retained for process teardown\n");return;}
        module->motor={};
        if(proxy!=physics::kNoBody){
            if(!box->destroy(proxy)){fail("destroy native proxy");return;}proxy=physics::kNoBody;
        }
        if(!device->waitForResourceIdle()||!host->shutdown(renderer)){fail("native host shutdown refused");return;}
        host.reset();
        for(const auto& key:keys)if(!exterior->unload(anchor.bodyId,key,[this](const auto&,const auto&){return stopping&&proxy==physics::kNoBody;})){
            fail("exterior retirement refused");return;
        }
        exterior.reset();
        if(!device->waitForResourceIdle()||!gi.shutdownAfterGpuIdle()){fail("GI shutdown refused");return;}
        device->destroyBuffer(marker.vb);device->destroyBuffer(marker.ib);
        view.planets().shutdown(*device);renderer.shutdown();initialized=false;
    }
};
// Retain a failed/quarantined owner until process exit, not unsafe teardown.
std::unique_ptr<Demo> demo;
}
int aiverseMain(int argc,char** argv){
    demo=std::make_unique<Demo>();
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--frames"&&i+1<argc)demo->maxFrames=unsigned(std::strtoul(argv[++i],nullptr,10));
        else if(arg=="--portal")demo->scenario=Scenario::Portal;
        else if(arg=="--swim")demo->scenario=Scenario::Swim;
        else if(arg=="--moving-view")demo->movingView=true;
        else if(arg=="--restore")demo->restoreScenario=true;
        else{std::fprintf(stderr,"Unknown argument: %s\n",arg.c_str());return 2;}
    }
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
