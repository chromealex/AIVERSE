#pragma once
// TerraWeave Caverns stage 8: host-side publication transaction coordinator.
// Prevents partial new revisions from being exposed as ready. It does NOT
// perform cross-subsystem rollback: every owner must stage without replacing
// visible state, then expose only at a shared owner barrier.
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {
enum class CavePublishPart:uint8_t {
    Renderer=1, SpatialWorld=2, Box3D=4, Navigation=8, Strata=16
};
constexpr uint32_t cavePart(CavePublishPart part) {return uint32_t(part);}
enum class CavePublishStatus:uint8_t {
    Staging, Ready, Committed, Cancelled
};
struct CavePublishTicket {
    ChunkKey3 key{};
    uint64_t generation=0;
    uint64_t sourceRevision=0;
    uint64_t renderRevision=0;
    uint32_t requiredMask=0;
};
struct CavePublishSnapshot {
    CavePublishTicket ticket{};
    uint32_t preparedMask=0;
    CavePublishStatus status=CavePublishStatus::Staging;
};
class CavePublicationCoordinator {
    using Key=ChunkKey3;
    struct Entry {
        CavePublishSnapshot snapshot{};
    };
    std::map<Key,Entry,KeyLess> active_;
    std::map<Key,uint64_t,KeyLess> generations_;
    uint64_t serial_=0;
    uint32_t cap_;
public:
    explicit CavePublicationCoordinator(uint32_t maxPending=256):cap_(maxPending) {
        if(maxPending==0||maxPending>65536)
            throw std::invalid_argument("Caverns publication budget");
    }
    // Begin replaces any previous in-flight ticket for the key and prevents
    // acknowledgements from earlier callbacks from affecting the new revision.
    CavePublishTicket begin(Key key,uint64_t sourceRevision,
                            uint64_t renderRevision,uint32_t requiredMask) {
        constexpr uint32_t validMask=31;
        constexpr uint32_t requiredCore=1|2|4|8;
        if(sourceRevision==0||renderRevision==0||
           (requiredMask&~validMask)!=0||
           (requiredMask&requiredCore)!=requiredCore)
            throw std::invalid_argument("Caverns publication requirements");
        if(!active_.contains(key)&&active_.size()>=cap_)
            throw std::length_error("Caverns pending publication budget");
        if(serial_==UINT64_MAX)
            throw std::overflow_error("Caverns publication generation exhausted");
        CavePublishTicket ticket{key,++serial_,sourceRevision,renderRevision,requiredMask};
        active_.insert_or_assign(key,Entry{{ticket,0,CavePublishStatus::Staging}});
        generations_.insert_or_assign(key,serial_);
        return ticket;
    }
    bool prepared(CavePublishTicket ticket,CavePublishPart part) {
        auto* entry=matching(ticket);
        if(!entry||entry->snapshot.status!=CavePublishStatus::Staging)return false;
        const uint32_t flag=cavePart(part);
        if(!(ticket.requiredMask&flag))return false;
        entry->snapshot.preparedMask|=flag;
        if(entry->snapshot.preparedMask==ticket.requiredMask)
            entry->snapshot.status=CavePublishStatus::Ready;
        return true;
    }
    // At this point all owners have successfully staged candidate resources.
    // The host must perform the *actual* shared-barrier swap and report its
    // success to commit(). Do not call before all owners have changed over.
    bool commit(CavePublishTicket ticket,bool ownerBarrierSucceeded) {
        auto* entry=matching(ticket);
        if(!entry||entry->snapshot.status!=CavePublishStatus::Ready)return false;
        if(!ownerBarrierSucceeded){
            entry->snapshot.status=CavePublishStatus::Cancelled;
            return false;
        }
        entry->snapshot.status=CavePublishStatus::Committed;
        return true;
    }
    bool abort(CavePublishTicket ticket) {
        auto* entry=matching(ticket);
        if(!entry||entry->snapshot.status==CavePublishStatus::Committed)return false;
        entry->snapshot.status=CavePublishStatus::Cancelled;
        return true;
    }
    bool retire(Key key) {return active_.erase(key)!=0;}
    std::optional<CavePublishSnapshot> snapshot(Key key)const {
        const auto it=active_.find(key);
        if(it==active_.end())return {};
        return it->second.snapshot;
    }
    bool readyForGameplay(Key key,uint64_t sourceRevision)const{
        auto value=snapshot(key);
        return value&&value->status==CavePublishStatus::Committed &&
            value->ticket.sourceRevision==sourceRevision;
    }
    size_t size()const{return active_.size();}
private:
    Entry* matching(CavePublishTicket ticket) {
        auto it=active_.find(ticket.key);
        if(it==active_.end())return nullptr;
        const auto& t=it->second.snapshot.ticket;
        if(t.generation!=ticket.generation||
           t.sourceRevision!=ticket.sourceRevision||
           t.renderRevision!=ticket.renderRevision||
           t.requiredMask!=ticket.requiredMask)return nullptr;
        return &it->second;
    }
};
} // namespace aiverse::terraweave::caverns
