#pragma once
// Native baseline leaf collision adapter. Uses the existing Box3D and
// SpatialWorld shadows, not a separate scene or simulation authority.
#include "engine/render/TerraWeaveCavernsPlanetPayload.h"
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
#include "engine/world/TerraWeaveCavernsColliderLeases.h"
#include <map>
#include <set>

namespace aiverse::terraweave::caverns {
class CavePlanetBaselineCollision {
    struct Entry {
        CaveCollisionPrepared original;
        ColliderLease lease{};
        std::unique_ptr<CaveVolumePhysicsStage> stage;
    };
public:
    using Exclusive=CaveGameplayPhysicsBarrier::ExclusiveProof;
    using Cells=std::function<world::Cell(const CavePlanetSurfaceSlotKey&)>;
    using Unowned=std::function<bool(const CavePlanetSurfaceSlotKey&)>;
    enum class Phase {Empty,Prepared,Solid,Cut,RestorePrepared,Recovery};
    CavePlanetBaselineCollision(physics::Scene& box,world::SpatialWorld& spatial,CaveColliderLeasePool& pool)
        :box_(box),spatial_(spatial),pool_(pool){}
    CavePlanetBaselineCollision(const CavePlanetBaselineCollision&)=delete;
    CavePlanetBaselineCollision& operator=(const CavePlanetBaselineCollision&)=delete;
    Phase phase()const noexcept{return phase_;}
    bool recoveryRequired()const noexcept{return phase_==Phase::Recovery||barrier_.status().recoveryRequired;}
    size_t size()const noexcept{return entries_.size();}
    uint64_t surfaceRevision()const noexcept{return surfaceRevision_;}
    // A host explicitly proves these leaves have no other collision owner.
    // Do not adopt arbitrary bodies by overlapping bounds or userData.
    bool prepare(int body,uint64_t surfaceRevision,
            std::span<const CavePlanetSurfaceSlot> source,const Cells& cells,
            const Unowned& unowned,const Exclusive& exclusive){
        if(phase_!=Phase::Empty||!exclusive||!exclusive()||!cells||!unowned||
           body<0||body>=game::Surface::kMaxBodies||!surfaceRevision||
           source.empty()||source.size()>64)return false;
        std::set<world::Cell> seenCells;size_t total=0;
        try{
            for(const auto& slot:source){
                const auto& k=slot.key;
                if(k.face<0||k.face>=6||k.x<0||k.z<0||k.size<=0||k.level<0||
                   !unowned(k)||entries_.contains(k)||slot.bodyTriangles.empty()||
                   slot.bodyTriangles.size()>65534||slot.bodyTriangles.size()>131072-total){
                    discard();return false;
                }
                const auto cell=cells(k);
                if(!seenCells.insert(cell).second){discard();return false;}
                auto& entry=entries_[k];
                entry.original.cell=cell;entry.original.revision=1;
                const auto anchor=game::Surface::anchor(body);
                const auto origin=slot.bodyTriangles.front().a+anchor;
                const Vec3 integerOrigin{Fixed::fromInt(origin.x.floorInt()),
                    Fixed::fromInt(origin.y.floorInt()),Fixed::fromInt(origin.z.floorInt())};
                for(const auto& t:slot.bodyTriangles){
                    const world::Triangle tri{t.a+anchor,t.b+anchor,t.c+anchor};
                    if(!world::SpatialWorld::validTriangle(tri)){discard();return false;}
                    entry.original.triangles.push_back(tri);
                }
                total+=entry.original.triangles.size();
                // CPU-only exact conversion; no Model is uploaded for baseline.
                (void)buildExactCavePayload(entry.original,integerOrigin);
                const auto lease=pool_.reserve(uint32_t(entry.original.triangles.size()));
                if(!lease){discard();return false;}
                entry.lease=*lease;
                entry.stage=std::make_unique<CaveVolumePhysicsStage>();
                if(!entry.stage->prepareExact(entry.original,entry.lease.first,spatial_)){
                    discard();return false;
                }
            }
        }catch(...){discard();throw;}
        body_=body;surfaceRevision_=surfaceRevision;phase_=Phase::Prepared;return true;
    }
    bool publish(const Exclusive& exclusive){
        if(phase_!=Phase::Prepared&&phase_!=Phase::RestorePrepared)return false;
        if(!exclusive||!exclusive())return false;
        std::vector<CaveVolumePhysicsStage*> stages;stages.reserve(entries_.size());
        for(auto& [key,entry]:entries_){
            (void)key;
            if(!pool_.holds(entry.lease)||!entry.stage->prepared(1))return false;
            stages.push_back(entry.stage.get());
        }
        try{
            if(!barrier_.applyPrepared(stages,1,box_,spatial_,exclusive,
                [&](uint64_t revision){return revision==1&&exclusive();})){
                if(barrier_.status().recoveryRequired)phase_=Phase::Recovery;
                return false;
            }
        }catch(...){phase_=Phase::Recovery;throw;}
        phase_=Phase::Solid;return true;
    }
    // Exact source identity, not just matching leaf IDs.
    bool matches(int body,uint64_t revision,
                 const std::map<CavePlanetSurfaceSlotKey,const CavePlanetCutSlot*>& slots)const{
        if(body!=body_||revision!=surfaceRevision_||slots.size()!=entries_.size())return false;
        const auto anchor=game::Surface::anchor(body);
        for(const auto& [key,slot]:slots){
            const auto it=entries_.find(key);
            if(!slot||it==entries_.end()||slot->originalBody.size()!=it->second.original.triangles.size())return false;
            for(size_t i=0;i<slot->originalBody.size();++i){
                const auto& a=slot->originalBody[i];const auto& b=it->second.original.triangles[i];
                if(a.a+anchor!=b.a||a.b+anchor!=b.b||a.c+anchor!=b.c)return false;
            }
        }
        return true;
    }
    bool solidReady()const{
        if(phase_!=Phase::Solid)return false;
        for(const auto& [key,e]:entries_){
            (void)key;
            if(!pool_.holds(e.lease)||!e.stage->boxOwner().canRetire(box_)||
               !e.stage->spatialOwner().canRetire(spatial_)||
               e.stage->boxOwner().activeBody()==physics::kNoBody||
               e.stage->spatialOwner().activeCount()!=e.original.triangles.size())return false;
        }
        return true;
    }
    bool cutReady()const{
        if(phase_!=Phase::Cut)return false;
        for(const auto& [key,e]:entries_){
            (void)key;if(!pool_.holds(e.lease))return false;
            if(e.stage->boxOwner().activeBody()!=physics::kNoBody||e.stage->spatialOwner().activeCount())return false;
            for(uint64_t n=e.lease.first;n<uint64_t(e.lease.first)+e.lease.count;++n)
                if(spatial_.colliders().contains(uint32_t(n)))return false;
        }
        return true;
    }
    // Called by the native owner's real originalsRetired callback, under
    // the same exclusion. IDs remain reserved for restoration.
    bool retireForCut(const Exclusive& exclusive){
        if(!exclusive||!exclusive())return false;
        if(phase_==Phase::Cut)return cutReady();
        if(!solidReady())return false;
        try{
            for(auto& [key,e]:entries_){
                (void)key;if(!exclusive())throw std::logic_error("Caverns baseline exclusion lost");
                e.stage->boxOwner().unload(box_);e.stage->spatialOwner().unload(spatial_);
            }
        }catch(...){phase_=Phase::Recovery;throw;}
        phase_=Phase::Cut;return cutReady();
    }
    // Stage original triangles BEFORE the cut owner begins irreversible
    // retirement. Installation occurs only inside its close callback.
    bool prepareRestore(const Exclusive& exclusive){
        if(!exclusive||!exclusive()||!cutReady())return false;
        std::vector<CaveVolumePhysicsStage*> prepared;prepared.reserve(entries_.size());
        const auto abort=[&]{
            for(auto* stage:prepared)if(!stage->abort(1))phase_=Phase::Recovery;
        };
        try{
            for(auto& [key,e]:entries_){
                (void)key;
                if(!e.stage->prepareExact(e.original,e.lease.first,spatial_)){abort();return false;}
                prepared.push_back(e.stage.get());
            }
        }catch(...){abort();throw;}
        phase_=Phase::RestorePrepared;return true;
    }
    bool cancelRestore(){
        if(phase_!=Phase::RestorePrepared)return false;
        for(auto& [key,e]:entries_){
            (void)key;if(!e.stage->abort(1)){phase_=Phase::Recovery;return false;}
        }
        phase_=Phase::Cut;return true;
    }
    bool shutdown(const Exclusive& exclusive){
        if(!exclusive||!exclusive()||recoveryRequired())return false;
        if(phase_==Phase::Empty)return true;
        if(phase_==Phase::Prepared){discard();return !recoveryRequired();}
        if(!solidReady())return false;
        if(!retireForCut(exclusive))return false;
        discard();return !recoveryRequired();
    }
private:
    // Only used when no installed baseline remains; never release a live
    // ID namespace during recovery.
    void discard(){
        for(auto& [key,e]:entries_){
            (void)key;
            if(e.stage&&e.stage->prepared(1)&&!e.stage->abort(1)){phase_=Phase::Recovery;return;}
        }
        for(auto& [key,e]:entries_){
            (void)key;
            if(e.lease.token&&!pool_.release(e.lease)){phase_=Phase::Recovery;return;}
            e.lease={};
        }
        entries_.clear();body_=-1;surfaceRevision_=0;phase_=Phase::Empty;
    }
    physics::Scene& box_;world::SpatialWorld& spatial_;CaveColliderLeasePool& pool_;
    CaveGameplayPhysicsBarrier barrier_;
    std::map<CavePlanetSurfaceSlotKey,Entry> entries_;
    Phase phase_=Phase::Empty;int body_=-1;uint64_t surfaceRevision_=0;
};
} // namespace aiverse::terraweave::caverns
