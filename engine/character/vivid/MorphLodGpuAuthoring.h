// VIVID shared full-model CSR for base facial mesh and authored LOD meshes.
// A single GpuActorStream dispatch deforms the existing full ModelVertex
// array, so Renderer3D's already implemented LOD selection remains valid.
#pragma once
#include "SparseMorph.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::vivid {
class MorphLodGpuAuthoring {
public:
    struct Layer {
        const SparseMorphLibrary* morphs=nullptr;
        std::span<const std::uint32_t> modelVertexIds{};
    };
    // Merge multiple authored LODs in one transaction, retaining target
    // order so a single ARKit weight vector drives every mesh stream.
    // Layers may be 0..8; all target vertex IDs must be pairwise disjoint
    // wherever the same morph has a nonzero delta.
    static SparseMorphLibrary combineMany(
        const SparseMorphLibrary& full,std::span<const Layer> layers) {
        if(!full.vertices() || layers.size()>8)
            throw std::invalid_argument("VIVID multi LOD: invalid model/layer budget");
        std::vector<MorphTargetInput> targets;
        targets.reserve(full.targets());
        for(std::size_t target=0;target<full.targets();++target) {
            MorphTargetInput result;
            result.name=std::string(full.targetName(target));
            const auto range=full.gpuRanges()[target];
            auto unpack=[](const GpuMorphDelta& gpu,std::uint32_t vertex) {
                MorphDelta delta;
                delta.vertex=vertex;
                for(int axis=0;axis<3;++axis) {
                    const auto a=std::size_t(axis);
                    delta.position[a]=gpu.position[axis];
                    delta.normal[a]=gpu.normal[axis];
                    delta.tangent[a]=gpu.tangent[axis];
                }
                return delta;
            };
            for(std::uint32_t i=0;i<range.count;++i) {
                const auto& gpu=full.gpuDeltas()[range.first+i];
                result.deltas.push_back(unpack(gpu,gpu.vertex));
            }
            for(const auto& layer:layers) {
                if(!layer.morphs ||
                   layer.morphs->vertices()!=layer.modelVertexIds.size() ||
                   layer.morphs->targets()!=full.targets() ||
                   layer.morphs->targetName(target)!=full.targetName(target))
                    throw std::invalid_argument("VIVID multi LOD: incompatible target layout");
                const auto lodRange=layer.morphs->gpuRanges()[target];
                for(std::uint32_t i=0;i<lodRange.count;++i) {
                    const auto& gpu=layer.morphs->gpuDeltas()[lodRange.first+i];
                    if(gpu.vertex>=layer.modelVertexIds.size())
                        throw std::invalid_argument("VIVID multi LOD: compact vertex out of range");
                    result.deltas.push_back(
                        unpack(gpu,layer.modelVertexIds[gpu.vertex]));
                }
            }
            std::sort(result.deltas.begin(),result.deltas.end(),
                      [](const MorphDelta& a,const MorphDelta& b){
                          return a.vertex<b.vertex;
                      });
            for(std::size_t i=1;i<result.deltas.size();++i)
                if(result.deltas[i-1].vertex==result.deltas[i].vertex)
                    throw std::invalid_argument("VIVID multi LOD: overlapping target deltas");
            targets.push_back(std::move(result));
        }
        for(const auto& layer:layers) {
            if(layer.modelVertexIds.empty() || !layer.morphs)
                throw std::invalid_argument("VIVID multi LOD: empty layer");
            for(std::size_t i=0;i<layer.modelVertexIds.size();++i)
                if(layer.modelVertexIds[i]>=full.vertices() ||
                   (i && layer.modelVertexIds[i]<=layer.modelVertexIds[i-1]))
                    throw std::invalid_argument("VIVID multi LOD: unsorted/out-of-range vertex IDs");
        }
        return SparseMorphLibrary::compile(full.vertices(),std::move(targets));
    }
    // compactLod targets correspond to sorted model-relative target IDs.
    // The names and order must match the original full-model library.
    // Fail closed if LOD indices overlap existing source-shape deltas.
    static SparseMorphLibrary combine(
        const SparseMorphLibrary& full,
        const SparseMorphLibrary& compactLod,
        std::span<const std::uint32_t> targetVertexIds) {
        if(full.vertices()==0 || compactLod.targets()!=full.targets() ||
           compactLod.vertices()!=targetVertexIds.size() ||
           compactLod.vertices()==0)
            throw std::invalid_argument("VIVID LOD GPU: target topology/count mismatch");
        for(std::size_t i=0;i<targetVertexIds.size();++i) {
            if(targetVertexIds[i]>=full.vertices() ||
               (i && targetVertexIds[i]<=targetVertexIds[i-1]))
                throw std::invalid_argument("VIVID LOD GPU: target IDs outside or unordered");
        }
        std::vector<MorphTargetInput> targets;
        targets.reserve(full.targets());
        for(std::size_t target=0;target<full.targets();++target) {
            if(full.targetName(target)!=compactLod.targetName(target))
                throw std::invalid_argument("VIVID LOD GPU: target name/order mismatch");
            MorphTargetInput authoring;
            authoring.name=std::string(full.targetName(target));
            const auto baseRange=full.gpuRanges()[target];
            const auto lodRange=compactLod.gpuRanges()[target];
            authoring.deltas.reserve(std::size_t(baseRange.count)+lodRange.count);
            auto unpack=[&](const GpuMorphDelta& input,std::uint32_t id) {
                MorphDelta out;
                out.vertex=id;
                for(std::size_t a=0;a<3;++a) {
                    out.position[a]=input.position[a];
                    out.normal[a]=input.normal[a];
                    out.tangent[a]=input.tangent[a];
                }
                return out;
            };
            for(std::uint32_t i=0;i<baseRange.count;++i) {
                const auto& gpu=full.gpuDeltas()[baseRange.first+i];
                authoring.deltas.push_back(unpack(gpu,gpu.vertex));
            }
            for(std::uint32_t i=0;i<lodRange.count;++i) {
                const auto& gpu=compactLod.gpuDeltas()[lodRange.first+i];
                if(gpu.vertex>=targetVertexIds.size())
                    throw std::invalid_argument("VIVID LOD GPU: compact vertex outside mapping");
                const auto global=targetVertexIds[gpu.vertex];
                // Source sparse deltas are canonically sorted by vertex ID.
                // Binary search avoids quadratic authoring work for dense LOD.
                const auto existing=std::lower_bound(
                    authoring.deltas.begin(),
                    authoring.deltas.begin()+baseRange.count,global,
                    [](const MorphDelta& delta,std::uint32_t id) {
                        return delta.vertex<id;
                    });
                if(existing!=authoring.deltas.begin()+baseRange.count &&
                   existing->vertex==global)
                    throw std::invalid_argument("VIVID LOD GPU: source and LOD facial vertices overlap");
                authoring.deltas.push_back(unpack(gpu,global));
            }
            targets.push_back(std::move(authoring));
        }
        return SparseMorphLibrary::compile(full.vertices(),std::move(targets));
    }
};
} // namespace aiverse::vivid
