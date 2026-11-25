#include "engine/world/TerraWeaveCavernsEditRuntime.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveEditRuntimeBridge bridge(4,100,16,1);
    auto first=std::make_shared<CaveVolumeSnapshot>();
    first->sourceRevision=21;
    first->settings.chamberChance=0;
    first->settings.tunnelChance=0;
    first->settings.minYcm=-1000;
    first->settings.maxYcm=-100;
    first->stamps.push_back({1,{50,-500,50},250,CaveStampMode::Carve});
    canonicalizeCaveSnapshot(*first);
    bridge.invalidate({0,-800,0,100,-200,100},first);
    for(size_t i=0;i<500 && bridge.preparedCount()!=bridge.dirtyCount();++i){
        bridge.tick(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    bridge.tick(2);
    auto visible=bridge.visualPreviewCandidates();
    assert(!visible.empty());
    assert(bridge.dirtyCount()>0); // a preview NEVER ACKs gameplay edits
    assert(visible.front().candidate.sourceRevision==21);
    auto second=std::make_shared<CaveVolumeSnapshot>(*first);
    second->sourceRevision=22;
    second->stamps.push_back({2,{50,-500,50},350,CaveStampMode::Fill});
    canonicalizeCaveSnapshot(*second);
    bridge.invalidate({0,-800,0,100,-200,100},second);
    assert(bridge.visualPreviewCandidates().empty()); // stale mesh hidden
    bridge.drain();
    std::cout<<"caverns stage31 visual-only preview does not ACK gameplay dirty state\n";
}
