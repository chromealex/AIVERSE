// Stage 103 source regression (execution deferred): multi-worker imports,
// source revisions, bounded in-flight work and background non-starvation.
#include "engine/render/AsyncModelCpuQueue.h"
#include <algorithm>
#include <atomic>
#include "../AlwaysCheck.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

int main() {
    using namespace aiverse::render;
    AsyncModelCpuQueue queue;
    queue.setWorkerCount(2);
    queue.setMaxQueued(12);
    auto textures=std::make_shared<ModelTextureCache>();
    std::atomic<int> running{0},peak{0};
    FileReader reader=[&](const std::string&,std::string&)->bool {
        const int active=++running;
        int before=peak.load();
        while(active>before && !peak.compare_exchange_weak(before,active)){}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        --running;
        return false; // missing models still produce a valid null result
    };
    AIVERSE_TEST_CHECK(queue.workerCount()==2);
    AIVERSE_TEST_CHECK(queue.request("busy0.glb",reader,textures));
    AIVERSE_TEST_CHECK(queue.request("busy1.glb",reader,textures));
    const auto startDeadline=std::chrono::steady_clock::now()+
                             std::chrono::seconds(2);
    while(queue.stats().activeWorkers<2 &&
          std::chrono::steady_clock::now()<startDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AIVERSE_TEST_CHECK(queue.stats().activeWorkers==2);
    AIVERSE_TEST_CHECK(queue.request("background.glb",reader,textures,
                         ModelLoadPriority::Background));
    AIVERSE_TEST_CHECK(queue.request("interactive.glb",reader,textures,
                         ModelLoadPriority::Interactive));
    AIVERSE_TEST_CHECK(queue.request("stale.glb",reader,textures,
                         ModelLoadPriority::Visible));
    queue.invalidate("stale.glb");
    AIVERSE_TEST_CHECK(queue.request("stale.glb",reader,textures,
                         ModelLoadPriority::Visible));
    std::vector<std::string> done;
    const auto deadline=std::chrono::steady_clock::now()+
                         std::chrono::seconds(4);
    while(done.size()<5 && std::chrono::steady_clock::now()<deadline) {
        AsyncModelCpuQueue::Prepared model;
        if(queue.pollReady(model))done.push_back(model.source);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AIVERSE_TEST_CHECK(done.size()==5);
    AIVERSE_TEST_CHECK(std::count(done.begin(),done.end(),"stale.glb")==1);
    AIVERSE_TEST_CHECK(std::count(done.begin(),done.end(),"background.glb")==1);
    AIVERSE_TEST_CHECK(std::count(done.begin(),done.end(),"interactive.glb")==1);
    AIVERSE_TEST_CHECK(peak.load()<=2);
    AIVERSE_TEST_CHECK(peak.load()>=1);
    AIVERSE_TEST_CHECK(queue.stats().finished==5);
    AIVERSE_TEST_CHECK(queue.stats().queued==0);
    AIVERSE_TEST_CHECK(queue.stats().activeWorkers==0);
    queue.setWorkerCount(0); // clamped to one
    AIVERSE_TEST_CHECK(queue.workerCount()==1);
    queue.setWorkerCount(100); // clamped to eight
    AIVERSE_TEST_CHECK(queue.workerCount()==8);
    queue.shutdown();
}
