#pragma once
#include "engine/data/Json.h"
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace aiverse::building {
// A host-owned publication channel. Producer copies on the simulation
// owner barrier; readers never touch the world. No global scene registry.
class BuildingGraphMonitor {
public:
    static constexpr size_t kMaxScenes=256,kHistory=32;
    void publish(uint64_t sceneId,Json snapshot){
        if(!sceneId||!snapshot.isObject()||snapshot.getStr("sceneId")!=std::to_string(sceneId))return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto found=scenes_.find(sceneId);
        if(found==scenes_.end()){
            if(scenes_.size()>=kMaxScenes)return;
            found=scenes_.emplace(sceneId,Entry{}).first;
        }
        auto& entry=found->second;
        // Revision can go backwards on rollback. Preserve observation order,
        // and record only changed snapshots, not every simulation tick.
        const auto identity=snapshot.dump(-1);
        if(identity==entry.identity)return;
        entry.identity=identity;
        entry.latest=std::move(snapshot);
        entry.history.push_back(entry.latest);
        if(entry.history.size()>kHistory)entry.history.pop_front();
    }
    Json read(uint64_t sceneId)const{
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it=scenes_.find(sceneId);if(it==scenes_.end())return Json{};
        Json result=it->second.latest,history=Json::array();
        for(const auto& item:it->second.history)history.push(item);
        result.set("history",std::move(history));return result;
    }
    void clear(){std::lock_guard<std::mutex> lock(mutex_);scenes_.clear();}
private:
    struct Entry {Json latest;std::string identity;std::deque<Json> history;};
    mutable std::mutex mutex_;
    std::map<uint64_t,Entry> scenes_;
};
} // namespace aiverse::building
