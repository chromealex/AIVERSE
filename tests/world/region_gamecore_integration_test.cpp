// Real GameCore + NativeModule integration of the shared region lifecycle.
// No new genre host and no changes to games that omit this opt-in module.
#include "engine/game/GameCore.h"
#include "engine/game/Native.h"
#include "engine/game/Components.h"
#include "engine/data/JsonCompose.h"
#include "engine/world/RegionLifecycleRuntime.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace g=aiverse::game;
namespace w=aiverse::world;

namespace fixture {
constexpr uint16_t kCommand=61537; // test native module command, not player input
struct RegionWork {
    int32_t completed=0;
    std::string assignment="idle";
};
void io(Archive& archive,RegionWork& state) {
    archive(state.completed,state.assignment);
}

class Native final:public g::NativeModule {
public:
    void setup(g::GameCore&,Simulation& sim,g::NativeState& state,const Json&)const override {
        w::RegionDormancyController controller;
        if(!controller.registerComponent<RegionWork>("fixture.region_work",1))
            throw std::logic_error("region work codec registration");
        w::RegionLifecycleRuntime::install(sim,std::move(controller),1);
        sim.registerPersistentComponent<RegionWork>("fixture.region_work",1);

        // Attach to a REAL spawned GameCore Unit, not a dummy GameModule.
        // Prefer an immobile map structure so the fixture has a stable home.
        auto& ecs=sim.world();
        Entity chosen=kNullEntity;
        ecs.eachExistingConst<g::Unit>([&](Entity e,const g::Unit&){
            if(!ecs.has<Transform>(e))return;
            if(ecs.has<g::Footprint>(e) && !chosen.valid())chosen=e;
        });
        if(!chosen.valid())ecs.eachExistingConst<g::Unit>(
            [&](Entity e,const g::Unit&){
                if(!chosen.valid() && ecs.has<Transform>(e))chosen=e;
            });
        if(!chosen.valid())throw std::runtime_error("no live GameCore map unit");
        const w::Cell region=w::SpatialWorld(32).cell(ecs.get<Transform>(chosen).pos);
        auto* ledger=ecs.resource<w::RegionEntityLedger>();
        if(!ledger || !w::RegionEcsBridge::bindNew(
                ecs,*ledger,chosen,1001,region))
            throw std::runtime_error("cannot bind real GameCore unit to region");
        ecs.add<RegionWork>(chosen,RegionWork{5,"city-duty"});
        state.values["entity"]=int64_t(chosen.packed());
        state.values["cell_x"]=region.x;
        state.values["cell_y"]=region.y;
        state.values["cell_z"]=region.z;
    }
    void tick(g::GameCore&,Simulation& sim,g::NativeState& state)const override {
        auto& ecs=sim.world();
        Entity e=Entity::fromPacked(uint64_t(state.values.at("entity")));
        if(!ecs.alive(e) || ecs.has<w::RegionDormant>(e))return;
        // The host's behaviour explicitly skips the parked component.
        if(auto* work=ecs.tryGet<RegionWork>(e)) ++work->completed;
    }
    void command(g::GameCore&,Simulation& sim,g::NativeState& state,
                 const Command& cmd)const override {
        if(cmd.player!=1 || (cmd.param!=1 && cmd.param!=2) ||
           !cmd.units.empty())return;
        const w::Cell region{
            int32_t(state.values.at("cell_x")),
            int32_t(state.values.at("cell_y")),
            int32_t(state.values.at("cell_z"))
        };
        // A validated authoritative game command, never a presentation call.
        if(!w::RegionLifecycleRuntime::request(sim,region,cmd.param==2))
            throw std::runtime_error("region activation queue unavailable");
    }
};
std::unique_ptr<g::NativeModule> factory(){
    return std::make_unique<Native>();
}
}

