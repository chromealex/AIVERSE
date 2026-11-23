#pragma once
// Stage35: genre-independent integration point for the real render/simulation
// owner (game client, editor play host, or authoritative server).
// The owner MUST call frameOutsidePass between GPU passes/physics steps and
// publishWithExclusiveWorld with a real lock/paused readers covering the
// entire call. This is NOT installed in the deterministic GameCore tick,
// because that tick must never touch RHI or mutate out-of-band Box3D.
#include "engine/world/TerraWeaveCavernsGameplayPhysicsBarrier.h"
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include "engine/world/TerraWeaveCavernsStreamJobs.h"
#include <algorithm>
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
                                      size_t pending=1024,size_t workers=2,
                                      StreamSettings streaming={})
        :residency_(streaming),
         streamJobs_(workers),
         host_(resolution,spacingCm,shards,pending,workers) {}
    // Explicit per-world entry point. Instances are single-world owners:
    // after closeWorldAfterGpuIdle succeeds, construct a new one for a new
    // world/device. No GPU shutdown happens in the C++ destructor itself.
    void openWorld(CaveDirtyBounds initialLoadedBounds,
                   std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_)throw std::logic_error("Caverns frame host already closed");
        onEditedVolume(initialLoadedBounds,std::move(snapshot));
    }
    void onEditedVolume(CaveDirtyBounds bounds,
                        std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns world closed or needs recovery");
        host_.edited(bounds,std::move(snapshot));
        readyStreamBuilds_.clear();
        pendingRetires_.clear();
        syncStreamingEditPins();
    }
    bool onAuthoringOperation(const Json& response,
                             std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(closed_||physicsBarrier_.status().recoveryRequired)return false;
        const bool accepted=host_.consumeAuthoringOperation(response,std::move(snapshot));
        if(accepted){
            readyStreamBuilds_.clear();
            pendingRetires_.clear();
            syncStreamingEditPins();
        }
        return accepted;
    }
    // Optional Stage38 real GPU upload source-byte budget and bounded
    // whole-shard count per frame, used by the gameplay edit host.
    void setGpuUploadLimits(CaveGpuUploadLimits limits){
        host_.setGpuUploadLimits(limits);
    }
    CaveGpuUploadStats gpuUploadStats()const noexcept{
        return host_.gpuUploadStats();
    }
    // Runs on an owner frame boundary, not from a deterministic simulation
    // system or an already open graphics command buffer.
    CaveEditTick frameOutsidePass(
        gfx::Device& device,const world::SpatialWorld& spatial,
        CaveNavConfig navConfig,CaveGameplayEditHost::TerrainHeight height,
        const CaveGameplayEditHost::ColliderIdAllocator& colliderIds,
        uint64_t nextVisualRevision,size_t jobsPerFrame=2,
        CaveBatchSpec graphicsOptions={}) {
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns game world closed or requires recovery");
        syncStreamingEditPins();
        (void)host_.collectGpuCompletedWithoutIdle(device);
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
        if(closed_||!exclusive || !exclusive()||
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
    void enableAdaptiveStreamLod(CaveDistanceLodSettings settings={}) {
        residency_.enableDistanceLod(settings);
    }
    void disableAdaptiveStreamLod(){residency_.disableDistanceLod();}
    std::vector<CaveResidencyTicket> pumpStreaming() {
        if(closed_||physicsBarrier_.status().recoveryRequired)return {};
        syncStreamingEditPins();
        return residency_.pump();
    }
    // Frame-limited streaming CPU dispatch. Returns retire intents which
    // require owner-side native resource retirement. Build intents are sent
    // to bounded immutable CPU workers; no GPU work starts on those threads.
    std::vector<CaveResidencyTicket> dispatchStreamingCpu(
        ChunkKey3 cameraChunk,uint32_t resolution=16,int32_t spacingCm=100) {
        if(physicsBarrier_.status().recoveryRequired)return {};
        setStreamCenter(cameraChunk);
        auto source=host_.sourceSnapshot();
        auto indexed=host_.indexedSnapshot();
        if(!source||!indexed)return {};
        syncStreamingEditPins();
        std::vector<CaveResidencyTicket> retire;
        for(const auto& request:residency_.pump()) {
            if(request.action==CaveResidencyAction::Retire) {
                pendingRetires_.insert_or_assign(request.key,request);
                retire.push_back(request);
            }else{
                try {
                    if(!streamJobs_.dispatch(request,source,resolution,spacingCm,indexed))
                        (void)residency_.fail(request);
                } catch(...) {
                    (void)residency_.fail(request);
                    throw;
                }
            }
        }
        return retire;
    }
    // Poll CPU completions at the owner frame boundary. The caller must
    // stage/publish the exact returned ticket through all real native owners
    // before calling acceptStreaming(ticket). Old generations are discarded.
    std::vector<CaveStreamCpuResult> pollStreamingCpu(
        CaveStreamJobStats* telemetry=nullptr) {
        auto results=streamJobs_.poll(telemetry);
        results.erase(std::remove_if(results.begin(),results.end(),
            [this](const CaveStreamCpuResult& r){
                if(!residency_.current(r.ticket))return true;
                if(!r.success){
                    (void)residency_.fail(r.ticket);
                    return true;
                }
                return false;
            }),results.end());
        for(auto& item:results)
            readyStreamBuilds_.insert_or_assign(item.ticket.key,item);
        // Keep completed CPU results alive until the actual GPU/Box3D/Nav
        // owner has published them; never ACK here.
        for(auto it=readyStreamBuilds_.begin();it!=readyStreamBuilds_.end();){
            if(!residency_.current(it->second.ticket))
                it=readyStreamBuilds_.erase(it);
            else ++it;
        }
        return results;
    }
    std::vector<CaveResidencyTicket> outstandingStreamRetires()const {
        std::vector<CaveResidencyTicket> result;
        for(const auto& [key,ticket]:pendingRetires_){
            (void)key;
            if(residency_.current(ticket))result.push_back(ticket);
        }
        return result;
    }
    // Streaming owner-barrier publication. The game host must actually
    // replace matching GPU, Box3D, SpatialWorld and nav resources BEFORE it
    // reports success. The exclusive proof must represent held exclusion.
    // The TWO meshes are intentionally separate. Visual may be coarsened
    // by camera distance; Box3D/SpatialWorld must use collisionMesh.
    using StreamBuildCommit=std::function<bool(const CaveResidencyTicket&,
                                              const CaveMesh& visualMesh,
                                              const CaveMesh& collisionMesh)>;
    using StreamRetireCommit=std::function<bool(const CaveResidencyTicket&)>;
    bool publishStreamBuild(
        CaveResidencyTicket ticket,
        CaveCertifiedChunk publishedMesh,uint64_t generatorVersion,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const StreamBuildCommit& actualNativeCommit) {
        const auto it=readyStreamBuilds_.find(ticket.key);
        if(ticket.action!=CaveResidencyAction::Build ||
           !exclusive || !exclusive() || !actualNativeCommit ||
           it==readyStreamBuilds_.end() ||
           !residency_.current(ticket) ||
           it->second.ticket.generation!=ticket.generation ||
           it->second.ticket.sourceRevision!=ticket.sourceRevision ||
           !generatorVersion || !publishedMesh.renderRevision ||
           publishedMesh.mesh.key!=ticket.key ||
           publishedMesh.sourceRevision!=ticket.sourceRevision ||
           publishedMesh.resolution==0 || publishedMesh.spacingCm<=0)
            return false;
        if(!actualNativeCommit(ticket,it->second.mesh,
                               it->second.collisionMesh))return false;
        try {
            // Match the already-published GPU/physics/nav revision with
            // committed CPU source and seam-residency metadata.
            host_.registerCommittedMesh(std::move(publishedMesh),
                                        generatorVersion);
        } catch(...) {
            physicsBarrier_.requireRecovery();
            throw;
        }
        if(!residency_.acknowledge(ticket))
            throw std::logic_error("Caverns streaming build ACK refused after native commit");
        readyStreamBuilds_.erase(it);
        return true;
    }
    bool publishStreamRetire(
        CaveResidencyTicket ticket,
        const CaveGameplayPhysicsBarrier::ExclusiveProof& exclusive,
        const StreamRetireCommit& actualNativeCommit) {
        const auto it=pendingRetires_.find(ticket.key);
        if(ticket.action!=CaveResidencyAction::Retire ||
           !exclusive || !exclusive() || !actualNativeCommit ||
           it==pendingRetires_.end() || !residency_.current(ticket) ||
           it->second.generation!=ticket.generation ||
           it->second.sourceRevision!=ticket.sourceRevision)
            return false;
        // Actual host must retire previous GeometryScene source/Box3D body/
        // SpatialWorld range/NavMesh at an exclusive owner barrier. Never
        // use an empty/dummy callback as proof of full retirement.
        if(!actualNativeCommit(ticket))return false;
        // The native owner has removed the old source. Forget its committed
        // catalog entry and loaded-neighbor seam certification; do not
        // forget a pending edit or a shard still in GPU staging.
        if(const auto* previous=host_.geometry().catalog().find(ticket.key)) {
            if(!host_.forgetCommittedMesh(ticket.key,previous->revision)){
                physicsBarrier_.requireRecovery();
                throw std::logic_error("Caverns streaming catalog retirement refused");
            }
        }
        if(!residency_.acknowledge(ticket))
            throw std::logic_error("Caverns streaming retirement ACK refused after native commit");
        pendingRetires_.erase(it);
        return true;
    }

    bool acceptStreaming(CaveResidencyTicket ticket){
        if(physicsBarrier_.status().recoveryRequired)return false;
        const bool confirmed=residency_.acknowledge(ticket);
        if(confirmed){
            if(ticket.action==CaveResidencyAction::Retire)
                pendingRetires_.erase(ticket.key);
            else readyStreamBuilds_.erase(ticket.key);
        }
        return confirmed;
    }
    bool failStreaming(CaveResidencyTicket ticket){
        const bool failed=residency_.fail(ticket);
        if(failed){
            if(ticket.action==CaveResidencyAction::Retire)
                pendingRetires_.erase(ticket.key);
            else readyStreamBuilds_.erase(ticket.key);
        }
        return failed;
    }
    uint64_t pinStreamChunk(ChunkKey3 key,CaveLeasePurpose purpose){
        if(closed_||physicsBarrier_.status().recoveryRequired)
            throw std::runtime_error("Caverns stream pin requires active recovered world");
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
    size_t collectGpuCompletedWithoutIdle(gfx::Device& device){
        return host_.collectGpuCompletedWithoutIdle(device);
    }
    bool collectAfterGpuIdle(gfx::Device& device){
        return host_.collectAfterGpuIdle(device);
    }
    bool shutdownAfterGpuIdle(gfx::Device& device){
        streamJobs_.drain();
        if(!host_.shutdownAfterGpuIdle(device))return false;
        pendingRetires_.clear();readyStreamBuilds_.clear();editPins_.clear();
        closed_=true;
        return true;
    }
    bool closeWorldAfterGpuIdle(gfx::Device& device){
        return shutdownAfterGpuIdle(device);
    }
    bool closed()const noexcept{return closed_;}
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
    CaveWorldStreamJobs streamJobs_{2};
    std::map<ChunkKey3,CaveResidencyTicket,KeyLess> pendingRetires_;
    std::map<ChunkKey3,CaveStreamCpuResult,KeyLess> readyStreamBuilds_;
    std::map<ChunkKey3,uint64_t,KeyLess> editPins_;
    CaveGameplayEditHost host_;
    bool closed_=false;
    CaveGameplayPhysicsBarrier physicsBarrier_;
};
} // namespace aiverse::terraweave::caverns
