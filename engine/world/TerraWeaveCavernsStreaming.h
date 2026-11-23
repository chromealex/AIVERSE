#pragma once
// TerraWeave Caverns stage-7 pure streaming planner.
// No GPU residency or jobs: caller schedules immutable CPU mesh builds and
// commits completed results at a serialized graphics/physics owner barrier.
#include "engine/world/TerraWeaveCaverns.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <stdexcept>
#include <limits>
namespace aiverse::terraweave::caverns {
enum class StreamAction:uint8_t { Build, Retire };
struct StreamRequest {
    StreamAction action=StreamAction::Build;
    ChunkKey3 key{};
    uint64_t generation=0; // invalidates jobs when resident intent changes
    uint32_t lod=0;        // visual-only; never alters authoritative collision
};
struct StreamSettings {
    uint32_t radiusXZ=2;
    uint32_t radiusY=1;
    uint32_t maxResident=256;
    uint32_t maxBuildsPerPump=4;
    uint32_t maxRetiresPerPump=8;
};
struct KeyLess {
    bool operator()(ChunkKey3 a,ChunkKey3 b)const {
        if(a.x!=b.x)return a.x<b.x;
        if(a.y!=b.y)return a.y<b.y;
        return a.z<b.z;
    }
};
struct StreamItem {
    uint64_t generation=0;
    uint64_t sourceRevision=0;
    uint32_t lod=0;
    bool ready=false;
    bool inFlight=false;
};
class CaveStreamPlanner {
public:
    explicit CaveStreamPlanner(StreamSettings settings={}):config_(settings) {
        if(config_.radiusXZ>16||config_.radiusY>8||config_.maxResident==0||
           config_.maxResident>65536||config_.maxBuildsPerPump==0||
           config_.maxBuildsPerPump>1024||config_.maxRetiresPerPump==0||
           config_.maxRetiresPerPump>1024)
            throw std::invalid_argument("Caverns streaming settings");
    }
    void setCenter(ChunkKey3 center){center_=center;}
    // Every runtime revision change invalidates all old completed jobs.
    void setSourceRevision(uint64_t revision) {
        if(revision==sourceRevision_)return;
        sourceRevision_=revision;
        for(auto& [key,item]:items_){
            (void)key; item.generation=++nextGeneration_;
            item.inFlight=false;item.ready=false;item.sourceRevision=revision;
        }
    }
    void setVisualLod(uint32_t lod) {
        if(lod>8)throw std::invalid_argument("Caverns visual LOD");
        lod_=lod;
        for(auto& [key,item]:items_){
            (void)key;
            if(item.lod!=lod){item.generation=++nextGeneration_;item.inFlight=false;item.ready=false;item.lod=lod;}
        }
    }
    // A completed worker result is accepted only if it still belongs to the
    // EXACT generation/LOD/revision desired by the streaming planner.
    bool accept(StreamRequest request,uint64_t revision) {
        if(request.action!=StreamAction::Build)return false;
        const auto it=items_.find(request.key);
        if(it==items_.end()||revision!=sourceRevision_||
           it->second.generation!=request.generation||
           it->second.lod!=request.lod||!it->second.inFlight)return false;
        it->second.inFlight=false;
        it->second.ready=true;
        return true;
    }
    std::vector<StreamRequest> pump(){
        std::vector<StreamRequest> requests;
        // Sort candidates by squared integer distance; keys break ties.
        struct Candidate{ChunkKey3 key;uint64_t distance=0;};
        std::vector<Candidate> wanted;
        for(int64_t z=-int64_t(config_.radiusXZ);z<=int64_t(config_.radiusXZ);++z)
        for(int64_t y=-int64_t(config_.radiusY);y<=int64_t(config_.radiusY);++y)
        for(int64_t x=-int64_t(config_.radiusXZ);x<=int64_t(config_.radiusXZ);++x) {
            if(center_.x+x < -kLimitCm || center_.x+x > kLimitCm ||
               center_.y+y < -kLimitCm || center_.y+y > kLimitCm ||
               center_.z+z < -kLimitCm || center_.z+z > kLimitCm)continue;
            wanted.push_back({{center_.x+x,center_.y+y,center_.z+z},
                uint64_t(x*x+y*y+z*z)});
        }
        std::sort(wanted.begin(),wanted.end(),[](const Candidate& a,const Candidate& b){
            if(a.distance!=b.distance)return a.distance<b.distance;
            return KeyLess{}(a.key,b.key);
        });
        if(wanted.size()>config_.maxResident)wanted.resize(config_.maxResident);
        std::set<ChunkKey3,KeyLess> desired;
        for(const auto& v:wanted)desired.insert(v.key);
        uint32_t retireCount=0;
        for(auto it=items_.begin();it!=items_.end();) {
            if(!desired.contains(it->first) && retireCount<config_.maxRetiresPerPump) {
                requests.push_back({StreamAction::Retire,it->first,it->second.generation,it->second.lod});
                it=items_.erase(it);++retireCount;
            }else ++it;
        }
        uint32_t buildCount=0;
        for(const auto& v:wanted) {
            auto [it,inserted]=items_.try_emplace(v.key);
            auto& item=it->second;
            if(inserted) {
                item.generation=++nextGeneration_;
                item.lod=lod_;
                item.sourceRevision=sourceRevision_;
            }
            if(!item.ready&&!item.inFlight&&buildCount<config_.maxBuildsPerPump) {
                item.inFlight=true;
                requests.push_back({StreamAction::Build,v.key,item.generation,item.lod});
                ++buildCount;
            }
        }
        return requests;
    }
    size_t tracked()const{return items_.size();}
    uint64_t revision()const{return sourceRevision_;}
private:
    StreamSettings config_{};
    ChunkKey3 center_{};
    uint64_t sourceRevision_=0,nextGeneration_=0;
    uint32_t lod_=0;
    std::map<ChunkKey3,StreamItem,KeyLess> items_;
};
} // namespace aiverse::terraweave::caverns
