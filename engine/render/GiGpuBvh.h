// CPU-built, GPU-consumed BVH for the opt-in Metal GI reference backend.
// One contiguous 48-byte node and 64-byte triangle stream; both have stable
// C++/Metal layouts, with deterministic centroid splits and a bounded stack.
// This is NOT the final chunk-streamed GPU BVH: a snapshot still flattens
// changed geometry on publication. It removes per-ray O(all triangles) scans.
#pragma once
#include "GiChunkedGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct GiGpuBvhTri {
    float4 a{}, b{}, c{}, albedo{};
};
struct GiGpuBvhNode {
    float4 lo{}, hi{};
    // x=left, y=right, z=first triangle, w=leaf triangle count.
    // Internal nodes have w=0 and x/y valid; leaf nodes have w=1..4.
    std::array<uint32_t,4> meta{};
};
static_assert(sizeof(GiGpuBvhTri)==64 && sizeof(GiGpuBvhNode)==48,
              "GI GPU BVH C++/MSL layout changed");

class GiGpuBvh {
public:
    static constexpr size_t kMaxTriangles=8192;
    static constexpr uint32_t kLeafSize=4;
    static constexpr uint32_t kGpuStackCapacity=64;
    std::vector<GiGpuBvhTri> triangles;
    std::vector<GiGpuBvhNode> nodes;

