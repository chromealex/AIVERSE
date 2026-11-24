#pragma once
// Caverns Stage32: frame-paced GPU preview upload admission.
// Pure C++20 policy, no RHI ownership, timers or hidden background work.
// Called once at Editor Scene pre-render; at most one shard per frame.
// Failed staging is retried with bounded frame cooldown, never tight-looped.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
class CavePreviewPacer {
public:
    explicit CavePreviewPacer(uint64_t retryFrames=30)
        :retryFrames_(retryFrames) {
        if(!retryFrames || retryFrames>600)
            throw std::invalid_argument("Caverns preview retry frames");
    }
    void reset(uint64_t authorEpoch,size_t shardCount){
        epoch_=authorEpoch;
        total_=shardCount;
        completed_=0;
        lastAttemptFrame_=UINT64_MAX;
        retryAfterFrame_=0;
        failureCount_=0;
    }
    bool mayStage(uint64_t authorEpoch,uint64_t frameSerial)const noexcept{
        return authorEpoch==epoch_ && completed_<total_ &&
            frameSerial>=retryAfterFrame_ &&
            frameSerial!=lastAttemptFrame_;
    }
    void staged(uint64_t frameSerial) {
        if(completed_>=total_ || lastAttemptFrame_==frameSerial)
            throw std::logic_error("Caverns preview upload budget exceeded");
        lastAttemptFrame_=frameSerial;
        ++completed_;
        failureCount_=0;
    }
    void failed(uint64_t frameSerial) noexcept {
        lastAttemptFrame_=frameSerial;
        ++failureCount_;
        retryAfterFrame_=
            frameSerial>UINT64_MAX-retryFrames_ ? UINT64_MAX:
            frameSerial+retryFrames_;
    }
    bool ready(uint64_t authorEpoch)const noexcept {
        return authorEpoch==epoch_ && total_>0 && completed_==total_;
    }
    size_t total()const noexcept{return total_;}
    size_t completed()const noexcept{return completed_;}
    size_t remaining()const noexcept{return total_-completed_;}
    uint64_t retryAfterFrame()const noexcept{return retryAfterFrame_;}
    uint64_t epoch()const noexcept{return epoch_;}
    size_t failures()const noexcept{return failureCount_;}
private:
    uint64_t retryFrames_,epoch_=0,lastAttemptFrame_=UINT64_MAX,retryAfterFrame_=0;
    size_t total_=0,completed_=0,failureCount_=0;
};
} // namespace aiverse::terraweave::caverns
