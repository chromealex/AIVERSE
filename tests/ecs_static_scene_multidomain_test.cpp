// Real SceneLoadCoordinator integration of multiple independent immutable
// StaticRef<T> prefab config domains, optional configurations and mutable
// per-instance overrides. Always-on checks are active in Release.
#include "engine/scene/PrefabStaticDomainRegistry.h"
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
struct Weapon {
    int32_t damage = 0;
    std::array<int32_t,12> ballistics{};
};
inline void io(aiverse::Archive& archive, Weapon& v) {
    archive(v.damage);
    for(auto& n:v.ballistics)archive(n);
}
struct Brain {
    int32_t aggression = 0;
    int32_t awareness = 0;
};
inline void io(aiverse::Archive& archive,Brain& v) {
    archive(v.aggression,v.awareness);
}
struct Inventory {
    int32_t slots = 0;
    int32_t capacity = 0;
};
inline void io(aiverse::Archive& archive,Inventory& v) {
    archive(v.slots,v.capacity);
}
struct InstanceState {
    int32_t hp = 0;
    int32_t ammo = 0;
};
inline void io(aiverse::Archive& archive, InstanceState& v) {
    archive(v.hp,v.ammo);
}
}
using namespace aiverse;
namespace sc=aiverse::scene;
namespace st=aiverse::storage;

template<class T>
using Catalog=st::PrefabStaticCatalog<T>;

static fixture::Weapon weapon(int32_t damage) {
    fixture::Weapon v;
    v.damage=damage;
    for(size_t i=0;i<v.ballistics.size();++i)
        v.ballistics[i]=damage+int32_t(i);
    return v;
}
struct Configs {
    std::shared_ptr<const Catalog<fixture::Weapon>> weapons;
    std::shared_ptr<const Catalog<fixture::Brain>> brains;
    std::shared_ptr<const Catalog<fixture::Inventory>> inventories;
    std::shared_ptr<const sc::PrefabStaticDomainRegistry> registry;
};
static Configs makeConfigs(bool reverseRegistration=false,
                           bool remapTurret=false) {
    Configs result;
    st::PrefabStaticCatalogBuilder<fixture::Weapon> weaponBuilder;
    // Input order is deliberately different from canonical prefab key order.
    AIVERSE_TEST_CHECK(weaponBuilder.add("prefabs/turret",weapon(30)));
    AIVERSE_TEST_CHECK(weaponBuilder.add("prefabs/soldier",weapon(50)));
    AIVERSE_TEST_CHECK(weaponBuilder.add("prefabs/archer",weapon(30)));
    result.weapons=weaponBuilder.seal();

    st::PrefabStaticCatalogBuilder<fixture::Brain> brainBuilder;
    AIVERSE_TEST_CHECK(brainBuilder.add("prefabs/turret",
        fixture::Brain{remapTurret?11:7,100}));
    AIVERSE_TEST_CHECK(brainBuilder.add("prefabs/archer",
        fixture::Brain{7,100}));
    AIVERSE_TEST_CHECK(brainBuilder.add("prefabs/soldier",
        fixture::Brain{11,100}));
    result.brains=brainBuilder.seal();

    st::PrefabStaticCatalogBuilder<fixture::Inventory> inventoryBuilder;
    AIVERSE_TEST_CHECK(inventoryBuilder.add("prefabs/soldier",
        fixture::Inventory{8,1000}));
    result.inventories=inventoryBuilder.seal();

    auto registry=std::make_shared<sc::PrefabStaticDomainRegistry>();
    auto addWeapon=[&] {
        AIVERSE_TEST_CHECK(registry->addDomain<fixture::Weapon>(
            "combat/weapon","test.prefab.weapon",result.weapons));
    };
    auto addBrain=[&] {
        AIVERSE_TEST_CHECK(registry->addDomain<fixture::Brain>(
            "ai/brain","test.prefab.brain",result.brains));
    };
    auto addInventory=[&] {
        AIVERSE_TEST_CHECK(registry->addDomain<fixture::Inventory>(
            "items/inventory","test.prefab.inventory",result.inventories,
            sc::MissingPrefabConfig::Skip));
    };
    if(reverseRegistration) {
        addInventory();addWeapon();addBrain();
    } else {
        addBrain();addInventory();addWeapon();
    }
    AIVERSE_TEST_CHECK(!registry->addDomain<fixture::Weapon>(
        "other/weapon","test.prefab.weapon2",result.weapons));
    AIVERSE_TEST_CHECK(!registry->addDomain<fixture::Inventory>(
        "../bad","test.prefab.other",result.inventories));
    AIVERSE_TEST_CHECK(!registry->addDomain<fixture::Brain>(
        "other/brain","test.prefab.brain",result.brains));
    AIVERSE_TEST_CHECK(registry->domainCount()==3);
    AIVERSE_TEST_CHECK(registry->seal());
    AIVERSE_TEST_CHECK(!registry->seal());
    AIVERSE_TEST_CHECK(!registry->addDomain<fixture::Weapon>(
        "after/seal","test.prefab.late",result.weapons));
    result.registry=std::move(registry);
    return result;
}