    static GiGpuBvh build(const GiChunkedGeometrySnapshot& snapshot) {
        if (snapshot.triangleCount()>kMaxTriangles)
            throw std::invalid_argument("GPU GI BVH triangle budget exceeded");
        GiGpuBvh result;
        result.triangles.reserve(snapshot.triangleCount());
        snapshot.forEachTriangle([&](const StaticShadowGeometry::Triangle& tri,float3 color) {
            const auto finite=[](float3 v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (!finite(tri.a) || !finite(tri.b) || !finite(tri.c) ||
                !finite(color) || color.x<0 || color.y<0 || color.z<0 ||
                color.x>10000 || color.y>10000 || color.z>10000)
                throw std::invalid_argument("invalid triangle in GPU GI BVH");
            result.triangles.push_back({
                {tri.a.x,tri.a.y,tri.a.z,0},
                {tri.b.x,tri.b.y,tri.b.z,0},
                {tri.c.x,tri.c.y,tri.c.z,0},
                {color.x,color.y,color.z,0}
            });
        });
        if (result.triangles.empty()) return result;
        result.nodes.reserve(result.triangles.size()*2);
        result.makeNode(0,uint32_t(result.triangles.size()),0);
        return result;
    }

    // Build a single chunk BLAS without re-reading any other chunk.
    static GiGpuBvh fromPackedTriangles(std::vector<GiGpuBvhTri> packed) {
        if (packed.size()>kMaxTriangles)
            throw std::invalid_argument("GPU GI packed BVH triangle budget exceeded");
        GiGpuBvh result;
        result.triangles=std::move(packed);
        if(!result.triangles.empty()) {
            result.nodes.reserve(result.triangles.size()*2);
            result.makeNode(0,uint32_t(result.triangles.size()),0);
        }
        return result;
    }

    struct RayHit {
        float distance=0;
        float3 normal{},albedo{};
        uint32_t trianglesVisited=0, nodesVisited=0;
    };

    RayHit trace(float3 origin,float3 dir,float limit) const {
        RayHit out;
        out.distance=limit;
        if (nodes.empty()) return out;
        // Same iterative DFS as Metal. Total pending siblings <= max tree depth.
        std::array<uint32_t,kGpuStackCapacity> stack{};
        uint32_t top=0;
        stack[top++]=0;
        while(top) {
            const auto& node=nodes[stack[--top]];
            ++out.nodesVisited;
            if (!box(node,origin,dir,out.distance)) continue;
            if (node.meta[3]) {
                const uint32_t end=node.meta[2]+node.meta[3];
                for (uint32_t i=node.meta[2];i<end;++i) {
                    const auto& tri=triangles[i];
                    ++out.trianglesVisited;
                    const float3 a{tri.a.x,tri.a.y,tri.a.z};
                    const float3 e1=float3{tri.b.x,tri.b.y,tri.b.z}-a;
                    const float3 e2=float3{tri.c.x,tri.c.y,tri.c.z}-a;
                    const float3 p=cross(dir,e2);
                    const float det=dot(e1,p);
                    if (std::abs(det)<1e-10f) continue;
                    const float inv=1.f/det;
                    const float3 s=origin-a;
                    const float u=dot(s,p)*inv;
                    if(u<0 || u>1) continue;
                    const float3 q=cross(s,e1);
                    const float v=dot(dir,q)*inv;
                    if(v<0 || u+v>1) continue;
                    const float hit=dot(e2,q)*inv;
                    if(hit>.001f && hit<out.distance) {
                        out.distance=hit;
                        out.normal=normalize(cross(e1,e2));
                        out.albedo={tri.albedo.x,tri.albedo.y,tri.albedo.z};
                    }
                }
            } else {
                const uint32_t children=node.meta[1]==UINT32_MAX?1u:2u;
                if (top+children>stack.size()) throw std::runtime_error("GPU GI BVH stack overflow");
                if (children==2u) stack[top++]=node.meta[1];
                stack[top++]=node.meta[0];
            }
        }
        return out;
    }

private:
    static float axis(float3 v,int k) {return k==0?v.x:k==1?v.y:v.z;}
    static float3 xyz(float4 v) {return {v.x,v.y,v.z};}
    static float3 low(const GiGpuBvhTri& t) {
        return min3(xyz(t.a),min3(xyz(t.b),xyz(t.c)));
    }
    static float3 high(const GiGpuBvhTri& t) {
        return max3(xyz(t.a),max3(xyz(t.b),xyz(t.c)));
    }
    static bool box(const GiGpuBvhNode& node,float3 origin,float3 direction,float maxDist) {
        float lo=0, hi=maxDist;
        for(int k=0;k<3;++k) {
            const float p=axis(origin,k),d=axis(direction,k);
            const float mn=axis(xyz(node.lo),k),mx=axis(xyz(node.hi),k);
            if(std::abs(d)<1e-10f) {
                if(p<mn || p>mx)return false;
                continue;
            }
            float t0=(mn-p)/d, t1=(mx-p)/d;
            if(t0>t1)std::swap(t0,t1);
            lo=std::max(lo,t0);
            hi=std::min(hi,t1);
            if(lo>hi)return false;
        }
        return true;
    }
    uint32_t makeNode(uint32_t first,uint32_t count,uint32_t depth) {
        if(depth>=kGpuStackCapacity-2)
            throw std::invalid_argument("GPU GI BVH depth limit exceeded");
        float3 lo=low(triangles[first]),hi=high(triangles[first]);
        for(uint32_t i=first+1;i<first+count;++i) {
            lo=min3(lo,low(triangles[i]));
            hi=max3(hi,high(triangles[i]));
        }
        const uint32_t index=uint32_t(nodes.size());
        nodes.push_back({{lo.x,lo.y,lo.z,0},{hi.x,hi.y,hi.z,0},{}});
        if(count<=kLeafSize) {
            nodes[index].meta={0,0,first,count};
            return index;
        }
        const float3 ext=hi-lo;
        int split=ext.x>=ext.y?0:1;
        if(ext.z>axis(ext,split))split=2;
        const auto centroid=[split](const GiGpuBvhTri& t) {
            return axis(xyz(t.a)+xyz(t.b)+xyz(t.c),split);
        };
        const uint32_t half=count/2;
        std::stable_sort(triangles.begin()+first,triangles.begin()+first+count,
                         [&](const auto& a,const auto& b){return centroid(a)<centroid(b);});
        const uint32_t left=makeNode(first,half,depth+1);
        const uint32_t right=makeNode(first+half,count-half,depth+1);
        nodes[index].meta={left,right,0,0};
        return index;
    }
};

} // namespace aiverse::render
