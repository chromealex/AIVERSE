// TerraWeave Horizon: bounded GPU colour-attribute updates for exact distant
// wind from an immutable PHYSIS/game wind lattice. Render-owner thread only.
// Reuses one uploaded forest vertex range, never reallocates atlas/geometry.
#pragma once
#include "engine/render/TerraWeaveVertexWind.h"
#include "engine/world/TerraWeaveWindField.h"
#include "engine/world/TerraWeaveWindUploadPolicy.h"
#include "engine/render/Model.h"
#include "engine/gfx/Gfx.h"
#include "engine/world/TerraWeaveHorizon.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct HorizonForestWindOverlay {
    uint32_t firstVertex=0;
    std::vector<render::ModelVertex> vertices; // contiguous forest-only copy
    std::vector<std::pair<int64_t,int64_t>> centersCm;
    uint64_t appliedRevision=0; // render presentation field revision, not bake
    // Stage38: storage exclusively while INACTIVE in the GPU LRU. The packed
    // byte stream is restored to EXACT ModelVertex bytes before reactivation.
    // Never drop vertices on an active or crossfading GPU model.
    std::vector<uint8_t> packedVertices;
    size_t packedVertexCount=0;
};

inline uint32_t horizonFieldWindVertexColor(
        const ForestWindState& sample,bool canopyTop) {
    const auto signedChannel=[](int32_t v)->uint32_t {
        // v is validated as part of the wind field and remains in +/-5000.
        const int64_t clamped=std::clamp<int64_t>(v,-5000,5000);
        const int64_t quantized=clamped>=0 ?
            (clamped*127+2500)/5000 :
            -((-clamped*127+2500)/5000);
        return static_cast<uint32_t>(128+quantized);
    };
    const uint32_t x=signedChannel(sample.xCmPerSec);
    const uint32_t z=signedChannel(sample.zCmPerSec);
    const uint32_t gust=std::min<uint32_t>(255u,
        (sample.gustPermille*255u+500u)/1000u);
    return (canopyTop ? 0xff000000u : 0u)|
           (gust<<16)|(z<<8)|x;
}

// Build on the CPU from a not-yet-uploaded Horizon model, on the owning
// publication thread. One combined forest primitive, two cards / cluster,
// four vertices / card. Runtime source remains the normal HorizonBake.
inline HorizonForestWindOverlay captureHorizonForestWindOverlay(
        const HorizonBake& bake,const render::Model& prepared) {
    HorizonForestWindOverlay overlay;
    if(bake.clusters.empty() || prepared.meshes.size()!=1)
        return overlay;
    const auto& primitives=prepared.meshes[0].primitives;
    const auto it=std::find_if(primitives.begin(),primitives.end(),
        [](const render::Primitive& p){return p.material==1;});
    if(it==primitives.end())return overlay;
    const size_t vertexCount=bake.clusters.size()*8u;
    constexpr size_t kMaxForestWindBytes=2u*1024u*1024u;
    if(it->vertexCount!=vertexCount ||
       vertexCount>kMaxForestWindBytes/sizeof(render::ModelVertex) ||
       it->firstVertex>prepared.vertices.size() ||
       vertexCount>prepared.vertices.size()-it->firstVertex)
        return overlay;
    overlay.firstVertex=it->firstVertex;
    overlay.vertices.assign(
        prepared.vertices.begin()+it->firstVertex,
        prepared.vertices.begin()+it->firstVertex+vertexCount);
    overlay.centersCm.reserve(bake.clusters.size());
    for(const auto& c:bake.clusters)
        overlay.centersCm.emplace_back(c.centerXcm,c.centerZcm);
    return overlay;
}

struct HorizonForestWindUploadRange {
    uint32_t firstVertex=0;  // relative to the forest-only CPU shadow
    uint32_t vertexCount=0;  // always a whole multiple of 8 card vertices
};

struct HorizonForestWindUploadPlan {
    std::vector<HorizonForestWindUploadRange> ranges;
    size_t changedClusters=0;
    size_t updatedBytes=0; // sum of selected contiguous ranges
    bool fullRangeFallback=false; // too fragmented/dense for sparse writes
};

// Pure planner: compare only RGBA8 wind attributes, since every other vertex
// field is immutable across weather updates. Group consecutive dirty clusters
// (8 vertices each) into at most maxRanges contiguous RHI writes. If the edits
// are too fragmented or cover >=70% of the region, one full-suffix write is
// cheaper than many buffer updates. The absolute 2 MiB shadow cap stays,
// while per-update bandwidth and planning thresholds are host-configurable.
inline std::optional<HorizonForestWindUploadPlan>
planHorizonForestWindUpload(
        std::span<const render::ModelVertex> oldVertices,
        std::span<const render::ModelVertex> newVertices,
        size_t maxRanges=8,uint32_t fullFallbackPercent=70) {
    constexpr size_t verticesPerCluster=8;
    constexpr size_t maxBytes=2u*1024u*1024u;
    const size_t count=oldVertices.size();
    if(count==0 || count!=newVertices.size() ||
       count%verticesPerCluster!=0 ||
       count>maxBytes/sizeof(render::ModelVertex) ||
       maxRanges==0 || maxRanges>8 ||
       fullFallbackPercent==0 || fullFallbackPercent>100)
        return std::nullopt;
    HorizonForestWindUploadPlan result;
    const size_t clusterCount=count/verticesPerCluster;
    for(size_t cluster=0;cluster<clusterCount;++cluster) {
        const size_t first=cluster*verticesPerCluster;
        bool changed=false;
        for(size_t j=0;j<verticesPerCluster;++j) {
            if(oldVertices[first+j].color!=newVertices[first+j].color) {
                changed=true;
                break;
            }
        }
        if(!changed)continue;
        ++result.changedClusters;
        if(!result.ranges.empty() &&
           size_t(result.ranges.back().firstVertex)+
                   result.ranges.back().vertexCount==first) {
            result.ranges.back().vertexCount+=verticesPerCluster;
        } else {
            result.ranges.push_back({
                static_cast<uint32_t>(first),
                static_cast<uint32_t>(verticesPerCluster)});
        }
    }
    if(!result.changedClusters)return result;
    const bool fragmented=result.ranges.size()>maxRanges;
    const bool dense=result.changedClusters*100 >=
                     clusterCount*fullFallbackPercent;
    if(fragmented || dense) {
        result.ranges={{0u,static_cast<uint32_t>(count)}};
        result.fullRangeFallback=true;
        result.updatedBytes=count*sizeof(render::ModelVertex);
    } else {
        for(const auto& r:result.ranges)
            result.updatedBytes+=size_t(r.vertexCount)*
                                 sizeof(render::ModelVertex);
    }
    return result;
}

