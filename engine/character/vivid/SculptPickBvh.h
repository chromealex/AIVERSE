// VIVID reusable topology BVH for editor-only deformed mesh ray picking.
// Build once per published Model/mesh; refit bounds for each new animated
// world-space surface. The source render::Model and ECS are never modified.
#pragma once
#include "SkinnedWeightPick.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace aiverse::vivid {

struct SculptPickBvhStats {
    std::size_t triangles=0;
    std::size_t visitedNodes=0;
    std::size_t testedTriangles=0;
};

class SculptPickBvh final {
    struct Triangle {
        std::uint32_t a=0,b=0,c=0;
        std::uint32_t ordinal=0; // deterministic first-triangle tie-break
    };
    struct Node {
        render::float3 lo{},hi{};
        std::uint32_t left=0,right=0,begin=0,count=0; // count > 0 for leaf
    };
    std::vector<Triangle> triangles_;
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> traversal_;
    std::size_t mesh_=std::numeric_limits<std::size_t>::max();
    std::size_t vertices_=0;
    SculptPickBvhStats stats_{};

    static float component(render::float3 p,int axis) noexcept {
        return axis==0?p.x:axis==1?p.y:p.z;
    }
    static bool finite(render::float3 p) noexcept {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    }
    static bool intersectBox(render::float3 origin,render::float3 direction,
                             render::float3 lo,render::float3 hi,
                             float maxDistance) noexcept {
        float near=0.0f,far=maxDistance;
        for(int axis=0;axis<3;++axis) {
            const float o=component(origin,axis),d=component(direction,axis);
            const float a=component(lo,axis),b=component(hi,axis);
            if(std::fabs(d)<1.e-10f) {
                if(o<a||o>b)return false;
            }else {
                float x=(a-o)/d,y=(b-o)/d;
                if(x>y)std::swap(x,y);
                near=std::max(near,x);
                far=std::min(far,y);
                if(near>far)return false;
            }
        }
        return far>1.e-5f;
    }

public:
    void clear() noexcept {
        triangles_.clear();
        order_.clear();
        nodes_.clear();
        traversal_.clear();
        mesh_=std::numeric_limits<std::size_t>::max();
        vertices_=0;
        stats_={};
    }
    bool ready() const noexcept {return !nodes_.empty();}
    std::size_t mesh() const noexcept {return mesh_;}
    std::size_t vertices() const noexcept {return vertices_;}
    const SculptPickBvhStats& stats() const noexcept {return stats_;}

    // Keep all mesh triangle faces, even those referencing non-authorable
    // LODs. Source IDs are checked AFTER ray visibility, same as the CPU
    // brute-force reference's optional author-map filtering.
    bool build(const render::Model& model,std::size_t meshId) {
        using namespace render;
        if(meshId>=model.meshes.size() || model.vertices.empty() ||
           model.vertices.size()>1000000u)return false;
        std::size_t count=0;
        for(const auto& primitive:model.meshes[meshId].primitives) {
            const std::size_t first=primitive.firstIndex;
            if(first>model.indices.size() ||
               primitive.indexCount>model.indices.size()-first ||
               primitive.indexCount%3u)return false;
            count+=primitive.indexCount/3u;
            if(count>1000000u)return false;
        }
        if(count==0)return false;
        std::vector<Triangle> triangles;
        triangles.reserve(count);
        std::vector<float3> centroids;
        centroids.reserve(count);
        for(const auto& primitive:model.meshes[meshId].primitives) {
            for(std::size_t i=primitive.firstIndex;
                i<primitive.firstIndex+primitive.indexCount;i+=3) {
                const auto a=model.indices[i],b=model.indices[i+1],
                           d=model.indices[i+2];
                if(a>=model.vertices.size()||b>=model.vertices.size()||
                   d>=model.vertices.size())return false;
                const auto pa=model.vertices[a].pos,
                           pb=model.vertices[b].pos,
                           pc=model.vertices[d].pos;
                if(!finite(pa)||!finite(pb)||!finite(pc))return false;
                triangles.push_back({a,b,d,std::uint32_t(triangles.size())});
                centroids.push_back((pa+pb+pc)*(1.f/3.f));
            }
        }
        std::vector<std::uint32_t> order(count);
        std::iota(order.begin(),order.end(),0u);
        std::vector<Node> nodes;
        nodes.reserve(count/4+8);
        const auto buildRange=[&](auto&& self,std::uint32_t first,
                                  std::uint32_t n)->std::uint32_t {
            const auto id=std::uint32_t(nodes.size());
            nodes.emplace_back();
            if(n<=8) {
                nodes[id].begin=first;
                nodes[id].count=n;
                return id;
            }
            const float inf=std::numeric_limits<float>::max();
            float3 lo{inf,inf,inf},hi{-inf,-inf,-inf};
            for(std::size_t i=first;i<std::size_t(first)+n;++i) {
                lo=min3(lo,centroids[order[i]]);
                hi=max3(hi,centroids[order[i]]);
            }
            const auto extent=hi-lo;
            const int axis=extent.x>=extent.y&&extent.x>=extent.z?0:
                           extent.y>=extent.z?1:2;
            const auto mid=first+n/2;
            std::nth_element(order.begin()+first,order.begin()+mid,
                             order.begin()+first+n,
                [&](std::uint32_t a,std::uint32_t b) {
                    const float lhs=component(centroids[a],axis),
                                rhs=component(centroids[b],axis);
                    return lhs!=rhs?lhs<rhs:a<b;
                });
            const auto left=self(self,first,mid-first);
            const auto right=self(self,mid,first+n-mid);
            nodes[id].left=left;
            nodes[id].right=right;
            return id;
        };
        (void)buildRange(buildRange,0u,std::uint32_t(count));
        triangles_.swap(triangles);
        order_.swap(order);
        nodes_.swap(nodes);
        traversal_.reserve(nodes_.size()>64?64:nodes_.size());
        mesh_=meshId;
        vertices_=model.vertices.size();
        stats_={};
        stats_.triangles=count;
        return true;
    }

