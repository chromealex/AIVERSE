// VIVID facial morph surface correspondence for independently decimated LODs.
// Pure CPU authoring, no model mutation or animation scheduler.
#pragma once
#include "SparseMorph.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::vivid {
struct LodSurfaceSample {
    std::uint32_t triangle=0; // triangle offset / 3 in source index array
    render::float3 bary{1,0,0};
    float distanceSquared=0;
};
class MorphLodSurface {
    static render::float3 closest(render::float3 p,render::float3 a,
                                  render::float3 b,render::float3 c,
                                  render::float3& weights) {
        using namespace render;
        const auto ab=b-a,ac=c-a,ap=p-a;
        const float d1=dot(ab,ap),d2=dot(ac,ap);
        if(d1<=0&&d2<=0){weights={1,0,0};return a;}
        const auto bp=p-b;
        const float d3=dot(ab,bp),d4=dot(ac,bp);
        if(d3>=0&&d4<=d3){weights={0,1,0};return b;}
        const float vc=d1*d4-d3*d2;
        if(vc<=0&&d1>=0&&d3<=0) {
            const float t=d1/(d1-d3);
            weights={1-t,t,0};return a+ab*t;
        }
        const auto cp=p-c;
        const float d5=dot(ab,cp),d6=dot(ac,cp);
        if(d6>=0&&d5<=d6){weights={0,0,1};return c;}
        const float vb=d5*d2-d1*d6;
        if(vb<=0&&d2>=0&&d6<=0) {
            const float t=d2/(d2-d6);
            weights={1-t,0,t};return a+ac*t;
        }
        const float va=d3*d6-d5*d4;
        if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
            const float t=(d4-d3)/((d4-d3)+(d5-d6));
            weights={0,1-t,t};return b+(c-b)*t;
        }
        const float denom=1.0f/(va+vb+vc);
        const float v=vb*denom,w=vc*denom;
        weights={1-v-w,v,w};
        return a+ab*v+ac*w;
    }
    static bool finite(render::float3 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
    }