static const char* kGlobal=R"json({
  "version":1,"id":"scenes/global","dependencies":[],
  "objects":[{"id":1,"prefab":"prefabs/soldier"}]})json";
static const char* kField=R"json({
  "version":1,"id":"scenes/field","dependencies":[],
  "objects":[
    {"id":3,"prefab":"prefabs/turret"},
    {"id":2,"prefab":"prefabs/soldier","properties":{"ammo":17}},
    {"id":1,"prefab":"prefabs/archer"}
  ]})json";
static const char* kDungeon=R"json({
  "version":1,"id":"scenes/dungeon","dependencies":[],
  "objects":[{"id":1,"prefab":"prefabs/turret"}]})json";
static const char* kBad=R"json({
  "version":1,"id":"scenes/bad","dependencies":[],
  "objects":[{"id":1,"prefab":"prefabs/unknown"}]})json";

static sc::SceneLoadCoordinator::ReadAsset reader() {
    auto files=std::make_shared<const std::map<std::string,std::string>>(
        std::map<std::string,std::string>{
            {"scenes/global",kGlobal},{"scenes/field",kField},
            {"scenes/dungeon",kDungeon},{"scenes/bad",kBad}
        });
    return [files](const std::string& name,std::string& out,std::string& error) {
        const auto it=files->find(name);
        if(it==files->end()){error="scene not found";return false;}
        out=it->second;
        return true;
    };
}

