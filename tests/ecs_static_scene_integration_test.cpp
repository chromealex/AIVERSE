// Integration of REAL SceneLoadCoordinator/SceneRuntime with immutable
// PrefabStaticCatalog across additive/replace/unload, independent ECS Worlds,
// COW rollback and native persistent ECS v10 save/load.
#include "engine/scene/PrefabStaticSceneBinding.h"
#include "engine/sim/SceneHierarchySystem.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fixture {
struct Config {
    int32_t maxHp = 0;
    std::array<int32_t,30> bonuses{};
    bool operator==(const Config&) const = default;
};
inline void io(aiverse::Archive& archive,Config& value) {
    archive(value.maxHp);
    for(auto& item:value.bonuses) archive(item);
}
inline Config config(int32_t hp) {
    Config value;
    value.maxHp=hp;
    for(size_t i=0;i<value.bonuses.size();++i)
        value.bonuses[i]=hp+int32_t(i);
    return value;
}
}
using namespace aiverse;
namespace sc=aiverse::scene;
namespace st=aiverse::storage;
using Catalog=st::PrefabStaticCatalog<fixture::Config>;

static std::shared_ptr<const Catalog> catalog(bool remapMage=false) {
    st::PrefabStaticCatalogBuilder<fixture::Config> builder;
    AIVERSE_TEST_CHECK(builder.add("prefabs/archer",fixture::config(80)));
    AIVERSE_TEST_CHECK(builder.add("prefabs/guard",fixture::config(120)));
    AIVERSE_TEST_CHECK(builder.add("prefabs/knight",fixture::config(120)));
    AIVERSE_TEST_CHECK(builder.add("prefabs/mage",
        fixture::config(remapMage?120:80)));
    return builder.seal();
}

static const char* kGlobal=R"json({
  "version":1,"id":"scenes/global",
  "dependencies":["assets/common"],
  "objects":[{"id":1,"prefab":"prefabs/guard"}]})json";
static const char* kTown=R"json({
  "version":1,"id":"scenes/town",
  "dependencies":["assets/common","assets/town"],
  "objects":[
    {"id":3,"parent":2,"prefab":"prefabs/archer","properties":{"x":3}},
    {"id":2,"prefab":"prefabs/guard","properties":{"x":10}}
  ]})json";
static const char* kCave=R"json({
  "version":1,"id":"scenes/cave",
  "dependencies":["assets/common","assets/cave"],
  "objects":[{"id":1,"prefab":"prefabs/mage"}]})json";
static const char* kBad=R"json({
  "version":1,"id":"scenes/bad",
  "dependencies":["assets/common"],
  "objects":[{"id":1,"prefab":"prefabs/unknown"}]})json";

