#include "engine/world/TerraWeaveCaverns.h"
#include <cassert>
#include <stdexcept>
#include <iostream>

using namespace aiverse::terraweave::caverns;
int main() {
    Settings s;
    s.minYcm=-50000; s.maxYcm=0;
    assert(!isVoid(s,{0,2000,0}));
    assert(!isVoid(s,{0,-60000,0}));
    // Same world coordinates in independent generation paths.
    auto a=generateChunk(s,{0,-1,0},16,100);
    auto b=generateChunk(s,{0,-1,0},16,100);
    assert(a.voidMask==b.voidMask);
    for(uint32_t z=0;z<16;++z)
    for(uint32_t y=0;y<16;++y)
    for(uint32_t x=0;x<16;++x) {
        PointCm p{int64_t(x)*100+50,-1600+int64_t(y)*100+50,int64_t(z)*100+50};
        assert(a.at(x,y,z)==isVoid(s,p));
    }
    // Negative coordinates must be floor-divided, not truncated to zero.
    assert(floorDiv(-1,100)==-1);
    assert(floorDiv(-101,100)==-2);
    assert(floorDiv(101,100)==1);
    // Caves can be completely disabled while still using same API.
    s.chamberChance=0; s.tunnelChance=0;
    assert(!isVoid(s,{0,-1000,0}));
    auto empty=generateChunk(s,{-1,-1,-1},4,100);
    for(auto cell:empty.voidMask) assert(cell==0);
    bool failed=false;
    try { auto bad=s; bad.cellCm=0; (void)isVoid(bad,{0,0,0}); }
    catch(const std::invalid_argument&) { failed=true; }
    assert(failed);
    std::cout<<"caverns core contract OK\n";
}