struct Module final : GameModule {
    std::shared_ptr<const sc::PrefabStaticDomainRegistry> registry;
    explicit Module(std::shared_ptr<const sc::PrefabStaticDomainRegistry> r)
        : registry(std::move(r)) {}
    std::string name() const override {return "multi-static-prefab"; }
    void setup(Simulation& sim,const Json&,const Json&) override {
        sc::registerScenePersistence(sim);
        registry->registerPersistence(sim);
        sim.registerPersistentComponent<fixture::InstanceState>(
            "test.prefab.instance",1);
        AIVERSE_TEST_CHECK(registry->bindWorld(sim.world()));
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    bool serializeState(Simulation&,Archive&) override {return true;}
    int result(Simulation&) override{return 0;}
};
static std::unique_ptr<Simulation> makeSimulation(const Configs& configs) {
    auto sim=std::make_unique<Simulation>(
        SimConfig{97,20,0,true},std::make_unique<Module>(configs.registry));
    sim->setup(Json::object(),Json::object());
    return sim;
}
static sc::SceneLoadCoordinator::SpawnPrefab spawn(const Configs& configs) {
    return configs.registry->callback(
        [weapons=configs.weapons,brains=configs.brains,
         inventories=configs.inventories](
             World& world,Entity entity,const sc::SceneObjectAsset& obj,
             std::string& error) {
            const auto* weaponConfig=weapons->read(world,entity);
            const auto* brainConfig=brains->read(world,entity);
            if(!weaponConfig || !brainConfig){
                error="required typed static configuration missing";
                return false;
            }
            const auto* inventoryConfig=inventories->read(world,entity);
            const int32_t ammo=int32_t(obj.properties.getInt(
                "ammo",weaponConfig->damage));
            world.add<fixture::InstanceState>(
                entity,fixture::InstanceState{weaponConfig->damage*2,ammo});
            // This dynamically mutable instance value is intentionally NOT
            // part of the immutable StaticTable in any configuration domain.
            if(inventoryConfig && inventoryConfig->capacity!=1000){
                error="incorrect optional inventory config";
                return false;
            }
            return true;
        });
}
static void awaitReady(sc::SceneLoadCoordinator& loader,sc::SceneTicket ticket) {
    AIVERSE_TEST_CHECK(ticket.valid());
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        loader.poll();
        if(loader.status(ticket)==sc::SceneLoadStatus::Failed)
            throw std::runtime_error(loader.error(ticket));
        if(loader.ready(ticket))return;
        std::this_thread::yield();
    }
    throw std::runtime_error("scene preparation timeout");
}
static std::vector<Entity> owned(const World& w,sc::SceneId id) {
    std::vector<Entity> result;
    w.eachExistingConst<sc::SceneOwner>([&](Entity e,const sc::SceneOwner& owner){
        if(owner.scene==id && w.alive(e))result.push_back(e);
    });
    return result;
}

