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
    // Trailing to preserve positional initializers from older terrain tests.
    // Adaptive LOD is GPU presentation only; never publish its morphed
    // edge geometry to the authoritative collision owner.
    bool presentationOnly=false;
    // Mesh vertices are local to the chunk; the owner can build the RHI
    // Model using the SAME heights and triangle ordering.
    size_t triangleCount() const {return collision.size();}
    // Derived topology, not a second height authority. A cut patch uses
    // its exact triangle soup for both rendering and native collision.
    bool clippedSurface=false;
    uint64_t surfaceEpoch=0;
    std::vector<RectCm> surfaceHoles; // conservative X/Z nav exclusion bounds
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

inline bool validTerrainPatchTopology(const TerrainPatch& patch) {
    if(patch.resolution<2||patch.resolution>129)return false;
    if(patch.clippedSurface)return patch.surfaceEpoch&&!patch.presentationOnly&&
        !patch.surfaceHoles.empty()&&!patch.collision.empty()&&patch.collision.size()<=65534;
    return patch.surfaceHoles.empty()&&patch.collision.size()==size_t(patch.resolution-1)*size_t(patch.resolution-1)*2;
}

struct TerrainCollisionRange {
    world::Cell cell{};
    uint32_t firstId=0;
    uint32_t count=0;
    uint64_t revision=0;
    std::vector<uint64_t> generations;
    bool clippedSurface=false;
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
        if(patch.presentationOnly)
            throw std::invalid_argument(
                "TerraWeave visual LOD cannot replace authoritative collision");
        if(!validTerrainPatchTopology(patch))
            throw std::invalid_argument("TerraWeave invalid terrain topology");
        const uint64_t limit=uint64_t(firstId)+count;
        for(const auto& [tile,old]:active_) {
            if(tile==patch.cell)continue;
            const uint64_t a=old.firstId,b=a+old.count;
            if(uint64_t(firstId)<b && a<limit)
                throw std::invalid_argument("TerraWeave overlapping terrain collider ranges");
        }
        if(owner_&&owner_!=&spatial)throw std::logic_error("TerraWeave collision world mismatch");
        const auto old=active_.find(patch.cell);
        if(old!=active_.end()&&!owns(spatial,old->second))
            throw std::logic_error("TerraWeave collision lease changed");
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
        auto& lease=next.insert_or_assign(patch.cell,TerrainCollisionRange{
            patch.cell,firstId,uint32_t(count),patch.sourceRevision,{},patch.clippedSurface}).first->second;
        lease.generations.resize(count);
        const auto install=[&](world::SpatialWorld& target){
            if(old!=active_.end())for(size_t i=0;i<old->second.count;++i)
                if(!target.removeIfGeneration(old->second.firstId+uint32_t(i),old->second.generations[i]))
                    throw std::logic_error("TerraWeave collision lease changed");
            for(size_t i=0;i<count;++i){
                const uint32_t id=firstId+uint32_t(i);
                if(!target.addTriangle(id,patch.collision[i]))
                    throw std::logic_error("TerraWeave prevalidated collision triangle rejected");
                lease.generations[i]=target.colliderGeneration(id);
            }
        };
        // Topology changes can alter the count/range. Stage in an isolated
        // copy and conditionally exchange the canonical world; caller holds
        // reader exclusion. Ordinary terrain preserves its existing path.
        if(patch.clippedSurface||(old!=active_.end()&&old->second.clippedSurface)){
            const auto version=spatial.mutationVersion();
            auto candidate=spatial;
            install(candidate);
            if(!spatial.publishCopyIfCurrent(std::move(candidate),version))
                throw std::logic_error("TerraWeave collision world changed before cut commit");
        }else install(spatial);
        active_.swap(next);owner_=&spatial;
    }
    void unload(world::SpatialWorld& spatial,world::Cell cell) {
        const auto it=active_.find(cell);
        if(it==active_.end())return;
        if(owner_!=&spatial||!owns(spatial,it->second))throw std::logic_error("TerraWeave retire lease changed");
        const auto& lease=it->second;
        if(lease.clippedSurface){
            const auto version=spatial.mutationVersion();auto candidate=spatial;
            for(size_t i=0;i<lease.count;++i)
                if(!candidate.removeIfGeneration(lease.firstId+uint32_t(i),lease.generations[i]))
                    throw std::logic_error("TerraWeave cut retire lease changed");
            if(!spatial.publishCopyIfCurrent(std::move(candidate),version))throw std::logic_error("TerraWeave cut retire world changed");
        }else for(size_t i=0;i<lease.count;++i)
            if(!spatial.removeIfGeneration(lease.firstId+uint32_t(i),lease.generations[i]))throw std::logic_error("TerraWeave retire lease changed");
        active_.erase(it);if(active_.empty())owner_=nullptr;
    }
private:
    static bool owns(const world::SpatialWorld& spatial,const TerrainCollisionRange& range){
        if(range.generations.size()!=range.count)return false;
        for(size_t i=0;i<range.count;++i)if(!range.generations[i]||spatial.colliderGeneration(range.firstId+uint32_t(i))!=range.generations[i])return false;
        return true;
    }
    world::SpatialWorld* owner_=nullptr;
    std::map<world::Cell,TerrainCollisionRange> active_;
};

} // namespace aiverse::terraweave