static std::unique_ptr<Simulation> make(bool enabled) {
    const std::string base=std::string(AIVERSE_TEMPLATES_DIR)+"/mushrooms/";
    Json rules=readComposedJson(base+"rules.json",fileReader());
    const Json map=Json::parseFile(base+"maps/duel.json");
    std::vector<g::NativeDefinition> definitions;
    if(enabled) {
        rules.set("nativeModules",Json::parse(R"([{"id":"region_lifecycle_fixture","command":61537}])"));
        definitions.push_back({"region_lifecycle_fixture",1,&fixture::factory});
    }
    auto sim=std::make_unique<Simulation>(
        SimConfig{29,20,0,true},std::make_unique<g::GameCore>(std::move(definitions)));
    sim->setup(rules,map);
    return sim;
}
static Entity target(Simulation& sim){
    const auto* store=sim.world().resourceConst<g::NativeStore>();
    AIVERSE_TEST_CHECK(store && store->modules.size()==1);
    return Entity::fromPacked(uint64_t(store->modules[0].values.at("entity")));
}
static w::Cell region(Simulation& sim) {
    const auto* state=sim.world().resourceConst<g::NativeStore>();
    AIVERSE_TEST_CHECK(state);
    const auto& v=state->modules[0].values;
    return {int32_t(v.at("cell_x")),int32_t(v.at("cell_y")),int32_t(v.at("cell_z"))};
}
static void command(Simulation& sim,int64_t action,uint8_t player=1) {
    Command cmd;
    cmd.player=player;
    cmd.type=fixture::kCommand;
    cmd.param=action;
    sim.submit(std::move(cmd));
    sim.step();
}
static void verifyLiveGameCore() {
    auto first=make(true);
    auto peer=make(true);
    auto& world=first->world();
    const Entity e=target(*first);
    AIVERSE_TEST_CHECK(world.alive(e) && world.has<g::Unit>(e));
    AIVERSE_TEST_CHECK(world.has<fixture::RegionWork>(e));
    AIVERSE_TEST_CHECK(first->checksum()==peer->checksum());
    const auto origin=region(*first);
    const auto* initial=world.resourceConst<w::RegionEntityLedger>();
    AIVERSE_TEST_CHECK(initial && initial->activeIn(origin)==std::vector<uint64_t>{1001});

    // Real GameCore: native command schedules a transition; shared system
    // processes it in Simulation::step and native module skips parked work.
    command(*first,1);command(*peer,1);
    AIVERSE_TEST_CHECK(first->checksum()==peer->checksum());
    AIVERSE_TEST_CHECK(world.has<w::RegionDormant>(e));
    AIVERSE_TEST_CHECK(!world.has<fixture::RegionWork>(e));
    AIVERSE_TEST_CHECK(world.has<g::Unit>(e)); // core gameplay entity stays allocated
    const auto* cold=world.resourceConst<w::RegionColdStore>();
    AIVERSE_TEST_CHECK(cold && cold->size()==1);
    AIVERSE_TEST_CHECK(world.resourceConst<w::RegionActivationQueue>()->pending.empty());

    // Invalid player cannot change a region. Native command ownership is enforced.
    const auto before=first->checksum();
    command(*first,2,2);
    AIVERSE_TEST_CHECK(world.has<w::RegionDormant>(e));
    AIVERSE_TEST_CHECK(world.resourceConst<w::RegionActivationQueue>()->pending.empty());
    AIVERSE_TEST_CHECK(first->checksum()!=before); // tick/sequence changed, not ownership

    const auto parked=first->serialize();
    auto reloaded=make(true);
    std::string error;
    AIVERSE_TEST_CHECK(reloaded->deserialize(parked,&error));
    AIVERSE_TEST_CHECK(reloaded->checksum()==first->checksum());
    AIVERSE_TEST_CHECK(reloaded->world().has<w::RegionDormant>(target(*reloaded)));

    command(*first,2);
    command(*reloaded,2);
    AIVERSE_TEST_CHECK(first->checksum()==reloaded->checksum());
    AIVERSE_TEST_CHECK(!world.has<w::RegionDormant>(e));
    AIVERSE_TEST_CHECK(world.has<fixture::RegionWork>(e));
    AIVERSE_TEST_CHECK(world.get<fixture::RegionWork>(e).assignment=="city-duty");
    AIVERSE_TEST_CHECK(world.get<fixture::RegionWork>(e).completed>=6);

    first->world().enableCowSnapshots();
    SimSnapshot snap;
    first->saveState(snap);
    const auto hash=first->checksum();
    command(*first,1);
    AIVERSE_TEST_CHECK(first->checksum()!=hash);
    first->loadState(snap);
    AIVERSE_TEST_CHECK(first->checksum()==hash);
    AIVERSE_TEST_CHECK(first->world().has<fixture::RegionWork>(e));
    AIVERSE_TEST_CHECK(!first->world().has<w::RegionDormant>(e));

    // Games without the native module never get an extra W1 resource.
    auto regular=make(false);
    AIVERSE_TEST_CHECK(!regular->world().resourceConst<w::RegionEntityLedger>());
    AIVERSE_TEST_CHECK(!regular->world().resourceConst<w::RegionActivationQueue>());
    AIVERSE_TEST_CHECK(!regular->world().resourceConst<w::RegionColdStore>());
    AIVERSE_TEST_CHECK(!regular->serialize().empty());
}

static void blockedRequestDoesNotLoseActors() {
    auto game=make(true);
    auto& ecs=game->world();
    auto* ledger=ecs.resource<w::RegionEntityLedger>();
    auto* cold=ecs.resource<w::RegionColdStore>();
    const Entity e=target(*game);
    const auto cell=region(*game);
    AIVERSE_TEST_CHECK(ledger && cold);
    // A ledger owner without a live ECS binding is a load/reification fault.
    // The scheduler must keep the request, not silently park the other actor.
    AIVERSE_TEST_CHECK(ledger->spawn(9999,cell));
    AIVERSE_TEST_CHECK(w::RegionLifecycleRuntime::request(*game,cell,false));
    const auto before=game->checksum();
    w::RegionDormancyController policy;
    AIVERSE_TEST_CHECK(policy.registerComponent<fixture::RegionWork>("fixture.region_work",1));
    AIVERSE_TEST_CHECK(w::RegionLifecycleRuntime::step(*game,policy,1)==0);
    AIVERSE_TEST_CHECK(game->checksum()==before);
    AIVERSE_TEST_CHECK(ecs.has<fixture::RegionWork>(e));
    AIVERSE_TEST_CHECK(!ecs.has<w::RegionDormant>(e));
    const auto* queue=ecs.resourceConst<w::RegionActivationQueue>();
    AIVERSE_TEST_CHECK(queue && queue->pending.size()==1 && queue->applied==0);
    // A new validated command can replace the failed request without clearing
    // the ledger identity of any already-living entity.
    AIVERSE_TEST_CHECK(w::RegionLifecycleRuntime::request(*game,cell,true));
    AIVERSE_TEST_CHECK(w::RegionLifecycleRuntime::step(*game,policy,1)==0);
    // An unmaterialized identity blocks both hot and cold transitions.
    AIVERSE_TEST_CHECK(ecs.has<fixture::RegionWork>(e));
}

int main() {
    blockedRequestDoesNotLoseActors();
    verifyLiveGameCore();
    std::cout<<"PASS real GameCore region lifecycle integration, commands, save and rollback\n";
}
