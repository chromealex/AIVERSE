// Conservative local-volume source selection for GPU GI. A huge world may
// contain millions of triangles outside a bounded probe clipmap's possible
// ray reach. Exclude ONLY those provably remote chunks; if the potentially
// intersecting set still exceeds the backend's triangle budget, refuse the
// GPU update. NEVER silently drop a relevant occluder: that leaks light.
//
// The selected immutable scene shares per-chunk BVHs via filteredChunks()
// and remains independent of authoritative ECS/terrain/physics ownership.
// CPU GI can retain the unfiltered snapshot as its exact fallback.
#pragma once
#include "GiGpuBvh.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::render {

struct GiGpuResidencyPlan {
    std::shared_ptr<const GiChunkedGeometrySnapshot> selected;
    size_t sourceChunks=0,sourceTriangles=0;
    size_t relevantChunks=0,relevantTriangles=0;
    size_t excludedRemoteChunks=0,excludedRemoteTriangles=0;
    bool admitted=false,exceededBudget=false;
};

class GiGpuResidencyPlanner {
public:
    static GiGpuResidencyPlan plan(
        const GiChunkedGeometrySnapshot& full,float3 volumeOrigin,
        float spacing,int nx,int ny,int nz,float traceDistance,
        float maximumMovingLightRadius=64,
        size_t maxTriangles=GiGpuBvh::kMaxTriangles) {
        const auto finite=[](float3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        if(!finite(volumeOrigin) || !std::isfinite(spacing) || spacing<=0 ||
           nx<=0 || ny<=0 || nz<=0 ||
           !std::isfinite(traceDistance) || traceDistance<=0 ||
           !std::isfinite(maximumMovingLightRadius) ||
           maximumMovingLightRadius<0 ||
           maxTriangles==0 || maxTriangles>GiGpuBvh::kMaxTriangles)
            throw std::invalid_argument("invalid GPU GI resident clip volume");

        // Any primary ray travels at most traceDistance. Sun/sky secondary
        // shadow rays travel another traceDistance. Point/spot shadow rays
        // extend at most maxMovingLightRadius. One extra cell covers offsets
        // from normal pushback/rounding at the probe volume boundary.
        const float radius=2.0f*traceDistance+
                           maximumMovingLightRadius+spacing;
        const float3 scale{spacing*float(nx),spacing*float(ny),
                           spacing*float(nz)};
        const GiBounds reach{
            volumeOrigin-float3{radius,radius,radius},
            volumeOrigin+scale+float3{radius,radius,radius}
        };
        if(!finite(reach.min) || !finite(reach.max))
            throw std::invalid_argument("GPU GI resident clip volume overflow");

        auto intersects=[&](GiBounds bounds) {
            return bounds.min.x<=reach.max.x && bounds.max.x>=reach.min.x &&
                   bounds.min.y<=reach.max.y && bounds.max.y>=reach.min.y &&
                   bounds.min.z<=reach.max.z && bounds.max.z>=reach.min.z;
        };

        GiGpuResidencyPlan stats;
        stats.sourceChunks=full.chunkCount();
        stats.sourceTriangles=full.triangleCount();
        auto filtered=full.filteredChunks(
            [&](uint64_t,GiBounds bounds,uint32_t triangles) {
                if(intersects(bounds)) {
                    ++stats.relevantChunks;
                    stats.relevantTriangles+=triangles;
                    return true;
                }
                ++stats.excludedRemoteChunks;
                stats.excludedRemoteTriangles+=triangles;
                return false;
            });
        if(stats.relevantTriangles>maxTriangles) {
            // No selected snapshot is published. Caller retains the previous
            // correctly fenced GPU atlas, hides it while dirty and may retry
            // with a coarser authored proxy scene or an exact CPU backend.
            stats.exceededBudget=true;
            return stats;
        }
        stats.selected=std::make_shared<const GiChunkedGeometrySnapshot>(
            std::move(filtered));
        stats.admitted=true;
        return stats;
    }
};

} // namespace aiverse::render
