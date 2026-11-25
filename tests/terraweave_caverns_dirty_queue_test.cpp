#include "engine/world/TerraWeaveCavernsDirtyQueue.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
int main(){
    CaveDirtyQueue queue(64);
    CaveSphereStamp stamp{1,{-20,-20,-20},100,CaveStampMode::Carve};
    const auto bounds=dirtyBoundsForStamp(stamp,20);
    queue.enqueue(bounds,4,100,10);
    auto first=queue.pending(64);
    assert(!first.empty());
    bool negative=false,positive=false;
    for(const auto& d:first){
        negative|=d.key.x<0;
        positive|=d.key.x>=0;
        assert(d.sourceRevision==10);
    }
    assert(negative && positive);
    const size_t count=queue.size();
    queue.enqueue(bounds,4,100,11);
    assert(queue.size()==count); // coalesced per-chunk
    assert(!queue.acknowledge(first.front())); // stale generation
    auto next=queue.pending(64);
    assert(!next.empty() && next.front().sourceRevision==11);
    assert(queue.acknowledge(next.front()));
    assert(queue.size()==count-1);
    bool rejected=false;
    try{queue.enqueue(bounds,4,100,0);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    std::cout<<"caverns stage21 dirty queue OK\n";
}
