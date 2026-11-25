#include "engine/render/TerraWeaveCavernsPreviewPacer.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace aiverse::terraweave::caverns;
int main(){
    CavePreviewPacer pacer(5);
    pacer.reset(8,3);
    assert(pacer.mayStage(8,11));
    pacer.staged(11);
    assert(!pacer.mayStage(8,11)); // one upload per device frame
    assert(pacer.mayStage(8,12));
    pacer.failed(12);
    assert(pacer.failures()==1);
    assert(pacer.retryAfterFrame()==17);
    assert(!pacer.mayStage(8,16));
    assert(pacer.mayStage(8,17));
    pacer.staged(17);
    assert(pacer.failures()==0);
    assert(pacer.mayStage(8,18));
    pacer.staged(18);
    assert(pacer.ready(8));
    assert(!pacer.mayStage(8,19));
    pacer.reset(9,1);
    assert(!pacer.ready(8));
    assert(pacer.remaining()==1);
    bool rejected=false;
    try{CavePreviewPacer invalid(0);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    std::cout<<"caverns stage32 preview pacer PASS\n";
}
