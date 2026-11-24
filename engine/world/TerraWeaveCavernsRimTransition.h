#pragma once
// Prepare-only constrained collar. The volume owner supplies an isolated,
// already-trimmed SDF shaft section. Do not feed untrimmed shaft walls into
// this adapter or publish the collar as an overlay on the removed section.
#include "engine/world/TerraWeaveCavernsSurfaceGroup.h"
#include <cmath>
#include <span>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {
inline Vec3 rimPoint(const SurfaceVertexKey& k){
    return {Fixed::fromRaw(k[0]),Fixed::fromRaw(k[1]),Fixed::fromRaw(k[2])};
}
using DirectedRimEdges=std::map<SurfaceEdgeKey,std::pair<SurfaceVertexKey,SurfaceVertexKey>>;
inline DirectedRimEdges retainedSdfPlaneBoundary(
        std::span<const world::Triangle> retained,Fixed plane){
    if(retained.empty()||retained.size()>65534)throw std::invalid_argument("Caverns retained shaft budget");
    struct Incidence {unsigned count=0;SurfaceVertexKey from{},to{};};
    std::map<SurfaceEdgeKey,Incidence> edges;
    for(const auto& tri:retained){
        if(!world::SpatialWorld::validTriangle(tri))throw std::invalid_argument("Caverns retained shaft triangle");
        const Vec3 v[]={tri.a,tri.b,tri.c};
        for(unsigned i=0;i<3;++i){
            if(v[i].y>plane)throw std::invalid_argument("Caverns retained shaft overlaps transition band");
            if(v[i].y!=plane||v[(i+1)%3].y!=plane)continue;
            const auto a=surfaceVertexKey(v[i]),b=surfaceVertexKey(v[(i+1)%3]);
            if(a==b)throw std::invalid_argument("Caverns degenerate rim edge");
            auto& incidence=edges[surfaceEdgeKey(v[i],v[(i+1)%3])];
            if(!incidence.count){incidence.from=a;incidence.to=b;}
            else if(incidence.count!=1||incidence.from!=b||incidence.to!=a)
                throw std::invalid_argument("Caverns nonmanifold SDF cut plane");
            ++incidence.count;
        }
    }
    DirectedRimEdges out;
    for(const auto& [key,e]:edges)if(e.count==1)out.emplace(key,std::make_pair(e.from,e.to));
    if(out.size()<3||out.size()>8192)throw std::invalid_argument("Caverns SDF cut plane boundary count");
    return out;
}
struct CaveRimTransition {
    uint64_t entranceId=0,surfaceEpoch=0,authorityRevision=0;
    std::vector<world::Triangle> triangles;
    std::set<SurfaceEdgeKey> terrainBoundary,volumeBoundary;
};
// Angularly ordered star-shaped rings only. Concave/multiple rings and
// coincident radial edges are refused rather than bridged speculatively.
inline std::vector<SurfaceVertexKey> orderShaftRing(
        const std::set<SurfaceEdgeKey>& edges,const Entrance& entrance){
    if(edges.size()<3||edges.size()>8192)throw std::invalid_argument("Caverns collar ring budget");
    std::map<SurfaceVertexKey,unsigned> degree;
    for(const auto& [a,b]:edges){++degree[a];++degree[b];}
    const double cx=double(entrance.xCm)/100.,cz=double(entrance.zCm)/100.;
    const double radius=double(entrance.radiusCm)/100.;
    const auto angle=[&](const SurfaceVertexKey& k){
        const auto p=rimPoint(k);
        return std::atan2(p.z.toDouble()-cz,p.x.toDouble()-cx);
    };
    std::vector<SurfaceVertexKey> ring;
    for(const auto& [v,n]:degree){
        const auto p=rimPoint(v);
        const double r=std::hypot(p.x.toDouble()-cx,p.z.toDouble()-cz);
        if(n!=2||r<radius*.5||r>radius*1.5)throw std::invalid_argument("Caverns unsupported shaft ring");
        ring.push_back(v);
    }
    std::sort(ring.begin(),ring.end(),[&](const auto& a,const auto& b){return angle(a)<angle(b);});
    constexpr double tau=6.2831853071795864769;
    for(size_t i=0;i<ring.size();++i){
        auto a=ring[i],b=ring[(i+1)%ring.size()];
        double gap=angle(b)-angle(a);if(i+1==ring.size())gap+=tau;
        if(gap<=1e-9||gap>=tau/4)throw std::invalid_argument("Caverns non-star-shaped or sparse shaft ring");
        if(b<a)std::swap(a,b);
        if(!edges.contains({a,b}))throw std::invalid_argument("Caverns disconnected or nonradial shaft ring");
    }
    return ring;
}
inline CaveRimTransition buildCaveRimTransition(const CaveSurfaceRim& terrain,
        const Entrance& entrance,std::span<const world::Triangle> retained,
        Fixed lowerPlane,uint64_t surfaceEpoch,uint64_t authorityRevision){
    if(terrain.entranceId!=entrance.id||!surfaceEpoch||!authorityRevision||
       entrance.radiusCm<=0||terrain.ring.size()<3||terrain.ring.size()>8192)
        throw std::invalid_argument("Caverns collar identity");
    CaveRimTransition out;out.entranceId=entrance.id;out.surfaceEpoch=surfaceEpoch;
    out.authorityRevision=authorityRevision;
    for(size_t i=0;i<terrain.ring.size();++i){
        const auto a=rimPoint(terrain.ring[i]),b=rimPoint(terrain.ring[(i+1)%terrain.ring.size()]);
        if(a.y<=lowerPlane||b.y<=lowerPlane||!out.terrainBoundary.insert(surfaceEdgeKey(a,b)).second)
            throw std::invalid_argument("Caverns collar terrain ring height or duplicate edge");
    }
    const auto directed=retainedSdfPlaneBoundary(retained,lowerPlane);
    for(const auto& [edge,direction]:directed){(void)direction;out.volumeBoundary.insert(edge);}
    const auto upper=orderShaftRing(out.terrainBoundary,entrance);
    const auto lower=orderShaftRing(out.volumeBoundary,entrance);
    const auto angle=[&](const SurfaceVertexKey& k){
        const auto p=rimPoint(k);
        return std::atan2(p.z.toDouble()-double(entrance.zCm)/100.,
                          p.x.toDouble()-double(entrance.xCm)/100.);
    };
    // The lower loop must have a consistent directed boundary. Reverse the
    // collar winding if needed so shared lower edges have opposite incidence.
    bool flip=false;
    for(size_t i=0;i<lower.size();++i){
        auto a=lower[i],b=lower[(i+1)%lower.size()];
        const auto edge=surfaceEdgeKey(rimPoint(a),rimPoint(b));
        const bool forward=directed.at(edge).first==a;
        if(i==0)flip=!forward;
        else if(flip==forward)throw std::invalid_argument("Caverns inconsistent retained rim winding");
    }
    const auto emit=[&](SurfaceVertexKey a,SurfaceVertexKey b,SurfaceVertexKey c){
        if(flip)std::swap(b,c);
        world::Triangle tri{rimPoint(a),rimPoint(b),rimPoint(c)};
        if(!world::SpatialWorld::validTriangle(tri))throw std::invalid_argument("Caverns degenerate collar fragment");
        out.triangles.push_back(tri);
    };
    constexpr double tau=6.2831853071795864769;
    // Start with the last vertices before the common angular sweep origin;
    // advance whichever ring has the next angle (zipper with exact edges).
    size_t i=0,j=0;
    auto a=upper.back(),b=lower.back();
    while(i<upper.size()||j<lower.size()){
        const double ua=i<upper.size()?angle(upper[i]):tau;
        const double la=j<lower.size()?angle(lower[j]):tau;
        if(ua<=la){emit(a,upper[i],b);a=upper[i++];}
        else {emit(a,lower[j],b);b=lower[j++];}
    }
    std::map<SurfaceEdgeKey,unsigned> incidence;
    for(const auto& t:out.triangles){
        const Vec3 v[]={t.a,t.b,t.c};
        for(unsigned k=0;k<3;++k)if(++incidence[surfaceEdgeKey(v[k],v[(k+1)%3])]>2)
            throw std::invalid_argument("Caverns collar nonmanifold");
    }
    auto expected=out.terrainBoundary;
    expected.insert(out.volumeBoundary.begin(),out.volumeBoundary.end());
    std::set<SurfaceEdgeKey> actual;
    for(const auto& [edge,n]:incidence)if(n==1)actual.insert(edge);
    if(actual!=expected||!matchesCaveSurfaceRim(terrain,out.terrainBoundary))
        throw std::invalid_argument("Caverns collar boundary mismatch");
    return out;
}
} // namespace aiverse::terraweave::caverns
