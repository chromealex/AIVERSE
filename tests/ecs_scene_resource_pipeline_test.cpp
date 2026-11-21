#include "engine/scene/SceneResourcePipeline.h"
#include "tests/AlwaysCheck.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>

using namespace aiverse;
namespace sc = aiverse::scene;

static const char* kPersistent = R"json({
 "version":1,"id":"scenes/global",
 "dependencies":["render/shared"],"objects":[]})json";
static const char* kVillage = R"json({
 "version":1,"id":"scenes/village",
 "dependencies":["collision/village","render/shared"],"objects":[]})json";
static const char* kLake = R"json({
 "version":1,"id":"scenes/lake",
 "dependencies":["render/shared","water/lake"],"objects":[]})json";
static const char* kUnknown = R"json({
 "version":1,"id":"scenes/unknown",
 "dependencies":["render/not-catalogued"],"objects":[]})json";

template<class F>
static void until(F condition) {
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline) {
        if(condition()) return;
        std::this_thread::yield();
    }
    AIVERSE_TEST_CHECK(false && "timed out waiting for scene resource fixture");
}
static void testPipeline() {
    const auto owner=std::this_thread::get_id();
    std::map<std::string, int> prepared, published, retired;
    sc::SceneResourceBackend resources({
        .stagingBytes=64, .residentBytes=96, .maxConcurrent=2, .maxEntries=64
    });
    auto registerKind = [&](sc::SceneResourceKind kind) {
        sc::SceneResourceHooks hooks;
        hooks.prepare = [&,owner](const std::string& key,std::string&) {
            AIVERSE_TEST_CHECK(std::this_thread::get_id()!=owner);
            // Each request is prepared once in this fixture.
            return std::static_pointer_cast<void>(
                std::make_shared<std::string>(key));
        };
        hooks.publish = [&](const std::string& key,
                            const sc::ScenePreparedResource& payload,
                            std::string&) {
            AIVERSE_TEST_CHECK(std::this_thread::get_id()==owner);
            AIVERSE_TEST_CHECK(*std::static_pointer_cast<std::string>(payload)==key);
            ++published[key];
            return true;
        };
        hooks.retire = [&](const std::string& key) {
            AIVERSE_TEST_CHECK(std::this_thread::get_id()==owner);
            ++retired[key];
        };
        AIVERSE_TEST_CHECK(resources.registerKind(kind,std::move(hooks)));
    };
    registerKind(sc::SceneResourceKind::Render);
    registerKind(sc::SceneResourceKind::Collision);
    registerKind(sc::SceneResourceKind::Water);
    AIVERSE_TEST_CHECK(resources.define("render/shared",
        {sc::SceneResourceKind::Render,32,32,16}));
    AIVERSE_TEST_CHECK(resources.define("collision/village",
        {sc::SceneResourceKind::Collision,32,32,16}));
    AIVERSE_TEST_CHECK(resources.define("water/lake",
        {sc::SceneResourceKind::Water,32,32,16}));
    AIVERSE_TEST_CHECK(!resources.define("../invalid",
        {sc::SceneResourceKind::Render,32,32,16}));
    AIVERSE_TEST_CHECK(!resources.define("render/shared",
        {sc::SceneResourceKind::Render,32,32,16}));

    World w;
    const auto files=std::make_shared<const std::map<std::string,std::string>>(
        std::map<std::string,std::string>{
          {"scenes/global",kPersistent},{"scenes/village",kVillage},
          {"scenes/lake",kLake},{"scenes/unknown",kUnknown}
        });
    sc::SceneLoadCoordinator loader(
        w,
        [files](const std::string& key,std::string& output,std::string& error) {
            const auto it=files->find(key);
            if(it==files->end()) { error="missing manifest";return false; }
            output=it->second;
            return true;
        },
        resources.coordinatorAdapter(),
        {});
    sc::SceneResourcePipeline pipeline(loader,resources);
    sc::SceneRuntime scenes(w);

    const auto global=pipeline.request("scenes/global",sc::LoadMode::Additive,true);
    const auto village=pipeline.request("scenes/village");
    AIVERSE_TEST_CHECK(global.valid()&&village.valid());
    AIVERSE_TEST_CHECK(scenes.record(global.scene)->phase==sc::ScenePhase::Loading);
    AIVERSE_TEST_CHECK(!pipeline.commit(global)); // Must not commit before publication.
    // Bounded publication: one 16-byte estimated upload per frame.
    until([&]{
        pipeline.poll(2,1,16);
        return pipeline.ready(global) && pipeline.ready(village);
    });
    auto stat=resources.status();
    AIVERSE_TEST_CHECK(stat.stagingReserved==0 && stat.residentReserved==64);
    AIVERSE_TEST_CHECK(published["render/shared"]==1);
    AIVERSE_TEST_CHECK(published["collision/village"]==1);
    AIVERSE_TEST_CHECK(pipeline.commit(global));
    AIVERSE_TEST_CHECK(pipeline.commit(village));
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==2);
    AIVERSE_TEST_CHECK(resources.pinCount("render/shared")==0);

    // Invalid resource catalog key rejects the new transition without
    // destroying existing active scenes.
    const auto unknown=pipeline.request("scenes/unknown",sc::LoadMode::Replace);
    until([&]{
        pipeline.poll(2,2,64);
        return !pipeline.error(unknown).empty();
    });
    AIVERSE_TEST_CHECK(!pipeline.commit(unknown));
    AIVERSE_TEST_CHECK(scenes.record(village.scene)!=nullptr);
    AIVERSE_TEST_CHECK(pipeline.cancel(unknown));

    // Replacement keeps the persistent scene, and retains its shared GPU
    // dependency through lease refcounts while collision is retired.
    const auto lake=pipeline.request("scenes/lake",sc::LoadMode::Replace);
    until([&]{
        pipeline.poll(2,1,16);
        return pipeline.ready(lake);
    });
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==2);
    AIVERSE_TEST_CHECK(pipeline.commit(lake));
    AIVERSE_TEST_CHECK(scenes.record(village.scene)==nullptr);
    AIVERSE_TEST_CHECK(scenes.record(global.scene)!=nullptr);
    AIVERSE_TEST_CHECK(retired["collision/village"]==1);
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==2);
    AIVERSE_TEST_CHECK(resources.status().residentReserved==64);

    // Restore a World snapshot: ECS entities/registry are restored, not
    // re-created. Rollback diffs residency: scenes still Active in the
    // restored World keep their leases (no restream); only the scene that
    // the rollback removed releases its private dependency.
    w.enableCowSnapshots(true);
    World before=w;
    const auto villageAgain=pipeline.request("scenes/village");
    until([&]{
        pipeline.poll(2,2,64);
        return pipeline.ready(villageAgain);
    });
    AIVERSE_TEST_CHECK(pipeline.commit(villageAgain));
    AIVERSE_TEST_CHECK(scenes.count()==3);
    AIVERSE_TEST_CHECK(resources.status().residentReserved==96);
    World withVillage=w;
    w.copyFrom(before);
    pipeline.invalidateAfterRestore();
    AIVERSE_TEST_CHECK(resources.status().residentReserved==64);
    AIVERSE_TEST_CHECK(retired["collision/village"]==2);
    AIVERSE_TEST_CHECK(retired["render/shared"]==0);
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(pipeline.queueRestoredResidency().empty());
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==2);

    // Forward restore to a World where village is Active again: only the
    // missing scene gets a residency job; shared render stays published.
    w.copyFrom(withVillage);
    pipeline.invalidateAfterRestore();
    AIVERSE_TEST_CHECK(resources.status().residentReserved==64);
    auto jobs=pipeline.queueRestoredResidency();
    AIVERSE_TEST_CHECK(jobs.size()==1 && jobs[0].scene==villageAgain.scene);
    until([&]{
        pipeline.poll(2,2,64);
        for(const auto& ticket:jobs) if(!pipeline.ready(ticket))return false;
        return true;
    });
    for(const auto& ticket:jobs)
        AIVERSE_TEST_CHECK(pipeline.commit(ticket));
    AIVERSE_TEST_CHECK(published["render/shared"]==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==3);
    AIVERSE_TEST_CHECK(pipeline.unload(villageAgain.scene));
    AIVERSE_TEST_CHECK(retired["collision/village"]==3);
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==2);
    AIVERSE_TEST_CHECK(scenes.count()==2);
    AIVERSE_TEST_CHECK(pipeline.unload(global.scene));
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==1);
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==1);
    AIVERSE_TEST_CHECK(pipeline.unload(lake.scene));
    AIVERSE_TEST_CHECK(loader.resourceRefCount("render/shared")==0);
    AIVERSE_TEST_CHECK(resources.leaseCount("render/shared")==0);
    // Shared render survived both rollbacks: published and retired once.
    AIVERSE_TEST_CHECK(retired["render/shared"]==1);
    AIVERSE_TEST_CHECK(published["render/shared"]==1);
    AIVERSE_TEST_CHECK(resources.status().residentReserved==0);
    AIVERSE_TEST_CHECK(resources.status().stagingReserved==0);
    AIVERSE_TEST_CHECK(resources.status().published==0);
}

