// Stable logical GPU BLAS pages for the existing flat Metal/Vulkan SSBO ABI.
//
// The old GiGpuChunkBvhCache::pack() relocates all downstream BLAS offsets
// whenever a preceding chunk changes size, forcing many GPU buffer uploads.
// This planner assigns each resident chunk independent, reusable spans within
// two long-lived GPU storage streams. An unchanged BLAS keeps its exact
// absolute triangle/node addresses; only a small reserved TLAS prefix changes.
// GPU shader still sees the existing GiGpuBvh flat 64B Tri / 48B Node layout.
// No device access here. The caller MUST wait for all submitted GI GPU tickets
// to complete before planning a changed packet, and only COMMIT after the
// RHI accepts the corresponding upload.
// This is SUBALLOCATED page streaming, not independent VkBuffer/MTLBuffer
// allocations per chunk or unbounded virtual memory.
#pragma once
#include "GiGpuChunkBvhCache.h"
#include "GiGpuSparsePatch.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

class GiGpuPagedBvhArena {
public:
    // Reserve a fixed TLAS head for <= 2048 local leaf chunks. First-level
    // GPU ray roots always start at node[0] without changing the shader ABI.
    static constexpr uint32_t kTlasCapacity=4096;
    static constexpr uint32_t kTriangleLimit=uint32_t(GiGpuBvh::kMaxTriangles);
    static constexpr uint32_t kNodeLimit=2*kTriangleLimit+4096;
    static constexpr uint32_t kMaxChunks=(kTlasCapacity+1)/2;

    struct Span {
        uint32_t first=0,capacity=0;
    };
    struct Page {
        Span triangles{},nodes{};
        uint32_t triangleCount=0,nodeCount=0;
        GiBounds bounds{};
        std::shared_ptr<const StaticShadowGeometry> source;
    };
    struct Stats {
        size_t reused=0,rewritten=0,relocated=0,added=0,removed=0;
        size_t relevantTriangles=0,physicalTriangles=0,physicalNodes=0;
        size_t tlasNodes=0,suballocatedPages=0,freeTriangleSlots=0,freeNodeSlots=0;
        bool compacted=false;
    };

private:
    struct State {
        std::map<uint64_t,Page> pages;
        std::vector<GiGpuBvhTri> triangles;
        std::vector<GiGpuBvhNode> nodes;
        std::vector<Span> freeTriangles,freeNodes;
        uint32_t triangleEnd=0,nodeEnd=kTlasCapacity;
    };
public:
    struct Plan {
        GiGpuBvh packet;
        Stats stats;
        bool accepted=false;
    private:
        friend class GiGpuPagedBvhArena;
        State next;
    };

    // Optional tighter physical address limits permit deterministic small
    // compaction tests without allocating thousands of triangles.
    explicit GiGpuPagedBvhArena(uint32_t triangleLimit=kTriangleLimit,
                                uint32_t nodeLimit=kNodeLimit)
      : triangleLimit_(triangleLimit),nodeLimit_(nodeLimit) {
        if(!triangleLimit_ || triangleLimit_>kTriangleLimit ||
           nodeLimit_<=kTlasCapacity || nodeLimit_>kNodeLimit)
            throw std::invalid_argument("invalid GI paged BLAS address budget");
    }

    void clear() { state_=State{}; }
    size_t pageCount() const { return state_.pages.size(); }
    const Page* findPage(uint64_t id) const {
        const auto it=state_.pages.find(id);
        return it==state_.pages.end()?nullptr:&it->second;
    }

