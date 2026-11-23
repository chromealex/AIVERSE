#pragma once
// Caverns Stage22: owner-thread bridge from editor dirty events to immutable
// asynchronous CPU mesh preparation, seam certification and batch readiness.
// GPU/STRATA, Box3D and NavMesh publish remain explicit host callbacks.
// NEVER report completion of a dirty region before a successful host commit.
#include "engine/world/TerraWeaveCavernsDirtyQueue.h"
#include "engine/world/TerraWeaveCavernsSeamGate.h"
#include "engine/data/Json.h"
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace aiverse::terraweave::caverns {

struct CavePreparedEdit {
    CaveDirtyChunk dirty{};
    CaveCertifiedChunk candidate{};
};
struct CaveEditTick {
    size_t dispatched=0,finished=0,stale=0,failed=0,pending=0;
    size_t ready=0,blocked=0;
};

// A runtime host creates one owner per loaded cave world and supplies only
// canonical immutable snapshots. All public methods are owner-thread-only.
// The callback on publishReady is executed synchronously on that owner thread.
class CaveEditRuntimeBridge {
public:
    CaveEditRuntimeBridge(uint32_t resolution=16,int32_t spacingCm=100,
                          size_t maxQueued=1024,size_t maxWorkers=2)
        :resolution_(resolution),spacingCm_(spacingCm),
         limit_(maxWorkers),maxQueued_(maxQueued),dirty_(maxQueued),gate_(maxQueued) {
        if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000||
           !maxWorkers||maxWorkers>32||maxQueued>1024)
            throw std::invalid_argument("Caverns edit runtime settings");
    }
    CaveEditRuntimeBridge(const CaveEditRuntimeBridge&)=delete;
    CaveEditRuntimeBridge& operator=(const CaveEditRuntimeBridge&)=delete;

    // Called after MapDoc mutation and after Undo/Redo. If the host cannot
    // supply exact changed bounds (e.g., undo), submit union of previous and
    // current authored stamp bounds or invalidate the entire loaded region.
    void invalidate(CaveDirtyBounds bounds,
                    std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(!snapshot || !snapshot->sourceRevision)
            throw std::invalid_argument("Caverns missing edit snapshot");
        if(snapshot_ && snapshot->sourceRevision<snapshot_->sourceRevision)
            throw std::invalid_argument("Caverns stale snapshot invalidation");
        // Every dirty worker captures exactly this immutable snapshot.
        // Do not mix unrelated generator settings/versions on adjacent cells.
        // A new source revision supersedes every not-yet-committed chunk,
        // not just the latest brush footprint. Otherwise earlier jobs would
        // retain an obsolete snapshot and block this batch forever.
        const auto old=dirty_.pending(maxQueued_);
        dirty_.enqueue(bounds,resolution_,spacingCm_,snapshot->sourceRevision);
        for(const auto& item:old) {
            const int64_t span=int64_t(resolution_)*spacingCm_;
            const CaveDirtyBounds full{item.key.x*span,item.key.y*span,item.key.z*span,
                (item.key.x+1)*span,(item.key.y+1)*span,(item.key.z+1)*span};
            dirty_.enqueue(full,resolution_,spacingCm_,snapshot->sourceRevision);
        }
        prepared_.clear();
        snapshot_=std::move(snapshot);
    }

    // Optional explicit adapter for the Stage20 Workspace operation return.
    // The editor caller forwards its successful result after MapDoc commit;
    // this runtime object never accesses Workspace or mutable MapDoc.
    bool consumeEditorResult(const Json& operationResult,
                             std::shared_ptr<const CaveVolumeSnapshot> snapshot){
        if(!operationResult.isObject() ||
           !operationResult.getBool("ok",false) ||
           !operationResult["dirtyBounds"].isObject())return false;
        const Json& b=operationResult["dirtyBounds"];
        const CaveDirtyBounds bounds{
            b.getInt("minXcm"),b.getInt("minYcm"),b.getInt("minZcm"),
            b.getInt("maxXcm"),b.getInt("maxYcm"),b.getInt("maxZcm")};
        if(!snapshot ||
           operationResult.getInt("revision",0)!=
                int64_t(snapshot->sourceRevision))
            throw std::invalid_argument("Caverns editor snapshot revision mismatch");
        invalidate(bounds,std::move(snapshot));
        return true;
    }

    // Provide an existing resident mesh for cross-chunk border certification.
    // Does not mark a dirty chunk complete or change GPU/collision resources.
    void registerResident(CaveCertifiedChunk current) {
        gate_.upsert(std::move(current));
        gate_.analyze();
    }

    // Streaming retirement: remove committed neighbor certification only
    // AFTER its native visual/collision/nav source has been safely released.
    bool forgetResident(ChunkKey3 key) {
        const bool removed=gate_.erase(key);
        if(removed)gate_.analyze();
        return removed;
    }

    // Prepare at most maxJobs CPU jobs; collect all ready futures without
    // waiting. Generation always uses the latest canonical snapshot.
    CaveEditTick tick(size_t maxJobs=8) {
        if(!maxJobs||maxJobs>dirtyLimit())
            throw std::invalid_argument("Caverns edit tick budget");
        CaveEditTick info;
        for(auto it=running_.begin();it!=running_.end();) {
            if(it->second.future.wait_for(std::chrono::seconds(0))!=
               std::future_status::ready){++it;continue;}
            ++info.finished;
            try{
                auto mesh=it->second.future.get();
                const auto expected=it->second.dirty;
                // An old job cannot erase a newer edit. Dirty queue remains
                // until the host acknowledges successful presentation commit.
                if(!snapshot_ || expected.sourceRevision!=snapshot_->sourceRevision ||
                   !isCurrent(expected)){
                    ++info.stale;
                }else{
                    CaveCertifiedChunk candidate;
                    candidate.mesh=std::move(mesh);
                    candidate.resolution=resolution_;
                    candidate.spacingCm=spacingCm_;
                    candidate.sourceRevision=expected.sourceRevision;
                    if(renderSerial_==UINT64_MAX)
                        throw std::overflow_error("Caverns render revision");
                    candidate.renderRevision=++renderSerial_;
                    prepared_.insert_or_assign(expected.key,
                        CavePreparedEdit{expected,std::move(candidate)});
                }
            }catch(const std::exception&){++info.failed;}
            it=running_.erase(it);
        }
        for(const auto& pending:dirty_.pending(dirtyLimit())){
            if(running_.size()>=limit_ || info.dispatched>=maxJobs)break;
            if(running_.contains(pending.key))continue;
            const auto found=prepared_.find(pending.key);
            if(found!=prepared_.end()&&found->second.dirty.serial==pending.serial)
                continue;
            if(!snapshot_||pending.sourceRevision!=snapshot_->sourceRevision)
                continue;
            auto frozen=snapshot_;
            const auto key=pending.key;
            const uint32_t resolution=resolution_;
            const int32_t spacingCm=spacingCm_;
            auto future=std::async(std::launch::async,[frozen,key,resolution,spacingCm](){
                return generateCaveSnapshotMesh(*frozen,key,resolution,spacingCm);
            });
            running_.emplace(key,Running{pending,std::move(future)});
            ++info.dispatched;
        }
        info.pending=dirty_.size();
        info.ready=prepared_.size();
        info.blocked=info.pending-info.ready;
        return info;
    }

    // A host barrier must first stage ALL candidate GPU/collision/navmesh
    // changes without exposing them, then return true only after successful
    // all-owner commit. If false, prepared CPU state and dirty markers remain.
    // All loaded-neighbor seams must be certified before invoking the host.
    template<class HostCommit>
    bool publishReady(HostCommit&& hostCommit) {
        if(running_.size() || prepared_.empty() ||
           prepared_.size()!=dirty_.size())return false;
        std::vector<CavePreparedEdit> candidates;
        candidates.reserve(prepared_.size());
        for(const auto& [key,entry]:prepared_) {
            (void)key;
            if(!isCurrent(entry.dirty))return false;
            candidates.push_back(entry);
        }
        // Stage prospective visual seams in a local gate. No GPU visibility
        // changes are made until the host commits the full candidate set.
        CaveSeamPublicationGate prospective=gate_;
        for(const auto& candidate:candidates)
            prospective.upsert(candidate.candidate);
        const auto result=prospective.analyze();
        if(result.fallbackFaces || result.pendingChunks)return false;
        for(const auto& candidate:candidates)
            if(!prospective.canPublish(candidate.dirty.key,
                                       candidate.candidate.renderRevision))
                return false;
        // Host is responsible for transactional graphics/physics visibility.
        // Throwing is treated as failure: NEVER ACK dirty edits.
        bool committed=false;
        try {committed=hostCommit(candidates);}
        catch(const std::exception&){return false;}
        if(!committed)return false;
        gate_=std::move(prospective);
        for(const auto& candidate:candidates){
            if(!dirty_.acknowledge(candidate.dirty))
                throw std::logic_error("Caverns dirty ACK changed during owner commit");
        }
        prepared_.clear();
        return true;
    }

    // Inspect a complete, seam-certified version-current CPU candidate batch
    // WITHOUT acknowledging gameplay dirty markers. Caller may stage graphics,
    // collision and nav owners, then publishReady at a real owner barrier.
    std::vector<CavePreparedEdit> certifiedCandidates()const {
        if(!running_.empty()||prepared_.empty()||
           prepared_.size()!=dirty_.size())return {};
        std::vector<CavePreparedEdit> result;
        result.reserve(prepared_.size());
        for(const auto& [key,item]:prepared_) {
            (void)key;
            if(!isCurrent(item.dirty))return {};
            result.push_back(item);
        }
        CaveSeamPublicationGate gate=gate_;
        for(const auto& item:result)gate.upsert(item.candidate);
        const auto stats=gate.analyze();
        if(stats.fallbackFaces||stats.pendingChunks)return {};
        for(const auto& item:result)
            if(!gate.canPublish(item.dirty.key,item.candidate.renderRevision))
                return {};
        return result;
    }

    // Backward-compatible editor-only alias; does not ACK gameplay.
    std::vector<CavePreparedEdit> visualPreviewCandidates()const {
        return certifiedCandidates();
    }
    // Owner-thread streaming integration: all uncommitted dirty keys must
    // remain residency-pinned until publishReady acknowledges a real commit.
    std::vector<ChunkKey3> dirtyKeys()const {
        std::vector<ChunkKey3> result;
        for(const auto& item:dirty_.pending(maxQueued_))
            result.push_back(item.key);
        return result;
    }
    size_t dirtyCount()const{return dirty_.size();}
    size_t preparedCount()const{return prepared_.size();}
    size_t inFlight()const{return running_.size();}
    void drain(){
        for(auto& [key,job]:running_){(void)key;job.future.wait();}
        running_.clear();
    }
private:
    struct Running {
        CaveDirtyChunk dirty{};
        std::future<CaveMesh> future;
    };
    size_t dirtyLimit()const{return maxQueued_;}
    bool isCurrent(const CaveDirtyChunk& request)const{
        // Bounded linear lookup preserves queue ownership without exposing
        // unsafe mutable iterators to asynchronous workers.
        for(const auto& item:dirty_.pending(dirtyLimit()))
            if(item.key==request.key)
                return item.sourceRevision==request.sourceRevision &&
                       item.serial==request.serial;
        return false;
    }
    uint32_t resolution_;
    int32_t spacingCm_;
    size_t limit_,maxQueued_;
    uint64_t renderSerial_=0;
    CaveDirtyQueue dirty_;
    CaveSeamPublicationGate gate_;
    std::shared_ptr<const CaveVolumeSnapshot> snapshot_;
    std::map<ChunkKey3,Running,KeyLess> running_;
    std::map<ChunkKey3,CavePreparedEdit,KeyLess> prepared_;
};
} // namespace aiverse::terraweave::caverns
