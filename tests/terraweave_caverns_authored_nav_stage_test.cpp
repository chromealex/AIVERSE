#include "engine/world/TerraWeaveCavernsAuthoredNavStage.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace aiverse::terraweave::caverns;
static std::shared_ptr<const CaveVolumeSnapshot> snapshot(
    uint64_t revision,bool fill) {
    auto s=std::make_shared<CaveVolumeSnapshot>();
    s->sourceRevision=revision;
    s->settings.tunnelChance=0;s->settings.chamberChance=0;
    s->settings.minYcm=-1000;s->settings.maxYcm=-100;
    s->stamps.push_back({1,{50,-500,50},350,CaveStampMode::Carve});
    if(fill)s->stamps.push_back({2,{50,-500,50},400,CaveStampMode::Fill});
    canonicalizeCaveSnapshot(*s);
    return s;
}
int main() {
    CaveNavConfig cfg;
    cfg.width=1;cfg.depth=1;cfg.columnCm=100;
    cfg.originXcm=0;cfg.originZcm=0;cfg.minYcm=-950;cfg.maxYcm=-100;
    cfg.verticalStepCm=25;cfg.agentHeightCm=100;cfg.tileColumns=1;
    auto height=[](int64_t,int64_t){return int64_t(0);};
    CaveAuthoredNavStage nav;
    assert(nav.prepare(snapshot(1,false),cfg,height));
    assert(nav.staged(1));
    assert(nav.active()==nullptr); // staged mesh cannot be queried
    assert(!nav.commit(2));
    assert(nav.commit(1));
    assert(nav.active() && nav.active()->built());
    assert(nav.activeRevision()==1);
    assert(nav.prepare(snapshot(2,true),cfg,height));
    assert(nav.staged(2));
    assert(nav.abort(2));
    assert(nav.activeRevision()==1);
    assert(nav.prepare(snapshot(3,true),cfg,height));
    assert(nav.commit(3));
    assert(nav.activeRevision()==3);
    assert(nav.activeSnapshot() && nav.activeSnapshot()->stamps.size()==2);
    std::cout<<"caverns stage29 authored nav shadow OK\n";
}
