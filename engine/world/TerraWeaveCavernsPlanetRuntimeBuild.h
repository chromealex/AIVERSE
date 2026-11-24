#pragma once
// Owner-side candidate preparation, reusing the existing immutable SDF
// jobs, native exact rim adapter and body-level publication payload.
#include "engine/world/TerraWeaveCavernsPlanetDemand.h"
#include "engine/world/TerraWeaveCavernsPlanetNativeOwner.h"
#include "engine/world/TerraWeaveCavernsOwnedRimGroup.h"
#include "engine/world/TerraWeaveCavernsStreamJobs.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetBuildRegion {
    uint64_t id=0;
    CavePlanetFrame frame;
    std::shared_ptr<const CaveVolumeSnapshot> snapshot;
    std::shared_ptr<const CaveSnapshotSpatialIndex> index;
    std::shared_ptr<CavePlanetCutPlan> cut;
    CavePlanetDemandConfig lattice;
    std::set<ChunkKey3,KeyLess> keys;
    std::map<ChunkKey3,CaveCertifiedChunk,KeyLess> chunks;
};
// Exact canonical snapshot equality without JSON serialization or transient
// allocation on the movement-admission path.
inline bool samePlanetRuntimeSnapshot(const CaveVolumeSnapshot& a,const CaveVolumeSnapshot& b){
    const auto& x=a.settings;const auto& y=b.settings;
    if(a.generatorVersion!=b.generatorVersion||a.sourceRevision!=b.sourceRevision||
       x.seed!=y.seed||x.cellCm!=y.cellCm||x.tunnelRadiusCm!=y.tunnelRadiusCm||
       x.chamberRadiusCm!=y.chamberRadiusCm||x.tunnelChance!=y.tunnelChance||
       x.chamberChance!=y.chamberChance||x.minYcm!=y.minYcm||x.maxYcm!=y.maxYcm||
       a.entrances!=b.entrances||a.stamps.size()!=b.stamps.size())return false;
    for(size_t i=0;i<a.stamps.size();++i)
        if(!authority_detail::sameStamp(a.stamps[i],b.stamps[i]))return false;
    return true;
}
struct CavePlanetRuntimeCandidate {
    uint64_t generation=0,nativeRevision=0;
    std::shared_ptr<const CavePlanetBodyCutPlan> cut;
    std::map<uint64_t,std::shared_ptr<const CaveVolumeSnapshot>> sources;
    std::vector<CavePlanetNativeSlot> volume;
    // Numeric source revisions alone are not sufficient after rollback.
    bool current(const World& ecs,uint64_t epoch)const{
        if(!generation||generation!=epoch||!cut||sources.size()!=cut->regions.size())return false;
        for(const auto& [id,source]:sources){
            const auto* entry=findPlanetCaveRegion(ecs,id);
            const auto it=cut->regions.find(id);
            if(!entry||!source||it==cut->regions.end()||!it->second||
               !sameCavePlanetAnchor(entry->region.anchor,it->second->anchor)||
               !samePlanetRuntimeSnapshot(entry->authority.volume,*source))return false;
        }
        return true;
    }
};
inline SurfaceRectCm planetRuntimeFootprint(const CaveVolumeSnapshot& source){
    if(source.entrances.empty())return {-1,-1,1,1};
    auto rect=entranceBounds(source.entrances.front());
    for(const auto& e:source.entrances){
        const auto b=entranceBounds(e);
        rect.minX=std::min(rect.minX,b.minX);rect.minZ=std::min(rect.minZ,b.minZ);
        rect.maxX=std::max(rect.maxX,b.maxX);rect.maxZ=std::max(rect.maxZ,b.maxZ);
    }
    return rect;
}
inline CaveSurfaceGroupPlan refinePlanetRuntimeCut(CavePlanetCutPlan& cut,
        const CavePlanetFrame& frame,uint32_t resolution,int32_t spacingCm){
    CaveSurfaceGroupPlan plan;
    plan.soilRevision=cut.surfaceRevision;plan.surfaceEpoch=cut.surfaceEpoch;
    plan.authorityRevision=cut.authorityRevision;plan.rims=cut.rims;
    plan.acceptedEntrances=cut.accepted;plan.rejectedEntrances=cut.rejected;
    // Temporary disjoint storage keys, NOT native or flat terrain leases.
    // Preserve exact native positions of all existing cut vertices.
    std::map<SurfaceVertexKey,Vec3> native;
    int32_t ordinal=0;
    for(const auto& [key,slot]:cut.slots){
        (void)key;
        if(slot.chartTriangles.size()!=slot.bodyTriangles.size())
            throw std::invalid_argument("Caverns runtime cut topology");
        for(size_t i=0;i<slot.chartTriangles.size();++i){
            const auto& t=slot.chartTriangles[i];const auto& b=slot.bodyTriangles[i];
            const Vec3 points[]={t.a,t.b,t.c},body[]={b.a,b.b,b.c};
            for(unsigned v=0;v<3;++v){
                auto [it,added]=native.emplace(surfaceVertexKey(points[v]),body[v]);
                if(!added&&it->second!=body[v])throw std::invalid_argument("Caverns runtime cut alias");
            }
        }
        plan.patches[{2*ordinal++,0,0}].collision=slot.chartTriangles;
    }
    plan=refineSurfaceGroupForRimGrid(std::move(plan),resolution,spacingCm);
    cut.rims=plan.rims;ordinal=0;
    const auto body=[&](Vec3 p){
        const auto it=native.find(surfaceVertexKey(p));
        return it==native.end()?frame.toBody(p):it->second;
    };
    for(auto& [key,slot]:cut.slots){
        (void)key;slot.chartTriangles=plan.patches.at({2*ordinal++,0,0}).collision;
        slot.bodyTriangles.clear();
        for(const auto& t:slot.chartTriangles)slot.bodyTriangles.push_back({body(t.a),body(t.b),body(t.c)});
    }
    return plan;
}
// A mesh-only edge check would miss an entirely empty (all air) boundary.
// Verify the actual sampled SDF is solid on the outer grid, and require all
// authored influences to fit within the interior lattice. This is a sampled
// lattice certificate, not a proof of unsampled sub-voxel geometry.
inline void certifyPlanetRuntimeInterior(const CavePlanetBuildRegion& r){
    if(r.keys.empty()||!r.index||!r.snapshot)throw std::invalid_argument("Caverns missing interior");
    const int64_t span=int64_t(r.lattice.resolution)*r.lattice.spacingCm;
    // Do not depend on the KeyLess iteration order for the actual bounds.
    int64_t low=0;
    for(const auto& k:r.keys)low=std::min({low,k.x,k.y,k.z});
    const int64_t cap=-low*span;
    const auto inside=[&](PointCm p,int64_t radius){
        return p.x-radius>-cap&&p.x+radius<cap&&p.y-radius>-cap&&p.y+radius<cap&&
               p.z-radius>-cap&&p.z+radius<cap;
    };
    for(const auto& stamp:r.snapshot->stamps)
        if(!inside(stamp.center,stamp.radiusCm))throw std::invalid_argument("Caverns runtime stamp outside interior lattice");
    for(const auto& e:r.snapshot->entrances)
        if(!inside({e.xCm,e.caveYcm,e.zCm},e.radiusCm)||
           !inside({e.xCm,e.surfaceYcm,e.zCm},e.radiusCm))
            throw std::invalid_argument("Caverns runtime shaft outside interior lattice");
    const auto spacing=r.lattice.spacingCm;
    if(cap<=0||spacing<=0||cap%spacing)throw std::invalid_argument("Caverns runtime interior grid");
    for(int64_t u=-cap;u<=cap;u+=spacing)
        for(int64_t v=-cap;v<=cap;v+=spacing)
            for(const auto sign:{-1,1}){
                const auto edge=sign*cap;
                for(const auto p:{PointCm{edge,u,v},PointCm{u,edge,v},PointCm{u,v,edge}})
                    if(r.index->field(p)>=0)throw std::invalid_argument("Caverns runtime boundary not solid");
            }
}
inline std::vector<CavePlanetNativeSlot> preparePlanetRuntimeVolume(CavePlanetBuildRegion& region,
        const CavePlanetDemandConfig& cfg,uint64_t nativeRevision,
        const std::function<world::Cell(uint64_t,ChunkKey3)>& cells){
    if(!cells||region.chunks.size()!=region.keys.size())throw std::invalid_argument("Caverns incomplete volume");
    const auto& snapshot=*region.snapshot;
    auto& cut=*region.cut;
    if(cut.accepted.size()!=snapshot.entrances.size())
        throw std::invalid_argument("Caverns runtime refused shaft: retain solid terrain");
    auto surface=refinePlanetRuntimeCut(cut,region.frame,cfg.resolution,cfg.spacingCm);
    std::map<uint64_t,Fixed> lowerPlanes;
    for(const auto& e:snapshot.entrances){
        const auto rim=std::find_if(cut.rims.begin(),cut.rims.end(),[&](const auto& r){return r.entranceId==e.id;});
        if(rim==cut.rims.end())throw std::invalid_argument("Caverns missing runtime rim");
        auto top=Fixed::fromRatio(e.surfaceYcm,100);
        for(const auto& k:rim->ring)top=fxMin(top,Fixed::fromRaw(k[1]));
        const auto bottom=Fixed::fromRatio(e.caveYcm,100);
        if(top-bottom<Fixed::fromRatio(1,2))throw std::invalid_argument("Caverns runtime shaft depth");
        lowerPlanes.emplace(e.id,(bottom+top)/2);
    }
    const auto requests=selectOwnedRimRequests(surface,snapshot,lowerPlanes,cfg.resolution,cfg.spacingCm);
    std::map<uint64_t,CaveOwnedRimGroupRequest> groups;
    for(const auto& r:requests){
        if(!region.keys.contains(r.key))throw std::invalid_argument("Caverns runtime shaft outside interior lattice");
        auto& g=groups[r.entranceId];g.entranceId=r.entranceId;g.lowerPlane=r.lowerPlane;g.keys.push_back(r.key);
    }
    std::map<ChunkKey3,std::vector<world::Triangle>,KeyLess> triangles;
    for(const auto& [id,g]:groups){
        (void)id;
        std::vector<CaveCertifiedChunk> sources;
        for(const auto& key:g.keys)sources.push_back(region.chunks.at(key));
        const auto result=prepareOwnedRimGroup(sources,g,surface,snapshot);
        for(const auto& slot:result.slots)
            triangles.emplace(slot.candidate.mesh.key,slot.collision.triangles);
    }
    std::vector<world::Triangle> all;
    const int64_t span=int64_t(cfg.resolution)*cfg.spacingCm;
    std::vector<CavePlanetNativeSlot> out;
    for(const auto& [key,chunk]:region.chunks){
        if(!triangles.contains(key))triangles.emplace(key,prepareCaveVolumeCollision(chunk.mesh,
            cfg.resolution,cfg.spacingCm,snapshot.sourceRevision).triangles);
        const auto& tris=triangles.at(key);
        if(tris.size()>262144-all.size())throw std::length_error("Caverns runtime volume triangle budget");
        all.insert(all.end(),tris.begin(),tris.end());
        CavePlanetNativeSlot slot;slot.id.region=region.id;slot.id.volume=key;
        slot.payload=prepareCavePlanetNativeTriangles(region.frame,tris,cells(region.id,key),
            nativeRevision,rimChunkOrigin(key,span));out.push_back(std::move(slot));
    }
    // No fabricated boundary caps: an open edge other than a certified
    // shaft mouth means the selected interior coverage is insufficient.
    std::set<SurfaceEdgeKey> expected;
    for(const auto& rim:cut.rims)for(size_t i=0;i<rim.ring.size();++i)
        expected.insert(surfaceEdgeKey(rimPoint(rim.ring[i]),rimPoint(rim.ring[(i+1)%rim.ring.size()])));
    if(planetaryOpenEdges(all)!=expected)
        throw std::invalid_argument("Caverns runtime open exterior or unmatched shaft");
    return out;
}
// One body candidate at a time; globally bounded workers, no thread pool
// or GPU work in worker callbacks. Tickets are revoked by owner generation.
class CavePlanetRuntimeBuilder {
public:
    explicit CavePlanetRuntimeBuilder(size_t workers=2):jobs_(workers){}
    bool busy()const noexcept{return !regions_.empty();}
    size_t inFlight()const noexcept{return jobs_.inFlight();}
    void begin(const World& ecs,const std::shared_ptr<const game::Surface>& surface,
            game::PlanetRenderer& planet,std::span<const uint64_t> ids,
            const CavePlanetDemandConfig& cfg,uint64_t generation,uint64_t nativeRevision,uint64_t surfaceEpoch){
        if(busy()||jobs_.inFlight()||ids.empty()||ids.size()>cfg.maxRegions||!generation||!nativeRevision||!surfaceEpoch)
            throw std::invalid_argument("Caverns runtime build identity");
        std::vector<CavePlanetBuildRegion> next;
        auto body=std::make_shared<CavePlanetBodyCutPlan>();
        body->surfaceRevision=planet.caveSurfaceRevision();body->surfaceEpoch=surfaceEpoch;
        size_t count=0;std::set<uint64_t> seen;
        for(auto id:ids){
            const auto* entry=findPlanetCaveRegion(ecs,id);
            if(!entry||!seen.insert(id).second)throw std::invalid_argument("Caverns runtime region ID");
            auto frame=CavePlanetFrame::make(surface,entry->region.anchor);
            if(body->bodyId.empty())body->bodyId=frame.anchor().bodyId;
            if(body->bodyId!=frame.anchor().bodyId)throw std::invalid_argument("Caverns mixed body build");
            const auto snapshot=freezePlanetCaveRegion(ecs,id);
            const auto lattice=resolvePlanetDemandLattice(frame.anchor(),cfg);
            auto keys=planetInteriorKeys(frame.anchor(),lattice);
            if(keys.size()>cfg.maxVolumeSlots-count)throw std::length_error("Caverns body volume budget");
            count+=keys.size();
            const auto leaves=planet.selectCaveSurfaceSlots(frame,planetRuntimeFootprint(*snapshot));
            const auto original=planet.captureCaveSurfaceSlots(frame.body(),leaves);
            auto cut=std::make_shared<CavePlanetCutPlan>(planCavePlanetCut(frame,*snapshot,original,
                body->surfaceRevision,surfaceEpoch));
            body->regions.emplace(id,cut);
            next.push_back({id,std::move(frame),snapshot,
                std::make_shared<const CaveSnapshotSpatialIndex>(snapshot),std::move(cut),lattice,std::move(keys),{}});
            certifyPlanetRuntimeInterior(next.back());
        }
        (void)certifyPlanetBodyCut(*body,surface);
        generation_=generation;nativeRevision_=nativeRevision;surface_=surface;
        plan_=std::move(body);regions_=std::move(next);region_=0;
    }
    bool current(const World& ecs,uint64_t generation)const{
        if(!busy()||generation!=generation_)return false;
        for(const auto& r:regions_){
            const auto* entry=findPlanetCaveRegion(ecs,r.id);
            if(!entry||!sameCavePlanetAnchor(entry->region.anchor,r.frame.anchor())||
               !samePlanetRuntimeSnapshot(entry->authority.volume,*r.snapshot))return false;
        }
        return true;
    }
    // Returns true only when all immutable source meshes have arrived.
    bool pump(size_t maxDispatch=2){
        if(!busy())return false;
        if(!maxDispatch||maxDispatch>32)throw std::invalid_argument("Caverns runtime pump budget");
        if(region_>=regions_.size())return true;
        auto& r=regions_[region_];
        for(auto& result:jobs_.poll()){
            if(!result.success||result.ticket.sourceRevision!=r.snapshot->sourceRevision||
               result.ticket.generation!=generation_||!r.keys.contains(result.ticket.key))
                throw std::runtime_error("Caverns runtime worker failed or stale");
            CaveCertifiedChunk c;
            c.mesh=std::move(result.collisionMesh);c.resolution=r.lattice.resolution;c.spacingCm=r.lattice.spacingCm;
            c.sourceRevision=r.snapshot->sourceRevision;c.renderRevision=nativeRevision_;
            r.chunks.emplace(result.ticket.key,std::move(c));
        }
        size_t sent=0;
        for(const auto& key:r.keys){
            if(sent>=maxDispatch)break;
            if(r.chunks.contains(key)||jobs_.hasJob(key))continue;
            if(!jobs_.dispatch({CaveResidencyAction::Build,key,generation_,r.snapshot->sourceRevision,0},
                r.snapshot,r.lattice.resolution,r.lattice.spacingCm,r.index))break;
            ++sent;
        }
        if(r.chunks.size()==r.keys.size()&&!jobs_.inFlight())++region_;
        return region_==regions_.size();
    }
    CavePlanetRuntimeCandidate finish(const std::function<world::Cell(uint64_t,ChunkKey3)>& cells){
        if(!busy()||region_!=regions_.size()||jobs_.inFlight())throw std::logic_error("Caverns runtime build incomplete");
        CavePlanetRuntimeCandidate out;out.generation=generation_;out.nativeRevision=nativeRevision_;
        size_t total=0;
        for(auto& r:regions_){
            auto slots=preparePlanetRuntimeVolume(r,r.lattice,nativeRevision_,cells);
            for(const auto& slot:slots){
                if(slot.payload.collision.triangles.size()>262144-total)throw std::length_error("Caverns body volume triangle budget");
                total+=slot.payload.collision.triangles.size();
            }
            for(auto& slot:slots)out.volume.push_back(std::move(slot));
            out.sources.emplace(r.id,r.snapshot);
        }
        (void)certifyPlanetBodyCut(*plan_,surface_);
        out.cut=std::move(plan_);regions_.clear();surface_.reset();region_=0;return out;
    }
    // Stop dispatching; caller polls outstanding jobs before restarting.
    void cancel(){regions_.clear();plan_.reset();surface_.reset();region_=0;}
    bool pollCancelled(){if(busy())return false;(void)jobs_.poll();return jobs_.inFlight()==0;}
    void drain(){cancel();jobs_.drain();}
private:
    CaveWorldStreamJobs jobs_;
    uint64_t generation_=0,nativeRevision_=0;
    std::shared_ptr<const game::Surface> surface_;
    std::shared_ptr<CavePlanetBodyCutPlan> plan_;
    std::vector<CavePlanetBuildRegion> regions_;
    size_t region_=0;
};
} // namespace aiverse::terraweave::caverns
