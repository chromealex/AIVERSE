// Native, genre-neutral executable. Uses a real GameModule, Simulation,
// SpatialWorld, native regional NavMesh and existing net::replica Server/
// Client over loopback. No ActorHost, FPS or RTS dependencies.
// A deterministic scripted actor stands in for the game's actual motor;
// GPU/device acceptance is a separate native graphics test.
#include "engine/buildings/BuildingReplicaBridge.h"
#include "engine/buildings/BuildingReplicaRenderScene.h"
#include "engine/net/Rollback.h"
#if defined(AIVERSE_BUILDING_PHYSIS_EXAMPLE)
#include "engine/buildings/BuildingPhysisSceneBinding.h"
#endif
#include "engine/net/Conn.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::building;
using namespace aiverse::lib::systems;
namespace replica=aiverse::net::replica;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)

static std::string readText(const std::string& path){
    std::ifstream file(path,std::ios::binary);
    CHECK(bool(file));
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}

class NativeBuildingModule final:public GameModule{
public:
    explicit NativeBuildingModule(std::string root,bool lockstep=false):root_(std::move(root)),lockstep_(lockstep){}
    std::string name()const override{return "native-building-example";}
    void setup(Simulation& sim,const Json&,const Json&)override{
        std::string error;
        CHECK(namespaces.define("instances/buildings/a",1));
        CHECK(namespaces.define("instances/buildings/b",1));
        CHECK(namespaces.seal());
        RuntimeBuildingGenerator generator;
        CHECK(generator.registerStyleJson(readText(root_+"/engine/assets/building-styles/scifi.json"),&error));
        const auto recipe=readFunctionalBuildingRecipe(
            Json::parse(readText(root_+"/engine/assets/building-recipes/vertical-circulation.json")));
        auto geometry=generator.generateRecipe("scifi",recipe);
        CHECK(geometry);
        BuildingCirculationHost layout;
        CHECK(layout.initialize(geometry,{},0,&error));
        CHECK(layout.lifts().size()==1);
        feature=layout.lifts().begin()->first;
        liftPose=layout.lifts().begin()->second.platformPose();
        for(uint32_t floor=0;floor<layout.lifts().begin()->second.floorCount();++floor)
            floorHeights.push_back(*layout.lifts().begin()->second.floorHeightMm(floor));
        width=recipe.width;depth=recipe.depth;
        buildings=BuildingWorldRuntime::install(sim,spatial,namespaces,0x6b01,
                    BuildingWorldRuntime::commonOwnerPolicy(),&error,
                    lockstep_?BuildingWorldRuntime::RecoveryMode::DeterministicLockstep:
                              BuildingWorldRuntime::RecoveryMode::AuthoritativeCheckpoint);
        CHECK(buildings);

        auto& ecs=sim.world();
        rider=ecs.create();
        stranger=ecs.create();
        const auto feet=buildingLocalToWorld((liftPose.x0+liftPose.x1)*0.5f,
             float(liftPose.yMm)/1000.f+liftPose.thickness,
             (liftPose.z0+liftPose.z1)*0.5f,width,depth,{},0);
        ecs.add<Transform>(rider,Transform{feet,feet,{}});
        const Vec3 away{150_fx,1_fx,150_fx};
        ecs.add<Transform>(stranger,Transform{away,away,{}});
        ecs.add<Owner>(rider,Owner{1});ecs.add<Owner>(stranger,Owner{2});
        buildings->setGraphMonitor(graphMonitor);
        const Vec3 half{Fixed::fromRatio(1,4),Fixed::fromRatio(9,10),Fixed::fromRatio(1,4)};
        ecs.add<BuildingPhysicalExtent>(rider,BuildingPhysicalExtent{half,true,{0_fx,half.y,0_fx}});
        ecs.add<BuildingPhysicalExtent>(stranger,BuildingPhysicalExtent{half,true,{0_fx,half.y,0_fx}});

        auto definition=[&](uint64_t id,const char* key,Vec3 origin,nav::Mesh& nav){
            BuildingWorldRuntime::Definition d;
            d.sceneId=id;d.instanceKey=key;d.origin=origin;d.regionalNavigation=&nav;
            d.authorize=[&sim](Entity actor,const std::string&,bool){
                return sim.world().alive(actor)&&sim.world().has<Owner>(actor);
            };
            d.reachable=[&sim](Entity actor,const world::Bounds& b){
                const auto* t=sim.world().tryGetConst<Transform>(actor);
                if(!t)return false;
                const Fixed radius=2_fx;
                return t->pos.x>=b.min.x-radius&&t->pos.x<=b.max.x+radius&&
                       t->pos.y>=b.min.y-radius&&t->pos.y<=b.max.y+radius&&
                       t->pos.z>=b.min.z-radius&&t->pos.z<=b.max.z+radius;
            };
            // This headless module owns no extra route reservations. The
            // runtime separately checks real ECS body volumes on unload.
            d.canUnload=[](){return true;};
            d.resetDerivedGraphs=[this,id](){graphRevisions.erase(id);return true;};
            d.graphOwners.emplace(BuildingGraphDomain::Gameplay,
                [this](const BuildingGraphChange& c){
                    if(!graphAvailable)return false;
                    // The derived cache belongs to the native module,
                    // not to the building generator. Reset on recovery.
                    if(c.kind==BuildingGraphChangeKind::Removed)graphRevisions.erase(c.sceneId);
                    else graphRevisions[c.sceneId]=c.revision;
                    return true;
                });
            return d;
        };
        auto first=definition(1,"instances/buildings/a",{},navA);
        first.atLiftLanding=[this,&sim](Entity actor,uint32_t lift,uint32_t floor){
            if(lift!=feature||floor>=2)return false;
            const auto* t=sim.world().tryGetConst<Transform>(actor);
            if(!t)return false;
            if(floor>=floorHeights.size())return false;
            const auto landing=buildingLocalToWorld((liftPose.x0+liftPose.x1)*0.5f,
                 float(floorHeights[floor])/1000.f+liftPose.thickness,
                 liftPose.z0-0.8f,width,depth,{},0);
            return fxAbs(t->pos.x-landing.x)<=1_fx&&fxAbs(t->pos.z-landing.z)<=1_fx&&
                   fxAbs(t->pos.y-landing.y)<=Fixed::fromRatio(1,2);
        };
        first.authorizeLift=[&sim](Entity actor,uint32_t,uint32_t,BuildingLiftCommandKind){
            return sim.world().alive(actor)&&sim.world().has<Owner>(actor);
        };
        CHECK(buildings->define(std::move(first),generator,"scifi",recipe,&error));
        FunctionalBuildingRecipe small;
        small.width=16;small.height=8;small.depth=12;
        BuildingFeature gate;gate.kind=BuildingFeatureKind::LargeGate;
        gate.width=4;gate.height=3;gate.position=8;gate.open=0.5f;
        small.features.push_back(gate);
        CHECK(buildings->define(definition(2,"instances/buildings/b",{80_fx,0_fx,0_fx},navB),
                                 generator,"scifi",small,&error));
    }
    void applyCommand(Simulation&,const Command&)override{++unexpectedModuleCommands;}
    void hashState(Simulation& sim,Hasher& h)override{
        for(Entity actor:{rider,stranger}){
            const auto& t=sim.world().get<Transform>(actor);
            h.v3(t.pos);h.v3(t.prevPos);h.fx(t.yaw);
            h.u32(sim.world().get<Owner>(actor).player);
        }
    }
    bool serializeState(Simulation& sim,Archive& a)override{
        // This example's two native actors have fixed handles created by
        // setup. Their existing Transform/Owner belong to the game module;
        // passenger and extent persistence belongs to the building registry.
        for(Entity actor:{rider,stranger}){
            if(!sim.world().alive(actor))return false;
            auto& t=sim.world().get<Transform>(actor);
            auto& owner=sim.world().get<Owner>(actor);
            a(t.pos,t.prevPos,t.yaw,owner.player);
        }
        return a.ok();
    }
    int result(Simulation&)override{return 0;}
    void move(Simulation& sim,Entity actor,Vec3 position){
        auto& t=sim.world().get<Transform>(actor);t.prevPos=t.pos;t.pos=position;
    }
    const BuildingLiftSnapshot& lift(const Simulation& sim)const{
        return sim.world().resourceConst<BuildingLiftWorldCheckpoints>()->scenes.at(1).lifts.at(feature).state;
    }

