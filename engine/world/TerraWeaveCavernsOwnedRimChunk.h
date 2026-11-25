#pragma once
#include "engine/render/TerraWeaveCavernsRimPayload.h"
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include "engine/world/TerraWeaveCavernsSeamGate.h"

namespace aiverse::terraweave::caverns {
struct CaveOwnedRimRequest {
    ChunkKey3 key{};
    uint64_t entranceId=0;
    Fixed lowerPlane{};
};
struct CaveOwnedRimChunk {
    CaveCertifiedChunk candidate;
    CaveRimReplacement replacement;
};
// Conservative partition: the source is one existing chunk slot. Every
// triangle that could be removed by the horizontal trim must belong to the
// interior shaft envelope. Unrelated geometry below the plane survives;
// arbitrary disconnected/other above-plane geometry refuses this path.
inline CaveOwnedRimChunk prepareOwnedRimChunk(const CaveCertifiedChunk& source,
        const CaveOwnedRimRequest& request,const CaveSurfaceGroupPlan& group,
        const CaveVolumeSnapshot& snapshot){
    if(source.mesh.key!=request.key||source.sourceRevision!=snapshot.sourceRevision||
       !source.resolution||source.resolution>64||source.spacingCm<=0||
       !group.surfaceEpoch||!group.authorityRevision)
        throw std::invalid_argument("Caverns owned rim chunk identity");
    const auto e=std::find_if(snapshot.entrances.begin(),snapshot.entrances.end(),
        [&](const auto& x){return x.id==request.entranceId;});
    const auto rim=std::find_if(group.rims.begin(),group.rims.end(),
        [&](const auto& x){return x.entranceId==request.entranceId;});
    if(e==snapshot.entrances.end()||rim==group.rims.end())
        throw std::invalid_argument("Caverns owned rim missing entrance");
    const auto original=prepareCaveVolumeCollision(source.mesh,source.resolution,
        source.spacingCm,source.sourceRevision);
    const int64_t spanCm=int64_t(source.resolution)*source.spacingCm;
    const Vec3 origin{Fixed::fromRatio(source.mesh.key.x*spanCm,100),
        Fixed::fromRatio(source.mesh.key.y*spanCm,100),Fixed::fromRatio(source.mesh.key.z*spanCm,100)};
    const auto extent=Fixed::fromRatio(spanCm,100);
    const auto inside=[&](Vec3 p){
        return p.x>origin.x&&p.y>origin.y&&p.z>origin.z&&
            p.x<origin.x+extent&&p.y<origin.y+extent&&p.z<origin.z+extent;
    };
    const double cx=double(e->xCm)/100.,cz=double(e->zCm)/100.,radius=double(e->radiusCm)/100.;
    const auto envelope=[&](Vec3 p){
        return inside(p)&&std::hypot(p.x.toDouble()-cx,p.z.toDouble()-cz)<=radius*1.5&&
            p.y.toDouble()<=(double(e->surfaceYcm)+e->radiusCm)/100.;
    };
    if(request.lowerPlane<=origin.y||request.lowerPlane>=origin.y+extent)
        throw std::invalid_argument("Caverns rim trim plane outside owning chunk interior");
    for(const auto& key:rim->ring)if(!envelope(rimPoint(key)))
        throw std::invalid_argument("Caverns rim crosses native chunk boundary");
    for(const auto& t:original.triangles){
        if(t.a.y>request.lowerPlane||t.b.y>request.lowerPlane||t.c.y>request.lowerPlane)
            for(const auto& p:{t.a,t.b,t.c})if(!envelope(p))
                throw std::invalid_argument("Caverns cannot isolate owned shaft from other geometry");
    }
    auto replacement=prepareCaveRimReplacement(*rim,*e,original.triangles,
        request.lowerPlane,group.surfaceEpoch,group.authorityRevision,source.sourceRevision,
        original.cell,origin);
    // Certify every exterior face in native Q16, not rounded cm. This also
    // protects unloaded neighbors whose future source uses the same lattice.
    TerrainPatch before,after;
    before.collision=original.triangles;after.collision=replacement.collision.triangles;
    const auto o=surfaceVertexKey(origin);
    for(unsigned axis=0;axis<3;++axis)for(const auto plane:{o[axis],o[axis]+extent.raw})
        if(surfaceTileEdge(before,axis,plane)!=surfaceTileEdge(after,axis,plane))
            throw std::invalid_argument("Caverns rim replacement changes exterior chunk seam");
    CaveOwnedRimChunk result;
    result.candidate=source;
    auto& mesh=result.candidate.mesh;
    mesh.vertices.clear();mesh.indices=replacement.model->indices;
    mesh.vertices.reserve(replacement.model->vertices.size());
    for(const auto& v:replacement.model->vertices)
        mesh.vertices.push_back({v.pos.x,v.pos.y,v.pos.z,v.nrm.x,v.nrm.y,v.nrm.z});
    for(const auto& t:replacement.collision.triangles)for(const auto& p:{t.a,t.b,t.c})
        if(p.x<origin.x||p.y<origin.y||p.z<origin.z||p.x>origin.x+extent||
           p.y>origin.y+extent||p.z>origin.z+extent)
            throw std::invalid_argument("Caverns rim replacement exceeds owning slot");
    result.replacement=std::move(replacement);
    return result;
}
} // namespace aiverse::terraweave::caverns
