#pragma once
#include "engine/world/TerraWeaveCavernsOwnedRimChunk.h"
#include <span>

namespace aiverse::terraweave::caverns {
struct CaveOwnedRimGroupRequest {
    uint64_t entranceId=0;
    Fixed lowerPlane{};
    std::vector<ChunkKey3> keys;
};
// Conservative automatic slot selection from a frozen mouth and a host
// selected trim plane. Include the entire padded shaft envelope; missing
// dirty slots refuse rather than silently narrowing the affected region.
inline std::vector<CaveOwnedRimRequest> selectOwnedRimRequests(
        const CaveSurfaceGroupPlan& surface,const CaveVolumeSnapshot& snapshot,
        const std::map<uint64_t,Fixed>& lowerPlanes,uint32_t resolution,int32_t spacingCm,
        size_t maxSlots=64){
    if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000||
       !maxSlots||maxSlots>64||lowerPlanes.size()!=surface.rims.size())
        throw std::invalid_argument("Caverns automatic rim slot budget");
    const int64_t span=int64_t(resolution)*spacingCm;
    // Native chunk grid is defined in signed integer centimetres.
    const auto floorCm=[](Fixed x){return floorDiv(x.raw*100,Fixed::kOne);};
    std::vector<CaveOwnedRimRequest> result;
    std::set<ChunkKey3,KeyLess> occupied;
    for(const auto& rim:surface.rims){
        const auto e=std::find_if(snapshot.entrances.begin(),snapshot.entrances.end(),
            [&](const auto& item){return item.id==rim.entranceId;});
        const auto plane=lowerPlanes.find(rim.entranceId);
        if(e==snapshot.entrances.end()||plane==lowerPlanes.end())
            throw std::invalid_argument("Caverns automatic rim selection identity");
        if(plane->second<Fixed::fromRatio(e->caveYcm,100)||
           plane->second>=Fixed::fromRatio(e->surfaceYcm,100))
            throw std::invalid_argument("Caverns automatic rim plane outside shaft");
        const int64_t radial=(int64_t(e->radiusCm)*3+1)/2+spacingCm;
        const int64_t lowY=floorCm(plane->second)-spacingCm;
        const int64_t highY=e->surfaceYcm+int64_t(e->radiusCm)+spacingCm;
        const ChunkKey3 lo{floorDiv(e->xCm-radial,span),floorDiv(lowY,span),floorDiv(e->zCm-radial,span)};
        const ChunkKey3 hi{floorDiv(e->xCm+radial,span),floorDiv(highY,span),floorDiv(e->zCm+radial,span)};
        const uint64_t nx=uint64_t(hi.x-lo.x+1),ny=uint64_t(hi.y-lo.y+1),nz=uint64_t(hi.z-lo.z+1);
        if(nx>maxSlots||ny>maxSlots||nz>maxSlots||nx*ny*nz>maxSlots-result.size())
            throw std::length_error("Caverns automatic shaft exceeds slot budget");
        for(int64_t z=lo.z;z<=hi.z;++z)for(int64_t y=lo.y;y<=hi.y;++y)for(int64_t x=lo.x;x<=hi.x;++x){
            const ChunkKey3 key{x,y,z};
            if(!occupied.insert(key).second)
                throw std::invalid_argument("Caverns automatic shaft owner groups overlap");
            result.push_back({key,rim.entranceId,plane->second});
        }
    }
    return result;
}
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
    // Uniform Q16 slot extents must add exactly across signed boundaries.
    if((spanCm*Fixed::kOne)%100)throw std::invalid_argument("Caverns slot span not exactly representable in Q16");
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
