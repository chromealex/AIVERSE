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
// Trim an exclusively owned isolated shaft section at an exact horizontal
// plane. Owner replaces this whole source, not arbitrary intersecting cave
// chunks. Adjacent owners must use identical clipping or be absent.
inline std::vector<world::Triangle> trimOwnedSdfShaft(
        std::span<const world::Triangle> source,Fixed plane){
    if(source.empty()||source.size()>32767)throw std::invalid_argument("Caverns shaft trim budget");
    std::vector<world::Triangle> out;
    const auto intersection=[&](Vec3 a,Vec3 b){
        if(surfaceVertexKey(b)<surfaceVertexKey(a))std::swap(a,b);
        const double t=double(plane.raw-a.y.raw)/double(b.y.raw-a.y.raw);
        const auto interpolate=[&](Fixed x,Fixed y){return Fixed::fromRaw(x.raw+
            int64_t(std::llround(double(y.raw-x.raw)*t)));};
        return Vec3{interpolate(a.x,b.x),plane,interpolate(a.z,b.z)};
    };
    for(const auto& tri:source){
        if(!world::SpatialWorld::validTriangle(tri))throw std::invalid_argument("Caverns malformed shaft source");
        const Vec3 v[]={tri.a,tri.b,tri.c};
        if(v[0].y==plane&&v[1].y==plane&&v[2].y==plane)
            throw std::invalid_argument("Caverns coplanar shaft cap");
        std::vector<Vec3> poly;
        const auto push=[&](Vec3 p){
            if(poly.empty()||surfaceVertexKey(poly.back())!=surfaceVertexKey(p))poly.push_back(p);
        };
        for(unsigned i=0;i<3;++i){
            const auto a=v[i],b=v[(i+1)%3];
            if(a.y<=plane)push(a);
            if((a.y<plane&&b.y>plane)||(a.y>plane&&b.y<plane))push(intersection(a,b));
        }
        if(poly.size()>1&&surfaceVertexKey(poly.front())==surfaceVertexKey(poly.back()))poly.pop_back();
        for(size_t i=1;i+1<poly.size();++i){
            world::Triangle t{poly[0],poly[i],poly[i+1]};
            if(!world::SpatialWorld::validTriangle(t))
                throw std::invalid_argument("Caverns degenerate trimmed shaft fragment");
            out.push_back(t);
        }
    }
    return out;
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
        if(v[0].y==plane&&v[1].y==plane&&v[2].y==plane)
            throw std::invalid_argument("Caverns retained shaft cap on transition plane");
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
// Pre-split the actual terrain boundary at the native volume grid before
// building the collar. The same refined plan MUST go to both owners.
// This addresses rim-edge crossings, not arbitrary noncollinear seams.
inline CaveSurfaceGroupPlan refineSurfaceGroupForRimGrid(CaveSurfaceGroupPlan plan,
        uint32_t resolution,int32_t spacingCm){
    if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000||
       plan.patches.size()>64)throw std::invalid_argument("Caverns rim grid refinement budget");
    const int64_t spanCm=int64_t(resolution)*spacingCm;
    if((spanCm*Fixed::kOne)%100)throw std::invalid_argument("Caverns rim grid Q16 span");
    const int64_t grid=Fixed::fromRatio(spanCm,100).raw;
    std::map<SurfaceEdgeKey,std::vector<SurfaceVertexKey>> splits;
    size_t splitCount=0;
    for(auto& rim:plan.rims){
        if(rim.ring.size()<3||rim.ring.size()>8192)throw std::invalid_argument("Caverns rim grid ring");
        std::vector<SurfaceVertexKey> ring;
        for(size_t i=0;i<rim.ring.size();++i){
            const auto from=rim.ring[i],to=rim.ring[(i+1)%rim.ring.size()];
            auto a=from,b=to;if(b<a)std::swap(a,b);
            if(a==b)throw std::invalid_argument("Caverns duplicate rim edge");
            std::vector<SurfaceVertexKey> points;
            for(unsigned axis=0;axis<3;++axis){
                const int64_t low=std::min(a[axis],b[axis]),high=std::max(a[axis],b[axis]);
                if(low==high)continue;
                const auto first=floorDiv(low,grid)+1;
                const auto last=floorDiv(high-1,grid);
                if(last-first>64)throw std::length_error("Caverns rim edge grid crossings");
                for(int64_t k=first;k<=last;++k){
                    const int64_t plane=k*grid;
                    const double t=double(plane-a[axis])/double(b[axis]-a[axis]);
                    SurfaceVertexKey p;
                    for(unsigned d=0;d<3;++d)p[d]=a[d]+int64_t(std::llround(double(b[d]-a[d])*t));
                    p[axis]=plane;points.push_back(p);
                }
            }
            std::sort(points.begin(),points.end());
            points.erase(std::unique(points.begin(),points.end()),points.end());
            if(!points.empty()){
                if(points.front()<=a||points.back()>=b||splitCount+points.size()>32768)
                    throw std::invalid_argument("Caverns rim grid precision/budget");
                splitCount+=points.size();
                if(!splits.emplace(SurfaceEdgeKey{a,b},points).second)
                    throw std::invalid_argument("Caverns duplicate grid rim ownership");
            }
            ring.push_back(from);
            if(from==a)ring.insert(ring.end(),points.begin(),points.end());
            else ring.insert(ring.end(),points.rbegin(),points.rend());
        }
        if(ring.size()>8192)throw std::length_error("Caverns refined rim budget");
        rim.ring=std::move(ring);
    }
    if(splits.empty())return plan;
    std::map<SurfaceEdgeKey,size_t> uses;
    size_t total=0;
    for(auto& [cell,patch]:plan.patches){
        (void)cell;
        std::vector<world::Triangle> next;
        for(const auto& t:patch.collision){
            const Vec3 original[]={t.a,t.b,t.c};
            std::vector<world::Triangle> fragments{t};
            for(unsigned side=0;side<3;++side){
                const auto edge=surfaceEdgeKey(original[side],original[(side+1)%3]);
                const auto split=splits.find(edge);if(split==splits.end())continue;
                bool replaced=false;
                for(size_t j=0;j<fragments.size()&&!replaced;++j){
                    const auto f=fragments[j];const Vec3 v[]={f.a,f.b,f.c};
                    for(unsigned e=0;e<3;++e){
                        if(surfaceEdgeKey(v[e],v[(e+1)%3])!=edge)continue;
                        auto points=split->second;
                        if(surfaceVertexKey(v[e])!=edge.first)std::reverse(points.begin(),points.end());
                        Vec3 a=v[e];const auto b=v[(e+1)%3],opposite=v[(e+2)%3];
                        std::vector<world::Triangle> pieces;
                        for(const auto& p:points){auto q=rimPoint(p);pieces.push_back({a,q,opposite});a=q;}
                        pieces.push_back({a,b,opposite});
                        for(const auto& piece:pieces)if(!world::SpatialWorld::validTriangle(piece))
                            throw std::invalid_argument("Caverns refined terrain triangle invalid");
                        fragments[j]=pieces.front();fragments.insert(fragments.end(),pieces.begin()+1,pieces.end());
                        ++uses[edge];replaced=true;break;
                    }
                }
                if(!replaced)throw std::invalid_argument("Caverns terrain rim split missing source edge");
            }
            if(fragments.size()>65534-next.size())throw std::length_error("Caverns refined tile triangle budget");
            next.insert(next.end(),fragments.begin(),fragments.end());
        }
        total+=next.size();if(total>1000000)throw std::length_error("Caverns refined surface group triangle budget");
        patch.collision.swap(next);
    }
    for(const auto& [edge,points]:splits){(void)points;if(uses[edge]!=1)
        throw std::invalid_argument("Caverns rim refinement edge incidence");}
    for(const auto& [cell,patch]:plan.patches)for(const unsigned axis:{0u,2u}){
        auto neighbor=cell;if(axis==0)++neighbor.x;else ++neighbor.z;
        const auto it=plan.patches.find(neighbor);if(it==plan.patches.end())continue;
        const auto plane=Fixed::fromInt(int64_t(axis==0?neighbor.x:neighbor.z)*plan.tileMetres).raw;
        if(surfaceTileEdge(patch,axis,plane)!=surfaceTileEdge(it->second,axis,plane))
            throw std::invalid_argument("Caverns refined terrain seam mismatch");
    }
    return plan;
}
} // namespace aiverse::terraweave::caverns
