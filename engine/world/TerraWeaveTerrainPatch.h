// TerraWeave: one deterministic terrain patch for BOTH visual geometry
// and authoritative collision, built from the same Earthworks height source.
// The host controls ECS tick barriers, GPU lifetime, navmesh and save/replay.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct TerrainPatch {
    world::Cell cell{};
    uint32_t resolution=0; // samples per side; 2..129
    int32_t cellSizeMetres=0;
    uint64_t sourceRevision=0;
    std::vector<int32_t> heightsCm;
    // One world-space collision triangle per matching visible triangle.
    // The winding/diagonal matches readChunkCollision: (a,c,b), (a,d,c).
    std::vector<world::Triangle> collision;
    // Mesh vertices are local to the chunk; the owner can build the RHI
    // Model using the SAME heights and triangle ordering.
    size_t triangleCount() const {return collision.size();}
};

inline TerrainPatch bakeEarthworksTerrainPatch(
        const EarthworksLayer& earth,world::Cell cell,
        int32_t cellSizeMetres,uint32_t resolution) {
    if(cell.y!=0 || resolution<2 || resolution>129 ||
       cellSizeMetres<16 || cellSizeMetres>4096)
        throw std::invalid_argument("TerraWeave terrain tile extent");
    const int64_t spanCm=int64_t(cellSizeMetres)*100;
    const int64_t ox=int64_t(cell.x)*spanCm;
    const int64_t oz=int64_t(cell.z)*spanCm;
    // SpatialWorld's Fixed triangle cast supports at most +/-1e6m,
    // narrower than the procedural Earth's +/-1e7m region.
    constexpr int64_t kCollisionLimitCm=100000000;
    if(ox< -kCollisionLimitCm || oz< -kCollisionLimitCm ||
       ox>kCollisionLimitCm-spanCm || oz>kCollisionLimitCm-spanCm)
        throw std::out_of_range("TerraWeave tile outside collision world");
    TerrainPatch out;
    out.cell=cell;
    out.resolution=resolution;
    out.cellSizeMetres=cellSizeMetres;
    out.sourceRevision=earth.revision();
    const size_t count=size_t(resolution)*resolution;
    out.heightsCm.reserve(count);
    const int64_t denominator=int64_t(resolution)-1;
    for(uint32_t z=0;z<resolution;++z)
        for(uint32_t x=0;x<resolution;++x) {
            // Fixed x/z are exactly those used by WorldAssets.cpp.
            const Fixed xFixed=Fixed::fromRatio(
                int64_t(x)*cellSizeMetres,denominator);
            const Fixed zFixed=Fixed::fromRatio(
                int64_t(z)*cellSizeMetres,denominator);
            const auto quantizeCm=[](Fixed v)->int64_t {
                const int64_t numerator=v.raw*100;
                return numerator>=0 ?
                    (numerator+Fixed::kOne/2)/Fixed::kOne :
                    (numerator-Fixed::kOne/2)/Fixed::kOne;
            };
            const int64_t sx=ox+quantizeCm(xFixed);
            const int64_t sz=oz+quantizeCm(zFixed);
            const int32_t h=earth.sampleHeightCm(sx,sz);
            // readChunkCollision restricts terrain heights to +/-10000m.
            if(h< -1000000 || h>1000000)
                throw std::out_of_range("TerraWeave collision terrain height");
            out.heightsCm.push_back(h);
        }
    out.collision.reserve(size_t(resolution-1u)*(resolution-1u)*2u);
    const Vec3 origin{Fixed::fromInt(int64_t(cell.x)*cellSizeMetres),
                      {},Fixed::fromInt(int64_t(cell.z)*cellSizeMetres)};
    const auto point=[&](uint32_t x,uint32_t z)->Vec3 {
        const size_t idx=size_t(z)*resolution+x;
        return origin+Vec3{
            Fixed::fromRatio(int64_t(x)*cellSizeMetres,denominator),
            Fixed::fromRatio(out.heightsCm[idx],100),
            Fixed::fromRatio(int64_t(z)*cellSizeMetres,denominator)};
    };
    for(uint32_t z=0;z+1u<resolution;++z)
        for(uint32_t x=0;x+1u<resolution;++x){
            const Vec3 a=point(x,z),b=point(x+1u,z),
                       c=point(x+1u,z+1u),d=point(x,z+1u);
            out.collision.push_back({a,c,b});
            out.collision.push_back({a,d,c});
        }
    return out;
}

