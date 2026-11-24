#include "engine/render/graph/TextureGraphGpuStepState.h"
#include <cstdlib>
#include <iostream>
using aiverse::render::texture_graph::gpu::StepFence;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    StepFence f;
    check(!f.busy()&&!f.submit(0)&&!f.retire(99));
    check(f.submit(8)&&f.busy());
    check(!f.submit(9)&&!f.retire(0)&&!f.retire(7)&&f.busy());
    check(f.retire(8)&&!f.busy()&&!f.retire(8));
    check(f.submit(10)&&!f.retire(9)&&f.retire(11));
    check(f.submit(12));f.abandon();check(!f.busy());
    check(f.submit(13)&&!f.retire(12)&&f.retire(13));
    std::cout<<"PASS: nonzero submission, ordered retirement, no overwrite, device abandon\n";
}
