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
struct CaveSurfaceGroupPlan {
    uint64_t soilRevision=0,authorityRevision=0,surfaceEpoch=0;
    int32_t tileMetres=0;
    uint32_t resolution=0;
    std::map<world::Cell,TerrainPatch> patches;
    std::vector<uint64_t> acceptedEntrances,rejectedEntrances;
    size_t certifiedEdges=0;
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
    return plan;
}
} // namespace aiverse::terraweave::caverns
