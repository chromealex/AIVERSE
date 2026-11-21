// CPU-side per-chunk GPU BVH cache. Rebuilds triangle BLAS only for chunks whose
// immutable source pointer changed; packs a small top-level BVH over chunk roots.
// The returned GiGpuBvh uses the existing Metal 48-byte node/64-byte triangle ABI.
// A publish still uploads the combined buffers; per-chunk persistent GPU pages are
// a later stage and MUST be fenced before relocation or reuse.
#pragma once
#include "GiGpuBvh.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

class GiGpuChunkBvhCache {
public:
    struct Stats {
        size_t chunks=0, reused=0, rebuilt=0, removed=0;
        size_t triangles=0, packedNodes=0;
    };

    Stats refresh(const GiChunkedGeometrySnapshot& snapshot) {
        if (snapshot.triangleCount()>GiGpuBvh::kMaxTriangles)
            throw std::invalid_argument("GPU GI combined chunk triangle budget exceeded");
        std::map<uint64_t,std::shared_ptr<const Entry>> candidate;
        Stats stats;
        snapshot.forEachChunkShared([&](uint64_t id,GiBounds bounds,
                                  const std::shared_ptr<const StaticShadowGeometry>& geometry) {
            auto old=entries_.find(id);
            if (old!=entries_.end() && old->second->source==geometry) {
                candidate.emplace(id,old->second);
                ++stats.reused;
            } else {
                std::vector<GiGpuBvhTri> packed;
                packed.reserve(geometry->triangles.size());
                for(const auto& tri:geometry->triangles) {
                    const float3 color=tri.surface<geometry->albedos.size()
                        ? geometry->albedos[tri.surface] : float3{};
                    packed.push_back({{tri.a.x,tri.a.y,tri.a.z,0},
                                      {tri.b.x,tri.b.y,tri.b.z,0},
                                      {tri.c.x,tri.c.y,tri.c.z,0},
                                      {color.x,color.y,color.z,0}});
                }
                auto entry=std::make_shared<Entry>();
                entry->source=geometry;
                entry->bounds=bounds;
                entry->blas=GiGpuBvh::fromPackedTriangles(std::move(packed));
                candidate.emplace(id,std::move(entry));
                ++stats.rebuilt;
            }
        });
        for(const auto& [id,entry]:entries_)
            if(candidate.find(id)==candidate.end())++stats.removed;
        stats.chunks=candidate.size();
        stats.triangles=snapshot.triangleCount();
        // Transactional state swap: build/validation failures do not evict old BLAS.
        entries_.swap(candidate);
        return stats;
    }

    size_t chunkCount() const { return entries_.size(); }
    // Strongly owned source is retained even after the builder and old
    // GiChunkedGeometrySnapshot are destroyed, until the cache entry retires.
    std::shared_ptr<const StaticShadowGeometry> chunkSource(uint64_t id) const {
        auto it=entries_.find(id);
        return it==entries_.end()?nullptr:it->second->source;
    }

    const GiGpuBvh* chunkBvh(uint64_t id) const {
        auto it=entries_.find(id);
        return it==entries_.end()?nullptr:&it->second->blas;
    }

    // Small-scene GPU packet: TLAS internal nodes + single-child chunk references
    // followed by cached BLAS nodes. The Metal shader skips a missing second
    // child marked UINT32_MAX. Top-level root index stays 0.
    GiGpuBvh pack() const {
        GiGpuBvh result;
        if(entries_.empty())return result;
        struct ChunkRef {
            uint64_t id=0;
            GiBounds bounds{};
        };
        std::vector<ChunkRef> refs;
        refs.reserve(entries_.size());
        size_t totalTriangles=0,totalBlasNodes=0;
        for(const auto& [id,entry]:entries_) {
            if(entry->blas.nodes.empty())continue;
            refs.push_back({id,entry->bounds});
            totalTriangles+=entry->blas.triangles.size();
            totalBlasNodes+=entry->blas.nodes.size();
        }
        if(refs.empty())return result;
        if(totalTriangles>GiGpuBvh::kMaxTriangles)
            throw std::invalid_argument("GPU GI packed chunk triangle budget exceeded");
        result.triangles.reserve(totalTriangles);
        result.nodes.reserve(2*refs.size()+totalBlasNodes);

        // First reserve a deterministic TLAS tree; remember leaf to chunk mappings.
        std::vector<std::pair<uint32_t,uint64_t>> leaves;
        const auto coord=[](float3 v,int i) {
            return i==0?v.x:i==1?v.y:v.z;
        };
        const auto buildTlas=[&](auto&& self,size_t first,size_t count)->uint32_t {
            GiBounds bounds=refs[first].bounds;
            for(size_t i=1;i<count;++i) {
                bounds.min=min3(bounds.min,refs[first+i].bounds.min);
                bounds.max=max3(bounds.max,refs[first+i].bounds.max);
            }
            const uint32_t id=uint32_t(result.nodes.size());
            result.nodes.push_back({{bounds.min.x,bounds.min.y,bounds.min.z,0},
                                    {bounds.max.x,bounds.max.y,bounds.max.z,0},{}});
            if(count==1) {
                // Fix up root reference once BLAS offsets are known.
                result.nodes[id].meta={0,UINT32_MAX,0,0};
                leaves.emplace_back(id,refs[first].id);
                return id;
            }
            const float3 extent=bounds.max-bounds.min;
            int axis=extent.x>=extent.y?0:1;
            if(extent.z>coord(extent,axis))axis=2;
            std::stable_sort(refs.begin()+first,refs.begin()+first+count,
                [&](const auto& a,const auto& b) {
                    float ca=coord(a.bounds.min,axis)+coord(a.bounds.max,axis);
                    float cb=coord(b.bounds.min,axis)+coord(b.bounds.max,axis);
                    return ca==cb?a.id<b.id:ca<cb;
                });
            const size_t half=count/2;
            const auto left=self(self,first,half);
            const auto right=self(self,first+half,count-half);
            result.nodes[id].meta={left,right,0,0};
            return id;
        };
        buildTlas(buildTlas,0,refs.size());

        for(const auto& [id,entry]:entries_) {
            const auto& blas=entry->blas;
            if(blas.nodes.empty())continue;
            const auto nodeStart=uint32_t(result.nodes.size());
            const auto triStart=uint32_t(result.triangles.size());
            for(auto n:blas.nodes) {
                if(n.meta[3]!=0) n.meta[2]+=triStart;
                else {
                    n.meta[0]+=nodeStart;
                    n.meta[1]+=nodeStart;
                }
                result.nodes.push_back(n);
            }
            result.triangles.insert(result.triangles.end(),
                                    blas.triangles.begin(),blas.triangles.end());
            for(const auto& [leaf,leafId]:leaves)
                if(leafId==id)result.nodes[leaf].meta[0]=nodeStart;
        }
        return result;
    }

private:
    struct Entry {
        std::shared_ptr<const StaticShadowGeometry> source;
        GiBounds bounds{};
        GiGpuBvh blas;
    };
    std::map<uint64_t,std::shared_ptr<const Entry>> entries_;
};

} // namespace aiverse::render
