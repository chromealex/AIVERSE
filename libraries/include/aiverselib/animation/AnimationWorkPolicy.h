// MotionWeave work-cost heuristics and deterministic chunk boundaries.
// Only presentation work is scheduled; never modifies simulation state.
#pragma once
#include "engine/render/AnimationGraph.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::lib::animation {
struct AnimationWorkHintComponent {
    float multiplier=1.f;  // rendering-only cost hint, clamped to [0.25,16]
    bool mainThread=false; // host explicitly requires a serial callback lane
};
inline uint32_t estimateAnimationWork(const render::AnimationGraph& graph) {
    const size_t bones=graph.model()?graph.model()->nodes.size():0;
    uint64_t cost=1+bones/8;
    for(const auto& node:graph.nodes()) {
        using K=render::AnimationGraph::Kind;
        switch(node.kind) {
            case K::Clip: cost+=3+bones/16;break;
            case K::Blend:
            case K::Additive: cost+=2+bones/32;break;
            case K::TwoBoneIK: cost+=15;break;
            case K::MotionMatching: cost+=24;break;
            case K::Controller: cost+=12;break;
            case K::Rig:
            case K::Stack: cost+=25;break;
            case K::Native: cost+=8;break;
            case K::Subgraph:
                cost+=node.subgraph?estimateAnimationWork(*node.subgraph):1;
                break;
            default:++cost;break;
        }
    }
    return uint32_t(std::clamp<uint64_t>(cost,1,4096));
}
inline uint32_t scaledAnimationCost(uint32_t cost,float multiplier) {
    const float m=std::isfinite(multiplier)?
        std::clamp(multiplier,.25f,16.f):1.f;
    return uint32_t(std::clamp<float>(std::ceil(float(cost)*m),1.f,65536.f));
}
struct AnimationWorkChunk {
    size_t begin=0,end=0;
    uint64_t estimatedCost=0;
};
// Stable contiguous output ranges. Heavy actors get smaller batches and
// an atomic work queue distributes these batches to available CPU threads.
inline std::vector<AnimationWorkChunk> partitionAnimationWork(
        const std::vector<uint32_t>& cost,size_t preferredChunks,
        size_t minimumActorsPerChunk=1) {
    std::vector<AnimationWorkChunk> chunks;
    if(cost.empty())return chunks;
    preferredChunks=std::clamp<size_t>(preferredChunks,1,cost.size());
    const size_t minActors=std::max<size_t>(1,minimumActorsPerChunk);
    uint64_t total=0;
    for(uint32_t v:cost)total+=std::max<uint32_t>(1,v);
    const uint64_t target=std::max<uint64_t>(1,(total+preferredChunks-1)/preferredChunks);
    size_t begin=0;
    uint64_t accumulated=0;
    for(size_t i=0;i<cost.size();++i) {
        accumulated+=std::max<uint32_t>(1,cost[i]);
        if(i+1-begin>=minActors && accumulated>=target &&
           chunks.size()+1<preferredChunks) {
            chunks.push_back({begin,i+1,accumulated});
            begin=i+1;accumulated=0;
        }
    }
    if(begin<cost.size())chunks.push_back({begin,cost.size(),accumulated});
    return chunks;
}
} // namespace aiverse::lib::animation
