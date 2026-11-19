#include <aiverselib/animation/AnimationBackendPolicy.h>
#include <cassert>
using namespace aiverse::lib::animation;
int main(){
    AnimationBackendOptions opt;
    opt.gpuAvailable=true;
    opt.visibleActors=3;
    auto cpu=selectAnimationBackend(AnimationBackendPreference::Auto,
        AnimationBackend::CpuGraph,opt);
    assert(cpu==AnimationBackend::CpuGraph);
    opt.visibleActors=24;
    auto gpu=selectAnimationBackend(AnimationBackendPreference::Auto,cpu,opt);
    assert(gpu==AnimationBackend::GpuBaked);
    opt.visibleActors=14;
    assert(selectAnimationBackend(AnimationBackendPreference::Auto,gpu,opt)==gpu);
    opt.visibleActors=12;
    assert(selectAnimationBackend(AnimationBackendPreference::Auto,gpu,opt)==cpu);
    opt.visibleActors=50;opt.requiresCustomGraph=true;
    assert(selectAnimationBackend(AnimationBackendPreference::GpuBaked,gpu,opt)==cpu);
    opt.requiresCustomGraph=false;opt.gpuAvailable=false;
    assert(selectAnimationBackend(AnimationBackendPreference::GpuBaked,gpu,opt)==cpu);
    assert(parseAnimationBackend("auto")==AnimationBackendPreference::Auto);
    assert(parseAnimationBackend("cpu")==AnimationBackendPreference::CpuGraph);
    assert(parseAnimationBackend("gpu")==AnimationBackendPreference::GpuBaked);
}
