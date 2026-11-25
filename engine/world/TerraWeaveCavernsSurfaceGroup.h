#pragma once
// Pure preflight: all terrain patches come from ONE frozen Earthworks
// revision and fixed resolution. No GPU/physics mutation here.
#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include <map>
#include <set>
#include <span>
#include <tuple>
#include <memory>
#include <string>

namespace aiverse::terraweave::caverns {
using SurfaceVertexKey=std::array<int64_t,3>;
using SurfaceEdgeKey=std::pair<SurfaceVertexKey,SurfaceVertexKey>;
inline SurfaceVertexKey surfaceVertexKey(Vec3 p){return {p.x.raw,p.y.raw,p.z.raw};}
inline SurfaceEdgeKey surfaceEdgeKey(Vec3 a,Vec3 b){
    auto x=surfaceVertexKey(a),y=surfaceVertexKey(b);
    if(y<x)std::swap(x,y);
    return {x,y};
}
// Exact shared-edge segment certificate. Reject T-junctions rather than
// silently treating a different triangulation as a certified seam.
inline std::set<SurfaceEdgeKey> surfaceTileEdge(const TerrainPatch& patch,
                                               unsigned axis,int64_t planeRaw){
    std::set<SurfaceEdgeKey> out;
    for(const auto& tri:patch.collision){
        const Vec3 v[]={tri.a,tri.b,tri.c};
        for(unsigned i=0;i<3;++i){
            const auto a=surfaceVertexKey(v[i]),b=surfaceVertexKey(v[(i+1)%3]);
            if(a[axis]==planeRaw&&b[axis]==planeRaw&&a!=b)
                out.insert(surfaceEdgeKey(v[i],v[(i+1)%3]));
        }
    }
    return out;
}
struct CaveSurfaceRim {
    uint64_t entranceId=0;
    // Ordered exact Q16 vertices of the cut boundary, closed implicitly.
    std::vector<SurfaceVertexKey> ring;
};
inline CaveSurfaceRim extractCaveSurfaceRimTriangles(
        std::span<const world::Triangle> triangles,const Entrance& e,
        size_t& work,size_t maxWork=4000000) {
    constexpr unsigned sides=16;
    constexpr double epsilonCm=0.01; // larger than one native Q16 unit
    const auto point=[](const SurfaceVertexKey& v){
        const auto cm=[](int64_t raw){return double(raw)*100./Fixed::kOne;};
        return CutVertexCm{cm(v[0]),cm(v[1]),cm(v[2])};
    };
    std::map<SurfaceEdgeKey,unsigned> edges;
    for(const auto& tri:triangles){
            if(++work>maxWork)throw std::length_error("Caverns rim extraction budget");
            const Vec3 v[]={tri.a,tri.b,tri.c};
            for(unsigned i=0;i<3;++i){
                const auto key=surfaceEdgeKey(v[i],v[(i+1)%3]);
                if(key.first==key.second)continue;
                const auto a=point(key.first),b=point(key.second);
                bool inside=true,onSide=false;
                for(unsigned side=0;side<sides;++side){
                    const auto da=cutPlane(a,e,side,sides),db=cutPlane(b,e,side,sides);
                    if(da>epsilonCm||db>epsilonCm){inside=false;break;}
                    if(std::abs(da)<=epsilonCm&&std::abs(db)<=epsilonCm)onSide=true;
                }
                if(inside&&onSide&&++edges[key]>2)
                    throw std::invalid_argument("Caverns nonmanifold terrain rim");
            }
    }
    std::map<SurfaceVertexKey,std::vector<SurfaceVertexKey>> adjacency;
    for(const auto& [edge,count]:edges)if(count==1){
        adjacency[edge.first].push_back(edge.second);
        adjacency[edge.second].push_back(edge.first);
    }
    if(adjacency.size()<3||adjacency.size()>65534)
        throw std::invalid_argument("Caverns missing or oversized terrain rim");
    for(auto& [v,neighbors]:adjacency){
        (void)v;
        if(neighbors.size()!=2)throw std::invalid_argument("Caverns terrain rim is not a closed cycle");
        std::sort(neighbors.begin(),neighbors.end());
    }
    CaveSurfaceRim rim;rim.entranceId=e.id;
    const auto first=adjacency.begin()->first;
    auto previous=first,current=first;
    do {
        if(rim.ring.size()>=adjacency.size())throw std::invalid_argument("Caverns rim cycle overflow");
        rim.ring.push_back(current);
        const auto& next=adjacency.at(current);
        const auto selected=current==first?next.front():(next.front()==previous?next.back():next.front());
        previous=current;current=selected;
    }while(current!=first);
    if(rim.ring.size()!=adjacency.size())throw std::invalid_argument("Caverns disconnected terrain rim");
    return rim;
}
inline CaveSurfaceRim extractCaveSurfaceRim(
        const std::map<world::Cell,TerrainPatch>& patches,const Entrance& e,
        size_t& work,size_t maxWork=4000000) {
    std::vector<world::Triangle> triangles;
    for(const auto& [cell,patch]:patches){
        const int64_t span=int64_t(patch.cellSizeMetres)*100;
        const int64_t x=int64_t(cell.x)*span,z=int64_t(cell.z)*span;
        if(!overlaps({x,z,x+span,z+span},entranceBounds(e)))continue;
        if(triangles.size()>maxWork||patch.collision.size()>maxWork-triangles.size())
            throw std::length_error("Caverns rim extraction source budget");
        triangles.insert(triangles.end(),patch.collision.begin(),patch.collision.end());
    }
    return extractCaveSurfaceRimTriangles(triangles,e,work,maxWork);
}
// Native volume transition owner must supply the actual final boundary,
// not an ideal analytic circle. Exact shared Q16 edges are required.
inline bool matchesCaveSurfaceRim(const CaveSurfaceRim& rim,
        const std::set<SurfaceEdgeKey>& volumeBoundary){
    if(rim.ring.size()<3)return false;
    std::set<SurfaceEdgeKey> terrainBoundary;
    for(size_t i=0;i<rim.ring.size();++i){
        auto a=rim.ring[i],b=rim.ring[(i+1)%rim.ring.size()];
        if(b<a)std::swap(a,b);
        if(!terrainBoundary.emplace(a,b).second)return false;
    }
    return terrainBoundary==volumeBoundary;
}
struct CaveSurfaceGroupPlan {
    uint64_t soilRevision=0,authorityRevision=0,surfaceEpoch=0;
    int32_t tileMetres=0;
    uint32_t resolution=0;
    std::map<world::Cell,TerrainPatch> patches;
    std::vector<uint64_t> acceptedEntrances,rejectedEntrances;
    size_t certifiedEdges=0;
    std::vector<CaveSurfaceRim> rims;
};
inline CaveSurfaceGroupPlan planCaveSurfaceGroup(const EarthworksLayer& earth,
        const CaveVolumeSnapshot& authored,std::span<const world::Cell> pinned,
        int32_t tileMetres,uint32_t resolution,uint64_t epoch){
    if(!epoch||pinned.empty()||pinned.size()>64||tileMetres<16||tileMetres>4096||
       resolution<2||resolution>129||pinned.size()*size_t(resolution-1)*(resolution-1)*2>1000000)
        throw std::invalid_argument("Caverns surface group budget");
    auto snapshot=authored;canonicalizeCaveSnapshot(snapshot);
    CaveSurfaceGroupPlan plan;
    plan.soilRevision=earth.revision();plan.authorityRevision=snapshot.sourceRevision;
    plan.surfaceEpoch=epoch;plan.tileMetres=tileMetres;plan.resolution=resolution;
    for(auto cell:pinned){
        if(!plan.patches.emplace(cell,bakeEarthworksTerrainPatch(earth,cell,tileMetres,resolution)).second)
            throw std::invalid_argument("Caverns duplicate pinned tile");
    }
    const int64_t span=int64_t(tileMetres)*100;
    std::vector<Entrance> accepted;
    size_t checks=0;
    constexpr size_t maxChecks=4000000;
    for(const auto& e:snapshot.entrances){
        const auto b=entranceBounds(e);
        const auto ax=floorDiv(b.minX,span),az=floorDiv(b.minZ,span);
        const auto bx=floorDiv(b.maxX-1,span),bz=floorDiv(b.maxZ-1,span);
        bool safe=bx-ax<64&&bz-az<64&&(bx-ax+1)*(bz-az+1)<=64;
        bool touches=false;
        for(const auto& [cell,p]:plan.patches){
            (void)p;
            if(cell.x>=ax&&cell.x<=bx&&cell.z>=az&&cell.z<=bz){touches=true;break;}
        }
        if(!touches)continue;
        double low=1e30,high=-1e30;
        for(int64_t z=az;safe&&z<=bz;++z)for(int64_t x=ax;safe&&x<=bx;++x){
            if(x<INT32_MIN||x>INT32_MAX||z<INT32_MIN||z>INT32_MAX){safe=false;break;}
            const auto it=plan.patches.find({int32_t(x),0,int32_t(z)});
            if(it==plan.patches.end()){safe=false;break;} // no partial footprint
            for(const auto& tri:it->second.collision){
                if(++checks>maxChecks)throw std::length_error("Caverns surface group certification budget");
                const CutTriangleCm t{toCutVertex(tri.a),toCutVertex(tri.b),toCutVertex(tri.c)};
                const double minX=std::min({t.a.x,t.b.x,t.c.x}),maxX=std::max({t.a.x,t.b.x,t.c.x});
                const double minZ=std::min({t.a.z,t.b.z,t.c.z}),maxZ=std::max({t.a.z,t.b.z,t.c.z});
                if(maxX<b.minX||minX>=b.maxX||maxZ<b.minZ||minZ>=b.maxZ)continue;
                low=std::min({low,t.a.y,t.b.y,t.c.y});high=std::max({high,t.a.y,t.b.y,t.c.y});
                if(!entranceIntersectsTriangleAtHeight(t,e)){safe=false;break;}
            }
        }
        if(safe)for(const auto& stamp:snapshot.stamps){
            if(++checks>maxChecks)throw std::length_error("Caverns surface group fill budget");
            if(stamp.mode!=CaveStampMode::Fill)continue;
            const auto& p=stamp.center;const auto r=stamp.radiusCm;
            if(p.x+r>=b.minX&&p.x-r<=b.maxX&&p.z+r>=b.minZ&&p.z-r<=b.maxZ&&
               p.y+r>=low&&p.y-r<=high){safe=false;break;}
        }
        // Overlapping mouth union rim is a distinct seam problem. Do not
        // certify two intersecting footprints as independent rings.
        if(safe)for(const auto& prior:accepted)
            if(overlaps(b,entranceBounds(prior))){safe=false;break;}
        if(safe&&low<=high){accepted.push_back(e);plan.acceptedEntrances.push_back(e.id);}
        else plan.rejectedEntrances.push_back(e.id);
    }
    size_t total=0;
    for(auto& [cell,patch]:plan.patches){
        (void)cell;
        patch=cutCaveTerrainPatch(std::move(patch),snapshot,epoch,&accepted);
        total+=patch.collision.size();
        if(total>1000000)throw std::length_error("Caverns surface group triangle budget");
    }
    for(const auto& [cell,patch]:plan.patches){
        for(const unsigned axis:{0u,2u}){
            auto neighbor=cell;if(axis==0)++neighbor.x;else ++neighbor.z;
            const auto it=plan.patches.find(neighbor);
            if(it==plan.patches.end())continue;
            const auto plane=Fixed::fromInt(int64_t(axis==0?neighbor.x:neighbor.z)*tileMetres).raw;
            if(surfaceTileEdge(patch,axis,plane)!=surfaceTileEdge(it->second,axis,plane))
                throw std::invalid_argument("Caverns multi-tile seam not certified");
            ++plan.certifiedEdges;
        }
    }
    size_t rimWork=0;
    for(const auto& e:accepted)plan.rims.push_back(extractCaveSurfaceRim(plan.patches,e,rimWork));
    return plan;
}
} // namespace aiverse::terraweave::caverns
