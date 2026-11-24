// Parallel-safe presentation animation evaluation over disjoint ECS lanes.
// No worker may invoke Lua/native callbacks or environment contacts.
#pragma once
#include "EcsAnimationSystems.h"
#include "engine/render/AnimationRig.h"
#include "engine/render/FullBodyIK.h"
#include <algorithm>
#include <functional>
#include <thread>
#include <vector>
namespace aiverse::lib::animation {
inline bool workerSafeGraph(const render::AnimationGraph& graph) {
    for(const auto& node:graph.nodes()) {
        switch(node.kind) {
            case render::AnimationGraph::Kind::Native:
            case render::AnimationGraph::Kind::Stack:
                return false; // native/Lua traits and VM are host-bound
            case render::AnimationGraph::Kind::Rig:
                // A pure forward rig (including FullBodyIK without
                // contacts) operates on the owning GraphInstance only.
                // Environmental contact and surface locomotion may call
                // host callbacks and remain on the caller thread.
                if(!node.rig)return false;
                for(const auto& unit:node.rig->units()) {
                    if(unit.locomotion ||
                       (unit.fullBody && unit.fullBody->contacts()))
                        return false;
                }
                break;
            case render::AnimationGraph::Kind::Subgraph:
                if(!node.subgraph || !workerSafeGraph(*node.subgraph))return false;
                break;
            default:break; // compiled clip/blend/IK/controller/motion matching
        }
        if(!node.traits.empty())return false;
    }
    return !graph.hasContacts();
}
inline bool canEvaluateAnimationParallel(const World& world) {
    bool safe=true;
    world.eachExistingConst<AnimationAssetComponent>(
        [&](Entity, const AnimationAssetComponent& asset) {
            if(asset.graph && !workerSafeGraph(*asset.graph))safe=false;
        });
    world.eachExistingConst<AnimationInputsComponent>(
        [&](Entity,const AnimationInputsComponent& in){
            if(in.overrides.environment)safe=false;
        });
    return safe;
}
// Generic host-job adapter. dispatch(laneCount, executeRange) MUST block until
// all lanes complete. This permits use of the engine's own persistent workers.
// It is never invoked if any registered graph contains callbacks/contact rig.
template<class Dispatch>
bool dispatchAnimationPresentation(World& world,float dt,size_t lanes,
                                    Dispatch&& dispatch) {
    if(!std::isfinite(dt) || dt<0.f)return false;
    if(lanes<=1 || !canEvaluateAnimationParallel(world)){
        stepAnimationPresentation(world,dt);return false;
    }
    auto prepared=prepareAnimationPresentation(world);
    if(!prepared.players || !prepared.poses)return false;
    const size_t total=prepared.players->size();
    if(total<64){evaluateAnimationPresentationRange(world,prepared,dt,0,total);return false;}
    lanes=std::min({lanes,total,size_t(8)});
    // Output ranges are disjoint by dense AnimationPlayer indices.
    dispatch(lanes,[&](size_t lane){
        const size_t begin=total*lane/lanes;
        const size_t end=total*(lane+1)/lanes;
        evaluateAnimationPresentationRange(world,prepared,dt,begin,end);
    });
    return true;
}
// Default threads; native projects may instead supply an existing JobSystem
// dispatcher to avoid per-frame thread creation.
inline bool stepAnimationPresentationParallel(World& world,float dt,size_t lanes=4) {
#if defined(__EMSCRIPTEN__)
    (void)lanes;stepAnimationPresentation(world,dt);return false;
#else
    return dispatchAnimationPresentation(world,dt,lanes,
        [](size_t count,const auto& worker){
            // std::thread + join, not std::jthread: Android NDK libc++ has no jthread.
            std::vector<std::thread> jobs;
            jobs.reserve(count>0?count-1:0);
            for(size_t i=1;i<count;++i)
                jobs.emplace_back([&,i]{worker(i);});
            worker(0);
            for(auto& j:jobs)j.join();   // barrier before the renderer reads poses
        });
#endif
}
} // namespace aiverse::lib::animation
