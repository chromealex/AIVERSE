#pragma once
// Caverns stage 9: per-chunk presentation lease and safe fallback routing.
// This is a pure owner-thread decision state: it does not call Renderer3D,
// STRATA GeometryScene, Box3D or RHI. The host must honor the returned route.
#include "engine/world/TerraWeaveCavernsPublication.h"
#include <map>
#include <optional>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
enum class CavePresentationRoute:uint8_t {
    Hidden=0, Renderer3D=1, Strata=2
};
struct CavePresentationTicket {
    ChunkKey3 key{};
    uint64_t generation=0;
    uint64_t sourceRevision=0;
    uint64_t renderRevision=0;
    constexpr bool operator==(const CavePresentationTicket&) const=default;
};
struct CavePresentationSnapshot {
    CavePresentationTicket ticket{};
    CavePresentationRoute route=CavePresentationRoute::Hidden;
    bool fallbackResident=false;
    bool strataPrepared=false;
    bool strataExclusive=false;
    bool retirementRequested=false;
};
class CavePresentationLeases {
public:
    explicit CavePresentationLeases(uint32_t capacity=1024):capacity_(capacity) {
        if(!capacity||capacity>65536)
            throw std::invalid_argument("Caverns presentation capacity");
    }
    // New source invalidates STRATA's previous ownership. The previous
    // committed Renderer3D resource remains visible until the host swaps it.
    CavePresentationTicket begin(ChunkKey3 key,uint64_t sourceRevision,
                                  uint64_t renderRevision){
        if(!sourceRevision||!renderRevision||next_==UINT64_MAX)
            throw std::invalid_argument("Caverns presentation revision");
        if(!items_.contains(key)&&items_.size()>=capacity_)
            throw std::length_error("Caverns presentation capacity reached");
        auto& item=items_[key];
        item.ticket={key,++next_,sourceRevision,renderRevision};
        item.strataPrepared=false;
        item.strataExclusive=false;
        item.retirementRequested=false;
        // Any previously acquired STRATA route is now stale. Require host
        // to explicitly enable old fallback BEFORE replacing geometry.
        item.route=item.fallbackResident?CavePresentationRoute::Renderer3D:
                                        CavePresentationRoute::Hidden;
        return item.ticket;
    }
    // Called only after Renderer3D's uploaded model is actually committed.
    bool confirmFallback(CavePresentationTicket ticket){
        auto* state=find(ticket);
        if(!state||state->retirementRequested)return false;
        state->fallbackResident=true;
        if(!state->strataExclusive)state->route=CavePresentationRoute::Renderer3D;
        return true;
    }
    // STRATA has staged the immutable mesh and validated source, materials,
    // geometry version and GPU lifetime. This does NOT switch render yet.
    bool confirmStrataPrepared(CavePresentationTicket ticket){
        auto* state=find(ticket);
        if(!state||state->retirementRequested)return false;
        state->strataPrepared=true;
        return true;
    }
    // At one Renderer3D/GeometryScene barrier, host proves exactly one draw
    // owner for every relevant pass. Without proof retain Renderer3D.
    bool activateStrata(CavePresentationTicket ticket,bool exclusiveDrawProof){
        auto* state=find(ticket);
        if(!state||!state->strataPrepared||!state->fallbackResident||
           !exclusiveDrawProof||state->retirementRequested)return false;
        state->strataExclusive=true;
        state->route=CavePresentationRoute::Strata;
        return true;
    }
    // Before invalidating STRATA residency, restore the live fallback first.
    // Does not itself submit a draw or free a STRATA GPU resource.
    bool deactivateStrata(CavePresentationTicket ticket){
        auto* state=find(ticket);
        if(!state||!state->fallbackResident)return false;
        state->strataExclusive=false;
        state->strataPrepared=false;
        state->route=CavePresentationRoute::Renderer3D;
        return true;
    }
    bool requestRetirement(CavePresentationTicket ticket){
        auto* state=find(ticket);
        if(!state)return false;
        state->retirementRequested=true;
        // Keep render route until RHI fence + collider/navigation retirement
        // completes. Calling requestRetirement alone never removes geometry.
        return true;
    }
    // Host has observed GPU fence completion, STRATA lease expiry and
    // collision/navmesh removal; no outstanding readers can use the chunk.
    bool finishRetirement(CavePresentationTicket ticket,bool resourcesReleased){
        auto* state=find(ticket);
        if(!state||!state->retirementRequested||!resourcesReleased)return false;
        items_.erase(ticket.key);return true;
    }
    CavePresentationRoute route(ChunkKey3 key)const{
        const auto it=items_.find(key);
        return it==items_.end()?CavePresentationRoute::Hidden:it->second.route;
    }
    std::optional<CavePresentationSnapshot> snapshot(ChunkKey3 key)const {
        auto it=items_.find(key);
        if(it==items_.end())return {};
        return it->second;
    }
    size_t count()const{return items_.size();}
private:
    CavePresentationSnapshot* find(CavePresentationTicket ticket){
        auto it=items_.find(ticket.key);
        if(it==items_.end()||!(it->second.ticket==ticket))return nullptr;
        return &it->second;
    }
    uint32_t capacity_=0;
    uint64_t next_=0;
    std::map<ChunkKey3,CavePresentationSnapshot,KeyLess> items_;
};
} // namespace aiverse::terraweave::caverns
