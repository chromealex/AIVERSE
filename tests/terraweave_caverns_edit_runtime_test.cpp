#include "engine/world/TerraWeaveCavernsEditRuntime.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace aiverse::terraweave::caverns;

int main() {
    auto first=std::make_shared<CaveVolumeSnapshot>();
    first->settings.minYcm=-10000;first->settings.maxYcm=-100;
    first->sourceRevision=17;
    first->stamps.push_back({1,{100,-600,100},200,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(*first);

    CaveEditRuntimeBridge bridge(4,100,16,2);
    const CaveDirtyBounds area{60,-650,60,140,-550,140};
    bridge.invalidate(area,first);
    assert(bridge.dirtyCount()==1);
    const auto firstTick=bridge.tick();
    assert(firstTick.dispatched==1);

    // A second edit during an unfinished worker invalidates its request.
    auto second=std::make_shared<CaveVolumeSnapshot>(*first);
    second->sourceRevision=18;
    second->stamps.push_back({2,{105,-590,105},80,CaveStampMode::Fill});
    canonicalizeCaveSnapshot(*second);
    bridge.invalidate(area,second);
    assert(bridge.dirtyCount()==1);
    bool ready=false;
    for(int i=0;i<500 && !ready;++i){
        bridge.tick();
        ready=bridge.preparedCount()==1 && bridge.inFlight()==0;
        if(!ready)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(ready);
    bool sawCurrent=false;
    assert(!bridge.publishReady([&](const std::vector<CavePreparedEdit>& items) {
        assert(items.size()==1);
        sawCurrent=items[0].candidate.sourceRevision==18;
        return false; // simulated GPU/physics owner rollback
    }));
    assert(sawCurrent);
    assert(bridge.dirtyCount()==1);
    assert(bridge.publishReady([](const std::vector<CavePreparedEdit>& items){
        return items.size()==1 && items.front().candidate.sourceRevision==18 &&
               items.front().candidate.renderRevision>0;
    }));
    assert(bridge.dirtyCount()==0&&bridge.preparedCount()==0);
    bridge.drain();
    std::cout<<"caverns stage22 revisioned edit bridge contract OK\n";
}
