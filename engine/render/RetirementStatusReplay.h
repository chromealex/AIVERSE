// Deterministic GPU-fence status replay for native test and diagnostics.
// This is NOT a gfx::Device backend and never produces a real GPU proof;
// real resource destruction must still use backend completion/fences.
#pragma once
#include "engine/gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace aiverse::render {

class RetirementStatusReplay final {
public:
    using Status=gfx::ResourceRetirementStatus;
    struct Entry {
        uint64_t step=0,token=0;
        Status status=Status::Pending;
    };
    // Preconfigure the status visible for each timestep of a new token.
    // The status sequence holds at its final element after it finishes.
    void script(uint64_t token,std::vector<Status> timeline) {
        if(token==0)return;
        if(timeline.empty())timeline.push_back(Status::Pending);
        timelines_[token]={std::move(timeline),0,false};
    }
    uint64_t issue() {
        const uint64_t token=nextToken_++;
        auto& timeline=timelines_[token];
        if(timeline.statuses.empty())timeline.statuses={Status::Pending};
        timeline.issued=true;
        record(token,status(token));
        return token;
    }
    void advance() {
        ++step_;
        for(auto& [token,timeline]:timelines_) {
            if(!timeline.issued)continue;
            const Status previous=status(token);
            if(timeline.position+1<timeline.statuses.size())
                ++timeline.position;
            const Status now=status(token);
            if(previous!=now)record(token,now);
        }
    }
    Status status(uint64_t token) const {
        const auto it=timelines_.find(token);
        if(it==timelines_.end() || !it->second.issued)
            return Status::Failed; // unknown token is NOT completion
        return it->second.statuses[it->second.position];
    }
    bool complete(uint64_t token) const {return status(token)==Status::Complete;}
    bool failed(uint64_t token) const {return status(token)==Status::Failed;}
    uint64_t step() const noexcept{return step_;}
    const std::vector<Entry>& events() const noexcept{return events_;}
    void clear() {
        timelines_.clear();events_.clear();nextToken_=1;step_=0;
    }
private:
    struct Timeline {
        std::vector<Status> statuses;
        size_t position=0;
        bool issued=false;
    };
    void record(uint64_t token,Status state) {
        events_.push_back({step_,token,state});
    }
    std::map<uint64_t,Timeline> timelines_;
    std::vector<Entry> events_;
    uint64_t nextToken_=1,step_=0;
};

} // namespace aiverse::render
