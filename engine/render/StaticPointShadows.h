#pragma once
#include "Math3D.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::render {
struct StaticPointShadow {
    int resolution=0;
    float bias=.025f;
    float3 emitterNormal{}; // Nonzero: diffuse-only hemisphere emitter for a baked bounce.
    std::vector<float> distances;
    std::vector<float3> normals; // Hit-plane normals allow receiver-plane depth correction.
};
// Load-time acceleration structure for immutable opaque geometry. Two-sided occlusion.
class StaticShadowGeometry {
public:
    struct Triangle { float3 a,b,c; uint32_t surface=0; };
    std::vector<Triangle> triangles;
    std::vector<float3> albedos;
    static float3 cubeDirection(int face,float u,float v) {
        switch(face){case 0:return normalize(float3{1,v,-u});case 1:return normalize(float3{-1,v,u});
        case 2:return normalize(float3{u,1,-v});case 3:return normalize(float3{u,-1,v});
        case 4:return normalize(float3{u,v,1});default:return normalize(float3{-u,v,-1});}
    }
    void build(){nodes_.clear();if(!triangles.empty())split(0,uint32_t(triangles.size()));}
    float distance(float3 origin,float3 direction,float limit,float3* normal=nullptr,float3* albedo=nullptr) const {
        float3 n{};uint32_t surface=~0u;
        float hit=nodes_.empty()?limit:trace(0,origin,direction,limit,n,surface);
        if(normal)*normal=n;
        if(albedo)*albedo=surface<albedos.size()?albedos[surface]:float3{};
        return hit;
    }
    StaticPointShadow bake(float3 origin,float radius,int resolution) const {
        if(resolution<4||resolution>256||!std::isfinite(radius)||radius<=0)throw std::runtime_error("invalid point shadow bake");
        StaticPointShadow out;out.resolution=resolution;out.distances.reserve(size_t(6*resolution*resolution));
        out.normals.reserve(size_t(6*resolution*resolution));
        for(int face=0;face<6;++face)for(int y=0;y<resolution;++y)for(int x=0;x<resolution;++x){
            float3 normal;out.distances.push_back(distance(origin,cubeDirection(face,2*(x+.5f)/resolution-1,2*(y+.5f)/resolution-1),radius,&normal));out.normals.push_back(normal);
        }
        return out;
    }
private:
    struct Node {float3 lo,hi;uint32_t begin=0,count=0,left=0,right=0;};
    std::vector<Node> nodes_;
    static float axis(float3 p,int a){return a==0?p.x:a==1?p.y:p.z;}
    static float3 min3(float3 a,float3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
    static float3 max3(float3 a,float3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
    uint32_t split(uint32_t begin,uint32_t count){
        Node n;n.lo={1e30f,1e30f,1e30f};n.hi={-1e30f,-1e30f,-1e30f};n.begin=begin;n.count=count;
        for(uint32_t i=begin;i<begin+count;++i){const auto& t=triangles[i];n.lo=min3(n.lo,min3(t.a,min3(t.b,t.c)));n.hi=max3(n.hi,max3(t.a,max3(t.b,t.c)));}
        uint32_t index=uint32_t(nodes_.size());nodes_.push_back(n);
        if(count>8){
            auto extent=n.hi-n.lo;int a=extent.x>extent.y?0:1;if(extent.z>axis(extent,a))a=2;
            auto mid=begin+count/2;
            std::nth_element(triangles.begin()+begin,triangles.begin()+mid,triangles.begin()+begin+count,[a](const Triangle& x,const Triangle& y){return axis(x.a+x.b+x.c,a)<axis(y.a+y.b+y.c,a);});
            auto left=split(begin,count/2),right=split(mid,count-count/2);
            nodes_[index].left=left;nodes_[index].right=right;nodes_[index].count=0;
        }
        return index;
    }
    static bool hitBox(const Node& n,float3 o,float3 d,float limit){
        float lo=0,hi=limit;
        for(int a=0;a<3;++a){float v=axis(d,a),p=axis(o,a),mn=axis(n.lo,a),mx=axis(n.hi,a);
            if(std::abs(v)<1e-10f){if(p<mn||p>mx)return false;continue;}
            float x=(mn-p)/v,y=(mx-p)/v;if(x>y)std::swap(x,y);lo=std::max(lo,x);hi=std::min(hi,y);if(lo>hi)return false;}
        return true;
    }
    float trace(uint32_t index,float3 o,float3 d,float limit,float3& normal,uint32_t& surface) const {
        const auto& n=nodes_[index];if(!hitBox(n,o,d,limit))return limit;
        if(!n.count){limit=trace(n.left,o,d,limit,normal,surface);return trace(n.right,o,d,limit,normal,surface);}
        for(uint32_t i=n.begin;i<n.begin+n.count;++i){const auto& t=triangles[i];auto e1=t.b-t.a,e2=t.c-t.a,p=cross(d,e2);float det=dot(e1,p);
            if(std::abs(det)<1e-10f)continue;
            float inv=1/det;auto s=o-t.a;float u=dot(s,p)*inv;if(u<0||u>1)continue;
            auto q=cross(s,e1);float v=dot(d,q)*inv;if(v<0||u+v>1)continue;
            float hit=dot(e2,q)*inv;if(hit>.001f&&hit<limit){limit=hit;normal=normalize(cross(e1,e2));surface=t.surface;}
        }
        return limit;
    }
};
}
