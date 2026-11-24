#pragma once
#include "engine/world/TerraWeaveCavernsOwnedRimChunk.h"
#include <span>

namespace aiverse::terraweave::caverns {
struct CaveOwnedRimGroupRequest {
    uint64_t entranceId=0;
    Fixed lowerPlane{};
    std::vector<ChunkKey3> keys;
};
struct CaveOwnedRimSlot {
    CaveCertifiedChunk candidate;
    CaveCollisionPrepared collision;
};
struct CaveOwnedRimGroup {
    uint64_t entranceId=0;
    CaveRimTransition collar;
    std::vector<CaveOwnedRimSlot> slots;
};
inline Vec3 rimChunkOrigin(ChunkKey3 key,int64_t spanCm){
    return {Fixed::fromRatio(key.x*spanCm,100),Fixed::fromRatio(key.y*spanCm,100),Fixed::fromRatio(key.z*spanCm,100)};
}
inline Fixed rimAxis(Vec3 p,unsigned axis){return axis==0?p.x:axis==1?p.y:p.z;}
inline Vec3 rimSetAxis(Vec3 p,unsigned axis,Fixed value){
    if(axis==0)p.x=value;else if(axis==1)p.y=value;else p.z=value;
    return p;
}
inline std::vector<Vec3> clipRimSlotPolygon(const std::vector<Vec3>& poly,
        unsigned axis,Fixed plane,bool positive){
    std::vector<Vec3> out;
    const auto push=[&](Vec3 p){if(out.empty()||surfaceVertexKey(out.back())!=surfaceVertexKey(p))out.push_back(p);};
    for(size_t i=0;i<poly.size();++i){
        auto a=poly[i],b=poly[(i+1)%poly.size()];
        const auto da=rimAxis(a,axis)-plane,db=rimAxis(b,axis)-plane;
        if(positive?da.raw>=0:da.raw<=0)push(a);
        if((da.raw<0&&db.raw>0)||(da.raw>0&&db.raw<0)){
            if(surfaceVertexKey(b)<surfaceVertexKey(a))std::swap(a,b);
            const double t=double(plane.raw-rimAxis(a,axis).raw)/double(rimAxis(b,axis).raw-rimAxis(a,axis).raw);
            const auto lerp=[&](Fixed x,Fixed y){return Fixed::fromRaw(x.raw+int64_t(std::llround(double(y.raw-x.raw)*t)));};
            push(rimSetAxis({lerp(a.x,b.x),lerp(a.y,b.y),lerp(a.z,b.z)},axis,plane));
        }
    }
    if(out.size()>1&&surfaceVertexKey(out.front())==surfaceVertexKey(out.back()))out.pop_back();
    return out;
}
// A complete rectangular slot group is required. Every source must be the
// same native epoch and lattice. No source outside the group is touched.
// The actual retained volume triangles stay in their original slots.
inline CaveOwnedRimGroup prepareOwnedRimGroup(
        std::span<const CaveCertifiedChunk> sources,const CaveOwnedRimGroupRequest& request,
        const CaveSurfaceGroupPlan& surface,const CaveVolumeSnapshot& snapshot){
    if(sources.empty()||sources.size()>64||sources.size()!=request.keys.size()||
       !surface.surfaceEpoch||!surface.authorityRevision)
        throw std::invalid_argument("Caverns owned rim group budget");
    const auto entrance=std::find_if(snapshot.entrances.begin(),snapshot.entrances.end(),[&](const auto& e){return e.id==request.entranceId;});
    const auto rim=std::find_if(surface.rims.begin(),surface.rims.end(),[&](const auto& r){return r.entranceId==request.entranceId;});
    if(entrance==snapshot.entrances.end()||rim==surface.rims.end())throw std::invalid_argument("Caverns missing group rim");
    const auto res=sources.front().resolution;const auto spacing=sources.front().spacingCm;
    if(!res||res>64||spacing<=0||spacing>10000)throw std::invalid_argument("Caverns group lattice");
    const int64_t spanCm=int64_t(res)*spacing;
    const auto extent=Fixed::fromRatio(spanCm,100);
    std::set<ChunkKey3,KeyLess> wanted(request.keys.begin(),request.keys.end());
    if(wanted.size()!=request.keys.size())throw std::invalid_argument("Caverns duplicate group slot");
    std::map<ChunkKey3,TerrainPatch,KeyLess> before,after;
    std::map<ChunkKey3,const CaveCertifiedChunk*,KeyLess> originals;
    auto lo=request.keys.front(),hi=lo;
    size_t originalCount=0;
    for(const auto& s:sources){
        const auto k=s.mesh.key;
        if(!wanted.contains(k)||s.sourceRevision!=snapshot.sourceRevision||s.resolution!=res||s.spacingCm!=spacing||
           !originals.emplace(k,&s).second)throw std::invalid_argument("Caverns mismatched owned group source");
        auto payload=prepareCaveVolumeCollision(s.mesh,res,spacing,s.sourceRevision);
        originalCount+=payload.triangles.size();
        if(originalCount>32767)throw std::length_error("Caverns group original triangle budget");
        before[k].collision=std::move(payload.triangles);
        lo={std::min(lo.x,k.x),std::min(lo.y,k.y),std::min(lo.z,k.z)};
        hi={std::max(hi.x,k.x),std::max(hi.y,k.y),std::max(hi.z,k.z)};
    }
    if(hi.x-lo.x>=64||hi.y-lo.y>=64||hi.z-lo.z>=64||
       (hi.x-lo.x+1)*(hi.y-lo.y+1)*(hi.z-lo.z+1)!=int64_t(wanted.size()))
        throw std::invalid_argument("Caverns group must contain complete rectangular slots");
    const auto min=rimChunkOrigin(lo,spanCm),max=rimChunkOrigin({hi.x+1,hi.y+1,hi.z+1},spanCm);
    const double cx=double(entrance->xCm)/100.,cz=double(entrance->zCm)/100.,radius=double(entrance->radiusCm)/100.;
    const auto interior=[&](Vec3 p){return p.x>min.x&&p.x<max.x&&p.y>min.y&&p.y<max.y&&p.z>min.z&&p.z<max.z;};
    const auto envelope=[&](Vec3 p){return interior(p)&&
        std::hypot(p.x.toDouble()-cx,p.z.toDouble()-cz)<=radius*1.5&&
        p.y.toDouble()<=(double(entrance->surfaceYcm)+entrance->radiusCm)/100.;};
    if(request.lowerPlane<=min.y||request.lowerPlane>=max.y)
        throw std::invalid_argument("Caverns group trim plane");
    for(const auto& k:rim->ring)if(!envelope(rimPoint(k)))throw std::invalid_argument("Caverns rim outside group envelope");
    std::vector<world::Triangle> retained;
    for(const auto& [key,patch]:before){
        for(const auto& t:patch.collision)if(t.a.y>request.lowerPlane||t.b.y>request.lowerPlane||t.c.y>request.lowerPlane)
            for(const auto& p:{t.a,t.b,t.c})if(!envelope(p))
                throw std::invalid_argument("Caverns group trim would alter unrelated geometry or exterior face");
        auto& dest=after[key].collision;
        if(!patch.collision.empty())dest=trimOwnedSdfShaft(patch.collision,request.lowerPlane);
        retained.insert(retained.end(),dest.begin(),dest.end());
    }
    auto collar=buildCaveRimTransition(*rim,*entrance,retained,request.lowerPlane,surface.surfaceEpoch,surface.authorityRevision);
    // Partition only the new collar. A face-coplanar polygon belongs to
    // the slot on the positive side, avoiding duplicate physical faces.
    size_t fragments=0;
    for(const auto& t:collar.triangles){
        for(const auto& key:wanted){
            const auto origin=rimChunkOrigin(key,spanCm);
            std::vector<Vec3> poly{t.a,t.b,t.c};
            for(unsigned axis=0;axis<3&&!poly.empty();++axis){
                const auto a=rimAxis(origin,axis),b=a+extent;
                poly=clipRimSlotPolygon(poly,axis,a,true);
                poly=clipRimSlotPolygon(poly,axis,b,false);
                if(!poly.empty()&&std::all_of(poly.begin(),poly.end(),[&](Vec3 p){return rimAxis(p,axis)==b;}))
                    poly.clear();
            }
            for(size_t i=1;i+1<poly.size();++i){
                world::Triangle f{poly[0],poly[i],poly[i+1]};
                if(!world::SpatialWorld::validTriangle(f))
                    throw std::invalid_argument("Caverns invalid partitioned collar fragment");
                if(++fragments>65534)throw std::length_error("Caverns partition triangle budget");
                after[key].collision.push_back(f);
            }
        }
    }
    // Internal faces must match exactly; exterior faces must equal the
    // previous source. Strict certification may refuse valid T-junctions.
    for(const auto& [key,patch]:after){
        const auto origin=surfaceVertexKey(rimChunkOrigin(key,spanCm));
        for(unsigned axis=0;axis<3;++axis)for(int side=0;side<2;++side){
            auto neighbor=key;const int delta=side?1:-1;
            if(axis==0)neighbor.x+=delta;else if(axis==1)neighbor.y+=delta;else neighbor.z+=delta;
            const auto plane=origin[axis]+(side?extent.raw:0);
            const auto it=after.find(neighbor);
            const auto& other=it==after.end()?before.at(key):it->second;
            if(surfaceTileEdge(patch,axis,plane)!=surfaceTileEdge(other,axis,plane))
                throw std::invalid_argument("Caverns owned group face certification failed");
        }
    }
    // Do not accept a cut which subdivides the exact terrain/retained rim
    // without the corresponding other owner also sharing those segments.
    std::map<SurfaceEdgeKey,unsigned> incidence;
    for(const auto& [key,patch]:after){(void)key;for(const auto& t:patch.collision){
        const Vec3 p[]={t.a,t.b,t.c};
        for(unsigned i=0;i<3;++i){const auto edge=surfaceEdgeKey(p[i],p[(i+1)%3]);
            if(collar.terrainBoundary.contains(edge)||collar.volumeBoundary.contains(edge))++incidence[edge];}
    }}
    for(const auto& edge:collar.terrainBoundary)if(incidence[edge]!=1)throw std::invalid_argument("Caverns partition changes terrain rim edge");
    for(const auto& edge:collar.volumeBoundary)if(incidence[edge]!=2)throw std::invalid_argument("Caverns partition changes retained rim edge");
    CaveOwnedRimGroup result;result.entranceId=entrance->id;result.collar=std::move(collar);
    for(auto& [key,patch]:after){
        CaveOwnedRimSlot slot;slot.candidate=*originals.at(key);
        slot.collision.revision=snapshot.sourceRevision;
        slot.collision.cell={int32_t(key.x),int32_t(key.y),int32_t(key.z)};
        slot.collision.triangles=std::move(patch.collision);
        auto model=buildExactCavePayload(slot.collision,rimChunkOrigin(key,spanCm));
        auto& mesh=slot.candidate.mesh;mesh.vertices.clear();mesh.indices.clear();
        if(model){mesh.indices=model->indices;for(const auto& v:model->vertices)
            mesh.vertices.push_back({v.pos.x,v.pos.y,v.pos.z,v.nrm.x,v.nrm.y,v.nrm.z});}
        result.slots.push_back(std::move(slot));
    }
    return result;
}
} // namespace aiverse::terraweave::caverns
