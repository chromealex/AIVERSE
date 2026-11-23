#pragma once
// Stage 17: bounded asynchronous cave seam rebuild jobs.
// This header owns WORKER CPU tasks only. It must be driven on the owner
// thread; GeometryScene, STRATA, RHI, SpatialWorld and Box3D are NEVER used
// by workers. This is deliberately separate from live publication.
#include "engine/world/TerraWeaveCavernsSeamGate.h"
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {

struct CaveRebuildResult {
    CaveRebuildRequest request{};
    CaveCertifiedChunk chunk{};
};

struct CaveRebuildPumpStats {
    size_t launched=0, completed=0, accepted=0, stale=0, failed=0;
    size_t inFlight=0;
};

class CaveAsyncRegenerator {
public:
    explicit CaveAsyncRegenerator(size_t maxWorkers=2)
        : limit_(maxWorkers) {
        if(maxWorkers==0 || maxWorkers>32)
            throw std::invalid_argument("Caverns worker budget");
    }

    CaveAsyncRegenerator(const CaveAsyncRegenerator&)=delete;
    CaveAsyncRegenerator& operator=(const CaveAsyncRegenerator&)=delete;

    // Call ONLY on the main/scene owner thread. Caller must first invoke
    // gate.analyze() and supply an immutable settings snapshot corresponding
    // to the requests' sourceRevision. Each task captures the snapshot by
    // value; a settings mutation cannot affect an already running task.
    //
    // A failed task is not retried automatically: caller must inspect errors,
    // fix the source or explicitly request another pump after acknowledgement.
    CaveRebuildPumpStats pump(CaveSeamPublicationGate& gate,
                              const Settings& settings,
                              size_t requestBudget=8) {
        validate(settings);
        CaveRebuildPumpStats stats;
        collect(gate,stats);
        const auto wanted=gate.requests(requestBudget);
        for(const auto& request:wanted) {
            if(jobs_.size()>=limit_)break;
            // A stale in-flight job for the same chunk must finish before
            // replacement work is launched, avoiding memory spikes.
            if(jobs_.contains(request.key))continue;
            const uint64_t next=serial_+1;
            if(next==0)throw std::overflow_error("Caverns render revision overflow");
            serial_=next;
            const Settings frozen=settings;
            jobs_.emplace(request.key,Job{request,
                std::async(std::launch::async,[frozen,request,next](){
                    CaveRebuildResult result;
                    result.request=request;
                    result.chunk.mesh=generateCaveMesh(
                        frozen,request.key,request.resolution,request.spacingCm);
                    result.chunk.resolution=request.resolution;
                    result.chunk.spacingCm=request.spacingCm;
                    result.chunk.sourceRevision=request.sourceRevision;
                    result.chunk.renderRevision=next;
                    return result;
                })});
            ++stats.launched;
        }
        stats.inFlight=jobs_.size();
        return stats;
    }

    // Poll non-blockingly on the owner thread, then invoke gate.analyze()
    // after ANY accepted result; never publish one side before that audit.
    CaveRebuildPumpStats poll(CaveSeamPublicationGate& gate) {
        CaveRebuildPumpStats stats;
        collect(gate,stats);
        if(stats.accepted)gate.analyze();
        stats.inFlight=jobs_.size();
        return stats;
    }

    size_t inFlight()const{return jobs_.size();}
    size_t maxWorkers()const{return limit_;}

    // For scene shutdown: wait until worker captures have completed before
    // destroying the gate or shutting down the process. No graphics calls.
    void drain() {
        for(auto& [key,job]:jobs_) {
            (void)key;
            job.future.wait();
        }
        jobs_.clear();
    }

private:
    struct Job {
        CaveRebuildRequest request{};
        std::future<CaveRebuildResult> future;
    };

    void collect(CaveSeamPublicationGate& gate,CaveRebuildPumpStats& stats) {
        for(auto it=jobs_.begin();it!=jobs_.end();) {
            auto& job=it->second;
            if(job.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) {
                ++it;continue;
            }
            ++stats.completed;
            try {
                auto result=job.future.get();
                if(gate.accept(result.request,std::move(result.chunk)))
                    ++stats.accepted;
                else ++stats.stale;
            }catch(const std::exception&) {
                // Caller can observe failures; gate remains unready. Do not
                // fabricate a replacement or silently publish old geometry.
                ++stats.failed;
            }
            it=jobs_.erase(it);
        }
    }

    const size_t limit_;
    uint64_t serial_=0;
    std::map<ChunkKey3,Job,KeyLess> jobs_;
};

} // namespace aiverse::terraweave::caverns
