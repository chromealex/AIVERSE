// Conservative presentation GPU allocation envelopes; render-owner thread only.
// Not a driver VRAM measurement. Device identity/fences must never be mixed.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>

namespace aiverse::gfx {
struct GpuAllocationBudgetStats {
    size_t limit=0,active=0,retiring=0,peak=0;
    uint64_t denied=0;
    size_t charged() const {return active+retiring;}
};
class GpuAllocationBudget final {
public:
    explicit GpuAllocationBudget(size_t limit=0){stats_.limit=limit;}
    const void* deviceKey() const {return device_;}
    // The Device invalidates this ledger on teardown; an external owner
    // cannot accidentally attach an old ledger to a reused pointer address.
    void closeDevice(const void* device){if(device_==device)closed_=true;}
    bool bind(const void* device){
        if(closed_||!device || (device_&&device_!=device))return false;
        device_=device;return true;
    }
    void setLimit(size_t limit){stats_.limit=limit;}
    const GpuAllocationBudgetStats& stats() const{return stats_;}
    // Unknown fence is permanently retained until this device budget dies.
    static constexpr uint64_t unknownFence=std::numeric_limits<uint64_t>::max();
    void collect(uint64_t completed){
        if(!completed)return;
        auto it=retired_.begin();
        while(it!=retired_.end()&&it->first!=unknownFence&&it->first<=completed){
            stats_.retiring-=it->second;it=retired_.erase(it);
        }
    }
    bool reserve(size_t bytes){
        const size_t total=stats_.charged();
        if(!bytes||bytes>std::numeric_limits<size_t>::max()-total||
           (stats_.limit&&(total>stats_.limit||bytes>stats_.limit-total))){
            ++stats_.denied;return false;
        }
        stats_.active+=bytes;stats_.peak=std::max(stats_.peak,stats_.charged());return true;
    }
    // Caller owns an exact reservation; fence=0 means never GPU referenced.
    void retire(size_t bytes,uint64_t fence){
        if(!bytes||bytes>stats_.active)return;
        stats_.active-=bytes;
        if(fence){stats_.retiring+=bytes;retired_[fence]+=bytes;}
    }
private:
    const void* device_=nullptr;
    bool closed_=false;
    GpuAllocationBudgetStats stats_{};
    std::map<uint64_t,size_t> retired_;
};
} // namespace aiverse::gfx
