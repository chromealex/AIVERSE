#pragma once
// Stage35: genre-independent integration point for the real render/simulation
// owner (game client, editor play host, or authoritative server).
// The owner MUST call frameOutsidePass between GPU passes/physics steps and
// publishWithExclusiveWorld with a real lock/paused readers covering the
// entire call. This is NOT installed in the deterministic GameCore tick,
// because that tick must never touch RHI or mutate out-of-band Box3D.
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
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
    }
    bool onAuthoringOperation(const Json& response,
                             std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(physicsBarrier_.status().recoveryRequired)return false;
        return host_.consumeAuthoringOperation(response,std::move(snapshot));
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
        return host_.commitAtOwnerBarrier([&](const CaveGameplayCommitView& view){
            if(!exclusive())return false;
            return physicsBarrier_.apply(view,box3D,spatial,exclusive);
        });
    }
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
    CaveGameplayEditHost host_;
    CaveGameplayPhysicsBarrier physicsBarrier_;
};
} // namespace aiverse::terraweave::caverns
