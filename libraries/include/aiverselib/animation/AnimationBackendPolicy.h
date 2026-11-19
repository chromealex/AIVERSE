// MotionWeave runtime backend selection. Does not touch the game simulation.
#pragma once
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string_view>
namespace aiverse::lib::animation {
enum class AnimationBackend { CpuGraph, GpuBaked };
enum class AnimationBackendPreference { Auto, CpuGraph, GpuBaked };
inline AnimationBackendPreference parseAnimationBackend(std::string_view value) {
    if(value=="auto")return AnimationBackendPreference::Auto;
    if(value=="cpu" || value=="graph")return AnimationBackendPreference::CpuGraph;
    if(value=="gpu" || value=="baked")return AnimationBackendPreference::GpuBaked;
    throw std::invalid_argument("MotionWeave: animationMode must be auto, cpu or gpu");
}
struct AnimationBackendOptions {
    size_t visibleActors=0;
    size_t gpuEnterActors=24;
    size_t gpuExitActors=12;
    bool gpuAvailable=false;
    bool requiresCustomGraph=false;
};
inline AnimationBackend selectAnimationBackend(AnimationBackendPreference pref,
        AnimationBackend old,AnimationBackendOptions options={}) {
    if(!options.gpuAvailable || options.requiresCustomGraph ||
       pref==AnimationBackendPreference::CpuGraph)
        return AnimationBackend::CpuGraph;
    if(pref==AnimationBackendPreference::GpuBaked)
        return AnimationBackend::GpuBaked;
    // Hysteresis avoids changing execution backend on transient joins/leaves.
    const size_t enter=std::max<size_t>(1,options.gpuEnterActors);
    const size_t leave=std::min(options.gpuExitActors,enter-1);
    if(old==AnimationBackend::GpuBaked)
        return options.visibleActors>leave?
            AnimationBackend::GpuBaked:AnimationBackend::CpuGraph;
    return options.visibleActors>=enter?
        AnimationBackend::GpuBaked:AnimationBackend::CpuGraph;
}
} // namespace aiverse::lib::animation