    // Refit all boxes from the *actual* morph+skin world positions. The
    // topology partition persists while animation changes positions.
    // A newly published model or changed topology requires clear()/build().
    bool refit(const SkinnedWeightSurface& surface) noexcept {
        using namespace render;
        if(!ready() || !surface.ready || surface.mesh!=mesh_ ||
           surface.world.size()!=vertices_ ||
           surface.included.size()!=vertices_)return false;
        const float inf=std::numeric_limits<float>::max();
        for(std::size_t k=nodes_.size();k-- > 0;) {
            auto& node=nodes_[k];
            if(node.count) {
                float3 lo{inf,inf,inf},hi{-inf,-inf,-inf};
                for(std::size_t j=node.begin;j<std::size_t(node.begin)+node.count;++j) {
                    const auto& t=triangles_[order_[j]];
                    if(!surface.included[t.a]||!surface.included[t.b]||
                       !surface.included[t.c])return false;
                    for(const auto vertex:{t.a,t.b,t.c}) {
                        const auto p=surface.world[vertex];
                        if(!finite(p))return false;
                        lo=min3(lo,p);
                        hi=max3(hi,p);
                    }
                }
                node.lo=lo;
                node.hi=hi;
            }else {
                node.lo=min3(nodes_[node.left].lo,nodes_[node.right].lo);
                node.hi=max3(nodes_[node.left].hi,nodes_[node.right].hi);
            }
        }
        return true;
    }

    // Does not allocate after first traversal reserve growth. Source map
    // returns original IDs for seam clones; non-authorable LOD triangles
    // are ignored exactly like the brute-force reference.
    SculptPick pick(const SkinnedWeightSurface& surface,
                    render::float3 origin,render::float3 direction,
                    std::size_t sourceVertexCount,
                    std::span<const std::uint32_t> authorIds={}) {
        using namespace render;
        SculptPick best;
        stats_.visitedNodes=stats_.testedTriangles=0;
        if(!ready() || !surface.ready || surface.mesh!=mesh_ ||
           surface.world.size()!=vertices_ ||
           surface.included.size()!=vertices_ ||
           (!authorIds.empty()&&authorIds.size()!=vertices_)||
           !finite(origin)||!finite(direction))return best;
        const float lengthDir=length(direction);
        if(!std::isfinite(lengthDir)||lengthDir<=1.e-8f)return best;
        direction=direction*(1.f/lengthDir);
        traversal_.clear();
        traversal_.push_back(0u);
        std::uint32_t bestOrdinal=std::numeric_limits<std::uint32_t>::max();
        while(!traversal_.empty()) {
            const auto id=traversal_.back();
            traversal_.pop_back();
            const auto& node=nodes_[id];
            ++stats_.visitedNodes;
            if(!intersectBox(origin,direction,node.lo,node.hi,
                             best.rayDistance))continue;
            if(!node.count) {
                traversal_.push_back(node.right);
                traversal_.push_back(node.left);
                continue;
            }
            for(std::size_t j=node.begin;j<std::size_t(node.begin)+node.count;++j) {
                const auto& t=triangles_[order_[j]];
                const auto sa=authorIds.empty()?t.a:authorIds[t.a];
                const auto sb=authorIds.empty()?t.b:authorIds[t.b];
                const auto sc=authorIds.empty()?t.c:authorIds[t.c];
                if(sa>=sourceVertexCount||sb>=sourceVertexCount||
                   sc>=sourceVertexCount)continue;
                ++stats_.testedTriangles;
                const auto a=surface.world[t.a],b=surface.world[t.b],
                           c=surface.world[t.c];
                const auto ab=b-a,ac=c-a,pvec=cross(direction,ac);
                const float det=dot(ab,pvec);
                if(std::fabs(det)<=1.e-8f)continue;
                const float inv=1.f/det;
                const auto v0=origin-a;
                const float u=dot(v0,pvec)*inv;
                if(u<0.f||u>1.f)continue;
                const auto q=cross(v0,ab);
                const float v=dot(direction,q)*inv;
                if(v<0.f||u+v>1.f)continue;
                const float distance=dot(ac,q)*inv;
                if(!std::isfinite(distance)||distance<=1.e-5f||
                   distance>best.rayDistance ||
                   (distance==best.rayDistance&&t.ordinal>=bestOrdinal))
                    continue;
                const auto hit=origin+direction*distance;
                const float da=length(hit-a),db=length(hit-b),
                            dc=length(hit-c);
                best.hit=true;
                best.rayDistance=distance;
                bestOrdinal=t.ordinal;
                if(da<=db&&da<=dc){best.vertex=sa;best.worldPosition=a;}
                else if(db<=dc){best.vertex=sb;best.worldPosition=b;}
                else{best.vertex=sc;best.worldPosition=c;}
            }
        }
        return best;
    }
};

} // namespace aiverse::vivid
