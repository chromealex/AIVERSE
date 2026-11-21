// TerraWeave Horizon incremental GPU atlas updates.
//
// Planner is pure CPU and acts only when scene geometry is byte-for-byte
// equivalent at the data-model level. The render owner calls updateTexture()
// through the existing gfx::Device interface. All other edits fall back to
// the ordinary full model upload (no backend fork, no silent geometry drift).
#pragma once

#include "engine/world/TerraWeaveHorizon.h"
#include "engine/render/Model.h"
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace aiverse::terraweave {

struct HorizonAtlasUploadRect {
    uint32_t x = 0, y = 0, width = 0, height = 0;
};

struct HorizonAtlasUploadPlan {
    std::vector<HorizonAtlasUploadRect> regions;
    size_t updatedBytes = 0;
    size_t changedTiles = 0;
};

inline bool horizonCanReuseHorizonLayout(const HorizonBake& oldBake,
                                         const HorizonBake& nextBake) {
    if (oldBake.originXcm != nextBake.originXcm ||
        oldBake.originZcm != nextBake.originZcm ||
        oldBake.sideCm != nextBake.sideCm ||
        oldBake.cameraXcm != nextBake.cameraXcm ||
        oldBake.cameraZcm != nextBake.cameraZcm ||
        oldBake.terrainResolution != nextBake.terrainResolution ||
        oldBake.terrainBeginCm != nextBake.terrainBeginCm ||
        oldBake.terrainSkirtDepthCm != nextBake.terrainSkirtDepthCm ||
        oldBake.innerTerrainLipCm != nextBake.innerTerrainLipCm ||
        oldBake.nearOwnedQuads != nextBake.nearOwnedQuads ||
        oldBake.maxNearClipTriangles != nextBake.maxNearClipTriangles ||
        oldBake.nearClipRects.size() != nextBake.nearClipRects.size() ||
        oldBake.atlasWidth != nextBake.atlasWidth ||
        oldBake.atlasHeight != nextBake.atlasHeight ||
        oldBake.terrainVertices.size() != nextBake.terrainVertices.size() ||
        oldBake.clusters.size() != nextBake.clusters.size())
        return false;
    for(size_t i=0;i<oldBake.nearClipRects.size();++i) {
        const auto& a=oldBake.nearClipRects[i];
        const auto& b=nextBake.nearClipRects[i];
        if(a.minX!=b.minX||a.minZ!=b.minZ||
           a.maxX!=b.maxX||a.maxZ!=b.maxZ)return false;
    }
    for(size_t i=0;i<oldBake.terrainVertices.size();++i) {
        const auto& a=oldBake.terrainVertices[i];
        const auto& b=nextBake.terrainVertices[i];
        if(a.xCm!=b.xCm||a.zCm!=b.zCm)return false;
    }
    for(size_t i=0;i<oldBake.clusters.size();++i) {
        const auto& a=oldBake.clusters[i];
        const auto& b=nextBake.clusters[i];
        if(a.centerXcm!=b.centerXcm||a.centerZcm!=b.centerZcm||
           a.tileSizeCm!=b.tileSizeCm||a.bottomCm!=b.bottomCm||
           a.topCm!=b.topCm||a.atlasX!=b.atlasX||
           a.atlasY!=b.atlasY||a.atlasSideX!=b.atlasSideX||
           a.atlasSideY!=b.atlasSideY||a.atlasSize!=b.atlasSize)
            return false;
        // Coverage is metadata and doesn't change the vertex topology.
    }
    return true;
}

// Old atlas-only API retains strict geometry equivalence; a separate
// terrain-vertex planner may reuse the same index/cluster layout while
// vertex heights, normals and colours change.
inline bool horizonCanReuseModelGeometry(const HorizonBake& oldBake,
                                         const HorizonBake& nextBake) {
    if(!horizonCanReuseHorizonLayout(oldBake,nextBake) ||
       oldBake.maxNearWeldSlopePermille!=nextBake.maxNearWeldSlopePermille ||
       oldBake.nearSeamNormalBlendPercent!=nextBake.nearSeamNormalBlendPercent ||
       oldBake.nearWeldSlopeFallback!=nextBake.nearWeldSlopeFallback ||
       oldBake.nearSeamSamples.size()!=nextBake.nearSeamSamples.size())
        return false;
    // Strict atlas-only uploads must reject ANY change to a height-weld
    // sample or its authored normal. Otherwise the resident vertex data
    // could silently keep stale Y/normals despite an updated CPU bake.
    for(size_t i=0;i<oldBake.nearSeamSamples.size();++i) {
        const auto& a=oldBake.nearSeamSamples[i];
        const auto& b=nextBake.nearSeamSamples[i];
        if(a.xQ16cm!=b.xQ16cm||a.zQ16cm!=b.zQ16cm||
           a.nearHeightCm!=b.nearHeightCm ||
           a.nearSlopeXPermille!=b.nearSlopeXPermille ||
           a.nearSlopeZPermille!=b.nearSlopeZPermille ||
           a.nearNormalValid!=b.nearNormalValid)
            return false;
    }
    for(size_t i=0;i<oldBake.terrainVertices.size();++i) {
        const auto& a=oldBake.terrainVertices[i];
        const auto& b=nextBake.terrainVertices[i];
        if(a.heightCm!=b.heightCm||a.colorRgba!=b.colorRgba||
           a.slopeXPermille!=b.slopeXPermille||
           a.slopeZPermille!=b.slopeZPermille)return false;
    }
    return true;
}

// Returns nullopt if geometry cannot be safely reused or updateTexture
// would upload a large fraction of the atlas / issue too many mip regenerations.
// An empty plan means the current GPU model already contains the correct data.
inline std::optional<HorizonAtlasUploadPlan> planHorizonAtlasUpload(
    const HorizonBake& oldBake, const HorizonBake& nextBake,
    uint32_t atlasTilePixels, size_t maxTextureUpdates=6,
    uint32_t maxUploadPercent=25, bool allowTerrainVertexChanges=false) {
    if(!(allowTerrainVertexChanges ?
         horizonCanReuseHorizonLayout(oldBake,nextBake) :
         horizonCanReuseModelGeometry(oldBake,nextBake)) ||
       atlasTilePixels==0||maxTextureUpdates==0||
       maxTextureUpdates>64||maxUploadPercent==0||
       maxUploadPercent>100||
       oldBake.atlasWidth==0||oldBake.atlasWidth!=oldBake.atlasHeight||
       oldBake.atlasWidth>2048||
       oldBake.atlasWidth%atlasTilePixels ||
       oldBake.atlasHeight%atlasTilePixels ||
       oldBake.atlasRgba.size()!=size_t(oldBake.atlasWidth)*
                                 oldBake.atlasHeight*4 ||
       oldBake.atlasRgba.size()!=nextBake.atlasRgba.size())
        return std::nullopt;

    const uint32_t cols=oldBake.atlasWidth/atlasTilePixels;
    const uint32_t rows=oldBake.atlasHeight/atlasTilePixels;
    // One bit per fixed atlas slot; avoids leaking state across unrelated
    // clusters when some road/biome masks make a tile empty.
    std::vector<uint8_t> dirty(size_t(cols)*rows,0);
    HorizonAtlasUploadPlan plan;
    const size_t stride=size_t(oldBake.atlasWidth)*4;
    for(uint32_t row=0;row<rows;++row)
        for(uint32_t col=0;col<cols;++col) {
            const size_t x=size_t(col)*atlasTilePixels*4;
            bool changed=false;
            for(uint32_t k=0;k<atlasTilePixels;++k) {
                const size_t at=(size_t(row)*atlasTilePixels+k)*stride+x;
                if(!std::equal(oldBake.atlasRgba.begin()+at,
                               oldBake.atlasRgba.begin()+at+
                                   size_t(atlasTilePixels)*4,
                               nextBake.atlasRgba.begin()+at)) {
                    changed=true;
                    break;
                }
            }
            if(changed) {
                dirty[size_t(row)*cols+col]=1;
                ++plan.changedTiles;
            }
        }
    if(!plan.changedTiles)return plan;

    // Horizontal tile runs: at most one update/mipmap regeneration per
    // contiguous row segment. Merge identical adjacent runs vertically.
    for(uint32_t row=0;row<rows;++row) {
        for(uint32_t col=0;col<cols;) {
            if(!dirty[size_t(row)*cols+col]) {++col;continue;}
            const uint32_t first=col;
            while(col<cols&&dirty[size_t(row)*cols+col])++col;
            const uint32_t x=first*atlasTilePixels;
            const uint32_t w=(col-first)*atlasTilePixels;
            const uint32_t y=row*atlasTilePixels;
            bool merged=false;
            for(auto& rect:plan.regions) {
                if(rect.x==x&&rect.width==w&&
                   rect.y+rect.height==y) {
                    rect.height+=atlasTilePixels;
                    merged=true;break;
                }
            }
            if(!merged)
                plan.regions.push_back({x,y,w,atlasTilePixels});
            if(plan.regions.size()>maxTextureUpdates)return std::nullopt;
        }
    }
    for(const auto& r:plan.regions)
        plan.updatedBytes+=size_t(r.width)*r.height*4;
    if(plan.updatedBytes>oldBake.atlasRgba.size()*maxUploadPercent/100)
        return std::nullopt;
    return plan;
}

inline bool applyHorizonAtlasUpload(gfx::Device& device,
                                   render::Model& resident,
                                   const HorizonBake& updated,
                                   const HorizonAtlasUploadPlan& plan) {
    if(resident.images.size()!=1||!resident.images[0].tex.valid()||
       resident.images[0].gpuCompressed||
       resident.images[0].borrowedTexture||resident.images[0].alphaCoverageMipmaps||
       resident.images[0].width!=static_cast<int>(updated.atlasWidth)||
       resident.images[0].height!=static_cast<int>(updated.atlasHeight)||
       updated.atlasRgba.size()!=size_t(updated.atlasWidth)*
                                 updated.atlasHeight*4)
        return false;
    if(plan.regions.empty())return true;
    // The standard RHI implements subimage update and subsequent mip-map
    // regeneration on GL, Metal and Vulkan; no vendor API is used here.
    const int rowPitch=static_cast<int>(updated.atlasWidth)*4;
    for(const auto& r:plan.regions) {
        if(!r.width||!r.height||
           r.x+r.width>updated.atlasWidth||
           r.y+r.height>updated.atlasHeight)return false;
    }
    // Validate *all* regions before writing the first one so a malformed
    // plan cannot partially mutate the resident GPU atlas.
    for(const auto& r:plan.regions) {
        const size_t offset=(size_t(r.y)*updated.atlasWidth+r.x)*4;
        device.updateTexture(resident.images[0].tex,
                             static_cast<int>(r.x),static_cast<int>(r.y),
                             static_cast<int>(r.width),static_cast<int>(r.height),
                             updated.atlasRgba.data()+offset,rowPitch);
    }
    return true;
}

} // namespace aiverse::terraweave
