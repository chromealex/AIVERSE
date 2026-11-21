// First executable native SpectraFX contract checks: presentation-only,
// no RHI device, editor, network, or GPU requirements.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxView.h"
#include "engine/fx/SpectraFxCueInbox.h"
#include "engine/fx/SpectraFxGpuLod.h"
#include "engine/fx/SpectraFxGpuBudgetGovernor.h"
#include "engine/fx/SpectraFxGpuEmitterLibrary.h"
#include "engine/fx/SpectraGpuCueBridge.h"

using namespace aiverse;
using namespace aiverse::fx;

namespace {
int failures=0;
void expect(bool value,const char* test) {
    if(!value) {
        std::fprintf(stderr,"FAILED SpectraFX contract: %s\n",test);
        ++failures;
    }
}
} // namespace

int main() {
    std::string error;
    SpectraFxHybridEmitter::Config emitter;
    expect(SpectraFxGpuEmitterAsset::readText(
        R"JSON({"version":1,"cpuEffect":"smoke","preference":"auto",
                 "particles":128,"allowCpuFallback":true,
                 "appearance":{"additive":true},
                 "spawn":{"burst":8,"rate":20,"life":0.5}})JSON",
        emitter,&error), "valid authored GPU emitter");
    expect(emitter.backend.particles==128,"GPU capacity parsed");
    expect(emitter.appearance.additive,"additive flag parsed");
    expect(!SpectraFxGpuEmitterAsset::readText(
        R"JSON({"version":1,"cpuEffect":"smoke","particles":128,
                 "gpuSortAlpha":true,"gpuIndirectCompaction":true})JSON",
        emitter,&error), "sorting and compaction mutually exclusive");

    // Cached GPU emitter assets are served without file IO on every Play;
    // only an explicit invalidate re-reads (and bumps the revision only
    // when the authored bytes changed).
    {
        int reads=0;
        std::string assetText=R"JSON({"version":1,"cpuEffect":"smoke","particles":64})JSON";
        SpectraFxGpuEmitterLibrary library([&](const std::string& path,std::string& text) {
            if(path!="fx/gpu/spark.gpu.json") return false;
            ++reads;text=assetText;return true;
        });
        const auto first=library.load("spark");
        expect(first.valid && first.reloaded && reads==1,"library loads asset once");
        for(int i=0;i<100;++i) library.load("spark");
        expect(reads==1,"cached library load performs no file IO");
        library.invalidate("spark");
        const auto same=library.load("spark");
        expect(reads==2 && same.valid && !same.reloaded && same.revision==first.revision,
               "invalidate re-reads; unchanged bytes keep revision");
        library.load("spark");
        expect(reads==2,"revalidated asset is cached again");
        assetText=R"JSON({"version":1,"cpuEffect":"smoke","particles":128})JSON";
        library.load("spark");
        expect(reads==2 && library.find("spark")->config.backend.particles==64,
               "edited file is not picked up without invalidate");
        library.invalidateAll();
        const auto edited=library.load("spark");
        expect(reads==3 && edited.reloaded && edited.revision==first.revision+1 &&
               library.find("spark")->config.backend.particles==128,
               "invalidateAll reloads edited asset with a new revision");
        assetText="{broken";
        library.invalidate("spark");
        const auto broken=library.load("spark");
        library.load("spark");
        expect(broken.valid && broken.fallback && reads==4,
               "failed reload keeps last known-good asset without retry storm");
    }

    SpectraFxGpuRouteAsset::Routes routes;
    std::optional<FxQuality> sceneQuality;
    SpectraFxGpuBudgetGovernor::Config budget;
    expect(SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"sceneQuality":"low",
                 "gpuFrameBudgetMs":12,"minGpuSpawnScale":0.25,
                 "routes":[{"eventType":7,"asset":"smoke",
                            "sharedPool":"fire","sharedPoolCapacity":1024}]})JSON",
        routes,&error,&budget,&sceneQuality), "valid project routes");
    expect(routes.count(7)==1 && routes.at(7).playback.sharedPool=="fire",
           "route pool resolved");
    expect(sceneQuality && *sceneQuality==FxQuality::Low,
           "scene quality parsed");
    expect(budget.targetFrameMs==12.0f,"GPU budget parsed");
    const auto preserved=routes;
    expect(!SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"routes":[{"eventType":7,"asset":"smoke"},
                  {"eventType":7,"asset":"smoke2"}]})JSON",
        routes,&error), "duplicate cue rejected");
    expect(routes.size()==preserved.size() && routes.count(7)==1,
           "failed route edit preserves old map");
    expect(!SpectraFxGpuRouteAsset::readText(
        R"JSON({"version":1,"routes":[{"eventType":1,"asset":"smoke",
                 "ownPlayerOnly":false}]})JSON",routes,&error),
           "invisible/unauthorized routes rejected");

    SpectraFxCueInbox inbox(2);
    const auto epoch=inbox.epoch();
    SpectraFxCue play{};
    play.id=7; play.key=5;
    expect(inbox.submit(play,true,epoch),"enqueue keyed play");
    SpectraFxCue move=play;
    move.action=SpectraFxCue::Action::Move;
    move.position={1,2,3};
    expect(inbox.submit(move,true,epoch),"enqueue move");
    move.position={4,5,6};
    expect(inbox.submit(move,true,epoch),"coalesce same-key move");
    expect(inbox.pending()==2 && inbox.stats().coalescedMoves==1,
           "move coalescing preserves capacity");
    SpectraFxCue kill=play;kill.action=SpectraFxCue::Action::Kill;
    expect(inbox.submit(kill,true,epoch),"kill prioritized in full queue");
    expect(inbox.pending()==2 && inbox.stats().evicted==1,
           "full queue evicts a cosmetic cue for Kill");
    int killed=0;
    inbox.drain([&](const SpectraFxCue& cue) {
        if(cue.action==SpectraFxCue::Action::Kill) ++killed;
        return true;
    });
    expect(killed==1,"bounded inbox drains lifecycle action");
    {
        // A queue saturated with other keys' Stop/Kill must still accept a
        // Stop for a looping effect (reserve), and coalesce same-key stops.
        SpectraFxCueInbox lifecycle(2);
        const auto e=lifecycle.epoch();
        SpectraFxCue stop{};stop.id=7;stop.action=SpectraFxCue::Action::Stop;
        stop.key=1; expect(lifecycle.submit(stop,true,e),"stop 1 queued");
        stop.key=2; expect(lifecycle.submit(stop,true,e),"stop 2 queued");
        stop.key=3; expect(lifecycle.submit(stop,true,e),"stop beyond capacity reserved");
        SpectraFxCue killOne=stop;killOne.key=1;killOne.action=SpectraFxCue::Action::Kill;
        expect(lifecycle.submit(killOne,true,e) && lifecycle.stats().coalescedStops==1,
               "same-key Kill coalesces into pending Stop");
        stop.key=4; expect(lifecycle.submit(stop,true,e),"reserve holds up to 2x capacity");
        stop.key=5; expect(!lifecycle.submit(stop,true,e),"lifecycle reserve is bounded");
        SpectraFxCue loop=stop;loop.action=SpectraFxCue::Action::Play;loop.key=9;
        expect(!lifecycle.submit(loop,true,e),"Play never uses the lifecycle reserve");
        int kills=0,stops=0;
        lifecycle.drain([&](const SpectraFxCue& c) {
            if(c.action==SpectraFxCue::Action::Kill && c.key==1) ++kills;
            if(c.action==SpectraFxCue::Action::Stop) ++stops;
            return true;
        });
        expect(kills==1 && stops==3,"upgraded Kill and reserved Stops drained");
    }
    inbox.reset();
    expect(!inbox.submit(play,true,epoch) && inbox.stats().stale==1,
           "old scene generation rejected");
    expect(!inbox.submit(play,false,inbox.epoch()),
           "visibility denial is honored");

    // Camera roll/vertical viewpoint must remain usable without an RTS
    // camera instance or any Editor/FPS module.
    SpectraFxView arbitraryView;
    arbitraryView.eye={0,10,0};
    arbitraryView.forward={0,-1,0};
    arbitraryView.right={1,0,0};
    arbitraryView.up={0,0,-1};
    arbitraryView.projectionYScale=1.75f;
    arbitraryView.width=1920;
    arbitraryView.height=1080;
    expect(arbitraryView.valid(),"genre-neutral vertical cinematic view");
    const auto verticalMatrix=render::mat4::lookAt(
        arbitraryView.eye,{0,0,0},arbitraryView.up);
    expect(verticalMatrix.at(0,0)>0.99f &&
           verticalMatrix.at(1,2)<-0.99f,
           "camera-defined up/right basis survives overhead lookAt");
    arbitraryView.width=0;
    expect(!arbitraryView.valid(),"zero-width view rejected");
    arbitraryView.width=1920;
    arbitraryView.projectionYScale=0;
    expect(!arbitraryView.valid(),"invalid projection scale rejected");

    SpectraFxGpuLodSettings lod;
    lod.nearDistance=10;lod.farDistance=100;
    expect(lod.validate(),"valid camera LOD");
    expect(lod.spawnScale(0)==1.0f && lod.spawnScale(101)==0,
           "distance LOD bounds");

    SpectraFxGpuBudgetGovernor governor;
    SpectraFxGpuBudgetGovernor::Config config;
    config.targetFrameMs=12;
    expect(governor.configure(config),"valid GPU frame-budget config");
    for(int i=0;i<60;++i) governor.observe(24,1.0f/60);
    expect(governor.spawnScale()<1,"over-budget GPU frames lower new-spawn scale");
    expect(governor.spawnScale()>=config.minSpawnScale,
           "spawn governor never goes below configured minimum");
    {
        // GpuCueBridge keeps cues the pool rejected (per-frame burst budget)
        // for the next flush instead of losing them; bounded retries.
        aiverse::spectra::PoolWorldBudgets poolBudgets;
        poolBudgets.maxBurstsPerFrame=2;
        aiverse::spectra::GpuPoolWorld pool;
        expect(pool.init(nullptr,poolBudgets),"headless CPU pool world");
        const Json graph=Json::parse(R"JSON({"version":1,"nodes":[
            {"id":"spawn","type":"Spawn","properties":{"burst":1}},
            {"id":"initialize","type":"Initialize","properties":{"life":1,"speed":0}},
            {"id":"update","type":"Update","properties":{}},
            {"id":"sprite","type":"SpriteOutput","properties":{"texture":"builtin:spark"}}],
            "links":[{"from":"spawn","to":"initialize"},{"from":"initialize","to":"update"},
                     {"from":"update","to":"sprite"}],"outputs":["sprite"]})JSON");
        expect(pool.defineGraph("spark",graph),"pooled CPU effect defined");
        aiverse::spectra::GpuCueBridge bridge(pool);
        expect(bridge.bind(1,"spark"),"cue kind bound");
        std::vector<aiverse::spectra::GpuCueEvent> events(5);
        for(auto& e:events) e.kind=1;
        expect(bridge.accept(1,events) && bridge.stats().queued==5,"five cues queued");
        SpectraFxView poolView;
        poolView.eye={0,5,-5};poolView.forward={0,0,1};poolView.right={1,0,0};
        poolView.up={0,1,0};poolView.projectionYScale=1;poolView.width=64;poolView.height=64;
        uint32_t emitted=0;
        for(int frame=0;frame<3;++frame) {
            expect(pool.beginPresentationFrame(poolView),"pool frame begins");
            emitted+=bridge.flush();
            expect(pool.step(1.0f/60),"pool steps");
            pool.draw();
            pool.endPresentationFrame();
        }
        expect(emitted==5 && bridge.stats().submitted==5 &&
               bridge.stats().rejectedByWorld==0 && bridge.stats().queued==0,
               "budget-rejected cues are emitted on later frames, not lost");
        expect(bridge.stats().deferredByWorld>0,"deferrals are reported");
        // A cue rejected every frame is dropped after bounded attempts.
        expect(bridge.bind(2,"missing_effect"),"bind unknown pooled effect");
        std::vector<aiverse::spectra::GpuCueEvent> bad(1);bad[0].kind=2;
        expect(bridge.accept(2,bad),"bad cue queued");
        for(int frame=0;frame<aiverse::spectra::GpuCueBridge::kMaxFlushAttempts;++frame) {
            expect(pool.beginPresentationFrame(poolView),"pool frame begins");
            bridge.flush();
            pool.step(1.0f/60);pool.endPresentationFrame();
        }
        expect(bridge.stats().queued==0 && bridge.stats().rejectedByWorld==1,
               "permanently rejected cue dropped after bounded retries");
        pool.shutdown();
    }
    if(failures) {
        std::fprintf(stderr,"%d SpectraFX contract failures\n",failures);
        return 1;
    }
    std::puts("SpectraFX native contracts passed");
    return 0;
}
