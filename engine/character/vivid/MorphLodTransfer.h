// VIVID explicit source-to-LOD sparse morph transfer.
// Authoring is conservative: a stable source-vertex map is mandatory.
#pragma once
#include "SparseMorph.h"
#include <cstdint>
#include "engine/render/Math3D.h"
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
class MorphLodTransfer {
public:
    // Diagnostic-only O(source * LOD) matcher with a strict 32K budget.\n    // Exact/near-exact vertex correspondence only. Ambiguous matches,
    // generated unrelated vertices and nonfinite positions fail closed.
    static std::vector<std::uint32_t> matchPositions(
            std::span<const render::float3> source,
            std::span<const render::float3> lod,
            float tolerance=1.e-5f) {
        if(source.empty() || lod.empty() ||
           source.size()>32768 || lod.size()>32768 ||
           !std::isfinite(tolerance) || tolerance<=0 ||
           tolerance>0.01f)
            throw std::invalid_argument("VIVID morph LOD: invalid geometric mapping budget");
        std::vector<std::uint32_t> mapping;
        mapping.reserve(lod.size());
        for(const auto& vertex:lod) {
            if(!std::isfinite(vertex.x)||!std::isfinite(vertex.y)||
               !std::isfinite(vertex.z))
                throw std::invalid_argument("VIVID morph LOD: nonfinite output");
            std::uint32_t matched=UINT32_MAX;
            for(std::size_t i=0;i<source.size();++i) {
                const auto p=source[i];
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||
                   !std::isfinite(p.z))
                    throw std::invalid_argument("VIVID morph LOD: nonfinite source");
                const auto delta=vertex-p;
                if(render::dot(delta,delta)>tolerance*tolerance)continue;
                if(matched!=UINT32_MAX)
                    throw std::invalid_argument("VIVID morph LOD: ambiguous vertex correspondence");
                matched=std::uint32_t(i);
            }
            if(matched==UINT32_MAX)
                throw std::invalid_argument("VIVID morph LOD: missing geometric source correspondence");
            mapping.push_back(matched);
        }
        return mapping;
    }
    // Each target LOD vertex references a source vertex. Invalid IDs are
    // rejected instead of applying approximate facial deformation silently.
    // Authoring tools can generate the mapping externally via surface bake.
    static SparseMorphLibrary transfer(
            const SparseMorphLibrary& source,
            std::span<const std::uint32_t> lodToSource) {
        if(lodToSource.empty()||lodToSource.size()>16'000'000)
            throw std::invalid_argument("VIVID morph LOD: invalid output vertex budget");
        std::vector<MorphTargetInput> targets;
        targets.reserve(source.targets());
        for(std::size_t targetIndex=0;targetIndex<source.targets();++targetIndex) {
            MorphTargetInput target;
            target.name=std::string(source.targetName(targetIndex));
            const auto range=source.gpuRanges()[targetIndex];
            std::vector<const GpuMorphDelta*> lookup(source.vertices(),nullptr);
            for(std::uint32_t i=0;i<range.count;++i) {
                const auto& delta=source.gpuDeltas()[range.first+i];
                lookup[delta.vertex]=&delta;
            }
            for(std::size_t v=0;v<lodToSource.size();++v) {
                const auto sourceId=lodToSource[v];
                if(sourceId>=source.vertices())
                    throw std::invalid_argument("VIVID morph LOD: missing source vertex");
                const auto* delta=lookup[sourceId];
                if(!delta)continue;
                MorphDelta d;
                d.vertex=std::uint32_t(v);
                for(std::size_t axis=0;axis<3;++axis) {
                    d.position[axis]=delta->position[axis];
                    d.normal[axis]=delta->normal[axis];
                    d.tangent[axis]=delta->tangent[axis];
                }
                target.deltas.push_back(d);
            }
            targets.push_back(std::move(target));
        }
        return SparseMorphLibrary::compile(lodToSource.size(),std::move(targets));
    }
};
} // namespace aiverse::vivid
