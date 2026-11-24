#pragma once
// AIVERSE generic world collision publication for authored SDF volumes.
// No actor, character, FPS, RTS or navigation algorithm dependencies.
// Prepares one private full SpatialWorld snapshot. All live change occurs
// under a real owner-held exclusive barrier, after optional other owners
// have published (or with recoveryRequired set when confirmation fails).
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveSpatialChunk {
    ChunkKey3 key{};
    CaveMesh mesh{};
    uint32_t resolution=16;
    int32_t spacingCm=100;
};
struct CaveSpatialLease {
    uint8_t slot=0;
    uint32_t count=0;
    std::vector<uint64_t> generations;
};
class CaveSpatialRevisionOwner {
public:
    static constexpr uint32_t kReservedBase=0x60000000u;
    static constexpr uint32_t kChunkSlots=64u;
    static constexpr uint32_t kIdsPerSlot=65536u;
    struct Candidate {
        std::unique_ptr<world::SpatialWorld> world;
        std::map<ChunkKey3,CaveSpatialLease,KeyLess> leases;
        uint64_t oldRevision=0,newRevision=0,baseWorldVersion=0;
        const world::SpatialWorld* baseWorld=nullptr;
        size_t changed=0;
    };
    uint64_t revision()const noexcept{return revision_;}
    bool recoveryRequired()const noexcept{return recovery_;}
    size_t residentChunks()const noexcept{return leases_.size();}
    static uint32_t firstId(uint8_t slot) {
        if(slot>=kChunkSlots)
            throw std::out_of_range("Caverns spatial lease slot");
        return kReservedBase+uint32_t(slot)*kIdsPerSlot;
    }
    // Initial map setup only. No other physics or navigation readers may
    // run until all startup sources are loaded and validated.
    void addInitial(world::SpatialWorld& active,
                    const CaveSpatialChunk& chunk,uint64_t sourceRevision) {
        if(recovery_||!sourceRevision||revision_>sourceRevision ||
           leases_.contains(chunk.key))
            throw std::invalid_argument("Caverns initial spatial epoch");
        const auto prepared=prepareCaveVolumeCollision(chunk.mesh,
            chunk.resolution,chunk.spacingCm,sourceRevision);
        const uint8_t slot=allocateSlot(leases_);
        const uint32_t start=firstId(slot);
        for(uint32_t i=0;i<prepared.triangles.size();++i)
            if(active.colliders().contains(start+i))
                throw std::invalid_argument("Caverns spatial ID conflict");
        uint32_t inserted=0;
        try {
            for(const auto& triangle:prepared.triangles) {
                if(!active.addTriangle(start+inserted,triangle))
                    throw std::runtime_error("Caverns initial spatial insert");
                ++inserted;
            }
            CaveSpatialLease lease{slot,uint32_t(prepared.triangles.size()),{}};
            lease.generations.reserve(inserted);
            for(uint32_t n=0;n<inserted;++n)
                lease.generations.push_back(active.colliderGeneration(start+n));
            leases_.emplace(chunk.key,std::move(lease));
            revision_=sourceRevision;
        } catch(...) {
            for(uint32_t n=0;n<inserted;++n)active.remove(start+n);
            throw;
        }
    }
    Candidate prepare(const world::SpatialWorld& current,
                      const std::vector<CaveSpatialChunk>& changed,
                      uint64_t nextRevision)const {
        if(recovery_||!nextRevision||nextRevision<=revision_||
           changed.empty()||changed.size()>kChunkSlots)
            throw std::invalid_argument("Caverns spatial staging epoch");
        Candidate stage;
        stage.baseWorld=&current;
        stage.baseWorldVersion=current.mutationVersion();
        stage.world=std::make_unique<world::SpatialWorld>(current);
        stage.leases=leases_;
        stage.oldRevision=revision_;
        stage.newRevision=nextRevision;
        std::set<ChunkKey3,KeyLess> seen;
        for(const auto& chunk:changed) {
            if(chunk.mesh.key!=chunk.key||!seen.insert(chunk.key).second)
                throw std::invalid_argument("Caverns duplicate chunk staging");
            auto prepared=prepareCaveVolumeCollision(
                chunk.mesh,chunk.resolution,chunk.spacingCm,nextRevision);
            auto it=stage.leases.find(chunk.key);
            const uint8_t slot=it==stage.leases.end()
                ?allocateSlot(stage.leases):it->second.slot;
            const uint32_t start=firstId(slot);
            if(it!=stage.leases.end()) {
                if(it->second.generations.size()!=it->second.count)
                    throw std::logic_error("Caverns malformed collision lease");
                for(uint32_t n=0;n<it->second.count;++n)
                    if(!stage.world->removeIfGeneration(start+n,it->second.generations[n]))
                        throw std::logic_error("Caverns collision lease no longer owns ID");
            }
            for(uint32_t n=0;n<prepared.triangles.size();++n) {
                if(stage.world->colliders().contains(start+n)||
                   !stage.world->addTriangle(start+n,prepared.triangles[n]))
                    throw std::runtime_error("Caverns staged spatial insertion");
            }
            // Empty surfaces keep their slot until a separate explicit
            // retirement decision; no other chunk can steal live IDs.
            CaveSpatialLease lease{slot,uint32_t(prepared.triangles.size()),{}};
            lease.generations.reserve(lease.count);
            for(uint32_t n=0;n<lease.count;++n)
                lease.generations.push_back(stage.world->colliderGeneration(start+n));
            stage.leases[chunk.key]=std::move(lease);
            ++stage.changed;
        }
        return stage;
    }
    // Retirement is staged against the same live-world version as edits.
    // Authored SDF revision can remain unchanged when a chunk streams out.
    Candidate prepareRetirement(const world::SpatialWorld& current,
                                const std::vector<ChunkKey3>& keys)const {
        if(recovery_||!revision_||keys.empty()||keys.size()>kChunkSlots)
            throw std::invalid_argument("Caverns spatial retirement");
        Candidate stage;
        stage.baseWorld=&current;
        stage.baseWorldVersion=current.mutationVersion();
        stage.world=std::make_unique<world::SpatialWorld>(current);
        stage.leases=leases_;
        stage.oldRevision=stage.newRevision=revision_;
        for(const auto& key:keys) {
            const auto it=stage.leases.find(key);
            if(it==stage.leases.end()||it->second.generations.size()!=it->second.count)
                throw std::invalid_argument("Caverns unknown or duplicate retirement");
            const auto first=firstId(it->second.slot);
            for(uint32_t n=0;n<it->second.count;++n)
                if(!stage.world->removeIfGeneration(first+n,it->second.generations[n]))
                    throw std::logic_error("Caverns retirement lost collision ownership");
            stage.leases.erase(it);
            ++stage.changed;
        }
        return stage;
    }
    // The owner must stop ALL SpatialWorld readers and writers until return.
    // It also owns coordination with Box3D/NavMesh/GPU, which are not
    // implicitly committed by this SpatialWorld-only revision switch.
    bool publish(world::SpatialWorld& active,Candidate& candidate,
                 const std::function<bool()>& exclusive) {
        if(recovery_||!exclusive||!exclusive()||!candidate.world||
           candidate.oldRevision!=revision_||
           candidate.newRevision<revision_||!candidate.changed||candidate.baseWorld!=&active)return false;
        if(!active.publishCopyIfCurrent(std::move(*candidate.world),candidate.baseWorldVersion))return false;
        leases_.swap(candidate.leases);
        revision_=candidate.newRevision;
        candidate.world.reset();
        return true;
    }
    void requireRecovery()noexcept{recovery_=true;}
    void resetAfterFullWorldRebuild()noexcept{
        leases_.clear();revision_=0;recovery_=false;
    }
private:
    static uint8_t allocateSlot(
        const std::map<ChunkKey3,CaveSpatialLease,KeyLess>& leases){
        bool used[kChunkSlots]{};
        for(const auto& [key,lease]:leases) {
            (void)key;
            if(lease.slot>=kChunkSlots)
                throw std::logic_error("Caverns malformed spatial lease");
            used[lease.slot]=true;
        }
        for(uint32_t s=0;s<kChunkSlots;++s)
            if(!used[s])return uint8_t(s);
        throw std::length_error("Caverns spatial chunk budget");
    }
    std::map<ChunkKey3,CaveSpatialLease,KeyLess> leases_;
    uint64_t revision_=0;
    bool recovery_=false;
};
} // namespace aiverse::terraweave::caverns
