#pragma once
// Caverns Stage41: authoring-epoch navigation invalidation bus.
// Source truth is ONE published immutable SDF; each external solver owns its
// own derived A*/HPA*/flow/navmesh/custom cache, no parallel cave world state.
// Call publish ONLY after native all-owner gameplay commit has succeeded.
#include "engine/world/TerraWeaveCavernsNavigationGraph.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
enum class CaveNavigationConsumer:uint8_t {
    AStar,HierarchicalAStar,FlowField,NavMesh,Hybrid,Custom
};
struct CaveNavigationSubscription {
    CaveNavigationConsumer algorithm=CaveNavigationConsumer::Custom;
    uint64_t token=0;
};
class CaveNavigationPublication {
public:
    using Callback=std::function<void(
        const CaveNavigationChange&,
        std::shared_ptr<const CaveNavigationDataProvider>)>;
    explicit CaveNavigationPublication(
        std::shared_ptr<const CaveSnapshotSpatialIndex> initial,
        CaveNavigationCallbacks callbacks={})
        :callbacks_(std::move(callbacks)) {
        if(initial)
            active_=std::make_shared<const CaveNavigationDataProvider>(
                std::move(initial),callbacks_);
    }
    CaveNavigationSubscription subscribe(
        CaveNavigationConsumer algorithm,Callback callback){
        if(!callback)throw std::invalid_argument("Caverns navigation callback");
        if(nextToken_==UINT64_MAX)
            throw std::overflow_error("Caverns navigation subscription token");
        const uint64_t id=++nextToken_;
        observers_.emplace(id,Observer{algorithm,std::move(callback)});
        return {algorithm,id};
    }
    bool unsubscribe(CaveNavigationSubscription ticket){
        const auto it=observers_.find(ticket.token);
        if(it==observers_.end()||it->second.algorithm!=ticket.algorithm)
            return false;
        observers_.erase(it);
        return true;
    }
    // Commit the *data epoch*, and deliver invalidation notifications
    // independently of algorithm choice. Consumers decide whether to mark
    // grid cells, HPA clusters, flow seeds or NavMesh tiles dirty, or rebuild.
    // If a callback throws, the event is retained for retry and callers
    // must not query a consumer's stale derived navigation cache.
    bool publish(std::shared_ptr<const CaveSnapshotSpatialIndex> committed,
                 CaveDirtyBounds bounds,bool fullRebuild=false) {
        if(!committed||!committed->revision())
            throw std::invalid_argument("Caverns missing nav commit source");
        if(active_&&committed->revision()<=active_->revision())return false;
        if(!fullRebuild &&
           (bounds.maxX<=bounds.minX||bounds.maxY<=bounds.minY||
            bounds.maxZ<=bounds.minZ))
            throw std::invalid_argument("Caverns nav dirty bounds");
        const uint64_t old=active_?active_->revision():0;
        auto next=std::make_shared<const CaveNavigationDataProvider>(
            std::move(committed),callbacks_);
        pending_.push_back({
            CaveNavigationChange{old,next->revision(),bounds,fullRebuild},
            std::move(next),0
        });
        active_=pending_.back().provider;
        return dispatchPending();
    }
    // A streamed chunk may appear/disappear WITHOUT changing authored SDF
    // sourceRevision. Emit a separate serial for the derived graph/cache
    // topology, but preserve the same immutable density provider.
    bool invalidateResidency(CaveDirtyBounds bounds) {
        if(!active_)return false;
        if(bounds.maxX<=bounds.minX||bounds.maxY<=bounds.minY||
           bounds.maxZ<=bounds.minZ)
            throw std::invalid_argument("Caverns nav stream invalidation bounds");
        pending_.push_back({
            CaveNavigationChange{active_->revision(),active_->revision(),
                                 bounds,false},active_,0
        });
        return dispatchPending();
    }
    // A solver that missed a callback due to an exception must not silently
    // use a stale derived cache. Call at a safe owner frame boundary.
    bool dispatchPending(){
        while(!pending_.empty()){
            auto& event=pending_.front();
            for(auto it=observers_.upper_bound(event.lastDelivered);
                it!=observers_.end();++it) {
                try {
                    it->second.callback(event.change,event.provider);
                }catch(...) {
                    return false;
                }
                event.lastDelivered=it->first;
            }
            pending_.erase(pending_.begin());
        }
        return true;
    }
    bool hasPending()const noexcept{return !pending_.empty();}
    uint64_t activeRevision()const noexcept{
        return active_?active_->revision():0;
    }
    std::shared_ptr<const CaveNavigationDataProvider> active()const{
        return active_;
    }
    // Registration is independent of pre-existing consumers. A new solver
    // must initialize its own cache from active(), not wait for an edit.
    size_t consumerCount()const noexcept{return observers_.size();}
private:
    struct Observer{CaveNavigationConsumer algorithm;Callback callback;};
    struct Event {
        CaveNavigationChange change{};
        std::shared_ptr<const CaveNavigationDataProvider> provider;
        uint64_t lastDelivered=0;
    };
    CaveNavigationCallbacks callbacks_;
    std::shared_ptr<const CaveNavigationDataProvider> active_;
    std::map<uint64_t,Observer> observers_;
    std::vector<Event> pending_;
    uint64_t nextToken_=0;
};
} // namespace aiverse::terraweave::caverns
