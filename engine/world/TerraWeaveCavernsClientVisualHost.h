#pragma once
// TerraWeave Caverns Stage40: native GameClient/Editor Play shared visual host.
// Opt-in scene presentation ONLY until a game world provides a real Box3D,
// SpatialWorld and nav owner. Never ACK gameplay edits from this adapter.
// The actual client and editor-hosted Game tab both use GameClient, so this
// adapter is owned once per match, not by FPS/RTS genre modules.
#include "engine/world/TerraWeaveCavernsSdfSpatialIndex.h"
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include "engine/world/TerraWeaveCavernsStreamJobs.h"
#include "engine/render/TerraWeaveCavernsShardGpuHost.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
class CaveClientVisualHost {
public:
    explicit CaveClientVisualHost(StreamSettings stream={})
        :residency_(stream),workers_(2) {
        geometry_.setUploadLimits({32ull*1024*1024,1,64});
    }
    // Initial snapshot must already be canonicalized by the map loader.
    void open(std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(!snapshot||!snapshot->sourceRevision)
            throw std::invalid_argument("Caverns client visual world snapshot");
        if(index_)throw std::logic_error("Caverns client world already open");
        index_=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
        residency_.sourceRevision(snapshot->sourceRevision);
    }
    // Frame owner, OUTSIDE a render pass. One edit to the source replaces
    // the immutable index; old async work retains its old source but its
    // ticket generation becomes invalid and cannot enter the draw catalog.
    void replaceSnapshot(std::shared_ptr<const CaveVolumeSnapshot> source) {
        if(!index_)throw std::logic_error("Caverns visual world not open");
        auto indexed=std::make_shared<const CaveSnapshotSpatialIndex>(source);
        if(indexed->revision()<=index_->revision())
            throw std::invalid_argument("Caverns client visual stale revision");
        index_=std::move(indexed);
        residency_.sourceRevision(index_->revision());
        ready_.clear();
    }
    // CPU jobs may finish in any order. GPU publication stays owner-thread
    // and is frame-admitted by the complete-shard geometry host.
    void beforeRender(gfx::Device& device,PointCm focus,
                      uint32_t resolution=16,int32_t spacingCm=100) {
        if(!index_)return;
        (void)geometry_.collectCompletedWithoutIdle(device);
        residency_.center(caveStreamingChunkAt(focus,resolution,spacingCm));
        const auto requests=residency_.pump();
        for(const auto& ticket:requests) {
            if(ticket.action==CaveResidencyAction::Build){
                if(!workers_.dispatch(ticket,index_->frozenSnapshot(),
                                      resolution,spacingCm,index_))
                    (void)residency_.fail(ticket);
            }else {
                // No visual retirement here until the full source replacement
                // for that shard is staged. Keep the resident/intent ticket
                // rather than dropping the old GPU geometry unsafely.
                pendingRetire_.insert_or_assign(ticket.key,ticket);
            }
        }
        for(auto& result:workers_.poll()) {
            if(!residency_.current(result.ticket))continue;
            if(!result.success){
                (void)residency_.fail(result.ticket);
                continue;
            }
            ready_.insert_or_assign(result.ticket.key,std::move(result));
        }
        // Exactly one GPU upload per frame (Stage38 source-size admission).
        for(auto it=ready_.begin();it!=ready_.end();){
            if(!residency_.current(it->second.ticket)) {
                it=ready_.erase(it);continue;
            }
            CaveBatchChunk chunk;
            chunk.mesh=it->second.mesh; // visual LOD; never used for physics
            chunk.resolution=caveVisualLodMeshSpec(
                resolution,spacingCm,it->second.ticket.lod).resolution;
            chunk.spacingCm=caveVisualLodMeshSpec(
                resolution,spacingCm,it->second.ticket.lod).spacingCm;
            chunk.sourceRevision=it->second.ticket.sourceRevision;
            chunk.generatorVersion=index_->snapshot().generatorVersion;
            chunk.revision=++visualRevision_;
            const auto shard=caveShardFor(chunk.mesh.key,geometry_.catalog().config());
            // Cross-shard stitching may demand rebuilding neighbors on one
            // common lattice. The owner refuses that upload rather than
            // exposing a crack.
            if(!geometry_.stage(device,shard,{chunk},visualRevision_))
                break;
            if(!geometry_.activateVisualOnly({shard}))
                throw std::runtime_error("Caverns client visual shard activation");
            if(!residency_.acknowledge(it->second.ticket))
                throw std::logic_error("Caverns client visual ACK mismatch");
            it=ready_.erase(it);
            break;
        }
    }
    void draw(render::Renderer3D& renderer){geometry_.draw(renderer);}
    bool closeAfterGpuIdle(gfx::Device& dev){
        workers_.drain();
        if(!geometry_.shutdownAfterGpuIdle(dev))return false;
        index_.reset();ready_.clear();pendingRetire_.clear();
        return true;
    }
    size_t pendingCpu()const noexcept{return workers_.inFlight();}
    CaveResidencyStats streaming()const{return residency_.stats();}
private:
    CaveWorldResidency residency_;
    CaveWorldStreamJobs workers_;
    CaveShardGpuHost geometry_;
    std::shared_ptr<const CaveSnapshotSpatialIndex> index_;
    std::map<ChunkKey3,CaveStreamCpuResult,KeyLess> ready_;
    std::map<ChunkKey3,CaveResidencyTicket,KeyLess> pendingRetire_;
    uint64_t visualRevision_=0;
};
} // namespace aiverse::terraweave::caverns
