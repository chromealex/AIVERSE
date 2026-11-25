// AIVERSE SpectraFX Stage 26: one immutable compute-frame dispatch plan.
// Metal VM is executed inside Spawn/Integrate phases of SpectraGpuShaders.
// No ECS state, RHI handle, or genre dependency is owned here.
#pragma once
#include <array>
#include <cstdint>

namespace aiverse::spectra::gpu_pipeline {

enum class Phase : uint8_t {
    ResetCounter, Spawn, Integrate, Compact, PublishIndirectArgs, Gather,
    CollectFreeSlots, AllocateSpawn, ReserveFreeSlots, UpdateAliveList, PublishAliveList, RenderAliveList
};
struct Dispatch {
    Phase phase{};
    uint32_t threads=0;
    uint32_t shaderMode=0;
};
struct FramePlan {
    std::array<Dispatch,8> dispatches{};
    uint32_t count=0;
    bool needsCompute() const {return count!=0;}
    bool push(Phase phase,uint32_t threads,uint32_t mode) {
        if(!threads || count>=dispatches.size())return false;
        dispatches[count++]={phase,threads,mode};
        return true;
    }
};

// The caller must compute 'survivors' from its physical slot registry and
// publish a CPU live slot gather map only when gpuIndirect == false.
// Reset must precede atomic compaction; Spawn must precede integration.
// GPU VM Spawn/Update run INSIDE the respective shader modes. Dispatch order
// is RHI command-encoder order, not an independently reordered graph.
inline FramePlan plan(uint32_t capacity,uint32_t births,
                      uint32_t activePrefix,uint32_t survivors,
                      bool gpuIndirect,bool integrate=true) {
    FramePlan p;
    if(!capacity || births>capacity || activePrefix>capacity ||
       survivors>activePrefix)return p; // invalid; caller checks inputs
    if(gpuIndirect)p.push(Phase::ResetCounter,1,3);
    if(births)p.push(Phase::Spawn,births,0);
    // A frozen frame can refresh camera-dependent vertices WITHOUT running
    // update VM (which can mutate registers even with dt=0). Keep the GPU
    // particle revision stable for a fresh asynchronous checkpoint export.
    if(activePrefix && integrate)p.push(Phase::Integrate,activePrefix,1);
    if(gpuIndirect) {
        if(activePrefix)p.push(Phase::Compact,activePrefix,4);
        p.push(Phase::PublishIndirectArgs,1,5);
    } else if(survivors) {
        p.push(Phase::Gather,survivors,2);
    }
    return p;
}
// GPU owns physical slots. Reserve a range from the persistent free stack,
// then allocate independent births. Source kills precede this plan.
inline FramePlan gpuAllocationPlan(uint32_t capacity,uint32_t births,bool integrate){
    FramePlan p;
    if(!capacity||births>capacity)return p;
    // Always publish work count (including no-birth frames) and clear next
    // list counter. Update still compacts when dt is frozen or kills queued.
    (void)integrate; // update flag is supplied in uniforms
    p.push(Phase::ReserveFreeSlots,1,10);
    if(births)p.push(Phase::AllocateSpawn,births,8);
    p.push(Phase::UpdateAliveList,capacity,11);
    p.push(Phase::PublishAliveList,1,13);
    p.push(Phase::RenderAliveList,capacity,12);
    p.push(Phase::PublishIndirectArgs,1,5);
    return p;
}
} // namespace aiverse::spectra::gpu_pipeline
