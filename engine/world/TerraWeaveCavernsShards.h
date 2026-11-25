#pragma once
// Stage 12: deterministic independently reloadable GeometryScene shards.
// Pure planning only. Each shard owns a bounded group of cave mesh sources.
// The host stages a replacement shard and retires its old GeometryScene with
// real GPU completion proof; other shards are never rebuilt for that edit.
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {
struct CaveShardKey {
    int64_t x=0,y=0,z=0;
    constexpr bool operator==(const CaveShardKey&)const=default;
};
struct CaveShardLess {
    bool operator()(CaveShardKey a,CaveShardKey b)const {
        if(a.x!=b.x)return a.x<b.x;
        if(a.y!=b.y)return a.y<b.y;
        return a.z<b.z;
    }
};
struct CaveShardConfig {
    uint32_t chunksPerAxis=2; // 2^3 cells, upper bound 4^3
    uint32_t maxPending=128;
};
inline void validateCaveShardConfig(CaveShardConfig c){
    if(!c.chunksPerAxis||c.chunksPerAxis>4||!c.maxPending||c.maxPending>4096)
        throw std::invalid_argument("Caverns shard settings");
}
inline CaveShardKey caveShardFor(ChunkKey3 k,CaveShardConfig c){
    validateCaveShardConfig(c);
    const int64_t span=c.chunksPerAxis;
    return {floorDiv(k.x,span),floorDiv(k.y,span),floorDiv(k.z,span)};
}
struct CaveShardBuild {
    CaveShardKey shard{};
    uint64_t generation=0;
    std::vector<ChunkKey3> keys;
    // An empty keys list means retire the old GeometryScene shard.
};
class CaveShardRegistry {
public:
    explicit CaveShardRegistry(CaveShardConfig config={}):config_(config){
        validateCaveShardConfig(config_);
    }
    // Mutations are owner-thread operations. A replacement for one chunk
    // invalidates only that shard, never adjacent shard scene assets.
    void upsert(ChunkKey3 key,uint64_t sourceRevision){
        if(!sourceRevision)throw std::invalid_argument("Caverns zero source revision");
        const auto shard=caveShardFor(key,config_);
        auto& group=groups_[shard];
        auto it=group.chunks.find(key);
        if(it!=group.chunks.end()&&it->second==sourceRevision)return;
        it=group.chunks.insert_or_assign(key,sourceRevision).first;
        (void)it;invalidate(group);
    }
    void erase(ChunkKey3 key){
        const auto shard=caveShardFor(key,config_);
        auto it=groups_.find(shard);
        if(it==groups_.end())return;
        if(it->second.chunks.erase(key))invalidate(it->second);
    }
    // At most maxCount rebuild operations per pump; issue newest generation
    // only once until an ACK, so a busy caller cannot duplicate GPU uploads.
    std::vector<CaveShardBuild> pump(uint32_t maxCount=4){
        if(!maxCount||maxCount>config_.maxPending)
            throw std::invalid_argument("Caverns shard pump budget");
        std::vector<CaveShardBuild> out;
        for(auto& [shard,group]:groups_){
            if(out.size()>=maxCount)break;
            if(group.dirty && !group.inFlight){
                group.inFlight=true;
                CaveShardBuild request;request.shard=shard;request.generation=group.generation;
                for(const auto& [key,rev]:group.chunks){(void)rev;request.keys.push_back(key);}
                out.push_back(std::move(request));
            }
        }
        return out;
    }
    // Call only after the new GeometryScene has been staged AND published;
    // a failed stage must be retried by fail(), never acknowledged as ready.
    bool acknowledge(CaveShardBuild req){
        auto it=groups_.find(req.shard);
        if(it==groups_.end()||!it->second.inFlight||
           it->second.generation!=req.generation)return false;
        auto& group=it->second;
        group.inFlight=false;group.dirty=false;
        group.publishedGeneration=req.generation;
        if(group.chunks.empty())groups_.erase(it);
        return true;
    }
    bool fail(CaveShardBuild req){
        auto it=groups_.find(req.shard);
        if(it==groups_.end()||!it->second.inFlight||
           it->second.generation!=req.generation)return false;
        it->second.inFlight=false;
        return true;
    }
    size_t shardCount()const{return groups_.size();}
    size_t dirtyShards()const{
        size_t result=0;
        for(const auto& [k,g]:groups_){(void)k;result+=g.dirty;}
        return result;
    }
private:
    struct Group{
        std::map<ChunkKey3,uint64_t,KeyLess> chunks;
        uint64_t generation=0,publishedGeneration=0;
        bool dirty=false,inFlight=false;
    };
    void invalidate(Group& g){
        if(next_==UINT64_MAX)throw std::overflow_error("Caverns shard generations exhausted");
        g.generation=++next_;
        g.dirty=true;g.inFlight=false;
    }
    CaveShardConfig config_{};
    uint64_t next_=0;
    std::map<CaveShardKey,Group,CaveShardLess> groups_;
};
} // namespace aiverse::terraweave::caverns