struct TerrainCollisionRange {
    world::Cell cell{};
    uint32_t firstId=0;
    uint32_t count=0;
    uint64_t revision=0;
};

// Transactionally replace the terrain triangles owned by this adapter.
// The host must reserve a unique nonzero collider-ID range for each tile,
// disjoint from gameplay actors and OTHER streamed terrain tiles.
// All validations are performed BEFORE the authoritative SpatialWorld is
// mutated in place; a rejected triangle cannot corrupt the old map.
class TerrainCollisionPublisher {
public:
    const std::map<world::Cell,TerrainCollisionRange>& ranges() const {
        return active_;
    }
    void publish(world::SpatialWorld& spatial,const TerrainPatch& patch,
                 uint32_t firstId) {
        const size_t count=patch.collision.size();
        if(!firstId || !count || count>65534u ||
           uint64_t(firstId)+count-1>UINT32_MAX)
            throw std::invalid_argument("TerraWeave terrain collider ID range");
        if(patch.resolution<2 || patch.resolution>129 ||
           count!=size_t(patch.resolution-1u)*
                  size_t(patch.resolution-1u)*2u)
            throw std::invalid_argument("TerraWeave invalid terrain topology");
        const uint64_t limit=uint64_t(firstId)+count;
        for(const auto& [tile,old]:active_) {
            if(tile==patch.cell)continue;
            const uint64_t a=old.firstId,b=a+old.count;
            if(uint64_t(firstId)<b && a<limit)
                throw std::invalid_argument("TerraWeave overlapping terrain collider ranges");
        }
        const auto old=active_.find(patch.cell);
        // Do not steal a foreign actor collider ID.
        for(uint64_t i=firstId;i<limit;++i) {
            const bool owned=old!=active_.end() &&
                i>=old->second.firstId &&
                i<uint64_t(old->second.firstId)+old->second.count;
            if(!owned && spatial.colliders().contains(uint32_t(i)))
                throw std::invalid_argument("TerraWeave terrain collider ID occupied");
        }
        // Prevalidate everything that can fail BEFORE touching the live
        // SpatialWorld, then mutate it in place (no full-world deep copy
        // per tile publish). On a validation failure nothing changes.
        for(size_t i=0;i<count;++i)
            if(!world::SpatialWorld::validTriangle(patch.collision[i]))
                throw std::invalid_argument("TerraWeave collision triangle rejected");
        auto next=active_;
        next.insert_or_assign(patch.cell,TerrainCollisionRange{
            patch.cell,firstId,uint32_t(count),patch.sourceRevision});
        if(old!=active_.end())
            for(uint64_t i=old->second.firstId;
                i<uint64_t(old->second.firstId)+old->second.count;++i)
                spatial.remove(uint32_t(i));
        for(size_t i=0;i<count;++i)
            if(!spatial.addTriangle(firstId+uint32_t(i),patch.collision[i]))
                throw std::logic_error("TerraWeave prevalidated collision triangle rejected");
        active_.swap(next);
    }
    void unload(world::SpatialWorld& spatial,world::Cell cell) {
        const auto it=active_.find(cell);
        if(it==active_.end())return;
        const uint64_t first=it->second.firstId,last=first+it->second.count;
        active_.erase(it);
        for(uint64_t i=first;i<last;++i)spatial.remove(uint32_t(i));
    }
private:
    std::map<world::Cell,TerrainCollisionRange> active_;
};

} // namespace aiverse::terraweave
