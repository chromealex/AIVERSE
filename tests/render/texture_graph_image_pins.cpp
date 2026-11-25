#include "engine/render/graph/TextureGraphImagePins.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    GraphImagePin color=std::make_shared<const ProceduralImagePin>();
    GraphImagePin normal=std::make_shared<const ProceduralImagePin>();
    GraphImagePin nextNormal=std::make_shared<const ProceduralImagePin>();
    check(graphImageUnpinned(color)&&!graphImageUnpinned({}));
    auto live=withGraphMaterialPin({},0,color);
    live=withGraphMaterialPin(live,1,normal);
    auto oldSnapshot=live;
    live=withGraphMaterialPin(live,1,nextNormal);
    check(!graphImageUnpinned(color)&&!graphImageUnpinned(normal)&&!graphImageUnpinned(nextNormal));
    check((*oldSnapshot)[1]==normal&&(*live)[1]==nextNormal);
    oldSnapshot.reset();check(graphImageUnpinned(normal)&&!graphImageUnpinned(color));
    auto external=nextNormal;live.reset();
    check(graphImageUnpinned(color)&&!graphImageUnpinned(nextNormal));
    external.reset();check(graphImageUnpinned(nextNormal));
    GraphImageSlotRetirement retirement;check(!retirement.reusable(7,true));
    retirement.epoch=7;retirement.retiredPin=normal;
    check(!retirement.reusable(7,true));normal.reset();
    check(!retirement.reusable(8,true)&&!retirement.reusable(7,false));
    check(retirement.reusable(7,true));
    bool rejected=false;try{(void)withGraphMaterialPin({},6,color);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected);
    // A weak native reference cannot keep a slot alive, and must never
    // silently resolve a new image at the same index after recycling.
    GraphImagePin first=std::make_shared<const ProceduralImagePin>();
    GraphImageReference reference(19,first);
    check(reference.index()==19&&graphImageUnpinned(first));
    check(!reference.acquire(18,first)&&!reference.acquire(19,{}));
    auto lease=reference.acquire(19,first);
    check(lease==first&&!graphImageUnpinned(first));
    lease.reset();check(graphImageUnpinned(first));
    auto replacement=std::make_shared<const ProceduralImagePin>();
    check(!reference.acquire(19,replacement));
    first.reset();
    check(!reference.acquire(19,replacement));
    GraphImageReference fresh(19,replacement);
    check(fresh.acquire(19,replacement)==replacement);
    GraphImageReference empty;check(!empty.acquire(0,replacement));
    // Same raw address with a different control block is not the same
    // generation, even while both control blocks are alive.
    GraphImagePin alias(replacement.get(),[](const ProceduralImagePin*){});
    check(!fresh.acquire(19,alias));
    std::cout<<"PASS immutable material-copy pins, partial replacement, external pin, last-reader release\n";
}
