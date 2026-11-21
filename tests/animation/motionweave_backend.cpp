#include <aiverselib/animation/AnimationBackendPolicy.h>
#include "../AlwaysCheck.h"
using namespace aiverse::lib::animation;
int main(){
    AnimationBackendOptions opt;
    opt.gpuAvailable=true;
    opt.visibleActors=3;
    auto cpu=selectAnimationBackend(AnimationBackendPreference::Auto,
        AnimationBackend::CpuGraph,opt);
    AIVERSE_TEST_CHECK(cpu==AnimationBackend::CpuGraph);
    opt.visibleActors=24;
    auto gpu=selectAnimationBackend(AnimationBackendPreference::Auto,cpu,opt);
    AIVERSE_TEST_CHECK(gpu==AnimationBackend::GpuBaked);
    opt.visibleActors=14;
    AIVERSE_TEST_CHECK(selectAnimationBackend(AnimationBackendPreference::Auto,gpu,opt)==gpu);
    opt.visibleActors=12;
    AIVERSE_TEST_CHECK(selectAnimationBackend(AnimationBackendPreference::Auto,gpu,opt)==cpu);
    opt.visibleActors=50;opt.requiresCustomGraph=true;
    AIVERSE_TEST_CHECK(selectAnimationBackend(AnimationBackendPreference::GpuBaked,gpu,opt)==cpu);
    opt.requiresCustomGraph=false;opt.gpuAvailable=false;
    AIVERSE_TEST_CHECK(selectAnimationBackend(AnimationBackendPreference::GpuBaked,gpu,opt)==cpu);
    AIVERSE_TEST_CHECK(parseAnimationBackend("auto")==AnimationBackendPreference::Auto);
    AIVERSE_TEST_CHECK(parseAnimationBackend("cpu")==AnimationBackendPreference::CpuGraph);
    AIVERSE_TEST_CHECK(parseAnimationBackend("gpu")==AnimationBackendPreference::GpuBaked);
}