    std::shared_ptr<BuildingGraphMonitor> graphMonitor=std::make_shared<BuildingGraphMonitor>();
    std::shared_ptr<BuildingWorldRuntime> buildings;
    world::SpatialWorld spatial;
    scene::SceneCollisionNamespaces namespaces;
    nav::Mesh navA,navB;
    Entity rider{},stranger{};
    uint32_t feature=0;
    BuildingLiftPlatformPose liftPose;
    std::vector<int64_t> floorHeights;
    float width=0,depth=0;
    bool graphAvailable=true;
    std::map<uint64_t,uint64_t> graphRevisions;
    uint32_t unexpectedModuleCommands=0;
private:
    std::string root_;
    bool lockstep_=false;
};

static void verifyLockstep(const std::string& root){
    auto module=std::make_unique<NativeBuildingModule>(root,true);
    auto* game=module.get();
    SimConfig cfg;cfg.tickRate=20;cfg.inputDelayTicks=1;
    Simulation sim(cfg,std::move(module));
    sim.setup(Json::object(),Json::parse(R"({"players":[{"name":"A"},{"name":"B"}]})"));
    auto& runtime=*game->buildings;
    CHECK(runtime.ready()&&runtime.lockstep());
    game->move(sim,game->stranger,{88_fx,Fixed::fromRatio(6,25),-1_fx});
    auto step=[&]{const auto tick=sim.tick();sim.step();CHECK(sim.tick()==tick+1);};
    auto inject=[&](std::optional<Command> c){
        CHECK(c);c->tick=sim.tick();c->seq=1;sim.inject(*c);
    };
    inject(runtime.makeLiftCommand(1,game->feature,0,game->rider,1,BuildingLiftCommandKind::Board));
    inject(runtime.makeCommand(2,"entrance/0/0",game->stranger,2,true));
    step();
    CHECK(game->lift(sim).passengerCount==1);
    inject(runtime.makeLiftCommand(1,game->feature,1,game->rider,1,BuildingLiftCommandKind::SelectFloor));
    inject(runtime.makeCommand(2,"entrance/0/0",game->stranger,2,false));
    step();
    // Capture moving gate + queued opposite request, and occupied lift.
    SimSnapshot saved;sim.saveState(saved);
    const auto startHash=sim.checksum();
    CHECK(sim.world().resourceConst<BuildingLockstepWorldState>()->scenes.at(2).motion.front().moving);
    CHECK(!sim.world().resourceConst<BuildingLockstepWorldState>()->scenes.at(2).session.queue.empty());
    for(int i=0;i<160;++i)step();
    const auto expected=sim.checksum();
    const auto expectedLift=game->lift(sim);
    const auto expectedFeet=sim.world().get<Transform>(game->rider).pos;
    sim.loadState(saved);
    CHECK(runtime.ready()&&sim.checksum()==startHash);
    for(int i=0;i<160;++i)step();
    CHECK(sim.checksum()==expected);
    CHECK(game->lift(sim)==expectedLift);
    CHECK(sim.world().get<Transform>(game->rider).pos==expectedFeet);
    CHECK(game->lift(sim).currentFloor==1);
    CHECK(!runtime.canTraverse(2,"entrance/0/0"));

    // Actual existing net::RollbackSession: predict no input, then a
    // late confirmed building command changes the predicted history.
    SimSnapshot beforePrediction;sim.saveState(beforePrediction);
    const uint32_t first=sim.tick();
    auto late=runtime.makeCommand(2,"entrance/0/0",game->stranger,2,true);
    CHECK(late);late->tick=first;late->seq=1;
    uint32_t target=first;
    uint64_t predictedHash=0;
    {
        net::SessionConfig options;options.rollbackWindow=8;options.checksumInterval=1;
        options.incrementalWorldSnapshots=true;
        net::RollbackSession session(sim,options,{},
            [](uint32_t,const std::vector<Command>&){},[](uint32_t,uint64_t){});
        session.update(0.2,1000);
        target=sim.tick();CHECK(target>first);
        session.confirm(first,1,{*late});
        session.update(0.0,1001);
        CHECK(!session.broken()&&runtime.ready()&&session.stats().rollbacks==1);
        CHECK(sim.tick()==target);
        predictedHash=sim.checksum();
    }
    sim.loadState(beforePrediction);CHECK(runtime.ready());
    sim.inject(*late);while(sim.tick()<target)step();
    CHECK(sim.checksum()==predictedHash);
    // Release the actual rider at its open landing before final teardown.
    const auto& top=game->lift(sim);
    game->move(sim,game->rider,buildingLocalToWorld((game->liftPose.x0+game->liftPose.x1)*0.5f,
        float(top.platformYmm)/1000.f+game->liftPose.thickness,game->liftPose.z0-0.8f,
        game->width,game->depth,{},0));
    inject(runtime.makeLiftCommand(1,game->feature,1,game->rider,1,BuildingLiftCommandKind::Exit));
    step();CHECK(game->lift(sim).passengerCount==0);
    CHECK(runtime.shutdown());
    std::puts("building deterministic checkpoint and late-input rollback: PASS");
}
#if defined(AIVERSE_BUILDING_PHYSIS_EXAMPLE)
static void verifyPhysisBinding(){
    physics::Scene physics;
    BuildingPhysisSceneBinding binding(physics);
    std::map<uint32_t,world::Bounds> shapes{
        {1,{{0_fx,0_fx,0_fx},{4_fx,1_fx,4_fx}}},
        {2,{{1_fx,1_fx,1_fx},{2_fx,3_fx,2_fx}}}};
    CHECK(binding.synchronize(shapes,{2}));
    CHECK(binding.bodyCount()==2&&physics.bodyCount()==2);
    CHECK(binding.telemetry().getInt("lastCreated")==2);
    const auto leaf=binding.body(2);
    shapes.at(2).min.x+=1_fx;shapes.at(2).max.x+=1_fx;
    CHECK(binding.synchronize(shapes,{2})&&binding.body(2)==leaf);
    CHECK(binding.telemetry().getInt("lastMoved")==1);
    CHECK(physics.position(leaf).x>2.4f&&physics.position(leaf).x<2.6f);
    physics::BodyDesc cargo;cargo.position={2.5f,2.f,1.5f};
    auto actor=physics.create(cargo);CHECK(actor);
    physics::QueryFilter filter;filter.mask=physics::kCategoryDynamic;
    CHECK(binding.occupied(shapes.at(2),shapes.at(2),filter));
    CHECK(binding.synchronizeRider(Entity{},Vec3{5_fx,6_fx,7_fx},
                                  [actor](Entity){return actor;}));
    CHECK(physics.position(actor).y==6.f);
    CHECK(binding.retire()&&physics.bodyCount()==1);
    CHECK(physics.destroy(actor));
    std::puts("building native PHYSIS body publication: PASS");
}
#endif
int main(int argc,char** argv){
    try{
        const std::string root=argc>1?argv[1]:".";
        if(argc>2&&std::string(argv[2])=="--lockstep"){verifyLockstep(root);return 0;}
#if defined(AIVERSE_BUILDING_PHYSIS_EXAMPLE)
        if(argc>2&&std::string(argv[2])=="--physis"){verifyPhysisBinding();return 0;}
#endif
        auto module=std::make_unique<NativeBuildingModule>(root);
        auto* game=module.get();
        SimConfig cfg;cfg.tickRate=20;cfg.inputDelayTicks=1;
        Simulation sim(cfg,std::move(module));
        sim.setup(Json::object(),Json::parse(R"({"players":[{"name":"A"},{"name":"B"}]})"));
        auto& world=*game->buildings;
        CHECK(world.ready());
        CHECK(game->namespaces.activeCount()==2);
        CHECK(game->navA.built()&&!game->navA.dirty());
        CHECK(game->navB.built()&&!game->navB.dirty());
        const auto navTelemetry=game->graphMonitor->read(1)["ownerTelemetry"]["navmesh"];
        CHECK(navTelemetry.isObject()&&navTelemetry.getInt("tileCount")>0);
        CHECK(navTelemetry["lastSuccessfulRefresh"].isObject());
        CHECK(world.canBoardLift(1,game->feature,0));
        CHECK(!world.canTraverse(2,"entrance/0/0"));
        CHECK(!game->spatial.colliders().empty());

        replica::Schema schema;
        replica::ClassDef cls;cls.name="world";cls.alwaysRelevant=true;
        replica::FieldDef field;field.name="tick";field.kind=replica::FieldKind::Int;field.min=0;field.max=100000;
        cls.fields.push_back(field);
        const auto classId=schema.add(std::move(cls));
        replica::ServerOptions options;options.tickRate=20;options.snapshotRate=20;
        replica::Server server(schema,options);
        CHECK(server.create(classId)!=replica::kNoObject);
        auto [serverEnd,clientEnd]=net::makeLoopbackPair();
        // The example owns both endpoints and admits this peer. Production
        // host authenticates its established connection before attach().
        const auto slot=server.attach(std::move(serverEnd));CHECK(slot==1);
        replica::Client client(schema,std::move(clientEnd));
        BuildingReplicaBridge bridge(game->buildings,610,611);
        CHECK(bridge.valid());
        size_t accepted=0,rejected=0;
        server.onMessage=[&](uint8_t peer,uint16_t channel,const net::Bytes& bytes){
            if(!bridge.handles(channel))return;
            if(bridge.receive(peer,channel,bytes))++accepted;else ++rejected;
        };
        uint64_t now=1000;
        auto pump=[&]{
            server.poll(now);server.tick();server.broadcast(now);client.poll(now);
            now+=50;
        };
        for(int i=0;i<100&&!client.ready();++i)pump();
        CHECK(client.ready()&&client.slot()==slot);
        auto step=[&]{
            const uint32_t before=sim.tick();
            sim.step();CHECK(sim.tick()==before+1);
        };
        step();
        auto send=[&](Command command,uint32_t seq,bool expect){
            command.seq=seq;
            net::Bytes bytes;
            CHECK(encodeBuildingReplicaCommand(command,bytes));
            const size_t before=accepted+rejected;
            client.send(610,bytes);
            for(int i=0;i<40&&accepted+rejected==before;++i)pump();
            CHECK(accepted+rejected==before+1);
            if(expect)CHECK(accepted>0);
        };
        // A client can name the wrong player, but actual admitted slot
        // and ECS Owner are authoritative. Foreign actor request fails.
        auto foreign=world.makeLiftCommand(1,game->feature,0,game->stranger,2,BuildingLiftCommandKind::Board);
        CHECK(foreign);send(*foreign,1,false);CHECK(rejected==1&&accepted==0);
        auto board=world.makeLiftCommand(1,game->feature,0,game->rider,2,BuildingLiftCommandKind::Board);
        CHECK(board);send(*board,2,true);CHECK(accepted==1);
        step();step();
        CHECK(game->lift(sim).passengerCount==1);
        const auto* passenger=sim.world().tryGetConst<BuildingLiftPassenger>(game->rider);
        CHECK(passenger&&passenger->attached&&passenger->sceneId==1);
        CHECK(game->unexpectedModuleCommands==0);
        CHECK(!world.unload(1));

        // Existing reliable message channel sends only a committed scene.
        BuildingEcsWorldState record;uint64_t content=0;
        CHECK(world.captureApertureState(1,record,content));
        BuildingReplicaView view;
        RuntimeBuildingGenerator remoteGenerator;
        CHECK(remoteGenerator.registerStyleJson(readText(root+"/engine/assets/building-styles/scifi.json")));
        const auto remoteRecipe=readFunctionalBuildingRecipe(Json::parse(
            readText(root+"/engine/assets/building-recipes/vertical-circulation.json")));
        BuildingReplicaRenderScene remote;
        BuildingReplicaRenderScene::Definition remoteDef;
        remoteDef.sceneId=1;remoteDef.instanceKey="instances/buildings/a";remoteDef.styleId="scifi";
        remoteDef.graphDomains=buildingGraphBit(BuildingGraphDomain::Navigation)|
                               buildingGraphBit(BuildingGraphDomain::Gameplay);
        CHECK(remote.prepare(remoteDef,remoteGenerator,remoteRecipe,{}));
        CHECK(remote.contentHash()==content);
        bool received=false;
        client.onMessage=[&](uint16_t channel,const net::Bytes& data){
            if(channel==611)received=view.accept(data,1,content,record.leaves.size())&&remote.accept(data);
        };
        CHECK(bridge.send(server,slot,1));
        for(int i=0;i<40&&!received;++i)pump();
        CHECK(received&&view.frame().hasCirculation);
        CHECK(view.frame().circulation.passengers.size()==1);
        const auto leafFrame=remote.sampleApertures(0);
        CHECK(leafFrame.valid&&leafFrame.leaves.size()==record.leaves.size()*2);
        size_t liftDraws=0;
        CHECK(remote.sampleLifts([&](const render::Model&,const std::string&,Vec3,uint32_t){
            ++liftDraws;return true;
        }));
        CHECK(liftDraws==5); // one cabin + four landing leaves
        CHECK(remote.clear());

        auto select=world.makeLiftCommand(1,game->feature,1,game->rider,1,BuildingLiftCommandKind::SelectFloor);
        CHECK(select);send(*select,3,true);CHECK(accepted==2);
        // A real graph owner temporarily refuses a physical change. No
        // additional gameplay tick may consume input while ACK is pending.
        game->graphAvailable=false;
        step();step();
        const uint32_t blockedTick=sim.tick();
        sim.step();CHECK(sim.tick()==blockedTick);
        net::Bytes pending;CHECK(!bridge.capture(1,pending));
        const auto diagnostics=world.graphDiagnostics(1);
        const auto gameplay=buildingGraphBit(BuildingGraphDomain::Gameplay);
        CHECK(diagnostics&&diagnostics->lastChange);
        CHECK((diagnostics->registeredDomains&gameplay)!=0);
        CHECK((diagnostics->pendingDomains&gameplay)!=0);
        CHECK((diagnostics->refusedDomains&gameplay)!=0);
        CHECK(diagnostics->attempts>0);
        const auto published=game->graphMonitor->read(1);
        CHECK(published.isObject()&&published.getInt("pendingDomains")!=0);
        CHECK(published["history"].isArray());
        game->graphAvailable=true;
        step();
        const auto settled=world.graphDiagnostics(1);
        CHECK(settled&&settled->lastChange&&settled->pendingDomains==0);
        CHECK((settled->acknowledgedDomains&gameplay)!=0);
        CHECK(game->graphMonitor->read(1).getInt("pendingDomains")==0);
        for(int i=0;i<80&&game->lift(sim).phase!=BuildingLiftPhase::Traveling;++i)step();
        CHECK(game->lift(sim).phase==BuildingLiftPhase::Traveling);
        for(int i=0;i<8;++i)step();
        CHECK(!world.retireActor(game->rider));
        CHECK(!world.canBoardLift(1,game->feature,1));

        const auto savedState=game->lift(sim);
        const auto savedFeet=sim.world().get<Transform>(game->rider).pos;
        const auto oldEpoch=world.commandEpoch();
        const std::string bytes=sim.serialize();CHECK(!bytes.empty());
        for(int i=0;i<8;++i)step();
        CHECK(game->lift(sim).platformYmm!=savedState.platformYmm);
        CHECK(sim.deserialize(bytes));
        CHECK(!world.ready());
        const auto restoredTick=sim.tick();
        sim.step();CHECK(sim.tick()==restoredTick);
        game->graphRevisions.clear();
        CHECK(world.reconcile());
        CHECK(world.commandEpoch()>oldEpoch&&world.ready());
        CHECK(game->lift(sim)==savedState);
        CHECK(sim.world().get<Transform>(game->rider).pos==savedFeet);
        CHECK(game->namespaces.activeCount()==2);
        CHECK(game->navA.built()&&!game->navA.dirty());

        // Old epoch commands can still arrive after reconnect/checkpoint.
        send(*select,4,false);CHECK(rejected==2);
        for(int i=0;i<220&&!world.canBoardLift(1,game->feature,1);++i)step();
        CHECK(world.canBoardLift(1,game->feature,1));
        CHECK(game->lift(sim).currentFloor==1);
        CHECK(sim.world().get<Transform>(game->rider).pos.y>savedFeet.y);
        const auto& top=game->lift(sim);
        // Scripted native actor motor test moves the rider through the
        // open exit zone. The real bridge independently validates exit.
        game->move(sim,game->rider,buildingLocalToWorld(
              (game->liftPose.x0+game->liftPose.x1)*0.5f,
              float(top.platformYmm)/1000.f+game->liftPose.thickness,
              game->liftPose.z0-0.8f,game->width,game->depth,{},0));
        auto exit=world.makeLiftCommand(1,game->feature,1,game->rider,1,BuildingLiftCommandKind::Exit);
        CHECK(exit);send(*exit,5,true);CHECK(accepted==3);
        step();step();
        CHECK(game->lift(sim).passengerCount==0);
        CHECK(!sim.world().get<BuildingLiftPassenger>(game->rider).attached);

        game->move(sim,game->rider,{150_fx,1_fx,150_fx});
        CHECK(world.retireActor(game->rider));
        CHECK(world.unload(1));
        CHECK(game->namespaces.activeCount()==1);
        CHECK(!game->spatial.colliders().empty());
        CHECK(world.load(1));
        CHECK(game->namespaces.activeCount()==2);
        CHECK(world.canBoardLift(1,game->feature,0));
        CHECK(world.unload(1));
        CHECK(world.unload(2));
        CHECK(game->spatial.colliders().empty());
        CHECK(game->namespaces.activeCount()==0);
        CHECK(world.shutdown());
        std::puts("building native world + Replica loopback + ECS lift checkpoint: PASS");
        return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"building native world example FAIL: %s\n",e.what());
        return 1;
    }
}
