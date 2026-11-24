// Offline, genre-independent triangle mesh signed-distance baker.
// Distances at grid nodes are Euclidean; interpolated distances are approximate.
// Exact-position welding handles duplicate vertices at UV/material seams.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace aiverse::bake {
struct MeshDistanceGrid {
    render::float3 origin{};
    float spacing=1;
    uint32_t dimension=0;
    std::vector<float> samples; // x-fastest
};
namespace distance_detail {
struct D3 {
    double x=0,y=0,z=0;
    D3 operator+(D3 b)const{return {x+b.x,y+b.y,z+b.z};}
    D3 operator-(D3 b)const{return {x-b.x,y-b.y,z-b.z};}
    D3 operator*(double k)const{return {x*k,y*k,z*k};}
};
inline D3 d(render::float3 p){return {p.x,p.y,p.z};}
inline double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double len2(D3 p){return dot(p,p);}
inline double length(D3 p){return std::sqrt(len2(p));}
inline bool validPoint(render::float3 p){
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<=1e6f&&std::abs(p.y)<=1e6f&&std::abs(p.z)<=1e6f;
}
struct Tri { D3 a,b,c; };
// Closest-point triangle Voronoi regions, double precision offline.
inline double distance2(D3 p,const Tri& t){
    const D3 ab=t.b-t.a,ac=t.c-t.a,ap=p-t.a;
    const double d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return len2(ap);
    const D3 bp=p-t.b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return len2(bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return len2(ap-ab*(d1/(d1-d3)));
    const D3 cp=p-t.c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return len2(cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return len2(ap-ac*(d2/(d2-d6)));
    const double va=d3*d6-d5*d4;
    if(va<=0&&d4-d3>=0&&d5-d6>=0)
        return len2(bp-(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6))));
    const double inv=1/(va+vb+vc);
    return len2(ap-ab*(vb*inv)-ac*(vc*inv));
}
inline double solidAngle(D3 p,const Tri& t){
    const D3 a=t.a-p,b=t.b-p,c=t.c-p;
    const double la=length(a),lb=length(b),lc=length(c);
    return 2*std::atan2(dot(a,cross(b,c)),
        la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb);
}
}
inline constexpr uint32_t kMeshDistanceMaxTriangles=8192;
inline constexpr uint64_t kMeshDistanceMaxWork=16ull*1024*1024;
// Closed consistently wound triangle surfaces only. Exact seam weld; reject
// open/nonmanifold edges and degenerate triangles. Does not prove absence of
// self intersections or nonmanifold vertices. Nonzero winding defines solid.
// On error leave previous output untouched. No automatic mesh simplification.
inline bool bakeMeshDistanceGrid(const std::vector<render::float3>& positions,
        const std::vector<uint32_t>& indices,uint32_t dimension,
        const render::mat4& transform,MeshDistanceGrid& output,std::string& error){
    using namespace distance_detail;
    auto fail=[&](const char* why){error=why;return false;};
    if(dimension<4||dimension>16)return fail("SDF bake dimension must be 4..16");
    const size_t count=indices.size()/3;
    const uint64_t nodes=uint64_t(dimension)*dimension*dimension;
    if(indices.empty()||indices.size()%3||count>kMeshDistanceMaxTriangles||
       nodes*count>kMeshDistanceMaxWork||positions.size()>kMeshDistanceMaxTriangles*3u)
        return fail("SDF mesh/sample work budget exceeded or invalid triangles");
    for(float v:transform.m)if(!std::isfinite(v))return fail("invalid transform");
    if(transform.at(3,0)!=0||transform.at(3,1)!=0||transform.at(3,2)!=0||
       transform.at(3,3)!=1)return fail("SDF bake requires affine transform");
    std::map<std::array<float,3>,uint32_t> welded;
    std::vector<render::float3> points;
    std::vector<uint32_t> remap;
    remap.reserve(positions.size());
    for(const auto p:positions){
        if(!validPoint(p))return fail("invalid mesh position");
        const auto q=transform.transformPoint(p);
        if(!validPoint(q))return fail("invalid transformed mesh position");
        auto [it,added]=welded.emplace(std::array<float,3>{q.x,q.y,q.z},uint32_t(points.size()));
        if(added)points.push_back(q);
        remap.push_back(it->second);
    }
    struct Edge {unsigned count=0;int direction=0;};
    std::map<std::pair<uint32_t,uint32_t>,Edge> edges;
    std::vector<Tri> triangles;triangles.reserve(count);
    render::float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
    for(size_t i=0;i<indices.size();i+=3){
        std::array<uint32_t,3> v{};
        for(unsigned j=0;j<3;++j){
            if(indices[i+j]>=remap.size())return fail("triangle index out of range");
            v[j]=remap[indices[i+j]];
            mn=render::min3(mn,points[v[j]]);mx=render::max3(mx,points[v[j]]);
        }
        Tri tri{d(points[v[0]]),d(points[v[1]]),d(points[v[2]])};
        const D3 ab=tri.b-tri.a,ac=tri.c-tri.a;
        const double area2=len2(cross(ab,ac));
        if(area2<=1e-24||area2<=1e-16*len2(ab)*len2(ac))return fail("degenerate triangle");
        triangles.push_back(tri);
        for(unsigned j=0;j<3;++j){
            const auto a=v[j],b=v[(j+1)%3];
            auto& e=edges[{std::min(a,b),std::max(a,b)}];
            ++e.count;e.direction+=a<b?1:-1;
            if(e.count>2)return fail("nonmanifold edge");
        }
    }
    for(const auto& entry:edges)
        if(entry.second.count!=2||entry.second.direction!=0)return fail("open mesh or inconsistent triangle winding");
    const auto extent=mx-mn;
    const float maxExtent=std::max({extent.x,extent.y,extent.z});
    MeshDistanceGrid grid;
    grid.dimension=dimension;
    grid.spacing=std::max(0.01f,maxExtent/float(dimension-3));
    if(grid.spacing>1000||!std::isfinite(grid.spacing))return fail("SDF mesh bounds exceed lattice scale");
    const float half=grid.spacing*float(dimension-1)*0.5f;
    grid.origin=(mn+mx)*0.5f-render::float3{half,half,half};
    if(!validPoint(grid.origin)||!validPoint(grid.origin+render::float3{2*half,2*half,2*half}))
        return fail("SDF padded bounds exceed coordinate range");
    grid.samples.reserve(size_t(nodes));
    constexpr double fourPi=12.56637061435917295385;
    const double eps=std::max(1e-9,double(grid.spacing)*1e-7);
    for(uint32_t z=0;z<dimension;++z)for(uint32_t y=0;y<dimension;++y)for(uint32_t x=0;x<dimension;++x){
        const D3 p=d(grid.origin)+D3{double(x)*grid.spacing,double(y)*grid.spacing,double(z)*grid.spacing};
        double best=std::numeric_limits<double>::infinity(),angle=0;
        for(const auto& tri:triangles)best=std::min(best,distance2(p,tri));
        double distance=std::sqrt(best);
        if(!std::isfinite(distance)||distance>100000)return fail("invalid baked distance");
        if(distance<=eps)distance=0;
        else {
            for(const auto& tri:triangles)angle+=solidAngle(p,tri);
            const double winding=std::abs(angle/fourPi);
            if(!std::isfinite(winding)||std::abs(winding-std::round(winding))>0.05)
                return fail("ambiguous winding in mesh");
            if(winding>0.5)distance=-distance;
        }
        grid.samples.push_back(float(distance));
    }
    output=std::move(grid);error.clear();return true;
}
}
