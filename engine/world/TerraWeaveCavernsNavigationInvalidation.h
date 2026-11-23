#pragma once
// Stage41: coordinate projection of immutable cave dirty bounds into each
// registered solver's cache. Used by grid A*, HPA* clusters, flow fields,
// hybrid, custom graph backends and existing native nav::Mesh tile invalidation.
#include "engine/world/TerraWeaveCavernsNavigationAdapters.h"
#include "engine/world/TerraWeaveCavernsNavigationPublication.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace aiverse::terraweave::caverns {
struct CaveGraphDirtyRange {
    CaveGraphCell min{},max{}; // INCLUSIVE
};
inline std::optional<CaveGraphDirtyRange> caveGraphDirtyRange(
    CaveDirtyBounds b,const CaveGraphGrid& grid,int32_t haloCells=1) {
    if(haloCells<0||haloCells>8||grid.cellCm<1||!grid.width||
       !grid.height||!grid.depth)
        throw std::invalid_argument("Caverns graph invalidation projection");
    if(b.maxX<=b.minX||b.maxY<=b.minY||b.maxZ<=b.minZ)
        return {};
    const auto project=[&](int64_t lower,int64_t upper,int64_t origin,
                           uint32_t size,int32_t& lo,int32_t& hi) {
        const int64_t a=floorDiv(lower-origin,grid.cellCm)-haloCells;
        const int64_t z=floorDiv(upper-1-origin,grid.cellCm)+haloCells;
        if(z<0||a>=size)return false;
        lo=int32_t(std::max<int64_t>(0,a));
        hi=int32_t(std::min<int64_t>(size-1,z));
        return lo<=hi;
    };
    CaveGraphDirtyRange result;
    if(!project(b.minX,b.maxX,grid.origin.x,grid.width,
                result.min.x,result.max.x)||
       !project(b.minY,b.maxY,grid.origin.y,grid.height,
                result.min.y,result.max.y)||
       !project(b.minZ,b.maxZ,grid.origin.z,grid.depth,
                result.min.z,result.max.z))
        return {};
    return result;
}
struct CaveClusterDirtyRange {
    CaveGraphCell min{},max{}; // inclusive 3D HPA cluster positions
};
inline std::optional<CaveClusterDirtyRange> caveHpaDirtyClusters(
    CaveDirtyBounds b,const CaveGraphGrid& grid,
    uint32_t clusterCells=16,int32_t haloCells=1){
    if(!clusterCells||clusterCells>64)
        throw std::invalid_argument("Caverns HPA cluster dimension");
    const auto range=caveGraphDirtyRange(b,grid,haloCells);
    if(!range)return {};
    return CaveClusterDirtyRange{
        {range->min.x/int32_t(clusterCells),
         range->min.y/int32_t(clusterCells),
         range->min.z/int32_t(clusterCells)},
        {range->max.x/int32_t(clusterCells),
         range->max.y/int32_t(clusterCells),
         range->max.z/int32_t(clusterCells)}
    };
}
// Existing native nav::Mesh is a 2.5D multi-span projection. It handles
// every underground level in its columns and refreshes only touched tiles.
// This function marks a conservative one-column ring; the owner's next
// refresh(source) uses the same published cave SDF provider.
inline bool markCaveNativeNavDirty(nav::Mesh& mesh,
                                   const CaveNavConfig& cfg,
                                   CaveDirtyBounds bounds,
                                   int32_t haloColumns=1){
    validateCaveNav(cfg);
    if(haloColumns<0||haloColumns>8)
        throw std::invalid_argument("Caverns native nav halo");
    if(bounds.maxX<=bounds.minX||bounds.maxZ<=bounds.minZ)
        return false;
    const auto range=[haloColumns](int64_t lo,int64_t hi,
                                   int64_t origin,int32_t cell,
                                   int32_t count,int32_t& first,int32_t& last){
        const int64_t a=floorDiv(lo-origin,cell)-haloColumns;
        const int64_t b=floorDiv(hi-1-origin,cell)+haloColumns;
        if(b<0||a>=count)return false;
        first=int32_t(std::max<int64_t>(0,a));
        last=int32_t(std::min<int64_t>(count-1,b));
        return first<=last;
    };
    int32_t x0=0,x1=0,z0=0,z1=0;
    if(!range(bounds.minX,bounds.maxX,cfg.originXcm,cfg.columnCm,
              cfg.width,x0,x1)||
       !range(bounds.minZ,bounds.maxZ,cfg.originZcm,cfg.columnCm,
              cfg.depth,z0,z1))
        return false;
    mesh.markDirty(x0,z0,x1,z1);
    return true;
}
} // namespace aiverse::terraweave::caverns
