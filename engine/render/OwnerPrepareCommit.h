// Genre-independent render-owner transaction coordinator.
// Participants stage private CPU/GPU resources during prepare(); prepare may
// throw. commit() and rollback() MUST be noexcept. This ensures that a late
// prepare failure never partially commits earlier participants.
// The host supplies one participant per resource owner; the coordinator is
// deliberately unaware of materials, cameras, RTS, or RHI device types.
#pragma once
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class OwnerPrepareCommit final {
public:
    class Participant {
    public:
        virtual ~Participant()=default;
        // Never mutate currently published resources; allocate private staging.
        virtual void prepare()=0;
        // Must only swap already prepared handles/state, without allocation.
        virtual void commit() noexcept=0;
        // Dispose ONLY unpublished staging handles; published GPU handles
        // require their owner's normal completion-token retirement process.
        virtual void rollback() noexcept=0;
    };
    OwnerPrepareCommit()=default;
    OwnerPrepareCommit(const OwnerPrepareCommit&)=delete;
    OwnerPrepareCommit& operator=(const OwnerPrepareCommit&)=delete;
    ~OwnerPrepareCommit() noexcept {rollback();}

    void add(Participant& participant) {
        if(state_!=State::Open)
            throw std::logic_error("OwnerPrepareCommit: cannot add after preparation");
        for(const auto* entry:participants_)
            if(entry==&participant)
                throw std::logic_error("OwnerPrepareCommit: duplicate owner");
        participants_.push_back(&participant);
    }
    void prepareAll() {
        if(state_!=State::Open)
            throw std::logic_error("OwnerPrepareCommit: already prepared");
        state_=State::Preparing;
        for(size_t i=0;i<participants_.size();++i) {
            attempted_=i+1;
            try {
                participants_[i]->prepare();
            } catch(...) {
                rollback();
                throw;
            }
        }
        state_=State::Prepared;
    }
    bool publish() noexcept {
        if(state_!=State::Prepared)return false;
        for(Participant* participant:participants_)
            participant->commit();
        attempted_=0;
        state_=State::Published;
        return true;
    }
    void rollback() noexcept {
        if(state_==State::Published || state_==State::Aborted)return;
        for(size_t i=attempted_;i>0;--i)
            participants_[i-1]->rollback();
        attempted_=0;
        state_=State::Aborted;
    }
    bool prepared() const noexcept{return state_==State::Prepared;}
    bool published() const noexcept{return state_==State::Published;}
    size_t participantCount() const noexcept{return participants_.size();}
private:
    enum class State {Open,Preparing,Prepared,Published,Aborted};
    std::vector<Participant*> participants_;
    State state_=State::Open;
    size_t attempted_=0;
};

} // namespace aiverse::render