    // Build transactionally. If previous churn fragmented the physical
    // address space, make ONE fresh compact layout after a genuine completed
    // compute fence. The compacted candidate is STILL NOT COMMITTED until the
    // RHI has accepted its complete relocated BVH upload.
    Plan plan(const GiChunkedGeometrySnapshot& snapshot,
              const GiGpuChunkBvhCache& blasCache) const {
        try {
            return planOnce(snapshot,blasCache,false);
        } catch(const AddressSpaceExhausted&) {
            auto compacted=planOnce(snapshot,blasCache,true);
            compacted.stats.compacted=true;
            return compacted;
        }
    }

private:
    struct AddressSpaceExhausted : std::runtime_error {
        AddressSpaceExhausted() : std::runtime_error("GI GPU physical page slots exhausted") {}
    };
    Plan planOnce(const GiChunkedGeometrySnapshot& snapshot,
                  const GiGpuChunkBvhCache& blasCache,
                  bool compact) const {
        if(snapshot.triangleCount()>triangleLimit_ ||
           snapshot.chunkCount()>kMaxChunks)
            throw std::invalid_argument("GI GPU paged source limit exceeded");
        Plan publication;
        State& next=publication.next;
        next=compact ? State{} : state_;
        if(next.nodes.empty())
            next.nodes.resize(kTlasCapacity); // unreachable TLAS reserve
        auto& stats=publication.stats;

        struct Ref {uint64_t id;GiBounds bounds;uint32_t node;};
        std::vector<Ref> refs;
        refs.reserve(snapshot.chunkCount());
        std::map<uint64_t,bool> admitted;
        snapshot.forEachChunkShared([&](uint64_t id,GiBounds bounds,
                                    const std::shared_ptr<const StaticShadowGeometry>& source) {
            const auto* bvh=blasCache.chunkBvh(id);
            if(!bvh)throw std::invalid_argument("GI GPU page missing cached chunk BLAS");
            if(bvh->nodes.empty())return; // empty occluder is not a resident page
            admitted.emplace(id,true);
        });
        // Removed spans are returned to the CPU allocator only after the
        // caller's completed GPU ticket fence (the runtime checks !busy()).
        for(auto it=next.pages.begin();it!=next.pages.end();) {
            if(admitted.find(it->first)==admitted.end()) {
                release(next.freeTriangles,it->second.triangles);
                release(next.freeNodes,it->second.nodes);
                it=next.pages.erase(it);
                ++stats.removed;
            } else ++it;
        }

        snapshot.forEachChunkShared([&](uint64_t id,GiBounds bounds,
                                    const std::shared_ptr<const StaticShadowGeometry>& source) {
            const GiGpuBvh* bvh=blasCache.chunkBvh(id);
            if(!bvh || bvh->nodes.empty())return;
            const uint32_t triCount=uint32_t(bvh->triangles.size());
            const uint32_t nodeCount=uint32_t(bvh->nodes.size());
            if(!triCount || !nodeCount)
                throw std::invalid_argument("GI GPU BLAS has nodes but no triangles");
            stats.relevantTriangles+=triCount;
            auto it=next.pages.find(id);
            bool fresh=it==next.pages.end();
            if(fresh) {
                it=next.pages.emplace(id,Page{}).first;
                ++stats.added;
            }
            Page& page=it->second;
            if(!fresh && page.source==source &&
               page.triangleCount==triCount && page.nodeCount==nodeCount) {
                ++stats.reused;
                refs.push_back({id,bounds,page.nodes.first});
                return;
            }
            const bool relocateTriangles=triCount>page.triangles.capacity;
            const bool relocateNodes=nodeCount>page.nodes.capacity;
            if(relocateTriangles) {
                const Span allocated=acquire(next.freeTriangles,next.triangleEnd,
                                             triangleLimit_,triCount);
                release(next.freeTriangles,page.triangles);
                page.triangles=allocated;
            }
            if(relocateNodes) {
                const Span allocated=acquire(next.freeNodes,next.nodeEnd,
                                             nodeLimit_,nodeCount);
                release(next.freeNodes,page.nodes);
                page.nodes=allocated;
            }
            if(!fresh && (relocateTriangles || relocateNodes))++stats.relocated;
            page.triangleCount=triCount;
            page.nodeCount=nodeCount;
            page.source=source;
            page.bounds=bounds;
            if(next.triangles.size()<next.triangleEnd)
                next.triangles.resize(next.triangleEnd);
            if(next.nodes.size()<next.nodeEnd)
                next.nodes.resize(next.nodeEnd);
            std::copy(bvh->triangles.begin(),bvh->triangles.end(),
                      next.triangles.begin()+page.triangles.first);
            for(uint32_t i=0;i<nodeCount;++i) {
                GiGpuBvhNode n=bvh->nodes[i];
                if(n.meta[3]!=0) {
                    n.meta[2]+=page.triangles.first;
                } else {
                    n.meta[0]+=page.nodes.first;
                    if(n.meta[1]!=UINT32_MAX)n.meta[1]+=page.nodes.first;
                }
                next.nodes[page.nodes.first+i]=n;
            }
            ++stats.rewritten;
            refs.push_back({id,bounds,page.nodes.first});
        });

        if(stats.relevantTriangles>kTriangleLimit ||
           refs.size()>kMaxChunks)
            throw std::invalid_argument("GI GPU resident page budget exceeded");

        // Recompute ONLY the small TLAS; all unchanged absolute BLAS addresses
        // survive edits to other chunks. refs are in stable source-ID order.
        std::vector<GiGpuBvhNode> tlas;
        tlas.reserve(refs.empty()?0:refs.size()*2-1);
        const auto coordinate=[](float3 v,int axis) {
            return axis==0?v.x:axis==1?v.y:v.z;
        };
        const auto addNode=[&](auto&& self,size_t first,size_t count)->uint32_t {
            GiBounds bounds=refs[first].bounds;
            for(size_t j=1;j<count;++j) {
                bounds.min=min3(bounds.min,refs[first+j].bounds.min);
                bounds.max=max3(bounds.max,refs[first+j].bounds.max);
            }
            const uint32_t index=uint32_t(tlas.size());
            tlas.push_back({{bounds.min.x,bounds.min.y,bounds.min.z,0},
                            {bounds.max.x,bounds.max.y,bounds.max.z,0},{}});
            if(count==1) {
                tlas[index].meta={refs[first].node,UINT32_MAX,0,0};
                return index;
            }
            const float3 ext=bounds.max-bounds.min;
            const int axis=ext.z>std::max(ext.x,ext.y) ? 2 :
                           ext.x>=ext.y ? 0 : 1;
            std::stable_sort(refs.begin()+first,refs.begin()+first+count,
                [&](const Ref& a,const Ref& b) {
                    const float ca=coordinate(a.bounds.min,axis)+coordinate(a.bounds.max,axis);
                    const float cb=coordinate(b.bounds.min,axis)+coordinate(b.bounds.max,axis);
                    return ca==cb?a.id<b.id:ca<cb;
                });
            const size_t half=count/2;
            const auto left=self(self,first,half);
            const auto right=self(self,first+half,count-half);
            tlas[index].meta={left,right,0,0};
            return index;
        };
        if(!refs.empty())addNode(addNode,0,refs.size());
        if(tlas.size()>kTlasCapacity)
            throw std::invalid_argument("GI GPU TLAS reserve exhausted");
        std::copy(tlas.begin(),tlas.end(),next.nodes.begin());
        stats.tlasNodes=tlas.size();
        stats.suballocatedPages=next.pages.size();
        stats.physicalTriangles=next.triangleEnd;
        stats.physicalNodes=next.nodeEnd;
        for(const auto& span:next.freeTriangles)stats.freeTriangleSlots+=span.capacity;
        for(const auto& span:next.freeNodes)stats.freeNodeSlots+=span.capacity;

        if(!refs.empty()) {
            publication.packet.triangles=next.triangles;
            publication.packet.nodes=next.nodes;
        }
        publication.accepted=true;
        return publication;
    }

public:
    void commit(Plan&& plan) {
        if(!plan.accepted)
            throw std::invalid_argument("unaccepted GI GPU page layout");
        state_=std::move(plan.next);
    }

private:
    static void release(std::vector<Span>& free,Span span) {
        if(!span.capacity)return;
        free.push_back(span);
        std::sort(free.begin(),free.end(),[](Span a,Span b) {
            return a.first<b.first;
        });
        size_t out=0;
        for(const auto s:free) {
            if(out && free[out-1].first+free[out-1].capacity==s.first)
                free[out-1].capacity+=s.capacity;
            else free[out++]=s;
        }
        free.resize(out);
    }

    static Span acquire(std::vector<Span>& free,uint32_t& end,uint32_t limit,
                        uint32_t needed) {
        if(!needed)return {};
        const uint32_t capacity=uint32_t(giGpuGrowCapacity(0,needed,limit));
        for(size_t i=0;i<free.size();++i) {
            if(free[i].capacity>=capacity) {
                const Span result{free[i].first,capacity};
                free[i].first+=capacity;
                free[i].capacity-=capacity;
                if(!free[i].capacity)free.erase(free.begin()+long(i));
                return result;
            }
        }
        if(end>limit || capacity>limit-end)
            throw AddressSpaceExhausted{};
        const Span result{end,capacity};
        end+=capacity;
        return result;
    }

    State state_;
    uint32_t triangleLimit_=kTriangleLimit;
    uint32_t nodeLimit_=kNodeLimit;
};

} // namespace aiverse::render
