#pragma once
// Stage35: genre-independent integration point for the real render/simulation
// owner (game client, editor play host, or authoritative server).
// The owner MUST call frameOutsidePass between GPU passes/physics steps and
// publishWithExclusiveWorld with a real lock/paused readers covering the
// entire call. This is NOT installed in the deterministic GameCore tick,
// because that tick must never touch RHI or mutate out-of-band Box3D.
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::terraweave::caverns {
class CaveGameplayRuntimeFrame {
public:
    explicit CaveGameplayRuntimeFrame(uint32_t resolution=16,
                                      int32_t spacingCm=100,
                                      CaveShardConfig shards={},
                                      size_t pending=1024,size_t workers=2)
        :host_(resolution,spacingCm,shards,pending,workers) {}
    void onEditedVolume(CaveDirtyBounds bounds,
                        std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns needs world recovery");
        host_.edited(bounds,std::move(snapshot));
        syncStreamingEditPins();
    }
    bool onAuthoringOperation(const Json& response,
                             std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(physicsBarrier_.status().recoveryRequired)return false;
        const bool accepted=host_.consumeAuthoringOperation(response,std::move(snapshot));
        if(accepted)syncStreamingEditPins();
        return accepted;
    }
    // Runs on an owner frame boundary, not from a deterministic simulation
    // system or an already open graphics command buffer.
    CaveEditTick frameOutsidePass(
        gfx::Device& device,const world::SpatialWorld& spatial,
        CaveNavConfig navConfig,CaveGameplayEditHost::TerrainHeight height,
        const CaveGameplayEditHost::ColliderIdAllocator& colliderIds,
        uint64_t nextVisualRevision,size_t jobsPerFrame=2,
        CaveBatchSpec graphicsOptions={}) {
        if(physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns game world requires recovery");
        syncStreamingEditPins();
        return host_.advance(device,spatial,navConfig,std::move(height),
                             colliderIds,nextVisualRevision,jobsPerFrame,
                             graphicsOptions);
    }
    // The exclusive predicate must remain TRUE and actually hold simulation,
    // physics queries, nav readers and draw submissions paused until this
    // method returns. An unconditional-true predicate is NOT sufficient
    // without a real owner quiescence mechanism.
    bool publishWithExclusiveWorld(
        physics::Scene& box3D,world::SpatialWorld& spatial,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive) {
        if(!exclusive || !exclusive()||
           physicsBarrier_.status().recoveryRequired)return false;
        bool physicsFinalized=false;
        bool published=false;
        try {
            published=host_.commitAtOwnerBarrier([&](const CaveGameplayCommitView& view){
                if(!exclusive())return false;
                physicsFinalized=physicsBarrier_.apply(view,box3D,spatial,exclusive);
                return physicsFinalized;
            });
        }catch(...) {
            if(physicsFinalized)physicsBarrier_.requireRecovery();
            throw;
        }
        if(!published&&physicsFinalized)
            physicsBarrier_.requireRecovery();
        if(published)syncStreamingEditPins();
        return published;
    }
    // The host drives interest once per world frame using the camera/player
    // center in CHUNK coordinates. Build tickets may be scheduled on worker
    // threads; retire tickets must go through the actual physics/GPU/nav
    // owner barrier. Tickets are NOT ACKed by pumpStreaming().
    void setStreamCenter(ChunkKey3 center){residency_.center(center);}
    void setStreamLod(uint32_t lod){residency_.visualLod(lod);}
    std::vector<CaveResidencyTicket> pumpStreaming() {
        if(physicsBarrier_.status().recoveryRequired)return {};
        syncStreamingEditPins();
        return residency_.pump();
    }
    bool acceptStreaming(CaveResidencyTicket ticket){
        if(physicsBarrier_.status().recoveryRequired)return false;
        return residency_.acknowledge(ticket);
    }
    bool failStreaming(CaveResidencyTicket ticket){
        return residency_.fail(ticket);
    }
    uint64_t pinStreamChunk(ChunkKey3 key,CaveLeasePurpose purpose){
        if(physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns stream pin requires recovered world");
        if(!residency_.stats().sourceRevision)
            throw std::logic_error("Caverns stream pin requires source revision");
        return residency_.pin(key,purpose);
    }
    bool unpinStreamChunk(ChunkKey3 key,uint64_t lease){
        return residency_.unpin(key,lease);
    }
    CaveResidencyStats streamingStats()const{return residency_.stats();}
    void drawCommitted(render::Renderer3D& renderer){
        if(!physicsBarrier_.status().recoveryRequired)host_.draw(renderer);
    }
    bool collectAfterGpuIdle(gfx::Device& device){
        return host_.collectAfterGpuIdle(device);
    }
    bool shutdownAfterGpuIdle(gfx::Device& device){
        return host_.shutdownAfterGpuIdle(device);
    }
    CaveGameplayEditStats stats()const{return host_.stats();}
    const CavePhysicsCommitStatus& physicsStatus()const noexcept{
        return physicsBarrier_.status();
    }
    CaveGameplayEditHost& host()noexcept{return host_;}
private:
    void syncStreamingEditPins() {
        const auto source=host_.stats().requestedRevision;
        if(!source)return;
        if(residency_.stats().sourceRevision!=source)
            residency_.sourceRevision(source);
        const auto pending=host_.pendingEditKeys();
        const std::set<ChunkKey3,KeyLess> wanted(pending.begin(),pending.end());
        for(auto it=editPins_.begin();it!=editPins_.end();) {
            if(!wanted.contains(it->first)) {
                residency_.unpin(it->first,it->second);
                it=editPins_.erase(it);
            }else ++it;
        }
        for(const auto& key:wanted)
            if(!editPins_.contains(key))
                editPins_.emplace(key,residency_.pin(key,CaveLeasePurpose::Edit));
    }
    CaveWorldResidency residency_;
    std::map<ChunkKey3,uint64_t,KeyLess> editPins_;
    CaveGameplayEditHost host_;
    CaveGameplayPhysicsBarrier physicsBarrier_;
};
} // namespace aiverse::terraweave::caverns
