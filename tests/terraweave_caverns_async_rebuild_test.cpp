#include "engine/world/TerraWeaveCavernsAsyncRebuild.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace aiverse::terraweave::caverns;

static CaveCertifiedChunk example(ChunkKey3 key,bool fine) {
    CaveCertifiedChunk c;
    c.mesh.key=key;c.resolution=fine?4:2;
    c.spacingCm=fine?100:200;
    c.sourceRevision=77;
    c.renderRevision=1;
    if(key.x==0) {
        c.mesh.vertices={{4,0,0,0,1,0},{4,4,0,0,1,0},{3,0,0,0,1,0}};
        c.mesh.indices={0,1,2};
    } else {
        c.mesh.vertices={{0,0,0,0,1,0},{0,2,1,0,1,0},
                         {0,4,0,0,1,0},{1,0,0,0,1,0}};
        c.mesh.indices={0,1,3,1,2,3};
    }
    return c;
}
int main() {
    CaveSeamPublicationGate gate;
    gate.upsert(example({0,0,0},false));
    gate.upsert(example({1,0,0},true));
    auto status=gate.analyze();
    assert(status.pendingChunks==2);
    assert(!gate.canPublish({0,0,0},1));

    CaveAsyncRegenerator workers(1);
    Settings settings;
    settings.cellCm=2000;
    settings.tunnelRadiusCm=400;
    settings.chamberRadiusCm=800;
    const auto start=workers.pump(gate,settings,2);
    assert(start.launched==1 && workers.inFlight()==1);
    size_t accepted=0;
    for(int attempt=0;attempt<300 && accepted<2;++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto polled=workers.pump(gate,settings,2);
        assert(polled.failed==0 && polled.stale==0 && polled.inFlight<=1);
        accepted+=polled.accepted;
    }
    assert(workers.inFlight()==0);
    assert(accepted==2 && gate.analyze().pendingChunks==0);
    workers.drain();
    std::cout<<"caverns stage17 async dispatch contract OK\n";
}