static void testBudgetAndLateCancellation() {
    sc::SceneResourceBackend resources({
        .stagingBytes=16, .residentBytes=16, .maxConcurrent=1, .maxEntries=8
    });
    std::promise<void> signal;
    const auto gate=signal.get_future().share();
    std::atomic<int> publishCalls=0;
    sc::SceneResourceHooks hooks;
    hooks.prepare=[gate](const std::string& key,std::string&) {
        gate.wait();
        return std::static_pointer_cast<void>(std::make_shared<std::string>(key));
    };
    hooks.publish=[&](const std::string&,
                      const sc::ScenePreparedResource&,std::string&) {
        ++publishCalls; return true;
    };
    hooks.retire=[](const std::string&){};
    AIVERSE_TEST_CHECK(resources.registerKind(sc::SceneResourceKind::Other,hooks));
    AIVERSE_TEST_CHECK(resources.define("other/a",
        {sc::SceneResourceKind::Other,16,16,16}));
    AIVERSE_TEST_CHECK(resources.define("other/b",
        {sc::SceneResourceKind::Other,16,16,16}));
    std::string error;
    AIVERSE_TEST_CHECK(resources.pin("other/a",&error));
    AIVERSE_TEST_CHECK(resources.pin("other/b",&error));
    resources.poll(2);
    AIVERSE_TEST_CHECK(resources.status().preparing==1);
    AIVERSE_TEST_CHECK(resources.status().pending==1);
    AIVERSE_TEST_CHECK(resources.status().stagingReserved==16);
    // Cancel must be nonblocking even if worker is waiting.
    resources.unpin("other/a");
    signal.set_value();
    until([&] {
        resources.poll(2);
        return resources.phase("other/a")==sc::SceneResourcePhase::Unknown &&
               resources.phase("other/b")==sc::SceneResourcePhase::Prepared;
    });
    AIVERSE_TEST_CHECK(resources.publish(1,8)==0); // publish budget too small
    AIVERSE_TEST_CHECK(resources.phase("other/b")==sc::SceneResourcePhase::Prepared);
    AIVERSE_TEST_CHECK(resources.publish(1,16)==1);
    AIVERSE_TEST_CHECK(publishCalls==1);
    AIVERSE_TEST_CHECK(resources.lease("other/b",error));
    resources.unpin("other/b");
    AIVERSE_TEST_CHECK(resources.status().published==1);
    resources.unlease("other/b");
    AIVERSE_TEST_CHECK(resources.status().residentReserved==0);
    AIVERSE_TEST_CHECK(resources.phase("other/b")==sc::SceneResourcePhase::Unknown);
}

