#pragma once
// Caverns Stage22: owner-thread bridge from editor dirty events to immutable
// asynchronous CPU mesh preparation, seam certification and batch readiness.
// GPU/STRATA, Box3D and NavMesh publish remain explicit host callbacks.
// NEVER report completion of a dirty region before a successful host commit.
#include "engine/world/TerraWeaveCavernsDirtyQueue.h"
#include "engine/world/TerraWeaveCavernsSdfSpatialIndex.h"
#include "engine/world/TerraWeaveCavernsSeamGate.h"
#include "engine/data/Json.h"
#include <chrono>
#include <future>
#include <map>
#include <set>
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
        // Validate/build the immutable index BEFORE altering dirty queue,
        // so an allocation or malformed authoring source cannot strand
        // pending chunks with a snapshot that was never installed.
        auto index=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
        const auto old=dirty_.pending(maxQueued_);
        dirty_.enqueue(bounds,resolution_,spacingCm_,snapshot->sourceRevision);
        for(const auto& item:old) {
            const int64_t span=int64_t(resolution_)*spacingCm_;
            const CaveDirtyBounds full{item.key.x*span,item.key.y*span,item.key.z*span,
                (item.key.x+1)*span,(item.key.y+1)*span,(item.key.z+1)*span};
            dirty_.enqueue(full,resolution_,spacingCm_,snapshot->sourceRevision);
        }
        prepared_.clear();
        lodOverrides_.clear();
        snapshot_=std::move(snapshot);
        index_=std::move(index);
    }

    // Restore an exact sparse set of loaded chunks plus outstanding dirty
    // work. Stage the complete queue before changing the live worker epoch.
    void invalidateKeys(const std::set<ChunkKey3,KeyLess>& keys,
                        std::shared_ptr<const CaveVolumeSnapshot> snapshot) {
        if(!snapshot||!snapshot->sourceRevision||keys.empty()||
           (snapshot_&&snapshot->sourceRevision<=snapshot_->sourceRevision))
            throw std::invalid_argument("Caverns restore worker epoch");
        auto nextKeys=keys;
        for(const auto& item:dirty_.pending(maxQueued_))nextKeys.insert(item.key);
        if(nextKeys.size()>maxQueued_)
            throw std::length_error("Caverns restore dirty budget");
        auto index=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
        auto nextDirty=dirty_;
        const int64_t span=int64_t(resolution_)*spacingCm_;
        const auto limit=(kLimitCm-span)/span;
        for(const auto& key:nextKeys){
            if(key.x< -limit||key.x>limit||key.y< -limit||key.y>limit||
               key.z< -limit||key.z>limit)
                throw std::out_of_range("Caverns restore chunk extent");
            nextDirty.enqueue({key.x*span,key.y*span,key.z*span,
                (key.x+1)*span,(key.y+1)*span,(key.z+1)*span},
                resolution_,spacingCm_,snapshot->sourceRevision);
        }
        dirty_=std::move(nextDirty);
        prepared_.clear();lodOverrides_.clear();
        snapshot_=std::move(snapshot);index_=std::move(index);
        // Old futures remain bounded and are discarded by their epoch check.
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

    // Stage37 LOD fallback: coalesce asynchronous rebuilds for coarse/fine
    // neighboring chunks using ONE current immutable authored SDF snapshot.
    // The chunk coordinate extent does not change: only its meshing lattice
    // (resolution/spacing) changes. This never ACKs gameplay dirtiness.
    bool requestLodRepairs(
        const std::map<ChunkKey3,CaveRebuildRequest,KeyLess>& requests) {
        if(!snapshot_||requests.empty())return false;
        const int64_t chunkSpan=int64_t(resolution_)*spacingCm_;
        const auto current=dirty_.pending(maxQueued_);
        std::set<ChunkKey3,KeyLess> pendingKeys;
        for(const auto& item:current)pendingKeys.insert(item.key);
        size_t added=0;
        for(const auto& [key,request]:requests) {
            if(key!=request.key||!request.resolution||
               request.resolution>64||request.spacingCm<=0||
               int64_t(request.resolution)*request.spacingCm!=chunkSpan)
                throw std::invalid_argument("Caverns invalid shared-lattice repair");
            if(!pendingKeys.contains(key))++added;
        }
        if(added>maxQueued_-dirty_.size())
            throw std::length_error("Caverns shared-lattice dirty budget");
        bool queued=false;
        for(const auto& [key,request]:requests){
            const auto override=lodOverrides_.find(key);
            const bool alreadyPending=pendingKeys.contains(key);
            if(alreadyPending && override!=lodOverrides_.end() &&
               override->second.first==request.resolution &&
               override->second.second==request.spacingCm)
                continue;
            const CaveDirtyBounds exact{
                key.x*chunkSpan,key.y*chunkSpan,key.z*chunkSpan,
                (key.x+1)*chunkSpan,(key.y+1)*chunkSpan,
                (key.z+1)*chunkSpan};
            dirty_.enqueue(exact,resolution_,spacingCm_,
                           snapshot_->sourceRevision);
            lodOverrides_.insert_or_assign(key,
                std::pair<uint32_t,int32_t>{request.resolution,
                                            request.spacingCm});
            prepared_.erase(key);
            queued=true;
        }
        return queued;
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
                    candidate.resolution=it->second.resolution;
                    candidate.spacingCm=it->second.spacingCm;
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
            auto frozen=index_;
            const auto key=pending.key;
            const auto override=lodOverrides_.find(key);
            const uint32_t resolution=override==lodOverrides_.end()
                ?resolution_:override->second.first;
            const int32_t spacingCm=override==lodOverrides_.end()
                ?spacingCm_:override->second.second;
            auto future=std::async(std::launch::async,[frozen,key,resolution,spacingCm](){
                return generateCaveIndexedSnapshotMesh(*frozen,key,resolution,spacingCm);
            });
            running_.emplace(key,Running{pending,resolution,spacingCm,
                                         std::move(future)});
            ++info.dispatched;
        }
        // Resolve unsupported contours BEFORE publication. All affected
        // chunks, including previously unedited resident neighbors, are
        // scheduled on the same latest snapshot at a shared fine lattice.
        // The job queue is versioned; no gameplay ACK occurs here.
        if(running_.empty()&&!prepared_.empty()&&
           prepared_.size()==dirty_.size()) {
            CaveSeamPublicationGate prospective=gate_;
            for(const auto& [key,entry]:prepared_){
                (void)key;
                prospective.upsert(entry.candidate);
            }
            const auto seamStatus=prospective.analyze();
            if(seamStatus.fallbackFaces){
                const auto requests=prospective.requests(dirtyLimit());
                std::map<ChunkKey3,CaveRebuildRequest,KeyLess> work;
                for(const auto& req:requests)work.emplace(req.key,req);
                (void)requestLodRepairs(work);
            }
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
    std::shared_ptr<const CaveSnapshotSpatialIndex> indexedSnapshot()const {
        return index_;
    }
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
        uint32_t resolution=0;
        int32_t spacingCm=0;
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
    std::shared_ptr<const CaveSnapshotSpatialIndex> index_;
    std::map<ChunkKey3,Running,KeyLess> running_;
    std::map<ChunkKey3,CavePreparedEdit,KeyLess> prepared_;
    std::map<ChunkKey3,std::pair<uint32_t,int32_t>,KeyLess> lodOverrides_;
};
} // namespace aiverse::terraweave::caverns