static void multiDomainIntegration() {
    const Configs configs=makeConfigs();
    const Configs sameOrderIndependent=makeConfigs(true);
    AIVERSE_TEST_CHECK(configs.weapons->fingerprint()==
        sameOrderIndependent.weapons->fingerprint());
    AIVERSE_TEST_CHECK(configs.brains->fingerprint()==
        sameOrderIndependent.brains->fingerprint());
    const Configs remapped=makeConfigs(false,true);
    AIVERSE_TEST_CHECK(configs.brains->table().fingerprint()==
        remapped.brains->table().fingerprint());
    AIVERSE_TEST_CHECK(configs.brains->fingerprint()!=
        remapped.brains->fingerprint());

    // Multi-domain binding is preflighted as a whole: a stale Brain
    // catalog prevents partial Weapon/Inventory bindings.
    World incompatible;
    AIVERSE_TEST_CHECK(remapped.brains->bindWorld(incompatible));
    AIVERSE_TEST_CHECK(!configs.registry->bindWorld(incompatible));
    AIVERSE_TEST_CHECK(incompatible.resourceConst<
        st::PrefabCatalogBinding<fixture::Weapon>>()==nullptr);
    AIVERSE_TEST_CHECK(incompatible.resourceConst<
        st::PrefabCatalogBinding<fixture::Inventory>>()==nullptr);

    // Required domain failure must leave even a newly created entity
    // without a partially attached StaticRef.
    World scratch;
    AIVERSE_TEST_CHECK(configs.registry->bindWorld(scratch));
    const Entity scratchEntity=scratch.create();
    sc::SceneObjectAsset unknown{1,0,"prefabs/unknown",Json::object()};
    std::string scratchError;
    AIVERSE_TEST_CHECK(!spawn(configs)(scratch,scratchEntity,unknown,scratchError));
    AIVERSE_TEST_CHECK(!scratchError.empty());
    AIVERSE_TEST_CHECK(!scratch.has<st::StaticRef<fixture::Weapon>>(scratchEntity));
    AIVERSE_TEST_CHECK(!scratch.has<st::StaticRef<fixture::Brain>>(scratchEntity));
    AIVERSE_TEST_CHECK(!scratch.has<st::StaticRef<fixture::Inventory>>(scratchEntity));

    auto game=makeSimulation(configs);
    auto preview=makeSimulation(configs);
    auto& world=game->world();
    sc::SceneLoadCoordinator loader(world,reader(),{},spawn(configs));
    sc::SceneLoadCoordinator previewLoader(
        preview->world(),reader(),{},spawn(configs));
    sc::SceneRuntime scenes(world);

    const auto persistent=loader.request(
        "scenes/global",sc::LoadMode::Additive,true);
    const auto field=loader.request("scenes/field");
    const auto previewField=previewLoader.request("scenes/field");
    awaitReady(loader,persistent);
    awaitReady(loader,field);
    awaitReady(previewLoader,previewField);
    AIVERSE_TEST_CHECK(loader.commit(persistent));
    AIVERSE_TEST_CHECK(loader.commit(field));
    AIVERSE_TEST_CHECK(previewLoader.commit(previewField));
    AIVERSE_TEST_CHECK(scenes.count()==2);

    AIVERSE_TEST_CHECK(configs.registry->validateWorld(world));
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(preview->world()));
    const auto persistentEntities=owned(world,persistent.scene);
    const auto fieldEntities=owned(world,field.scene);
    const auto previewEntities=owned(preview->world(),previewField.scene);
    AIVERSE_TEST_CHECK(persistentEntities.size()==1);
    AIVERSE_TEST_CHECK(fieldEntities.size()==3);
    AIVERSE_TEST_CHECK(previewEntities.size()==3);

    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Weapon>>()==4);
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Brain>>()==4);
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Inventory>>()==2);
    AIVERSE_TEST_CHECK(world.countExisting<fixture::InstanceState>()==4);
    AIVERSE_TEST_CHECK(configs.weapons->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(configs.brains->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(configs.inventories->uniqueConfigCount()==1);

    // The external immutable config address is EXACTLY the same in the
    // main game and preview World. They do not duplicate this value.
    AIVERSE_TEST_CHECK(configs.weapons->read(world,fieldEntities[0])==
        configs.weapons->read(preview->world(),previewEntities[0]));
    AIVERSE_TEST_CHECK(configs.brains->read(world,fieldEntities[1])==
        configs.brains->read(preview->world(),previewEntities[1]));
    AIVERSE_TEST_CHECK(configs.inventories->read(world,fieldEntities[0])==nullptr);
    AIVERSE_TEST_CHECK(configs.inventories->read(world,fieldEntities[1])!=nullptr);

    // Reversed registration order must still produce identical native
    // checksums and serialization when the same scenes are committed in
    // canonical order. Domain mapping order is sorted by domain key.
    auto reverse=makeSimulation(sameOrderIndependent);
    sc::SceneLoadCoordinator reverseLoader(
        reverse->world(),reader(),{},spawn(sameOrderIndependent));
    const auto reverseGlobal=reverseLoader.request(
        "scenes/global",sc::LoadMode::Additive,true);
    const auto reverseField=reverseLoader.request("scenes/field");
    awaitReady(reverseLoader,reverseGlobal);
    awaitReady(reverseLoader,reverseField);
    AIVERSE_TEST_CHECK(reverseLoader.commit(reverseGlobal));
    AIVERSE_TEST_CHECK(reverseLoader.commit(reverseField));
    AIVERSE_TEST_CHECK(reverse->checksum()==game->checksum());
    AIVERSE_TEST_CHECK(reverse->serialize()==game->serialize());
    AIVERSE_TEST_CHECK(sameOrderIndependent.registry->validateWorld(reverse->world()));

    // Mutating one instance cannot mutate any static domain.
    const auto* baseWeapon=configs.weapons->read(world,fieldEntities[1]);
    AIVERSE_TEST_CHECK(baseWeapon && baseWeapon->damage==50);
    const int32_t originalAmmo=world.get<fixture::InstanceState>(
        fieldEntities[1]).ammo;
    AIVERSE_TEST_CHECK(originalAmmo==17);
    world.get<fixture::InstanceState>(fieldEntities[1]).ammo=999;
    AIVERSE_TEST_CHECK(configs.weapons->read(world,fieldEntities[1])->damage==50);
    AIVERSE_TEST_CHECK(preview->world().get<fixture::InstanceState>(
        previewEntities[1]).ammo==17);

    SimSnapshot before;
    game->saveState(before);
    const uint64_t checksum=game->checksum();
    const std::string serialized=game->serialize();
    AIVERSE_TEST_CHECK(!serialized.empty());

    const auto replacement=loader.request("scenes/dungeon",sc::LoadMode::Replace);
    awaitReady(loader,replacement);
    AIVERSE_TEST_CHECK(loader.commit(replacement));
    AIVERSE_TEST_CHECK(scenes.count()==2); // persistent + dungeon
    AIVERSE_TEST_CHECK(owned(world,field.scene).empty());
    AIVERSE_TEST_CHECK(owned(world,persistent.scene).size()==1);
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Inventory>>()==1);
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(world));
    AIVERSE_TEST_CHECK(owned(preview->world(),previewField.scene).size()==3);

    // Missing REQUIRED configs cause a rejected replacement. New Entity
    // mutation is prevented by preflight; all existing scenes survive.
    const auto bad=loader.request("scenes/bad",sc::LoadMode::Replace);
    awaitReady(loader,bad);
    AIVERSE_TEST_CHECK(!loader.commit(bad));
    AIVERSE_TEST_CHECK(!loader.error(bad).empty());
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(owned(world,replacement.scene).size()==1);
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(world));

    game->loadState(before);
    loader.invalidateAfterRestore();
    AIVERSE_TEST_CHECK(game->checksum()==checksum);
    AIVERSE_TEST_CHECK(world.get<fixture::InstanceState>(fieldEntities[1]).ammo==999);
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(world));
    AIVERSE_TEST_CHECK(world.countExisting<st::StaticRef<fixture::Weapon>>()==4);
    // Rollback diffs residency: the persistent scene stayed Active and keeps
    // its leases; only the restored field scene needs a residency job.
    AIVERSE_TEST_CHECK(loader.residentSceneCount()==1);
    const auto restored=loader.queueRestoredResidency();
    AIVERSE_TEST_CHECK(restored.size()==1 && restored[0].scene==field.scene);
    for(const auto ticket:restored) {
        awaitReady(loader,ticket);
        AIVERSE_TEST_CHECK(loader.commit(ticket));
    }
    AIVERSE_TEST_CHECK(game->checksum()==checksum);

    auto cold=makeSimulation(configs);
    std::string error;
    AIVERSE_TEST_CHECK(cold->deserialize(serialized,&error));
    AIVERSE_TEST_CHECK(cold->checksum()==checksum);
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(cold->world()));
    AIVERSE_TEST_CHECK(sc::SceneRuntime(cold->world()).count()==2);

    // A changed prefab association with the exact same underlying unique
    // config values must be detected after native deserialize/rollback.
    AIVERSE_TEST_CHECK(!remapped.registry->validateWorld(cold->world()));
    AIVERSE_TEST_CHECK(!remapped.registry->bindWorld(cold->world()));

    AIVERSE_TEST_CHECK(loader.unload(field.scene));
    AIVERSE_TEST_CHECK(loader.unload(persistent.scene));
    AIVERSE_TEST_CHECK(previewLoader.unload(previewField.scene));
    AIVERSE_TEST_CHECK(configs.weapons->uniqueConfigCount()==2);
    AIVERSE_TEST_CHECK(configs.registry->validateWorld(world));
}

int main() {
    multiDomainIntegration();
    std::cout<<"PASS multi static domains, optional prefab config, real scene replace, "
        "cross-World tables, v10 native persistence and COW rollback\n";
}