struct HorizonForestWindPatchResult {
    bool ready=false;        // GPU colour data matches desired revision
    bool uploaded=false;     // true iff one or more RHI writes were issued
    size_t bytes=0;          // exact total bytes sent to RHI this update
    size_t ranges=0;         // number of RHI updateBuffer calls
    size_t changedClusters=0;
    bool fullRangeFallback=false;
};

// Single-region atomic *plan* and all-range preflight. The device update
// function returns void, so device loss remains the caller's responsibility;
// on the normal RHI path revision advances only after every planned write.
// No terrain vertices, atlas pixels, indices or topology are changed.
inline HorizonForestWindPatchResult applyHorizonForestWindOverlay(
        gfx::Device& device,render::Model& resident,
        HorizonForestWindOverlay& overlay,
        const ForestWindField* field,
        const ForestWindState& fallback,
        uint64_t desiredRevision,
        size_t remainingUploadBytes,
        const HorizonWindUploadPolicy& policy={}) {
    validateHorizonWindUploadPolicy(policy);
    if(overlay.appliedRevision==desiredRevision)
        return {true,false,0};
    const size_t count=overlay.vertices.size();
    const size_t bytes=count*sizeof(render::ModelVertex);
    if(count==0 || count!=overlay.centersCm.size()*8u ||
       !resident.vb.valid() || !resident.ib.valid() ||
       resident.geometryPoolPage || resident.gpuBaseVertex!=0 ||
       resident.gpuFirstIndex!=0 || resident.batching.allowGeometryPool ||
       !device.canUpdateSharedBuffers() ||
       bytes>2u*1024u*1024u ||
       resident.meshes.size()!=1)
        return {};
    const auto& primitives=resident.meshes[0].primitives;
    const auto forest=std::find_if(primitives.begin(),primitives.end(),
        [](const render::Primitive& p){return p.material==1;});
    if(forest==primitives.end() ||
       forest->firstVertex!=overlay.firstVertex ||
       forest->vertexCount!=count)
        return {};

    std::vector<render::ModelVertex> replacement=overlay.vertices;
    for(size_t i=0;i<overlay.centersCm.size();++i){
        const auto [x,z]=overlay.centersCm[i];
        const ForestWindState local=sampleForestWindField(
            field,x,z,fallback);
        for(size_t j=0;j<8u;++j){
            const size_t index=i*8u+j;
            replacement[index].color=field ?
                horizonFieldWindVertexColor(local,(j%4u)>=2u) :
                horizonSpatialWindVertexColor(
                    x,z,0x544552524157494eULL,(j%4u)>=2u);
        }
    }
    const auto plan=planHorizonForestWindUpload(
        overlay.vertices,replacement,policy.maxSparseRanges,
        policy.fullFallbackPercent);
    if(!plan || plan->updatedBytes>remainingUploadBytes ||
       plan->updatedBytes>policy.maxBytesPerUpdate)
        return {};
    // All ranges have already been bounded/validated by the planner; check
    // absolute byte offsets before performing the first RHI write.
    for(const auto& range:plan->ranges) {
        if(!range.vertexCount || range.firstVertex>count ||
           range.vertexCount>count-range.firstVertex ||
           (range.firstVertex%8)!=0 ||
           (range.vertexCount%8)!=0)
            return {};
        const size_t absoluteVertex=size_t(overlay.firstVertex)+
                                    range.firstVertex;
        if(absoluteVertex>std::numeric_limits<size_t>::max()/
                          sizeof(render::ModelVertex))
            return {};
    }
    for(const auto& range:plan->ranges) {
        const size_t absoluteVertex=size_t(overlay.firstVertex)+
                                    range.firstVertex;
        device.updateBuffer(
            resident.vb,replacement.data()+range.firstVertex,
            size_t(range.vertexCount)*sizeof(render::ModelVertex),
            absoluteVertex*sizeof(render::ModelVertex));
    }
    overlay.vertices.swap(replacement);
    overlay.appliedRevision=desiredRevision;
    return {true,!plan->ranges.empty(),plan->updatedBytes,
            plan->ranges.size(),plan->changedClusters,
            plan->fullRangeFallback};
}

} // namespace aiverse::terraweave
