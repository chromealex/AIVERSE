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
        if(!source||!source->sourceRevision)
            throw std::invalid_argument("Caverns client visual snapshot");
        // Undo/Redo can restore a lower MapDoc revision. The live client
        // uses a strictly increasing runtime epoch for worker tickets.
        auto normalized=std::make_shared<CaveVolumeSnapshot>(*source);
        canonicalizeCaveSnapshot(*normalized);
        if(index_->revision()==UINT64_MAX)
            throw std::overflow_error("Caverns client visual revision overflow");
        normalized->sourceRevision=std::max(
            normalized->sourceRevision,index_->revision()+1);
        auto indexed=std::make_shared<const CaveSnapshotSpatialIndex>(
            std::move(normalized));
        index_=std::move(indexed);
        residency_.sourceRevision(index_->revision());
        cancelPendingVisualBatch();
        ready_.clear();
        pendingRetire_.clear();
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
        // Retire one undesired source at a time using a complete new shard
        // manifest. Never remove the visual from just the CPU catalog:
        // the old GPU owner is retained until a real completed fence.
        if(!visualBatch_)for(auto it=pendingRetire_.begin();it!=pendingRetire_.end();) {
            const auto ticket=it->second;
            if(!residency_.current(ticket)){
                it=pendingRetire_.erase(it);continue;
            }
            const auto* resident=geometry_.catalog().find(ticket.key);
            if(!resident){
                if(!residency_.acknowledge(ticket))
                    throw std::logic_error("Caverns visual no-source retirement ACK");
                ready_.erase(ticket.key);
                it=pendingRetire_.erase(it);
                continue;
            }
            const auto sourceRevision=resident->revision;
            ++visualRevision_;
            if(!geometry_.stageVisualRetire(device,ticket.key,sourceRevision,
                                             visualRevision_))
                break; // budget or pending GPU source: retry later
            const auto shard=caveShardFor(ticket.key,geometry_.catalog().config());
            if(!geometry_.activateVisualOnly({shard}))
                throw std::runtime_error("Caverns client visual retirement activation");
            if(!residency_.acknowledge(ticket))
                throw std::logic_error("Caverns client retired source ACK mismatch");
            ready_.erase(ticket.key);
            it=pendingRetire_.erase(it);
            // Keep a single whole-shard GPU publication per frame.
            return;
        }
        // Stage ONE connected mixed-LOD plan across frames and publish ALL
        // affected shards together. A single-key immediate commit here would
        // expose cracks while neighboring authored revisions are still old.
        if(visualBatch_){
            bool current=true;
            for(const auto& item:batchTickets_){
                if(!residency_.current(item)) {current=false;break;}
            }
            if(!current)cancelPendingVisualBatch();
        }
        if(!visualBatch_ && !ready_.empty()){
            std::vector<CaveBatchChunk> candidates;
            std::vector<CaveResidencyTicket> tickets;
            for(const auto& [key,result]:ready_){
                (void)key;
                if(!residency_.current(result.ticket))continue;
                const auto visual=caveVisualLodMeshSpec(
                    resolution,spacingCm,result.ticket.lod);
                CaveBatchChunk chunk;
                chunk.mesh=result.mesh;
                chunk.resolution=visual.resolution;
                chunk.spacingCm=visual.spacingCm;
                chunk.sourceRevision=result.ticket.sourceRevision;
                chunk.generatorVersion=index_->snapshot().generatorVersion;
                chunk.revision=visualRevision_+1;
                candidates.push_back(std::move(chunk));
                tickets.push_back(result.ticket);
            }
            if(!candidates.empty()){
                auto grouped=groupCaveEditsByShard(candidates,
                                                  geometry_.catalog().config());
                auto plan=planCaveMixedLodShardBatch(
                    geometry_.catalog(),grouped,visualRevision_+1);
                if(plan.ready()){
                    ++visualRevision_;
                    batchTickets_=std::move(tickets);
                    visualBatch_=std::make_unique<CaveMixedLodShardBatch>(
                        std::move(plan));
                }
                // If a neighbor has an incompatible old SDF border, wait
                // for its new streamed mesh; never force-publish a crack.
            }
        }
        if(visualBatch_ &&
           batchCursor_<visualBatch_->snapshots.size()){
            const auto& shard=visualBatch_->snapshots[batchCursor_];
            if(geometry_.stagePlannedShard(
                    device,shard,visualBatch_->visual)){
                stagedShards_.push_back(shard.shard);
                ++batchCursor_;
            }
        }
        if(visualBatch_ &&
           batchCursor_==visualBatch_->snapshots.size()){
            for(const auto& ticket:batchTickets_)
                if(!residency_.current(ticket)){
                    cancelPendingVisualBatch();return;
                }
            if(!geometry_.activateVisualOnly(stagedShards_))
                throw std::runtime_error(
                    "Caverns client multi-shard visual activation");
            for(const auto& ticket:batchTickets_){
                if(!residency_.acknowledge(ticket))
                    throw std::logic_error(
                        "Caverns client batch visual ACK mismatch");
                ready_.erase(ticket.key);
            }
            visualBatch_.reset();
            batchTickets_.clear();
            stagedShards_.clear();
            batchCursor_=0;
        }
    }
    void cancelPendingVisualBatch(){
        for(const auto& shard:stagedShards_)
            (void)geometry_.cancel(shard);
        visualBatch_.reset();
        batchTickets_.clear();
        stagedShards_.clear();
        batchCursor_=0;
    }
    void draw(render::Renderer3D& renderer){geometry_.draw(renderer);}
    bool closeAfterGpuIdle(gfx::Device& dev){
        workers_.drain();
        cancelPendingVisualBatch();
        if(!geometry_.shutdownAfterGpuIdle(dev))return false;
        index_.reset();ready_.clear();pendingRetire_.clear();
        return true;
    }
    size_t pendingCpu()const noexcept{return workers_.inFlight();}
    bool opened()const noexcept{return index_!=nullptr;}
    uint64_t sourceRevision()const noexcept{return index_?index_->revision():0;}
    int64_t suggestedFocusYcm()const noexcept{
        if(!index_)return -1000;
        const auto& c=index_->snapshot().settings;
        // Follow the authored cave vertical range, not a fixed Y=-10 m
        // assumption that would make deeper generated caves invisible.
        return std::clamp<int64_t>(c.maxYcm-800,
                                   c.minYcm+1,c.maxYcm-1);
    }
    CaveResidencyStats streaming()const{return residency_.stats();}
private:
    CaveWorldResidency residency_;
    CaveWorldStreamJobs workers_;
    CaveShardGpuHost geometry_;
    std::shared_ptr<const CaveSnapshotSpatialIndex> index_;
    std::map<ChunkKey3,CaveStreamCpuResult,KeyLess> ready_;
    std::map<ChunkKey3,CaveResidencyTicket,KeyLess> pendingRetire_;
    std::unique_ptr<CaveMixedLodShardBatch> visualBatch_;
    std::vector<CaveResidencyTicket> batchTickets_;
    std::vector<CaveShardKey> stagedShards_;
    size_t batchCursor_=0;
    uint64_t visualRevision_=0;
};
} // namespace aiverse::terraweave::caverns
