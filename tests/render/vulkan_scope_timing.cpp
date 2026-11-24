#include "engine/gfx/vulkan/VkScopeTiming.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::gfx::vk;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    ScopeTimingFrame frame;uint32_t q=99;
    check(!frame.begin(0,q)&&!frame.end(q));
    check(frame.begin(7,q)&&q==0&&!frame.begin(8,q));
    check(frame.end(q)&&q==1&&frame.record(0).ended);
    check(frame.begin(9,q)&&q==2);
    frame.abandonActive();check(!frame.record(1).ended&&!frame.active());
    for(uint32_t i=2;i<ScopeTimingFrame::maxScopes;++i){
        check(frame.begin(i+10,q)&&q==2*i);check(frame.end(q)&&q==2*i+1);
    }
    check(!frame.begin(100,q));frame.clear();check(frame.begin(100,q)&&q==0);
    check(std::abs(scopeMilliseconds(250,5,8,1000)-0.011)<1e-12);
    check(std::abs(scopeMilliseconds(UINT64_MAX-4,5,64,1000)-0.01)<1e-12);
    check(scopeMilliseconds(1,1,64,1)==0&&scopeMilliseconds(1,2,0,1)==0);
    check(scopeMilliseconds(1,2,65,1)==0&&scopeMilliseconds(1,2,64,-1)==0);
    std::cout<<"PASS scope pairing, bounds, abandonment, timestamp wrap and units\n";
}
