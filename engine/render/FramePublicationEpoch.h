// AIVERSE LENS: generic single-owner-thread coherent presentation-frame fence.
//
// This is a publication/consumption contract, NOT a GPU transaction.
// Owners may commit internal resources independently; when a later owner fails,
// the current frame is invalidated so no secondary view may replay a mixed
// frame. Full multi-owner GPU rollback needs owner-level prepare/commit APIs.
//
// A ticket remains active through all synchronous frame producers. Each required
// producer marks completion only after successful preparation. A ticket that
// goes out of scope without finish() cancels the candidate frame.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace aiverse::render {

class FramePublicationEpoch final {
public:
    enum Owner : uint32_t {
        Geometry=1u<<0,
        Terrain=1u<<1,
        Ground=1u<<2,
        Roads=1u<<3,
        Shores=1u<<4,
        Fog=1u<<5,
        Sprites=1u<<6,
        Animation=1u<<7,
        Lighting=1u<<8,
        Overlays=1u<<9,
    };
    struct Snapshot {
        uint64_t id=0;
        uint64_t sourceFrame=0;
        uint32_t required=0,completed=0;
        int width=0,height=0;
        double seconds=0;
        bool ready() const noexcept {
            return id!=0 && (completed&required)==required;
        }
    };
    class Ticket {
    public:
        Ticket()=default;
        Ticket(const Ticket&)=delete;
        Ticket& operator=(const Ticket&)=delete;
        Ticket(Ticket&& other) noexcept {
            owner_=std::exchange(other.owner_,nullptr);
            id_=std::exchange(other.id_,0);
        }
        Ticket& operator=(Ticket&& other) noexcept {
            if(this!=&other) {
                cancel();
                owner_=std::exchange(other.owner_,nullptr);
                id_=std::exchange(other.id_,0);
            }
            return *this;
        }
        ~Ticket() noexcept {cancel();}
        void mark(uint32_t owners) {
            if(!owner_ || !owner_->active_ || owner_->attempt_.id!=id_)
                throw std::logic_error("FramePublicationEpoch: stale ticket");
            if((owners & ~owner_->attempt_.required)!=0)
                throw std::logic_error("FramePublicationEpoch: owner not required");
            owner_->attempt_.completed|=owners;
        }
        bool ready() const noexcept {
            return owner_ && owner_->active_ && owner_->attempt_.id==id_ &&
                (owner_->attempt_.completed & owner_->attempt_.required)==owner_->attempt_.required;
        }
        bool finish() noexcept {
            if(!owner_ || !owner_->active_ || owner_->attempt_.id!=id_)
                return false;
            if((owner_->attempt_.completed & owner_->attempt_.required) !=
                owner_->attempt_.required) {
                cancel();
                return false;
            }
            owner_->published_=owner_->attempt_;
            owner_->active_=false;
            owner_=nullptr;
            id_=0;
            return true;
        }
        void cancel() noexcept {
            if(!owner_)return;
            if(owner_->active_ && owner_->attempt_.id==id_)
                owner_->invalidate();
            owner_=nullptr;
            id_=0;
        }
        uint64_t id() const noexcept{return id_;}
    private:
        friend class FramePublicationEpoch;
        Ticket(FramePublicationEpoch& owner,uint64_t id) noexcept
            :owner_(&owner),id_(id){}
        FramePublicationEpoch* owner_=nullptr;
        uint64_t id_=0;
    };
    FramePublicationEpoch()=default;
    FramePublicationEpoch(const FramePublicationEpoch&)=delete;
    FramePublicationEpoch& operator=(const FramePublicationEpoch&)=delete;

    Ticket begin(uint32_t required,uint64_t sourceFrame,
                 int width,int height,double seconds) {
        if(active_)throw std::logic_error("FramePublicationEpoch: nested frame");
        invalidate();
        active_=true;
        attempt_={++nextId_,sourceFrame,required,0,width,height,seconds};
        return Ticket(*this,attempt_.id);
    }
    void invalidate() noexcept {
        active_=false;
        attempt_={};
        published_={};
    }
    Snapshot published() const noexcept{return published_;}
    bool active() const noexcept{return active_;}
    bool readyFor(int width,int height,double seconds) const noexcept {
        return !active_ && published_.ready() &&
               published_.width==width && published_.height==height &&
               published_.seconds==seconds;
    }
private:
    uint64_t nextId_=0;
    bool active_=false;
    Snapshot attempt_{},published_{};
};

} // namespace aiverse::render
