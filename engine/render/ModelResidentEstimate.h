// Conservative source-derived estimates, NOT driver-accounted VRAM.
// Never charge shared GPU pool pages or borrowed shared texture leases to a
// model; those allocations have independent owners and their size is unknown.
#pragma once
#include "Model.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace aiverse::render {

struct ModelResidentEstimate {
    uint64_t dedicatedGeometryBytes=0;
    uint64_t ownedImageBytes=0;
    uint64_t poseTextureBytes=0;
    size_t sharedGeometryModels=0;
    size_t borrowedImages=0;
    uint64_t knownBytes() const noexcept {
        const auto max=std::numeric_limits<uint64_t>::max();
        const uint64_t sum=ownedImageBytes>max-dedicatedGeometryBytes
            ?max:dedicatedGeometryBytes+ownedImageBytes;
        return poseTextureBytes>max-sum?max:sum+poseTextureBytes;
    }
};

// Width/height/mesh declarations survive CPU upload even when raw vertex
// and index arrays have been discarded. This may overestimate overlapping
// primitives or underestimate implementation-defined GPU padding.
inline ModelResidentEstimate estimateModelResident(const Model& model) {
    ModelResidentEstimate out;
    auto add=[](uint64_t& total,uint64_t amount) {
        total=amount>std::numeric_limits<uint64_t>::max()-total
            ?std::numeric_limits<uint64_t>::max():total+amount;
    };
    if(model.geometryPoolPage)out.sharedGeometryModels=1;
    else {
        uint64_t vertices=model.vertices.size();
        uint64_t indices=model.indices.size();
        if(vertices==0 || indices==0) {
            uint64_t authoredVertices=0,authoredIndices=0;
            for(const auto& mesh:model.meshes)
                for(const auto& primitive:mesh.primitives) {
                    add(authoredVertices,primitive.vertexCount);
                    add(authoredIndices,primitive.indexCount);
                }
            if(vertices==0)vertices=authoredVertices;
            if(indices==0)indices=authoredIndices;
        }
        // These are format/layout estimates, not driver allocation sizes.
        if(model.vb.valid())add(out.dedicatedGeometryBytes,
            vertices*sizeof(ModelVertex));
        if(model.ib.valid())add(out.dedicatedGeometryBytes,
            indices*sizeof(uint32_t));
    }
    for(const auto& image:model.images) {
        if(!image.tex.valid())continue;
        if(image.borrowedTexture){++out.borrowedImages;continue;}
        if(image.width<=0 || image.height<=0)continue;
        const uint64_t pixels=uint64_t(image.width)*uint64_t(image.height);
        // Compressed/image-specific GPU format and mip layout are backend
        // dependent; treat RGBA8 base mip only as a comparable lower estimate.
        add(out.ownedImageBytes,pixels*4u);
    }
    if(model.baked.tex.valid() && model.baked.joints>0 && model.baked.rows>0)
        add(out.poseTextureBytes,
            uint64_t(model.baked.joints)*3u*
            uint64_t(model.baked.rows)*16u); // RGBA32F matrix rows
    // Morph textures lack an authoritative height in Model metadata, so
    // they are intentionally excluded rather than fabricated.
    return out;
}

} // namespace aiverse::render
