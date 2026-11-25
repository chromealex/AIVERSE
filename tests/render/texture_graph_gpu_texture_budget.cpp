#include "engine/render/graph/TextureGraphGpuTextureBudget.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    constexpr size_t MiB=1024u*1024u;
    constexpr auto small=textureRequestBudget(2,256,2048,32*MiB);
    static_assert(small.textureBytes==512&&small.validationBytes==2048&&small.dagBudget==32*MiB);
    constexpr auto full=textureRequestBudget(6,5592404,4*MiB,32*MiB);
    static_assert(full.textureBytes==33554424&&full.dagBudget==28*MiB+8);
    check(bool(textureRequestBudget(1,16,16,32,33)));
    check(textureRequestBudget(1,16,16,32,33).dagBudget==1);
    check(!textureRequestBudget(1,16,16,32,32));
    check(!textureRequestBudget(0,16,16,32));
    check(!textureRequestBudget(7,16,16,32));
    check(!textureRequestBudget(1,0,16,32));
    check(!textureRequestBudget(1,16,0,32));
    constexpr auto fenceOnly=textureRequestBudget(2,16,0,32,64,true);
    check(fenceOnly.textureBytes==32&&fenceOnly.validationBytes==0&&fenceOnly.dagBudget==32);
    check(!textureRequestBudget(2,16,16,32,64,true));
    check(!textureRequestBudget(2,16,0,32,32,true));
    check(!textureRequestBudget(1,16,16,0));
    check(!textureRequestBudget(6,std::numeric_limits<size_t>::max(),16,32));
    check(!textureRequestBudget(1,16,16,32,maxTextureRequestBytes+1));
    std::cout<<"PASS: texture request cap, all candidates, validation reserve, overflow and exact boundary\n";
}
