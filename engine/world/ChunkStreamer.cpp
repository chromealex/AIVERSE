#include "ChunkStreamer.h"
#include <algorithm>
#include <limits>
#include <cstdlib>

namespace aiverse::world {
bool ChunkStreamer::add(Cell cell,size_t bytes) {
    if(!bytes || catalog_.size()>=100000)return false;
    return catalog_.emplace(cell,Entry{bytes}).second;
}
void ChunkStreamer::failed(Cell cell) {
    resident_.erase(cell);
    auto it=catalog_.find(cell);if(it==catalog_.end())return;
    it->second.failed=true;it->second.retryAt=update_+std::max(1u,limits_.retryUpdates);
}
StreamStatus ChunkStreamer::update(std::span<const StreamObserver> observers,std::span<const Cell> pinned,
                                  size_t loadBudget,const Streamer::Load& load,const Streamer::Unload& unload) {
    ++update_;status_={};
    // Distance zero is reserved for explicit pins, then Chebyshev rings around observers.
    std::map<Cell,int> wanted;
    for(const auto& observer:observers) {
        const int radius=std::clamp(observer.radius,0,4);
        for(int x=-radius;x<=radius;++x)for(int y=-radius;y<=radius;++y)for(int z=-radius;z<=radius;++z) {
            const int64_t cx=int64_t(observer.cell.x)+x,cy=int64_t(observer.cell.y)+y,cz=int64_t(observer.cell.z)+z;
            constexpr auto lo=std::numeric_limits<int32_t>::min(),hi=std::numeric_limits<int32_t>::max();
            if(cx<lo||cx>hi||cy<lo||cy>hi||cz<lo||cz>hi)continue;
            Cell cell{int32_t(cx),int32_t(cy),int32_t(cz)};
            if(!catalog_.contains(cell))continue;
            const int distance=1+std::max({std::abs(x),std::abs(y),std::abs(z)});
            auto [it,inserted]=wanted.emplace(cell,distance);
            if(!inserted)it->second=std::min(it->second,distance);
        }
    }
    std::set<Cell> missingPins;
    for(auto cell:pinned) {
        if(catalog_.contains(cell))wanted[cell]=0;else missingPins.insert(cell);
    }
    status_.budgetBlocked=missingPins.size();
    std::vector<std::pair<int,Cell>> order;
    for(const auto& [cell,distance]:wanted)order.emplace_back(distance,cell);
    std::sort(order.begin(),order.end());
    std::set<Cell> selected;
    size_t bytes=0;
    for(const auto& [distance,cell]:order) {
        (void)distance;const auto cost=catalog_.at(cell).bytes;
        if(selected.size()>=limits_.residentChunks || cost>limits_.residentBytes-bytes) {++status_.budgetBlocked;continue;}
        selected.insert(cell);bytes+=cost;
    }
    // Evict before allocation: teleports and failed loads cannot accumulate old worlds.
    for(auto it=resident_.begin();it!=resident_.end();) {
        if(!selected.contains(*it)){unload(*it);it=resident_.erase(it);++status_.unloads;}
        else ++it;
    }
    for(const auto& [distance,cell]:order) {
        (void)distance;
        if(!selected.contains(cell)||resident_.contains(cell))continue;
        auto& entry=catalog_.at(cell);
        if(loadBudget && update_>=entry.retryAt) {
            --loadBudget;++status_.loads;
            if(load(cell)){resident_.insert(cell);entry.failed=false;entry.retryAt=0;}
            else {entry.failed=true;entry.retryAt=update_+std::max(1u,limits_.retryUpdates);}
        }
        if(entry.failed)++status_.failed;
    }
    status_.wanted=wanted.size()+missingPins.size();status_.resident=resident_.size();
    status_.pending=selected.size()-resident_.size();
    for(auto cell:resident_)status_.reservedBytes+=catalog_.at(cell).bytes;
    return status_;
}
}