// Review 2026-11-21: an entry cancelled while its worker still runs must be
// resurrected by a new pin (it used to fail forever with an empty error, so
// restored residency could never become ready).
static void testRepinCancelledEntry() {
    sc::SceneResourceBackend resources({
        .stagingBytes=16, .residentBytes=16, .maxConcurrent=1, .maxEntries=8
    });
    std::promise<void> signal;
    const auto gate=signal.get_future().share();
    std::atomic<int> prepareCalls=0;
    sc::SceneResourceHooks hooks;
    hooks.prepare=[gate,&prepareCalls](const std::string& key,std::string&) {
        ++prepareCalls;
        gate.wait();
        return std::static_pointer_cast<void>(std::make_shared<std::string>(key));
    };
    hooks.publish=[](const std::string&,const sc::ScenePreparedResource&,
                     std::string&) { return true; };
    hooks.retire=[](const std::string&){};
    AIVERSE_TEST_CHECK(resources.registerKind(sc::SceneResourceKind::Other,hooks));
    AIVERSE_TEST_CHECK(resources.define("other/a",
        {sc::SceneResourceKind::Other,16,16,16}));
    std::string error;
    AIVERSE_TEST_CHECK(resources.pin("other/a",&error));
    resources.poll(1);
    AIVERSE_TEST_CHECK(resources.phase("other/a")==sc::SceneResourcePhase::Preparing);
    resources.unpin("other/a");
    AIVERSE_TEST_CHECK(resources.phase("other/a")==sc::SceneResourcePhase::Cancelled);
    error.clear();
    AIVERSE_TEST_CHECK(resources.pin("other/a",&error));
    AIVERSE_TEST_CHECK(error.empty());
    AIVERSE_TEST_CHECK(resources.phase("other/a")==sc::SceneResourcePhase::Preparing);
    AIVERSE_TEST_CHECK(resources.status().stagingReserved==16);
    signal.set_value();
    until([&]{
        resources.update(1,1,16);
        return resources.phase("other/a")==sc::SceneResourcePhase::Published;
    });
    AIVERSE_TEST_CHECK(prepareCalls==1); // The running worker was reused.
    AIVERSE_TEST_CHECK(resources.status().stagingReserved==0);
    AIVERSE_TEST_CHECK(resources.status().residentReserved==16);
    resources.unpin("other/a");
    AIVERSE_TEST_CHECK(resources.phase("other/a")==sc::SceneResourcePhase::Unknown);
    AIVERSE_TEST_CHECK(resources.status().residentReserved==0);
}

int main() {
    testPipeline();
    testBudgetAndLateCancellation();
    testRepinCancelledEntry();
    std::cout<<"PASS scene resource pipeline budgets, cross-system leases, replace and rollback\n";
}
