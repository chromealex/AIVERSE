#pragma once
// Stage45 — genre-neutral transactional SpatialWorld cave collider publication.
// ALL candidate triangles are installed into a private COPY of the existing
// SpatialWorld. Only one owner/exclusive world barrier may swap the copy live.
// Other actor/terrain colliders and shared character controllers are preserved.
// This owns no Box3D and never claims that Box3D/GPU/NavMesh are committed.
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveActorCollisionRecord {
    uint8_t slot=0;
    uint32_t count=0;
    uint64_t sourceRevision=0;
};
struct CaveActorCollisionUpdate {
    ChunkKey3 key{};
    CaveMesh mesh{};
    uint32_t resolution=16;
    int32_t spacingCm=100;
};
class CaveActorCollisionCatalog {
public:
    static constexpr uint32_t kBase=0x60000000u;
    static constexpr uint32_t kCapacity=64;
    static constexpr uint32_t kIdsPerChunk=65536u;
    static constexpr size_t kMaxTriangles=65534;
    const std::map<ChunkKey3,CaveActorCollisionRecord,KeyLess>&
    records()const noexcept{return records_;}
    uint64_t revision()const noexcept{return revision_;}
    size_t count()const noexcept{return records_.size();}
    bool contains(ChunkKey3 key)const noexcept{return records_.contains(key);}
    static uint32_t firstId(uint8_t slot){
        if(slot>=kCapacity)
            throw std::out_of_range("Caverns ActorHost collision slot");
        return kBase+uint32_t(slot)*kIdsPerChunk;
    }
    // Setup-only: map does not yet have any cave triangles, and actor
    // spawns have not been validated. Existing collider namespaces are
    // respected. No per-frame ID allocation or release guesswork.
    bool addInitial(world::SpatialWorld& spatial,
                    const CaveActorCollisionUpdate& update,
                    uint64_t sourceRevision) {
        if(!sourceRevision||revision_>sourceRevision||
           records_.contains(update.key)||records_.size()>=kCapacity)
            return false;
        const uint8_t slot=takeSlot();
        const auto prepared=prepareCaveVolumeCollision(
            update.mesh,update.resolution,update.spacingCm,sourceRevision);
        const uint32_t id=firstId(slot);
        for(uint32_t i=0;i<prepared.triangles.size();++i)
            if(spatial.colliders().contains(id+i))
                throw std::invalid_argument("Caverns native collision ID conflict");
        // The setup caller owns startup failure recovery. No player/physics
        // readers exist until the complete map has been loaded.
        uint32_t added=0;
        try {
            for(const auto& triangle:prepared.triangles){
                if(!spatial.addTriangle(id+added,triangle))
                    throw std::runtime_error("Caverns initial triangle insert");
                ++added;
            }
        }catch(...){
            for(uint32_t i=0;i<added;++i)spatial.remove(id+i);
            throw;
        }
        records_.emplace(update.key,CaveActorCollisionRecord{
            slot,uint32_t(prepared.triangles.size()),sourceRevision});
        revision_=sourceRevision;
        return true;
    }
    struct Candidate {
        std::unique_ptr<world::SpatialWorld> spatial;
        std::map<ChunkKey3,CaveActorCollisionRecord,KeyLess> records;
        uint64_t revision=0;
        size_t replaced=0,retired=0,created=0;
    };
    // New authoritative meshes may include empty sources (retire all old
    // triangles), or a new neighboring chunk. All sources MUST belong to
    // the same new SDF epoch. Only changed chunks are provided.
    Candidate prepare(const world::SpatialWorld& active,
                      const std::vector<CaveActorCollisionUpdate>& edits,
                      uint64_t oldRevision,uint64_t newRevision)const {
        if(!newRevision||newRevision<=oldRevision||
           oldRevision!=revision_||edits.empty()||edits.size()>kCapacity)
            throw std::invalid_argument("Caverns actor collision epoch");
        Candidate next;
        next.spatial=std::make_unique<world::SpatialWorld>(active);
        next.records=records_;
        next.revision=newRevision;
        std::set<ChunkKey3,KeyLess> seen;
        for(const auto& edit:edits){
            if(edit.mesh.key!=edit.key||!seen.insert(edit.key).second)
                throw std::invalid_argument("Caverns duplicate/incorrect chunk");
            auto payload=prepareCaveVolumeCollision(edit.mesh,
                edit.resolution,edit.spacingCm,newRevision);
            auto found=next.records.find(edit.key);
            uint8_t slot=0;
            if(found==next.records.end()){
                if(next.records.size()>=kCapacity)
                    throw std::length_error("Caverns ActorHost collision slots");
                slot=takeSlot(next.records);
            }else{
                slot=found->second.slot;
                ++next.replaced;
                ++next.retired;
                for(uint32_t i=0;i<found->second.count;++i)
                    next.spatial->remove(firstId(slot)+i);
            }
            const uint32_t begin=firstId(slot);
            for(uint32_t i=0;i<payload.triangles.size();++i) {
                if(next.spatial->colliders().contains(begin+i)||
                   !next.spatial->addTriangle(begin+i,payload.triangles[i]))
                    throw std::runtime_error("Caverns staged actor collision conflict");
            }
            next.records[edit.key]={slot,uint32_t(payload.triangles.size()),
                                    newRevision};
            ++next.created;
        }
        return next;
    }
    // Caller holds a TRUE quiescent world owner lock. SpatialWorld and
    // catalog map swaps are constant-time/no allocation for std::map.
    // After this point all SpatialWorld queries see the new epoch.
    bool publish(world::SpatialWorld& active,Candidate& candidate,
                 const std::function<bool()>& exclusive) {
        if(!exclusive||!exclusive()||!candidate.spatial||
           candidate.revision<=revision_)return false;
        using std::swap;
        swap(active,*candidate.spatial);
        records_.swap(candidate.records);
        revision_=candidate.revision;
        candidate.spatial.reset();
        return true;
    }
    // Used only for recovery/reloading the entire native world.
    void reset()noexcept{records_.clear();revision_=0;}
private:
    static uint8_t takeSlot(
        const std::map<ChunkKey3,CaveActorCollisionRecord,KeyLess>& existing) {
        bool taken[kCapacity]{};
        for(const auto& [key,item]:existing){
            (void)key;
            if(item.slot>=kCapacity)
                throw std::logic_error("Caverns catalog slot corrupted");
            taken[item.slot]=true;
        }
        for(uint32_t i=0;i<kCapacity;++i)
            if(!taken[i])return uint8_t(i);
        throw std::length_error("Caverns ActorHost collision slot exhausted");
    }
    uint8_t takeSlot()const{return takeSlot(records_);}
    std::map<ChunkKey3,CaveActorCollisionRecord,KeyLess> records_;
    uint64_t revision_=0;
};
} // namespace aiverse::terraweave::caverns
