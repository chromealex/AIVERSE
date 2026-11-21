#include "engine/scene/SceneLoadCoordinator.h"
#include "tests/AlwaysCheck.h"
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

using namespace aiverse;
namespace sc = aiverse::scene;

struct PrefabTag { std::string name; };
static const char* kGlobal = R"json({
  "version":1,"id":"scenes/global",
  "dependencies":["textures/sun","materials/shared"],
  "objects":[{"id":1,"prefab":"world/sun"}]})json";
static const char* kCity = R"json({
  "version":1,"id":"scenes/city",
  "dependencies":["textures/house","materials/shared"],
  "objects":[
    {"id":9,"prefab":"world/window","parent":2,"properties":{"x":3}},
    {"id":2,"prefab":"world/house","properties":{"x":10}}
  ]})json";
static const char* kDesert = R"json({
  "version":1,"id":"scenes/desert",
  "dependencies":["materials/shared","textures/sand"],
  "objects":[{"id":1,"prefab":"world/cactus"}]})json";
static const char* kMissing = R"json({
  "version":1,"id":"scenes/missing",
  "dependencies":["resource/missing"],
  "objects":[{"id":1,"prefab":"world/rock"}]})json";
static const char* kBadPrefab = R"json({
  "version":1,"id":"scenes/badprefab",
  "dependencies":["materials/shared"],
  "objects":[{"id":1,"prefab":"fail"}]})json";
static const char* kSlow = R"json({
  "version":1,"id":"scenes/slow",
  "dependencies":[], "objects":[]})json";

