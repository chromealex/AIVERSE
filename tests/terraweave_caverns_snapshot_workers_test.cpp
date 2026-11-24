#include "engine/world/TerraWeaveCavernsSnapshotWorkers.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace aiverse::terraweave::caverns;

int main() {
    CaveVolumeSnapshot snapshot;
    snapshot.settings.minYcm=-30000;
    snapshot.settings.maxYcm=-100;
    snapshot.sourceRevision=27;
    snapshot.stamps={
        {20,{0,-3000,0},1200,CaveStampMode::Fill},
        {10,{0,-3000,0},2200,CaveStampMode::Carve}
    };
    canonicalizeCaveSnapshot(snapshot);
    assert(snapshot.stamps[0].id==10);
    // Filling overrides an overlapping carved cavity.
    const PointCm origin{0,-3000,0};
    auto withFill=caveSnapshotFieldCm(snapshot,origin);
    auto noFill=snapshot;
    noFill.stamps.erase(noFill.stamps.begin()+1);
    assert(caveSnapshotFieldCm(noFill,origin)>withFill);
    assert(withFill<0);

    // Repeated sampling and generation from identical immutable input.
    auto a=generateCaveSnapshotMesh(snapshot,{0,-2,0},4,200);
    auto b=generateCaveSnapshotMesh(snapshot,{0,-2,0},4,200);
    assert(a.indices==b.indices);
    assert(a.vertices.size()==b.vertices.size());

    bool rejectsDuplicate=false;
    try {
        auto bad=snapshot;bad.stamps.push_back(bad.stamps[0]);
        canonicalizeCaveSnapshot(bad);
    }catch(const std::invalid_argument&){rejectsDuplicate=true;}
    assert(rejectsDuplicate);

    // A seam mismatch requests one coarse-side rebuild, using the precise
    // immutable snapshot at the source revision declared in that request.
    CaveSeamPublicationGate gate;
    CaveCertifiedChunk coarse;
    coarse.mesh.key={0,0,0};coarse.resolution=2;coarse.spacingCm=200;
    coarse.sourceRevision=27;coarse.renderRevision=1;
    coarse.mesh.vertices={{4,0,0,0,1,0},{4,4,0,0,1,0},{3,0,0,0,1,0}};
    coarse.mesh.indices={0,1,2};
    CaveCertifiedChunk fine;
    fine.mesh.key={1,0,0};fine.resolution=4;fine.spacingCm=100;
    fine.sourceRevision=27;fine.renderRevision=1;
    fine.mesh.vertices={{0,0,0,0,1,0},{0,2,0,0,1,0},{0,4,0,0,1,0},{1,0,0,0,1,0}};
    fine.mesh.indices={0,1,3,1,2,3};
    gate.upsert(coarse);gate.upsert(fine);
    assert(gate.analyze().pendingChunks==1);
    CaveSnapshotRegenerator workers(1);
    auto frozen=std::make_shared<const CaveVolumeSnapshot>(snapshot);
    auto started=workers.pump(gate,frozen,2);
    assert(started.started==1);
    // Wrong snapshot revision cannot launch another pending job.
    auto wrong=std::make_shared<CaveVolumeSnapshot>(snapshot);
    wrong->sourceRevision=99;
    assert(workers.pump(gate,wrong,2).started==0);
    size_t completed=0;
    for(int i=0;i<500&&workers.inFlight();++i){
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        completed+=workers.poll(gate).finished;
    }
    assert(workers.inFlight()==0&&completed==1);
    workers.drain();
    std::cout<<"caverns authored snapshot and async worker contracts OK\n";
}
