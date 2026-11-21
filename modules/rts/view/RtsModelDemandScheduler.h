// Host-owned model residency and demand policy for editor/game tooling.
// No renderer, RtsView, game rules, or GPU dependency. All methods are
// intended to be used on the presentation host thread.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::game::view_adapter {

enum class ModelDemandPriority : uint8_t {
    Background=0, Visible=1, Interactive=2
};

class RtsModelDemandScheduler final {
public:
    void resize(size_t count) {
        lastUsed_.assign(count,-1.0e30);
        pending_.clear();
        ++revision_;
    }
    size_t size() const noexcept {return lastUsed_.size();}
    double lastUsed(size_t type) const noexcept {
        return type<lastUsed_.size()?lastUsed_[type]:-1.0e30;
    }
    uint64_t revision() const noexcept {return revision_;}
    void invalidate() {
        pending_.clear();
        ++revision_;
    }
    void touch(size_t type,double now,
               ModelDemandPriority priority=ModelDemandPriority::Visible) {
        if(type>=lastUsed_.size())return;
        lastUsed_[type]=now;
        auto it=std::find_if(pending_.begin(),pending_.end(),
            [type](const Request& r){return r.type==type;});
        if(it!=pending_.end()) {
            if(static_cast<uint8_t>(priority)>static_cast<uint8_t>(it->priority))
                it->priority=priority;
        } else pending_.push_back({type,priority,sequence_++});
    }
    // Highest-priority first; same priority retains insertion order.
    // A host may consume a small budget at each render-thread tick.
    template<class Load>
    size_t drain(size_t maxCount,Load&& load) {
        size_t count=0;
        while(count<maxCount && !pending_.empty()) {
            const auto it=std::max_element(pending_.begin(),pending_.end(),
                [](const Request& a,const Request& b){
                    if(a.priority!=b.priority)return a.priority<b.priority;
                    return a.sequence>b.sequence;
                });
            const auto type=it->type;
            pending_.erase(it);
            load(type);
            ++count;
        }
        return count;
    }
    // Per-frame best-effort time budget. One synchronous load may exceed
    // the budget; never start another after time has elapsed.
    template<class Load>
    size_t drainBudget(double budgetMs,size_t maxCount,Load&& load) {
        if(!(budgetMs>0) || maxCount==0)return 0;
        const auto start=std::chrono::steady_clock::now();
        size_t processed=0;
        while(processed<maxCount && !pending_.empty()) {
            if(processed>0) {
                const auto elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                if(elapsed>=budgetMs)break;
            }
            processed+=drain(1,load);
        }
        return processed;
    }
    bool recentlyUsed(size_t type,double now,double grace) const noexcept {
        return type<lastUsed_.size() && now-lastUsed_[type]<grace;
    }
    void markSweep(double now) noexcept {lastSweep_=now;}
    bool shouldSweep(double now,double interval,bool force=false) const noexcept {
        return force || now-lastSweep_>=interval;
    }
    size_t pendingCount() const noexcept {return pending_.size();}
private:
    struct Request {
        size_t type;
        ModelDemandPriority priority;
        uint64_t sequence;
    };
    std::vector<double> lastUsed_;
    std::vector<Request> pending_;
    double lastSweep_=0;
    uint64_t sequence_=0,revision_=0;
};

} // namespace aiverse::game::view_adapter
