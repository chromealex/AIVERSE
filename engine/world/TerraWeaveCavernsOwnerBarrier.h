#pragma once
// Caverns Stage25: owner-thread visibility barrier for a whole edited batch.
// CPU-only protocol, not a replacement for graphics/physics staging APIs.
// Every owner must stage independently BEFORE any live state is changed.
// Host receives an immutable revision-bound ticket and must provide genuine
// all-or-none external commit; no callback is assumed to be transactional.
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
namespace aiverse::terraweave::caverns {
enum class CaveOwnerPart : uint32_t {
    Geometry=1,Spatial=2,Box3D=4,Navigation=8
};
constexpr uint32_t caveOwnerBit(CaveOwnerPart part) noexcept {
    return static_cast<uint32_t>(part);
}
constexpr uint32_t kCaveRequiredOwnerMask=15;
struct CaveOwnerBatchTicket {
    uint64_t serial=0;
    uint64_t sourceRevision=0;
    uint64_t renderRevision=0;
    size_t chunkCount=0;
    constexpr bool operator==(const CaveOwnerBatchTicket&)const=default;
};
enum class CaveOwnerBarrierState:uint8_t {Idle,Preparing,Ready,Committed,Aborted};
class CaveOwnerBarrier {
public:
    CaveOwnerBatchTicket begin(uint64_t sourceRevision,uint64_t renderRevision,
                                size_t chunks) {
        if(!sourceRevision||!renderRevision||!chunks||chunks>1024)
            throw std::invalid_argument("Caverns owner batch ticket");
        if(serial_==UINT64_MAX)
            throw std::overflow_error("Caverns owner batch serial");
        active_={++serial_,sourceRevision,renderRevision,chunks};
        prepared_=0;
        state_=CaveOwnerBarrierState::Preparing;
        return active_;
    }
    bool prepared(CaveOwnerBatchTicket ticket,CaveOwnerPart owner) {
        const auto part=caveOwnerBit(owner);
        if(ticket!=active_||state_!=CaveOwnerBarrierState::Preparing||
           !(part&kCaveRequiredOwnerMask) ||
           (part&(part-1))!=0)return false;
        prepared_|=part;
        if(prepared_==kCaveRequiredOwnerMask)
            state_=CaveOwnerBarrierState::Ready;
        return true;
    }
    bool ready(CaveOwnerBatchTicket ticket)const noexcept {
        return ticket==active_&&state_==CaveOwnerBarrierState::Ready;
    }
    // Pure bookkeeping: the host must first successfully switch ALL native
    // render/collider/nav owners at a controlled barrier. No partial swap is
    // undone by passing false here. Do not call on tentative success.
    bool confirm(CaveOwnerBatchTicket ticket,bool externalAtomicCommit) {
        if(!ready(ticket))return false;
        state_=externalAtomicCommit?CaveOwnerBarrierState::Committed:
                                    CaveOwnerBarrierState::Aborted;
        return externalAtomicCommit;
    }
    bool abort(CaveOwnerBatchTicket ticket) {
        if(ticket!=active_||state_==CaveOwnerBarrierState::Committed||
           state_==CaveOwnerBarrierState::Idle)return false;
        state_=CaveOwnerBarrierState::Aborted;
        return true;
    }
    CaveOwnerBarrierState state()const noexcept{return state_;}
    uint32_t preparedMask()const noexcept{return prepared_;}
    CaveOwnerBatchTicket ticket()const noexcept{return active_;}
private:
    uint64_t serial_=0;
    CaveOwnerBatchTicket active_{};
    CaveOwnerBarrierState state_=CaveOwnerBarrierState::Idle;
    uint32_t prepared_=0;
};
} // namespace aiverse::terraweave::caverns
