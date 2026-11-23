#pragma once
// TerraWeave Caverns Stage36: bounded CPU streaming work scheduler.
// The immutable world snapshot is captured by each job. The resulting mesh
// is NOT automatically made resident: the owner must stage GeometryScene,
// Box3D, SpatialWorld and NavMesh, then ACK the exact residency ticket.
#include "engine/world/TerraWeaveCavernsWorldResidency.h"
#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveStreamCpuResult {
    CaveResidencyTicket ticket{};
    CaveMesh mesh{};
    bool success=true;
};
struct CaveStreamJobStats {
    size_t dispatched=0,completed=0,failed=0,inFlight=0;
};
class CaveWorldStreamJobs {
public:
    explicit CaveWorldStreamJobs(size_t workers=2):limit_(workers){
        if(workers<1||workers>32)
            throw std::invalid_argument("Caverns stream worker count");
    }
    CaveWorldStreamJobs(const CaveWorldStreamJobs&)=delete;
    CaveWorldStreamJobs& operator=(const CaveWorldStreamJobs&)=delete;
    // An old job on the same key may still run after its ticket is revoked.
    // Wait for its ready future to be collected before launching the new one.
    bool dispatch(CaveResidencyTicket request,
                  std::shared_ptr<const CaveVolumeSnapshot> snapshot,
                  uint32_t resolution,int32_t spacingCm){
        if(request.action!=CaveResidencyAction::Build || !snapshot ||
           !request.sourceRevision || !request.generation ||
           snapshot->sourceRevision!=request.sourceRevision ||
           !resolution||resolution>64||spacingCm<=0||spacingCm>10000)
            throw std::invalid_argument("Caverns stream worker request");
        if(jobs_.size()>=limit_ || jobs_.contains(request.key))return false;
        auto work=std::async(std::launch::async,
            [snapshot,request,resolution,spacingCm](){
                CaveStreamCpuResult out;
                out.ticket=request;
                out.mesh=generateCaveSnapshotMesh(*snapshot,request.key,
                                                    resolution,spacingCm);
                return out;
            });
        jobs_.emplace(request.key,Job{request,std::move(work)});
        ++dispatched_;
        return true;
    }
    // Only the scene owner pumps this. The job future never touches GPU/Box3D.
    // Failed job requests remain unacknowledged: the residency host must call
    // fail(ticket) so a newer generation can be scheduled later.
    std::vector<CaveStreamCpuResult> poll(CaveStreamJobStats* report=nullptr){
        std::vector<CaveStreamCpuResult> ready;
        CaveStreamJobStats stats;
        for(auto it=jobs_.begin();it!=jobs_.end();){
            if(it->second.future.wait_for(std::chrono::seconds(0))!=
                std::future_status::ready){++it;continue;}
            ++stats.completed;
            try {ready.push_back(it->second.future.get());}
            catch(const std::exception&){
                ++stats.failed;
                CaveStreamCpuResult failed;
                failed.ticket=it->second.ticket;
                failed.success=false;
                ready.push_back(std::move(failed));
            }
            it=jobs_.erase(it);
        }
        stats.dispatched=std::exchange(dispatched_,0);
        stats.inFlight=jobs_.size();
        if(report)*report=stats;
        return ready;
    }
    bool hasJob(ChunkKey3 key)const{return jobs_.contains(key);}
    size_t inFlight()const{return jobs_.size();}
    // Call only at world teardown, not from inside a render pass.
    void drain(){
        for(auto& [key,job]:jobs_){(void)key;job.future.wait();}
        jobs_.clear();
    }
private:
    struct Job {
        CaveResidencyTicket ticket{};
        std::future<CaveStreamCpuResult> future;
    };
    size_t limit_=2,dispatched_=0;
    std::map<ChunkKey3,Job,KeyLess> jobs_;
};
} // namespace aiverse::terraweave::caverns
