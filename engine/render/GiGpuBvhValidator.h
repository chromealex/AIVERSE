// Preflight the FLAT Metal/Vulkan BVH packet before uploading any GPU bytes.
// The same ABI is used by single-scene, chunk-cached and paged/compacted GI.
// Node index 0 is the traversal root. Page arenas may contain unreachable
// freed slots or a reserved TLAS prefix; validate only ROOT-REACHABLE nodes.
// Never let corrupt child/triangle pointers, cycles or excessive traversal
// stack depth reach MSL/SPIR-V storage-buffer readers.
#pragma once
#include "GiGpuBvh.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::render {

struct GiGpuBvhValidation {
    bool valid=false;
    const char* error=nullptr;
    size_t reachableNodes=0;
    size_t referencedTriangles=0;
    size_t maxDepth=0;
};

inline GiGpuBvhValidation giValidateGpuBvh(const GiGpuBvh& bvh) {
    GiGpuBvhValidation result;
    if(bvh.nodes.empty() && bvh.triangles.empty()) {
        result.valid=true;return result;
    }
    if(bvh.nodes.empty() || bvh.triangles.empty()) {
        result.error="nonempty GPU GI packet requires root and triangle data";
        return result;
    }
    if(bvh.triangles.size()>GiGpuBvh::kMaxTriangles ||
       bvh.nodes.size()>2*GiGpuBvh::kMaxTriangles+4096) {
        result.error="GPU GI packet physical capacity exceeded";
        return result;
    }
    const auto finite=[](float3 value) {
        return std::isfinite(value.x) &&
               std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    const auto xyz=[](float4 value) {
        return float3{value.x,value.y,value.z};
    };
    // A malformed but in-bounds index can still reference a child whose
    // physical box sits OUTSIDE its parent's box. The shader would prune
    // that child, causing false sunlight/visibility and light leaks.
    const auto contains=[](float3 parentLo,float3 parentHi,
                           float3 childLo,float3 childHi) {
        for(int axis=0;axis<3;++axis) {
            const auto get=[&](float3 v) {
                return axis==0?v.x:axis==1?v.y:v.z;
            };
            const float a=get(parentLo),b=get(parentHi);
            // The old 1e-5 * ABSOLUTE world position tolerance grows to
            // thousands of metres in a large world and can admit escaped
            // triangles, causing false BVH ray pruning and light leaks.
            // Parent/child boxes share the same float32 ABI; allow only one
            // representable float step of rounding at each endpoint.
            const float lo=std::nextafter(a,-std::numeric_limits<float>::infinity());
            const float hi=std::nextafter(b, std::numeric_limits<float>::infinity());
            if(get(childLo)<lo || get(childHi)>hi)
                return false;
        }
        return true;
    };
    std::vector<uint8_t> status(bvh.nodes.size(),0);
    const auto visit=[&](auto&& self,uint32_t index,size_t depth)->bool {
        if(index>=bvh.nodes.size()) {
            result.error="GPU GI BVH child index outside node storage";
            return false;
        }
        if(depth>=GiGpuBvh::kGpuStackCapacity) {
            result.error="GPU GI BVH traversal exceeds 64-level stack";
            return false;
        }
        if(status[index]==1) {
            result.error="GPU GI BVH cycle";
            return false;
        }
        if(status[index]==2) return true; // a shared acyclic child is safe
        status[index]=1;
        const auto& node=bvh.nodes[index];
        const float3 lo=xyz(node.lo),hi=xyz(node.hi);
        if(!finite(lo)||!finite(hi)||
           lo.x>hi.x||lo.y>hi.y||lo.z>hi.z) {
            result.error="GPU GI BVH has invalid node bounds";
            return false;
        }
        result.maxDepth=std::max(result.maxDepth,depth+1);
        ++result.reachableNodes;
        const uint32_t count=node.meta[3];
        if(count>0) {
            if(count>GiGpuBvh::kLeafSize ||
               uint64_t(node.meta[2])+count>bvh.triangles.size()) {
                result.error="GPU GI BVH leaf triangle range invalid";
                return false;
            }
            for(uint32_t i=0;i<count;++i) {
                const auto& tri=bvh.triangles[node.meta[2]+i];
                const float3 a=xyz(tri.a),b=xyz(tri.b),c=xyz(tri.c);
                const float3 color=xyz(tri.albedo);
                if(!finite(a)||!finite(b)||!finite(c)||!finite(color)||
                   !contains(lo,hi,a,a) || !contains(lo,hi,b,b) ||
                   !contains(lo,hi,c,c) ||
                   color.x<0||color.y<0||color.z<0||
                   color.x>10000||color.y>10000||color.z>10000) {
                    result.error="GPU GI BVH leaf contains invalid triangle";
                    return false;
                }
            }
            result.referencedTriangles+=count;
        } else {
            const uint32_t left=node.meta[0],right=node.meta[1];
            if(left==index || right==index) {
                result.error="GPU GI BVH self-cycle";
                return false;
            }
            const auto containedChild=[&](uint32_t child) {
                if(child>=bvh.nodes.size())return true; // visit reports bad ID
                return contains(lo,hi,xyz(bvh.nodes[child].lo),
                                xyz(bvh.nodes[child].hi));
            };
            if(!containedChild(left) ||
               (right!=UINT32_MAX && !containedChild(right))) {
                result.error="GPU GI BVH child AABB escapes parent";
                return false;
            }
            if(!self(self,left,depth+1))return false;
            if(right!=UINT32_MAX && !self(self,right,depth+1))return false;
        }
        status[index]=2;
        return true;
    };
    if(!visit(visit,0,0))return result;
    result.valid=true;
    return result;
}

} // namespace aiverse::render
