// Offline, genre-independent triangle mesh signed-distance baker.
// Distances at grid nodes are Euclidean; interpolated distances are approximate.
// Exact-position welding handles duplicate vertices at UV/material seams.
#pragma once
#include "MeshDistanceQueries.h"
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
inline constexpr uint32_t kMeshDistanceMaxDimension=32;
inline constexpr uint32_t kMeshDistanceMaxTriangles=32768;
inline constexpr uint64_t kMeshDistanceMaxWork=64ull*1024*1024;
// Closed consistently wound triangle surfaces only. Exact seam weld; reject
// open/nonmanifold edges and degenerate triangles. Does not prove absence of
// self intersections or nonmanifold vertices. Nonzero winding defines solid.
// On error leave previous output untouched. No automatic mesh simplification.
inline bool bakeMeshDistanceGrid(const std::vector<render::float3>& positions,
        const std::vector<uint32_t>& indices,uint32_t dimension,
        const render::mat4& transform,MeshDistanceGrid& output,std::string& error,
        MeshDistanceQueryStats* stats=nullptr,uint64_t workLimit=kMeshDistanceMaxWork){
    using namespace distance_detail;
    QueryWork work;work.limit=std::min(workLimit,kMeshDistanceMaxWork);
    auto fail=[&](const char* why){error=why;if(stats)*stats=work.stats;return false;};
    if(dimension<4||dimension>kMeshDistanceMaxDimension)return fail("SDF bake dimension must be 4..32");
    const size_t count=indices.size()/3;
    const uint64_t nodes=uint64_t(dimension)*dimension*dimension;
    if(indices.empty()||indices.size()%3||count>kMeshDistanceMaxTriangles||
       positions.size()>kMeshDistanceMaxTriangles*3u)
        return fail("SDF mesh triangle/vertex budget exceeded or invalid triangles");
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
    const MeshDistanceBvh bvh(triangles);
    const double eps=std::max(1e-9,double(grid.spacing)*1e-7);
    for(uint32_t z=0;z<dimension;++z)for(uint32_t y=0;y<dimension;++y)for(uint32_t x=0;x<dimension;++x){
        const D3 p=d(grid.origin)+D3{double(x)*grid.spacing,double(y)*grid.spacing,double(z)*grid.spacing};
        double best=0;
        if(!bvh.nearestSquared(p,best,work))return fail("SDF traversal work budget exceeded");
        double distance=std::sqrt(best);
        if(!std::isfinite(distance)||distance>100000)return fail("invalid baked distance");
        if(distance<=eps)distance=0;
        else {
            bool inside=false,invalid=false;
            if(!bvh.inside(p,inside,work,invalid))
                return fail(invalid?"ambiguous winding in mesh":"SDF traversal work budget exceeded");
            if(inside)distance=-distance;
        }
        grid.samples.push_back(float(distance));
    }
    output=std::move(grid);error.clear();if(stats)*stats=work.stats;return true;
}
}