static void checkAssets() {
    sc::SceneAsset asset;
    std::string error;
    AIVERSE_TEST_CHECK(sc::SceneAsset::decode(Json::parse(kCity), asset, &error));
    AIVERSE_TEST_CHECK(asset.id == "scenes/city" && asset.objects.size() == 2);
    AIVERSE_TEST_CHECK(asset.objects[0].id == 2 && asset.objects[1].parent == 2);
    AIVERSE_TEST_CHECK(asset.dependencies[0] == "materials/shared");
    AIVERSE_TEST_CHECK(!sc::SceneAsset::validKey("../escape"));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::validKey("scene//bad"));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::validKey("C:/windows"));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":["a","a"],"objects":[]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":2,"parent":3,"prefab":"one"},{"id":3,"parent":2,"prefab":"two"}]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(error.find("cycle") != std::string::npos);
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":1,"parent":88,"prefab":"one"}]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":1,"prefab":"one"},{"id":1,"prefab":"two"}]}
    )json"),asset,&error));
    // Scene-local IDs and version must not use Json::integer(), which
    // converts fractional JSON values through Fixed and truncates them.
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1.5,"id":"scene/test","dependencies":[],"objects":[]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":1.5,"prefab":"one"}]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":1,"parent":1.2,"prefab":"one"}]}
    )json"),asset,&error));
    AIVERSE_TEST_CHECK(!sc::SceneAsset::decode(Json::parse(R"json(
      {"version":1,"id":"scene/test","dependencies":[],"objects":[
      {"id":999999999999999999999999,"prefab":"one"}]}
    )json"),asset,&error));
}

template<class Predicate>
static void awaitStatus(sc::SceneLoadCoordinator& loader, Predicate done) {
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(std::chrono::steady_clock::now() < timeout) {
        loader.poll();
        if(done()) return;
        std::this_thread::yield();
    }
    AIVERSE_TEST_CHECK(false && "timed out waiting for async scene fixture");
}
static void awaitReady(sc::SceneLoadCoordinator& loader, sc::SceneTicket ticket) {
    awaitStatus(loader, [&] {
        return loader.status(ticket) == sc::SceneLoadStatus::Ready ||
               loader.status(ticket) == sc::SceneLoadStatus::Failed;
    });
    AIVERSE_TEST_CHECK(loader.status(ticket) == sc::SceneLoadStatus::Ready);
}

static void coordinatorLifecycle() {
    const std::thread::id ownerThread = std::this_thread::get_id();
    auto files = std::make_shared<const std::map<std::string,std::string>>(
        std::map<std::string,std::string>{
            {"scenes/global",kGlobal},{"scenes/city",kCity},
            {"scenes/desert",kDesert},{"scenes/missing",kMissing},
            {"scenes/badprefab",kBadPrefab}
        });
    sc::SceneLoadCoordinator::ReadAsset reader =
        [files,ownerThread](const std::string& key, std::string& out,
                            std::string& error) {
            AIVERSE_TEST_CHECK(std::this_thread::get_id() != ownerThread);
            const auto it = files->find(key);
            if(it == files->end()) { error = "not found"; return false; }
            out = it->second;
            return true;
        };
    std::map<std::string, int> acquired, released;
    sc::SceneResourceAdapter resources;
    resources.acquire = [&](const std::string& key, std::string& error) {
        AIVERSE_TEST_CHECK(std::this_thread::get_id() == ownerThread);
        if(key == "resource/missing") { error = "missing resource"; return false; }
        ++acquired[key];
        return true;
    };
    resources.release = [&](const std::string& key) {
        AIVERSE_TEST_CHECK(std::this_thread::get_id() == ownerThread);
        ++released[key];
    };
    int spawned = 0;
    sc::SceneLoadCoordinator::SpawnPrefab spawn =
        [&](World& w, Entity e, const sc::SceneObjectAsset& object,
            std::string& error) {
            AIVERSE_TEST_CHECK(std::this_thread::get_id() == ownerThread);
            ++spawned;
            w.add<PrefabTag>(e, PrefabTag{object.prefab});
            if(object.prefab == "fail") {
                error = "failed prefab fixture";
                return false;
            }
            if(object.properties.has("x")) {
                LocalTransform local;
                local.position.x = Fixed::fromInt(
                    object.properties.getInt("x"));
                w.add<LocalTransform>(e,local);
            }
            return true;
        };
    World world;
    SceneHierarchy hierarchy;
    sc::SceneRuntime scenes(world);

    {
        sc::SceneLoadCoordinator loader(world,reader,resources,spawn,&hierarchy);
        const sc::SceneTicket global = loader.request(
            "scenes/global",sc::LoadMode::Additive,true);
        const sc::SceneTicket city = loader.request("scenes/city");
        AIVERSE_TEST_CHECK(global.valid() && city.valid());
        awaitReady(loader,global);
        awaitReady(loader,city);
        AIVERSE_TEST_CHECK(loader.commit(global));
        AIVERSE_TEST_CHECK(loader.commit(city));
        AIVERSE_TEST_CHECK(loader.residentSceneCount() == 2);
        AIVERSE_TEST_CHECK(loader.resourceRefCount("materials/shared") == 2);
        AIVERSE_TEST_CHECK(acquired["materials/shared"] == 1);
        AIVERSE_TEST_CHECK(scenes.count() == 2);
        AIVERSE_TEST_CHECK(spawned == 3);
        hierarchy.update(world);
        int childCount=0;
        world.eachExistingConst<Parent>([&](Entity e,const Parent& parent) {
            AIVERSE_TEST_CHECK(scenes.ownerOf(e) == city.scene);
            AIVERSE_TEST_CHECK(world.alive(parent.value));
            const auto* t = world.tryGetConst<WorldTransform>(e);
            AIVERSE_TEST_CHECK(t && t->position.x == 13_fx);
            ++childCount;
        });
        AIVERSE_TEST_CHECK(childCount == 1);

        // Resource failure must not unload existing active city.
        const auto missing = loader.request("scenes/missing",sc::LoadMode::Replace);
        awaitReady(loader,missing);
        AIVERSE_TEST_CHECK(!loader.commit(missing));
        AIVERSE_TEST_CHECK(loader.error(missing) == "missing resource");
        AIVERSE_TEST_CHECK(scenes.record(city.scene) && scenes.record(global.scene));
        AIVERSE_TEST_CHECK(loader.resourceRefCount("materials/shared") == 2);
        AIVERSE_TEST_CHECK(scenes.record(missing.scene) == nullptr);

        // A spawner may fail midway; the new scene and leases are rolled back.
        const auto bad = loader.request("scenes/badprefab",sc::LoadMode::Replace);
        awaitReady(loader,bad);
        AIVERSE_TEST_CHECK(!loader.commit(bad));
        AIVERSE_TEST_CHECK(scenes.record(bad.scene) == nullptr);
        AIVERSE_TEST_CHECK(scenes.record(city.scene) != nullptr);
        AIVERSE_TEST_CHECK(loader.resourceRefCount("materials/shared") == 2);

        // Commit a correct replacement. Persistent global remains alive;
        // the old city is removed and its private texture is released.
        const auto desert = loader.request("scenes/desert",sc::LoadMode::Replace);
        awaitReady(loader,desert);
        AIVERSE_TEST_CHECK(loader.commit(desert));
        AIVERSE_TEST_CHECK(scenes.record(city.scene) == nullptr);
        AIVERSE_TEST_CHECK(scenes.record(global.scene) != nullptr);
        AIVERSE_TEST_CHECK(scenes.record(desert.scene) != nullptr);
        AIVERSE_TEST_CHECK(released["textures/house"] == 1);
        AIVERSE_TEST_CHECK(loader.resourceRefCount("materials/shared") == 2);
        AIVERSE_TEST_CHECK(loader.status(city) == sc::SceneLoadStatus::Unloaded);

        // COW rollback of authoritative World invalidates host-only leases
        // and in-flight tickets; restored entities must NOT be respawned.
        world.enableCowSnapshots(true);
        World before = world;
        const auto forest = loader.request("scenes/city",sc::LoadMode::Additive);
        awaitReady(loader,forest);
        AIVERSE_TEST_CHECK(loader.commit(forest));
        AIVERSE_TEST_CHECK(scenes.count() == 3);
        const int beforeRestoreSpawned = spawned;
        world.copyFrom(before);
        hierarchy.rebuild(world);
        hierarchy.update(world);
        const auto houseReleasesBefore = released["textures/house"];
        const auto sharedReleasesBefore = released["materials/shared"];
        loader.invalidateAfterRestore();
        AIVERSE_TEST_CHECK(loader.status(forest) == sc::SceneLoadStatus::Unknown);
        AIVERSE_TEST_CHECK(scenes.count() == 2);
        // Rollback diff: global + desert are still Active in the restored
        // World and keep their leases; only the forest lease is released.
        AIVERSE_TEST_CHECK(loader.residentSceneCount() == 2);
        AIVERSE_TEST_CHECK(released["textures/house"] == houseReleasesBefore + 1);
        AIVERSE_TEST_CHECK(released["materials/shared"] == sharedReleasesBefore);
        AIVERSE_TEST_CHECK(loader.resourceRefCount("materials/shared") == 2);
        AIVERSE_TEST_CHECK(loader.queueRestoredResidency().empty());
        AIVERSE_TEST_CHECK(spawned == beforeRestoreSpawned);

        // Cold restore (all leases lost, e.g. a fresh coordinator after
        // deserialize) still re-queues residency for every Active scene.
        {
            sc::SceneLoadCoordinator cold(world,reader,resources,spawn,&hierarchy);
            auto reload = cold.queueRestoredResidency();
            AIVERSE_TEST_CHECK(reload.size() == 2);
            AIVERSE_TEST_CHECK(cold.queueRestoredResidency().empty());
            for(const auto& ticket : reload) {
                awaitReady(cold,ticket);
                AIVERSE_TEST_CHECK(cold.commit(ticket));
            }
            AIVERSE_TEST_CHECK(spawned == beforeRestoreSpawned);
            AIVERSE_TEST_CHECK(cold.residentSceneCount() == 2);
            AIVERSE_TEST_CHECK(cold.resourceRefCount("materials/shared") == 2);
        }
        AIVERSE_TEST_CHECK(loader.unload(desert.scene));
        AIVERSE_TEST_CHECK(loader.unload(global.scene));
        AIVERSE_TEST_CHECK(loader.residentSceneCount() == 0);
    }
    for(const auto& [name, n] : acquired)
        AIVERSE_TEST_CHECK(released[name] == n);
}

static void cancelledWorker() {
    World world;
    std::promise<void> permit;
    const std::shared_future<void> gate = permit.get_future().share();
    sc::SceneLoadCoordinator loader(
        world,
        [gate](const std::string&,std::string& value,std::string&) {
            gate.wait();
            value = kSlow;
            return true;
        },
        {},
        {},
        nullptr);
    const auto ticket = loader.request("scenes/slow", sc::LoadMode::Replace);
    AIVERSE_TEST_CHECK(ticket.valid());
    AIVERSE_TEST_CHECK(loader.cancel(ticket)); // Must not block on the worker.
    AIVERSE_TEST_CHECK(sc::SceneRuntime(world).record(ticket.scene) == nullptr);
    permit.set_value();
    awaitStatus(loader,[&]{return loader.status(ticket) == sc::SceneLoadStatus::Unknown;});
    AIVERSE_TEST_CHECK(!loader.commit(ticket));
    AIVERSE_TEST_CHECK(sc::SceneRuntime(world).count() == 0);
}

// Review 2026-11-21: a rollback to a snapshot taken while a Replace load was
// Loading must re-schedule a full job for the restored Loading record;
// otherwise its pendingReplace blocks every later Replace forever.
static void rollbackDuringLoad() {
    World world;
    world.enableCowSnapshots(true);
    std::map<std::string,int> acquired, released;
    sc::SceneResourceAdapter resources;
    resources.acquire = [&](const std::string& key,std::string&) {
        ++acquired[key]; return true;
    };
    resources.release = [&](const std::string& key) { ++released[key]; };
    sc::SceneLoadCoordinator loader(
        world,
        [](const std::string& key,std::string& out,std::string& error) {
            if(key == "scenes/global") out = kGlobal;
            else if(key == "scenes/desert") out = kDesert;
            else { error = "not found"; return false; }
            return true;
        },
        resources,
        [](World&,Entity,const sc::SceneObjectAsset&,std::string&) { return true; },
        nullptr);
    sc::SceneRuntime scenes(world);
    const auto desert = loader.request("scenes/desert",sc::LoadMode::Replace);
    AIVERSE_TEST_CHECK(desert.valid());
    World loading = world; // Snapshot while the Replace is still Loading.
    awaitReady(loader,desert);
    AIVERSE_TEST_CHECK(loader.commit(desert));
    AIVERSE_TEST_CHECK(scenes.record(desert.scene)->phase == sc::ScenePhase::Active);

    world.copyFrom(loading);
    loader.invalidateAfterRestore();
    AIVERSE_TEST_CHECK(loader.residentSceneCount() == 0);
    AIVERSE_TEST_CHECK(scenes.record(desert.scene)->phase == sc::ScenePhase::Loading);
    AIVERSE_TEST_CHECK(scenes.stateView()->pendingReplace == desert.scene.value);
    // A new Replace is blocked by the restored pendingReplace...
    AIVERSE_TEST_CHECK(!loader.request("scenes/global",sc::LoadMode::Replace).valid());
    // ...until the restored Loading record is committed through a new job.
    auto restored = loader.queueRestoredResidency();
    AIVERSE_TEST_CHECK(restored.size() == 1 && restored[0].scene == desert.scene);
    AIVERSE_TEST_CHECK(loader.queueRestoredResidency().empty());
    awaitReady(loader,restored[0]);
    AIVERSE_TEST_CHECK(loader.commit(restored[0]));
    AIVERSE_TEST_CHECK(scenes.record(desert.scene)->phase == sc::ScenePhase::Active);
    AIVERSE_TEST_CHECK(scenes.stateView()->pendingReplace == 0);
    AIVERSE_TEST_CHECK(loader.residentSceneCount() == 1);
    const auto next = loader.request("scenes/global",sc::LoadMode::Replace);
    AIVERSE_TEST_CHECK(next.valid());
    awaitReady(loader,next);
    AIVERSE_TEST_CHECK(loader.commit(next));
    AIVERSE_TEST_CHECK(scenes.record(desert.scene) == nullptr);
    AIVERSE_TEST_CHECK(loader.unload(next.scene));
    for(const auto& [name,n] : acquired) AIVERSE_TEST_CHECK(released[name] == n);
}

// Review 2026-11-21: terminal job records (Committed/Unloaded) are bounded;
// a committed job does not retain its manifest.
static void terminalJobsArePruned() {
    World world;
    sc::SceneLoadCoordinator loader(
        world,
        [](const std::string&,std::string& out,std::string&) {
            out = kGlobal; return true;
        },
        {[](const std::string&,std::string&) { return true; },
         [](const std::string&) {}},
        [](World&,Entity,const sc::SceneObjectAsset&,std::string&) { return true; },
        nullptr);
    sc::SceneTicket last{};
    for(int i = 0; i < 200; ++i) {
        const auto ticket = loader.request("scenes/global");
        AIVERSE_TEST_CHECK(ticket.valid());
        awaitReady(loader,ticket);
        AIVERSE_TEST_CHECK(loader.commit(ticket));
        AIVERSE_TEST_CHECK(loader.status(ticket) == sc::SceneLoadStatus::Committed);
        AIVERSE_TEST_CHECK(loader.prepared(ticket) == nullptr);
        AIVERSE_TEST_CHECK(loader.unload(ticket.scene));
        last = ticket;
    }
    loader.poll();
    AIVERSE_TEST_CHECK(loader.jobCount() <= sc::SceneLoadCoordinator::kMaxTerminalJobs);
    AIVERSE_TEST_CHECK(loader.status(last) == sc::SceneLoadStatus::Unloaded);
    AIVERSE_TEST_CHECK(loader.residentSceneCount() == 0);
}

// Review 2026-11-21: a throwing release callback (contract violation) must
// not leave a stale refcount entry behind.
static void throwingReleaseKeepsRefcountsConsistent() {
    World world;
    sc::SceneLoadCoordinator loader(
        world,
        [](const std::string&,std::string& out,std::string&) {
            out = kGlobal; return true;
        },
        {[](const std::string&,std::string&) { return true; },
         [](const std::string&) { throw std::runtime_error("release fixture"); }},
        [](World&,Entity,const sc::SceneObjectAsset&,std::string&) { return true; },
        nullptr);
    const auto ticket = loader.request("scenes/global");
    awaitReady(loader,ticket);
    AIVERSE_TEST_CHECK(loader.commit(ticket));
    AIVERSE_TEST_CHECK(loader.sharedResourceCount() > 0);
    AIVERSE_TEST_CHECK(loader.unload(ticket.scene));
    AIVERSE_TEST_CHECK(loader.sharedResourceCount() == 0);
    AIVERSE_TEST_CHECK(loader.residentSceneCount() == 0);
}

int main() {
    checkAssets();
    coordinatorLifecycle();
    cancelledWorker();
    rollbackDuringLoad();
    terminalJobsArePruned();
    throwingReleaseKeepsRefcountsConsistent();
    std::cout << "PASS scene manifest, async jobs, leases, replace, cancel, COW and residency restore\n";
}
