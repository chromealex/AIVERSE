// VIVID pre-upload palette seam separation with full-model morph replication.
// Source vertex IDs never move. Clones append at the end of the model stream.
// This bounded authoring operation is intentionally independent of all genres.
#pragma once
#include "SkinSeamAnalysis.h"
#include "SparseMorph.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class SkinSeamSplit final {
public:
    struct Clone {std::uint32_t source=0,target=0;std::size_t mesh=0,primitive=0;};
    struct Result {
        std::vector<Clone> clones;
        std::size_t primitives=0;
    };
    // Guarantees no writes to source Model or source facial sparse data
    // until the whole replacement has been compiled successfully.
    static Result apply(render::Model& model,SparseMorphLibrary& morph,
                        std::size_t maxClones=65536) {
        if(model.vb.valid() || model.ib.valid() || model.geometryPoolPage ||
           model.geometry || morph.vertices()!=model.vertices.size())
            throw std::invalid_argument("VIVID seam split: requires private pre-upload Model/morph");
        const auto conflicts=SkinSeamAnalysis::detect(model,maxClones);
        if(conflicts.empty())return {};
        // Stage full Model in scratch. A failed allocation or invalid
        // primitive can never damage the currently visible imported Model.
        auto vertices=model.vertices;
        auto indices=model.indices;
        auto meshes=model.meshes;
        struct Key {
            std::size_t mesh=0,primitive=0;
            bool operator<(const Key& b)const noexcept {
                return mesh!=b.mesh?mesh<b.mesh:primitive<b.primitive;
            }
        };
        std::map<Key,std::vector<std::uint32_t>> sourceByPrimitive;
        for(const auto& conflict:conflicts) {
            const Key key{conflict.otherMesh,conflict.otherPrimitive};
            if(key.mesh>=meshes.size() ||
               key.primitive>=meshes[key.mesh].primitives.size())
                throw std::invalid_argument("VIVID seam split: conflict primitive missing");
            sourceByPrimitive.try_emplace(key);
        }
        // The same MeshDef cannot be rebound to different skin owners
        // through a single global vertex stream. A shared index interval
        // cannot be rewritten for just one primitive either.
        std::vector<std::size_t> owners(model.meshes.size(),0);
        for(const auto& node:model.nodes)
            if(node.mesh>=0 && node.skin>=0) {
                if(std::size_t(node.mesh)>=owners.size())
                    throw std::invalid_argument("VIVID seam split: invalid mesh owner");
                ++owners[std::size_t(node.mesh)];
            }
        for(const auto& [key,unused]:sourceByPrimitive) {
            (void)unused;
            if(owners[key.mesh]!=1)
                throw std::invalid_argument("VIVID seam split: multi-owner skinned mesh");
            const auto& target=meshes[key.mesh].primitives[key.primitive];
            const auto begin=std::uint64_t(target.firstIndex);
            const auto end=begin+target.indexCount;
            for(std::size_t meshIndex=0;meshIndex<meshes.size();++meshIndex)
                for(std::size_t primitiveIndex=0;
                    primitiveIndex<meshes[meshIndex].primitives.size();
                    ++primitiveIndex) {
                    if(meshIndex==key.mesh && primitiveIndex==key.primitive)
                        continue;
                    const auto& other=meshes[meshIndex].primitives[primitiveIndex];
                    const auto otherBegin=std::uint64_t(other.firstIndex);
                    const auto otherEnd=otherBegin+other.indexCount;
                    if(begin<otherEnd && otherBegin<end)
                        throw std::invalid_argument(
                            "VIVID seam split: overlapping primitive index intervals");
                }
        }
        Result result;
        for(auto& [key,unused]:sourceByPrimitive) {
            (void)unused;
            auto& p=meshes[key.mesh].primitives[key.primitive];
            // An already GPU-baked legacy morph texture cannot be safely
            // rewritten in place. CPU morph arrays can be reindexed below.
            if(model.morphTex.valid() || p.morphTargets<0 ||
               p.morphTargets>render::kMaxMorphTargets)
                throw std::invalid_argument(
                    "VIVID seam split: GPU legacy morph atlas is already resident");
            if(p.morphTargets!=0 &&
               (p.vertexCount==0 ||
                std::size_t(p.morphTargets)>std::numeric_limits<std::size_t>::max()/
                    (std::size_t(p.vertexCount)*6) ||
                p.morphDeltas.size()!=
                    std::size_t(p.morphTargets)*p.vertexCount*6))
                throw std::invalid_argument(
                    "VIVID seam split: legacy morph source stride invalid");
            if(!p.morphDeltas.empty() && p.morphTargets==0)
                throw std::invalid_argument(
                    "VIVID seam split: orphan legacy morph deltas");
            const auto first=std::size_t(p.firstIndex);
            if(first>indices.size() ||
               p.indexCount>indices.size()-first ||
               !p.indexCount || vertices.size()>std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("VIVID seam split: invalid primitive range");
            // Clone the ENTIRE primitive into its own contiguous vertex
            // interval. The old source vertex stream is never reindexed.
            std::map<std::uint32_t,std::uint32_t> remap;
            std::vector<std::uint32_t> ordered;
            for(std::size_t k=first;k<first+p.indexCount;++k) {
                const auto source=indices[k];
                if(source>=model.vertices.size())
                    throw std::invalid_argument("VIVID seam split: primitive index out of range");
                ordered.push_back(source);
            }
            std::sort(ordered.begin(),ordered.end());
            ordered.erase(std::unique(ordered.begin(),ordered.end()),ordered.end());
            if(ordered.size()>maxClones-result.clones.size() ||
               ordered.size()>std::numeric_limits<std::uint32_t>::max()-vertices.size())
                throw std::invalid_argument("VIVID seam split: clone budget exceeded");
            // Repack legacy per-primitive deltas as
            // [target][new local vertex][position xyz,normal xyz].
            // Original primitive.firstVertex is the only authoritative
            // local-index origin. Reject noncontiguous legacy references.
            std::vector<float> repackedLegacy;
            if(p.morphTargets>0) {
                repackedLegacy.reserve(std::size_t(p.morphTargets)*
                                       ordered.size()*6);
                const std::uint64_t oldBegin=p.firstVertex;
                const std::uint64_t oldEnd=oldBegin+p.vertexCount;
                for(auto source:ordered)
                    if(source<oldBegin||source>=oldEnd)
                        throw std::invalid_argument(
                            "VIVID seam split: legacy morph references outside vertex interval");
                for(int target=0;target<p.morphTargets;++target)
                    for(auto source:ordered) {
                        const std::size_t from=
                            (std::size_t(target)*p.vertexCount+
                             (source-p.firstVertex))*6;
                        repackedLegacy.insert(repackedLegacy.end(),
                            p.morphDeltas.begin()+std::ptrdiff_t(from),
                            p.morphDeltas.begin()+std::ptrdiff_t(from+6));
                    }
            }
            const auto base=std::uint32_t(vertices.size());
            for(auto source:ordered) {
                const auto target=std::uint32_t(vertices.size());
                vertices.push_back(model.vertices[source]);
                remap.emplace(source,target);
                result.clones.push_back({source,target,key.mesh,key.primitive});
            }
            for(std::size_t k=first;k<first+p.indexCount;++k)
                indices[k]=remap.at(indices[k]);
            p.firstVertex=base;
            p.vertexCount=std::uint32_t(ordered.size());
            if(p.morphTargets>0) {
                p.morphDeltas.swap(repackedLegacy);
                p.morphTexel=0; // GPU upload will assign a new atlas offset
            }
            // The cloned skinned primitive must not retain draw batching
            // state from its old vertex/index interval.
            p.shadowBatched=false;
            ++result.primitives;
        }
        // Precompiled shadow/material batches contain stale primitive
        // ranges after topology edits. Drop their descriptors on the staged
        // mesh, never destroy original model data. Native batch builders
        // deliberately skip skinned meshes, so plain primitive drawing
        // remains authoritative. Old appended batch index ranges may stay
        // in the immutable index stream but are no longer submitted.
        for(const auto& [key,unused]:sourceByPrimitive) {
            (void)unused;
            auto& mesh=meshes[key.mesh];
            mesh.shadowBatch={};
            mesh.materialBatches.clear();
            for(auto& p:mesh.primitives)p.shadowBatched=false;
        }
        // Sparse facial targets are keyed by model-global vertex ID. Clone
        // every nonzero delta on a duplicated source so both primitive
        // halves retain exactly the same facial expression response.
        std::vector<MorphTargetInput> targets;
        targets.reserve(morph.targets());
        for(std::size_t i=0;i<morph.targets();++i) {
            MorphTargetInput target;
            target.name=std::string(morph.targetName(i));
            const auto range=morph.gpuRanges()[i];
            for(std::uint32_t j=0;j<range.count;++j) {
                const auto& gpu=morph.gpuDeltas()[range.first+j];
                MorphDelta delta;
                delta.vertex=gpu.vertex;
                for(int axis=0;axis<3;++axis) {
                    delta.position[axis]=gpu.position[axis];
                    delta.normal[axis]=gpu.normal[axis];
                    delta.tangent[axis]=gpu.tangent[axis];
                }
                target.deltas.push_back(delta);
            }
            const auto original=target.deltas;
            for(const auto& clone:result.clones)
                for(const auto& entry:original)
                    if(entry.vertex==clone.source) {
                        auto copied=entry;
                        copied.vertex=clone.target;
                        target.deltas.push_back(copied);
                    }
            targets.push_back(std::move(target));
        }
        auto expanded=SparseMorphLibrary::compile(vertices.size(),std::move(targets));
        // All may-throw operations completed. The four swaps publish
        // geometry, primitive ranges and morph CSR as one logical update.
        model.vertices.swap(vertices);
        model.indices.swap(indices);
        model.meshes.swap(meshes);
        morph=std::move(expanded);
        return result;
    }
};
} // namespace aiverse::vivid