struct Module final: GameModule {
    std::shared_ptr<const Catalog> defs;
    std::shared_ptr<SceneHierarchy> hierarchy;
    Module(std::shared_ptr<const Catalog> c,std::shared_ptr<SceneHierarchy> h)
        : defs(std::move(c)),hierarchy(std::move(h)) {}
    std::string name() const override {return "static-scene-real-integration";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        sc::registerScenePersistence(sim);
        st::registerPrefabStaticPersistence<fixture::Config>(
            sim,"sample.prefab.config",1);
        AIVERSE_TEST_CHECK(defs->bindWorld(sim.world()));
        installSceneHierarchy(sim,hierarchy);
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    bool serializeState(Simulation&,Archive&) override {return true;}
    int result(Simulation&) override {return 0;}
};
struct Game {
    std::unique_ptr<Simulation> simulation;
    std::shared_ptr<SceneHierarchy> hierarchy;
};
static Game makeGame(std::shared_ptr<const Catalog> defs) {
    Game result;
    result.hierarchy=std::make_shared<SceneHierarchy>();
    result.simulation=std::make_unique<Simulation>(
        SimConfig{123,20,0,true},
        std::make_unique<Module>(std::move(defs),result.hierarchy));
    result.simulation->setup(Json::object(),Json::object());
    return result;
}
static sc::SceneLoadCoordinator::ReadAsset reader() {
    const auto data=std::make_shared<const std::map<std::string,std::string>>(
        std::map<std::string,std::string>{{"scenes/global",kGlobal},
          {"scenes/town",kTown},{"scenes/cave",kCave},{"scenes/bad",kBad}});
    return [data](const std::string& id,std::string& bytes,std::string& error){
        const auto it=data->find(id);
        if(it==data->end()) {error="scene manifest missing";return false;}
        bytes=it->second;
        return true;
    };
}
struct ResourceCounters {
    std::map<std::string,uint32_t> acquired,released;
    sc::SceneResourceAdapter adapter() {
        return {
            [this](const std::string& key,std::string&) {
                ++acquired[key];return true;
            },
            [this](const std::string& key) { ++released[key]; }
        };
    }
};
static sc::SceneLoadCoordinator::SpawnPrefab initializer(
    std::shared_ptr<const Catalog> defs) {
    sc::StaticPrefabSceneBinding<fixture::Config> binding(
        std::move(defs),
        [](World& world,Entity entity,const sc::SceneObjectAsset& obj,
           std::string&) {
            LocalTransform local{};
            local.position.x=Fixed::fromInt(obj.properties.getInt("x",0));
            world.add<LocalTransform>(entity,local);
            return true;
        });
    return binding.callback();
}
static void awaitReady(sc::SceneLoadCoordinator& coordinator,sc::SceneTicket t) {
    AIVERSE_TEST_CHECK(t.valid());
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        coordinator.poll();
        if(coordinator.status(t)==sc::SceneLoadStatus::Failed)
            throw std::runtime_error("scene prepare failed: "+coordinator.error(t));
        if(coordinator.ready(t))return;
        std::this_thread::yield();
    }
    throw std::runtime_error("scene prepare timeout");
}
static std::vector<Entity> entities(const World& world,sc::SceneId scene) {
    std::vector<Entity> out;
    world.eachExistingConst<sc::SceneOwner>(
        [&](Entity e,const sc::SceneOwner& owner){
            if(owner.scene==scene && world.alive(e)) out.push_back(e);
        });
    return out;
}
static void nativeSceneLifecycle() {
    const auto defs=catalog();
    const auto remapped=catalog(true);
    AIVERSE_TEST_CHECK(defs->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(defs->table().fingerprint()==remapped->table().fingerprint());
    AIVERSE_TEST_CHECK(defs->fingerprint()!=remapped->fingerprint());

    Game game=makeGame(defs);
    Game preview=makeGame(defs);
    auto& world=game.simulation->world();
    ResourceCounters gameResources,previewResources;
    sc::SceneLoadCoordinator loader(world,reader(),gameResources.adapter(),
                                    initializer(defs),game.hierarchy.get());
    sc::SceneLoadCoordinator previewLoader(
        preview.simulation->world(),reader(),previewResources.adapter(),
        initializer(defs),preview.hierarchy.get());
    sc::SceneRuntime sceneState(world);

    // An unbound World cannot create static-config prefab instances.
    World unbound;
    const Entity unboundEntity=unbound.create();
    std::string error;
    AIVERSE_TEST_CHECK(!initializer(defs)(unbound,unboundEntity,
        sc::SceneObjectAsset{1,0,"prefabs/archer",Json::object()},error));
    AIVERSE_TEST_CHECK(error.find("not bound")!=std::string::npos);

    const auto global=loader.request("scenes/global",sc::LoadMode::Additive,true);
    const auto town=loader.request("scenes/town");
    const auto previewTown=previewLoader.request("scenes/town");
    awaitReady(loader,global);
    awaitReady(loader,town);
    awaitReady(previewLoader,previewTown);
    AIVERSE_TEST_CHECK(loader.commit(global));
    AIVERSE_TEST_CHECK(loader.commit(town));
    AIVERSE_TEST_CHECK(previewLoader.commit(previewTown));
    game.hierarchy->update(world);
    preview.hierarchy->update(preview.simulation->world());

    AIVERSE_TEST_CHECK(sceneState.count()==2);
    AIVERSE_TEST_CHECK(gameResources.acquired["assets/common"]==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("assets/common")==2);
    AIVERSE_TEST_CHECK(defs->validateWorld(world));
    AIVERSE_TEST_CHECK(defs->validateWorld(preview.simulation->world()));
    const auto allTown=entities(world,town.scene);
    const auto allPreview=entities(preview.simulation->world(),previewTown.scene);
    const auto persistent=entities(world,global.scene);
    AIVERSE_TEST_CHECK(allTown.size()==2 && allPreview.size()==2 &&
                      persistent.size()==1);
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Config>>()==3);
    AIVERSE_TEST_CHECK(defs->read(world,allTown[0])!=nullptr);
    AIVERSE_TEST_CHECK(defs->read(world,allTown[0])==
                      defs->read(preview.simulation->world(),allPreview[0]));
    // Two Worlds use exactly the same sealed table object, not merely
    // equivalent values or copied payloads.
    AIVERSE_TEST_CHECK(defs->read(world,allTown[0])==
                      defs->read(preview.simulation->world(),allPreview[0]));
    int childCount=0;
    world.eachExistingConst<Parent>([&](Entity e,const Parent& parent) {
        AIVERSE_TEST_CHECK(world.alive(parent.value));
        const auto* t=world.tryGetConst<WorldTransform>(e);
        AIVERSE_TEST_CHECK(t && t->position.x==13_fx);
        ++childCount;
    });
    AIVERSE_TEST_CHECK(childCount==1);

    SimSnapshot before;
    game.simulation->saveState(before);
    const uint64_t checksum=game.simulation->checksum();
    const std::string saved=game.simulation->serialize();
    AIVERSE_TEST_CHECK(!saved.empty());

    // Unload one additive scene: permanent immutable table + other Worlds
    // remain intact; only matching ECS entities are destroyed.
    AIVERSE_TEST_CHECK(loader.unload(town.scene));
    for(Entity e:allTown)AIVERSE_TEST_CHECK(!world.alive(e));
    AIVERSE_TEST_CHECK(world.alive(persistent[0]));
    AIVERSE_TEST_CHECK(gameResources.released["assets/town"]==1);
    AIVERSE_TEST_CHECK(gameResources.released["assets/common"]==0);
    AIVERSE_TEST_CHECK(entities(preview.simulation->world(),previewTown.scene).size()==2);
    AIVERSE_TEST_CHECK(defs->read(world,persistent[0])!=nullptr);

    const auto cave=loader.request("scenes/cave",sc::LoadMode::Replace);
    awaitReady(loader,cave);
    AIVERSE_TEST_CHECK(loader.commit(cave));
    AIVERSE_TEST_CHECK(world.alive(persistent[0]));
    const auto caveEntities=entities(world,cave.scene);
    AIVERSE_TEST_CHECK(caveEntities.size()==1);
    AIVERSE_TEST_CHECK(defs->read(world,caveEntities[0])->maxHp==80);

    // A missing prefab must fail atomically without deleting the existing
    // replacement scene or its persistent actors.
    const auto bad=loader.request("scenes/bad",sc::LoadMode::Replace);
    awaitReady(loader,bad);
    AIVERSE_TEST_CHECK(!loader.commit(bad));
    AIVERSE_TEST_CHECK(!loader.error(bad).empty());
    AIVERSE_TEST_CHECK(world.alive(caveEntities[0]));
    AIVERSE_TEST_CHECK(world.alive(persistent[0]));
    AIVERSE_TEST_CHECK(sceneState.record(bad.scene)==nullptr);

    // Restore prior additive scene state without respawning configs.
    game.simulation->loadState(before);
    loader.invalidateAfterRestore();
    AIVERSE_TEST_CHECK(game.simulation->checksum()==checksum);
    AIVERSE_TEST_CHECK(defs->validateWorld(world));
    AIVERSE_TEST_CHECK(sceneState.count()==2);
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Config>>()==3);
    AIVERSE_TEST_CHECK(entities(world,town.scene).size()==2);
    // Rollback diffs residency: the persistent global scene stayed Active
    // and keeps its leases; only the restored town needs a residency job.
    AIVERSE_TEST_CHECK(loader.residentSceneCount()==1);
    auto restoredTickets=loader.queueRestoredResidency();
    AIVERSE_TEST_CHECK(restoredTickets.size()==1 &&
                       restoredTickets[0].scene==town.scene);
    for(const auto ticket:restoredTickets) {
        awaitReady(loader,ticket);
        AIVERSE_TEST_CHECK(loader.commit(ticket));
    }
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Config>>()==3);
    AIVERSE_TEST_CHECK(game.simulation->checksum()==checksum);

    // Native serialized v10 state stores Entity IDs, static refs and scene
    // records, not duplicated config values or GPU/presentation residency.
    Game restored=makeGame(defs);
    std::string failure;
    AIVERSE_TEST_CHECK(restored.simulation->deserialize(saved,&failure));
    AIVERSE_TEST_CHECK(restored.simulation->checksum()==checksum);
    AIVERSE_TEST_CHECK(defs->validateWorld(restored.simulation->world()));
    AIVERSE_TEST_CHECK(sc::SceneRuntime(restored.simulation->world()).count()==2);
    AIVERSE_TEST_CHECK(!remapped->bindWorld(restored.simulation->world()));
    AIVERSE_TEST_CHECK(!remapped->validateWorld(restored.simulation->world()));
    AIVERSE_TEST_CHECK(defs->read(world,persistent[0])==
        defs->read(preview.simulation->world(),allPreview[0]));

    AIVERSE_TEST_CHECK(loader.unload(town.scene));
    AIVERSE_TEST_CHECK(loader.unload(global.scene));
    AIVERSE_TEST_CHECK(previewLoader.unload(previewTown.scene));
    AIVERSE_TEST_CHECK(loader.residentSceneCount()==0);
    AIVERSE_TEST_CHECK(previewLoader.residentSceneCount()==0);
    AIVERSE_TEST_CHECK(defs->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(defs->table().logicalPayloadBytes()==
                      2*sizeof(fixture::Config));
}

int main() {
    nativeSceneLifecycle();
    std::cout<<"PASS StaticPrefabSceneBinding, additive/replace/unload, "
                "shared config table, dual Worlds, rollback and save\n";
}
