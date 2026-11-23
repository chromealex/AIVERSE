#pragma once
// Caverns Stage21: bounded, revision-aware dirty chunk notification queue.
// Immutable input coordinates only; caller owns worker scheduling and all
// GeometryScene/STRATA/physics publication barriers.
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveDirtyChunk {
    ChunkKey3 key{};
    uint64_t sourceRevision=0;
    uint64_t serial=0;
};
class CaveDirtyQueue {
public:
    explicit CaveDirtyQueue(size_t limit=4096):limit_(limit){
        if(!limit||limit>65536)throw std::invalid_argument("Caverns dirty queue cap");
    }
    // Every stamp update is applied across its bounding 3D region and a
    // gradient halo, using signed world-coordinate floor division.
    void enqueue(CaveDirtyBounds bounds,uint32_t resolution,int32_t spacingCm,
                 uint64_t sourceRevision){
        if(!sourceRevision)throw std::invalid_argument("Caverns dirty revision");
        auto keys=dirtyCaveChunks(bounds,resolution,spacingCm,limit_);
        size_t additional=0;
        for(auto key:keys)if(!dirty_.contains(key))++additional;
        if(dirty_.size()+additional>limit_)
            throw std::length_error("Caverns dirty queue exhausted");
        if(serial_==UINT64_MAX)
            throw std::overflow_error("Caverns dirty serial exhausted");
        const uint64_t serial=++serial_;
        for(auto key:keys) {
            auto [it,inserted]=dirty_.try_emplace(key,CaveDirtyChunk{key,sourceRevision,serial});
            if(!inserted && it->second.sourceRevision<=sourceRevision)
                it->second={key,sourceRevision,serial};
        }
    }
    std::vector<CaveDirtyChunk> pending(size_t maxCount=64)const {
        if(!maxCount||maxCount>limit_)throw std::invalid_argument("Caverns dirty poll cap");
        std::vector<CaveDirtyChunk> result;
        for(const auto& [key,value]:dirty_){
            (void)key;
            if(result.size()>=maxCount)break;
            result.push_back(value);
        }
        return result;
    }
    // ACK only after the exact snapshot was successfully accepted for
    // regeneration. Stale completions may never clear newer dirty edits.
    bool acknowledge(CaveDirtyChunk item) {
        auto it=dirty_.find(item.key);
        if(it==dirty_.end()||it->second.serial!=item.serial||
           it->second.sourceRevision!=item.sourceRevision)return false;
        dirty_.erase(it);
        return true;
    }
    size_t size()const{return dirty_.size();}
    void clear(){dirty_.clear();}
private:
    size_t limit_;
    uint64_t serial_=0;
    std::map<ChunkKey3,CaveDirtyChunk,KeyLess> dirty_;
};
} // namespace aiverse::terraweave::caverns