public:
    // Bounded O(LOD vertices x source triangles) reference projection;
    // at most eight million triangle checks per publication.
    // maxDistance is measured in model-space
    // units. Samples that are too far from the original mesh fail closed.
    // Indices must reference the same source vertex stream as morph CSR.
    static std::vector<LodSurfaceSample> project(
        std::span<const render::float3> source,
        std::span<const std::uint32_t> sourceIndices,
        std::span<const render::float3> lod,
        float maxDistance=0.05f) {
        using namespace render;
        if(source.empty()||source.size()>65536||lod.empty()||
           lod.size()>32768||sourceIndices.empty()||
           sourceIndices.size()%3!=0 || sourceIndices.size()/3>131072||
           !std::isfinite(maxDistance)||maxDistance<=0||maxDistance>10)
            throw std::invalid_argument("VIVID LOD surface: invalid mesh/budget/tolerance");
        for(const auto& p:source)if(!finite(p))
            throw std::invalid_argument("VIVID LOD surface: nonfinite source");
        for(auto index:sourceIndices)if(index>=source.size())
            throw std::invalid_argument("VIVID LOD surface: invalid source index");
        // Reject degenerate source triangles, which have unstable barycentrics.
        for(std::size_t i=0;i<sourceIndices.size();i+=3) {
            const auto a=source[sourceIndices[i]],b=source[sourceIndices[i+1]],
                       c=source[sourceIndices[i+2]];
            if(length(cross(b-a,c-a))<1.e-8f)
                throw std::invalid_argument("VIVID LOD surface: degenerate source triangle");
        }
        std::vector<LodSurfaceSample> result;
        result.reserve(lod.size());
        const float allowed=maxDistance*maxDistance;
        for(const auto& point:lod) {
            if(!finite(point))
                throw std::invalid_argument("VIVID LOD surface: nonfinite LOD position");
            LodSurfaceSample best;
            best.distanceSquared=std::numeric_limits<float>::max();
            bool found=false;
            for(std::size_t i=0;i<sourceIndices.size();i+=3) {
                const auto a=source[sourceIndices[i]],b=source[sourceIndices[i+1]],
                           c=source[sourceIndices[i+2]];
                float3 bary{};
                const auto candidate=closest(point,a,b,c,bary);
                const auto delta=point-candidate;
                const float distance=dot(delta,delta);
                if(distance<best.distanceSquared) {
                    best.triangle=std::uint32_t(i/3);
                    best.bary=bary;
                    best.distanceSquared=distance;
                    found=true;
                }
            }
            if(!found||best.distanceSquared>allowed)
                throw std::invalid_argument("VIVID LOD surface: LOD vertex outside projection tolerance");
            result.push_back(best);
        }
        return result;
    }
    // Median-split BVH reference bake for production-size source surfaces.
    // Same closest-triangle semantics as project(), with stable ID tie-break.
    // Triangle/vertex caps keep memory and traversal depth bounded.
    static std::vector<LodSurfaceSample> projectAccelerated(
        std::span<const render::float3> source,
        std::span<const std::uint32_t> sourceIndices,
        std::span<const render::float3> lod,
        float maxDistance=0.05f) {
        using namespace render;
        if(source.empty() || source.size()>1'000'000 ||
           lod.empty() || lod.size()>1'000'000 ||
           sourceIndices.empty() || sourceIndices.size()%3 ||
           sourceIndices.size()/3>2'000'000 ||
           !std::isfinite(maxDistance) || maxDistance<=0 || maxDistance>10)
            throw std::invalid_argument("VIVID LOD BVH: invalid input/budget");
        for(const auto& p:source)if(!finite(p))
            throw std::invalid_argument("VIVID LOD BVH: nonfinite source");
        for(const auto& p:lod)if(!finite(p))
            throw std::invalid_argument("VIVID LOD BVH: nonfinite target");
        for(const auto i:sourceIndices)if(i>=source.size())
            throw std::invalid_argument("VIVID LOD BVH: source index outside mesh");
        struct Node {
            float3 lo{},hi{};
            std::uint32_t left=0,right=0,begin=0,count=0;
        };
        const auto triangleCount=std::uint32_t(sourceIndices.size()/3);
        std::vector<std::uint32_t> order(triangleCount);
        std::iota(order.begin(),order.end(),0u);
        std::vector<Node> nodes;
        nodes.reserve(std::size_t(triangleCount)*2);
        auto triangle=[&](std::uint32_t t,int corner)->float3 {
            return source[sourceIndices[std::size_t(t)*3+std::size_t(corner)]];
        };
        auto component=[](float3 p,int axis) {
            return axis==0?p.x:axis==1?p.y:p.z;
        };
        auto expand=[](Node& node,float3 p) {
            node.lo=min3(node.lo,p);
            node.hi=max3(node.hi,p);
        };
        std::function<std::uint32_t(std::uint32_t,std::uint32_t)> build=
            [&](std::uint32_t begin,std::uint32_t count)->std::uint32_t {
            const float inf=std::numeric_limits<float>::max();
            Node node;
            node.lo={inf,inf,inf};
            node.hi={-inf,-inf,-inf};
            for(std::uint32_t i=begin;i<begin+count;++i)
                for(int corner=0;corner<3;++corner)
                    expand(node,triangle(order[i],corner));
            const auto id=std::uint32_t(nodes.size());
            nodes.push_back(node);
            if(count<=8) {
                nodes[id].begin=begin;
                nodes[id].count=count;
                return id;
            }
            const auto extent=node.hi-node.lo;
            const int axis=(extent.x>=extent.y&&extent.x>=extent.z)?0:
                           (extent.y>=extent.z)?1:2;
            const auto mid=begin+count/2;
            std::nth_element(order.begin()+begin,order.begin()+mid,
                order.begin()+begin+count,[&](auto lhs,auto rhs) {
                    const auto a=(triangle(lhs,0)+triangle(lhs,1)+triangle(lhs,2))*(1.0f/3.0f);
                    const auto b=(triangle(rhs,0)+triangle(rhs,1)+triangle(rhs,2))*(1.0f/3.0f);
                    const float av=component(a,axis),bv=component(b,axis);
                    return av==bv?lhs<rhs:av<bv;
                });
            const auto left=build(begin,count/2);
            const auto right=build(mid,count-count/2);
            nodes[id].left=left;nodes[id].right=right;
            return id;
        };
        for(std::uint32_t i=0;i<triangleCount;++i) {
            const auto a=triangle(i,0),b=triangle(i,1),c=triangle(i,2);
            if(length(cross(b-a,c-a))<1.e-8f)
                throw std::invalid_argument("VIVID LOD BVH: degenerate source triangle");
        }
        build(0,triangleCount);
        const auto allowed=maxDistance*maxDistance;
        std::vector<LodSurfaceSample> output;
        output.reserve(lod.size());
        auto distanceToBox=[](float3 p,const Node& node) {
            const float dx=std::max({node.lo.x-p.x,0.0f,p.x-node.hi.x});
            const float dy=std::max({node.lo.y-p.y,0.0f,p.y-node.hi.y});
            const float dz=std::max({node.lo.z-p.z,0.0f,p.z-node.hi.z});
            return dx*dx+dy*dy+dz*dz;
        };
        std::vector<std::uint32_t> stack;
        stack.reserve(64);
        for(const auto point:lod) {
            LodSurfaceSample best;
            best.distanceSquared=std::numeric_limits<float>::max();
            stack.clear();
            stack.push_back(0);
            while(!stack.empty()) {
                const auto id=stack.back();stack.pop_back();
                const auto& node=nodes[id];
                if(distanceToBox(point,node)>best.distanceSquared)continue;
                if(node.count) {
                    for(std::uint32_t i=node.begin;i<node.begin+node.count;++i) {
                        const auto tri=order[i];
                        float3 bary{};
                        const auto p=closest(point,triangle(tri,0),
                                            triangle(tri,1),triangle(tri,2),bary);
                        const auto delta=point-p;
                        const float dsq=dot(delta,delta);
                        if(dsq<best.distanceSquared ||
                          (dsq==best.distanceSquared&&tri<best.triangle)) {
                            best.triangle=tri;
                            best.bary=bary;
                            best.distanceSquared=dsq;
                        }
                    }
                }else {
                    const auto a=node.left,b=node.right;
                    const auto da=distanceToBox(point,nodes[a]);
                    const auto db=distanceToBox(point,nodes[b]);
                    // Process nearer subtree first; stable tie break by node ID.
                    if(da<db || (da==db && a<b)) {
                        stack.push_back(b);stack.push_back(a);
                    }else {
                        stack.push_back(a);stack.push_back(b);
                    }
                }
            }
            if(best.distanceSquared>allowed)
                throw std::invalid_argument("VIVID LOD BVH: outside projection tolerance");
            output.push_back(best);
        }
        return output;
    }
    static SparseMorphLibrary transfer(
        const SparseMorphLibrary& source,
        std::span<const std::uint32_t> sourceIndices,
        std::span<const LodSurfaceSample> mapping) {
        if(mapping.empty()||mapping.size()>16'000'000||
           sourceIndices.size()%3!=0)
            throw std::invalid_argument("VIVID LOD surface: invalid transfer topology");
        for(auto id:sourceIndices)if(id>=source.vertices())
            throw std::invalid_argument("VIVID LOD surface: source vertex outside CSR");
        for(const auto& sample:mapping) {
            if(sample.triangle>=sourceIndices.size()/3 ||
               !finite(sample.bary) || !std::isfinite(sample.distanceSquared) ||
               sample.distanceSquared<0)
                throw std::invalid_argument("VIVID LOD surface: invalid sample");
            const float sum=sample.bary.x+sample.bary.y+sample.bary.z;
            if(std::fabs(sum-1)>1.e-3f ||
               sample.bary.x< -1.e-4f || sample.bary.y< -1.e-4f ||
               sample.bary.z< -1.e-4f)
                throw std::invalid_argument("VIVID LOD surface: non-barycentric weights");
        }
        std::vector<MorphTargetInput> authored;
        authored.reserve(source.targets());
        for(std::size_t t=0;t<source.targets();++t) {
            MorphTargetInput target;
            target.name=std::string(source.targetName(t));
            std::vector<const GpuMorphDelta*> lookup(source.vertices(),nullptr);
            const auto range=source.gpuRanges()[t];
            for(std::uint32_t i=0;i<range.count;++i) {
                const auto& d=source.gpuDeltas()[range.first+i];
                lookup[d.vertex]=&d;
            }
            for(std::size_t v=0;v<mapping.size();++v) {
                const auto& sample=mapping[v];
                const std::size_t first=std::size_t(sample.triangle)*3;
                const auto ids=sourceIndices.subspan(first,3);
                const float weights[]={sample.bary.x,sample.bary.y,sample.bary.z};
                MorphDelta delta;
                delta.vertex=std::uint32_t(v);
                for(int corner=0;corner<3;++corner) {
                    const auto* d=lookup[ids[std::size_t(corner)]];
                    if(!d)continue;
                    for(int axis=0;axis<3;++axis) {
                        const auto i=std::size_t(axis);
                        delta.position[i]+=d->position[axis]*weights[corner];
                        delta.normal[i]+=d->normal[axis]*weights[corner];
                        delta.tangent[i]+=d->tangent[axis]*weights[corner];
                    }
                }
                // The core compiler strips zero deltas and preserves names.
                target.deltas.push_back(delta);
            }
            authored.push_back(std::move(target));
        }
        return SparseMorphLibrary::compile(mapping.size(),std::move(authored));
    }
};
} // namespace aiverse::vivid
