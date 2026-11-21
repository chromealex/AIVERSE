// Stage 101/104 deferred: native persistent CPU queue dedupe,
// bounded waiting requests, invalidated worker result and safe shutdown.
#include "engine/render/AsyncModelCpuQueue.h"
#include <atomic>
#include "../AlwaysCheck.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    AsyncModelCpuQueue queue;
    auto textures=std::make_shared<ModelTextureCache>();
    std::atomic<bool> entered{false},release{false};
    const FileReader absent=[&](const std::string& path,std::string&) {
        if(path=="missing/a.glb" && !release.load()) {
            entered.store(true);
            while(!release.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    queue.setMaxQueued(1);
    AIVERSE_TEST_CHECK(queue.request("missing/a.glb",absent,textures));
    const auto deadline=std::chrono::steady_clock::now()+
                         std::chrono::seconds(5);
    while(!entered && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AIVERSE_TEST_CHECK(entered);
    AIVERSE_TEST_CHECK(queue.contains("missing/a.glb"));
    AIVERSE_TEST_CHECK(!queue.request("missing/a.glb",absent,textures)); // dedupe
    AIVERSE_TEST_CHECK(queue.request("missing/b.glb",absent,textures));  // waiting slot
    AIVERSE_TEST_CHECK(!queue.request("missing/c.glb",absent,textures)); // bounded
    queue.invalidate("missing/a.glb"); // running result becomes stale
    release=true;

    AsyncModelCpuQueue::Prepared prepared;
    bool gotB=false;
    while(std::chrono::steady_clock::now()<deadline) {
        if(queue.pollReady(prepared)) {
            AIVERSE_TEST_CHECK(prepared.source=="missing/b.glb");
            AIVERSE_TEST_CHECK(prepared.model==nullptr);
            gotB=true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AIVERSE_TEST_CHECK(gotB);
    AIVERSE_TEST_CHECK(queue.stats().discarded==1);
    AIVERSE_TEST_CHECK(queue.stats().finished==1);
    AIVERSE_TEST_CHECK(queue.stats().queued==0);
    AIVERSE_TEST_CHECK(!queue.stats().workerActive);

    // The updated source is requested with the latest revision.
    AIVERSE_TEST_CHECK(queue.request("missing/a.glb",absent,textures));
    queue.shutdown();
    AIVERSE_TEST_CHECK(!queue.stats().workerActive && queue.stats().queued==0);
    AIVERSE_TEST_CHECK(queue.stats().liveThreads==0);
}
