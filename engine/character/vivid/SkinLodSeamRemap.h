// Extend authored LOD correspondence for appended seam-clone vertices.
// Immutable sidecar remains untouched; clones inherit their source vertex
// barycentric sample and are published only on the private candidate model.
#pragma once
#include "MorphLodMapping.h"
#include "SkinSeamSplit.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::vivid {
class SkinLodSeamRemap final {
public:
    static MorphLodMapping expandTarget(
        const MorphLodMapping& original,
        const std::vector<SkinSeamSplit::Clone>& clones,
        std::size_t targetMesh) {
        original.validate();
        if(original.targetVertexIds.empty())
            throw std::invalid_argument("VIVID skin seam LOD: indexed mapping required");
        std::map<std::uint32_t,LodSurfaceSample> samples;
        for(std::size_t i=0;i<original.targetVertexIds.size();++i)
            samples.emplace(original.targetVertexIds[i],original.samples[i]);
        for(const auto& clone:clones) {
            if(clone.mesh!=targetMesh)continue;
            const auto source=samples.find(clone.source);
            if(source==samples.end())
                throw std::invalid_argument(
                    "VIVID skin seam LOD: cloned target has no authored source sample");
            if(!samples.emplace(clone.target,source->second).second)
                throw std::invalid_argument("VIVID skin seam LOD: clone ID collision");
        }
        MorphLodMapping expanded=original;
        expanded.samples.clear();
        expanded.targetVertexIds.clear();
        expanded.samples.reserve(samples.size());
        expanded.targetVertexIds.reserve(samples.size());
        for(const auto& [id,sample]:samples) {
            expanded.targetVertexIds.push_back(id);
            expanded.samples.push_back(sample);
        }
        expanded.validate();
        return expanded;
    }
};
} // namespace aiverse::vivid
