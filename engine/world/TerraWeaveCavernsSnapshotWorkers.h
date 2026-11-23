#pragma once
// Stage18: asynchronous immutable full-volume rebuilds using existing Stage17
// gate protocol. No mutable ECS/renderer/device access from worker threads.
#include "TerraWeaveCavernsVolumeSnapshot.h"
#include "TerraWeaveCavernsSeamGate.h"
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
namespace aiverse::terraweave::caverns {

struct CaveSnapshotRebuildResult {
    CaveRebuildRequest request{};
    CaveCertifiedChunk chunk{};
};
struct CaveSnapshotPumpStats {
    size_t started=0,finished=0,accepted=0,stale=0,failed=0,inFlight=0;
};
class CaveSnapshotRegenerator {
public:
    explicit CaveSnapshotRegenerator(size_t maxWorkers=2):limit_(maxWorkers) {
        if(!maxWorkers||maxWorkers>32)
            throw std::invalid_argument("Caverns snapshot worker limit");
    }
    CaveSnapshotRegenerator(const CaveSnapshotRegenerator&)=delete;
    CaveSnapshotRegenerator& operator=(const CaveSnapshotRegenerator&)=delete;

    // Owner thread. Snapshot must already be validated/canonicalized and is
    // immutable for the complete duration of any submitted worker job.
    // Calls with a different snapshot sourceRevision cannot schedule old work.
    CaveSnapshotPumpStats pump(CaveSeamPublicationGate& gate,
        std::shared_ptr<const CaveVolumeSnapshot> snapshot,size_t budget=8){
        if(!snapshot)throw std::invalid_argument("Caverns missing snapshot");
        CaveSnapshotPumpStats result=poll(gate);
        for(const auto& req:gate.requests(budget)){
            if(jobs_.size()>=limit_)break;
            if(jobs_.contains(req.key)||req.sourceRevision!=snapshot->sourceRevision)
                continue;
            if(nextRenderRevision_==UINT64_MAX)
                throw std::overflow_error("Caverns render revision overflow");
            const uint64_t renderRevision=++nextRenderRevision_;
            auto worker=std::async(std::launch::async,
                [snapshot,req,renderRevision](){
                    CaveSnapshotRebuildResult done;
                    done.request=req;
                    done.chunk.mesh=generateCaveSnapshotMesh(
                        *snapshot,req.key,req.resolution,req.spacingCm);
                    done.chunk.resolution=req.resolution;
                    done.chunk.spacingCm=req.spacingCm;
                    done.chunk.sourceRevision=req.sourceRevision;
                    done.chunk.renderRevision=renderRevision;
                    return done;
                });
            jobs_.emplace(req.key,std::move(worker));
            ++result.started;
        }
        result.inFlight=jobs_.size();
        return result;
    }
    CaveSnapshotPumpStats poll(CaveSeamPublicationGate& gate) {
        CaveSnapshotPumpStats result;
        for(auto it=jobs_.begin();it!=jobs_.end();){
            if(it->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready){
                ++it;continue;
            }
            ++result.finished;
            try {
                auto done=it->second.get();
                if(gate.accept(done.request,std::move(done.chunk)))++result.accepted;
                else ++result.stale;
            } catch(const std::exception&) {
                ++result.failed; // fail closed; owner decides retry
            }
            it=jobs_.erase(it);
        }
        if(result.accepted)gate.analyze();
        result.inFlight=jobs_.size();
        return result;
    }
    // Only at world shutdown; never from a rendering frame.
    void drain(){
        for(auto& [key,future]:jobs_){(void)key;future.wait();}
        jobs_.clear();
    }
    size_t inFlight()const{return jobs_.size();}
private:
    size_t limit_;
    uint64_t nextRenderRevision_=0;
    std::map<ChunkKey3,std::future<CaveSnapshotRebuildResult>,KeyLess> jobs_;
};
} // namespace aiverse::terraweave::caverns
