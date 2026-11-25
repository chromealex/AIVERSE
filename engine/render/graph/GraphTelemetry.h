// Genre-independent value-only telemetry storage. Owner thread publishes after commit.
#pragma once
#include "engine/data/Json.h"
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace aiverse::graph_telemetry {
struct Identity {
    std::string world, entity, program, domain;
    bool operator<(const Identity& o) const {
        if(world!=o.world)return world<o.world;
        if(entity!=o.entity)return entity<o.entity;
        if(program!=o.program)return program<o.program;
        return domain<o.domain;
    }
};
class History {
    struct Track { uint64_t last=0; std::deque<Json> frames; };
    std::map<Identity,Track> tracks_;
public:
    static constexpr size_t maxTracks=16, maxFrames=120, maxFrameBytes=8192;
    // Caller owns world epoch, entity generation and immutable program identity.
    // No pointers, callbacks, ECS writes, or concurrent access.
    bool publish(const Identity& id,uint64_t sequence,const Json& frame){
        for(const auto* s:{&id.world,&id.entity,&id.program,&id.domain})
            if(s->empty()||s->size()>128)return false;
        if(!sequence||!frame.isObject()||frame.dump(-1).size()>maxFrameBytes)return false;
        Json copy=frame;copy.set("sequence",Json::string(std::to_string(sequence)));
        if(copy.dump(-1).size()>maxFrameBytes)return false;
        auto it=tracks_.find(id);
        if(it==tracks_.end()){
            if(tracks_.size()>=maxTracks)return false;
            it=tracks_.emplace(id,Track{}).first;
        }
        auto& t=it->second;if(sequence<=t.last)return false;
        t.frames.push_back(std::move(copy));t.last=sequence;
        if(t.frames.size()>maxFrames)t.frames.pop_front();
        return true;
    }
    Json read(const Identity& id) const {
        Json out=Json::array();auto it=tracks_.find(id);
        if(it!=tracks_.end())for(const auto& f:it->second.frames)out.push(f);
        return out;
    }
    void erase(const Identity& id){tracks_.erase(id);}
    void clearWorld(const std::string& world){
        for(auto it=tracks_.begin();it!=tracks_.end();)
            if(it->first.world==world)it=tracks_.erase(it);else ++it;
    }
    void clear(){tracks_.clear();}
};
} // namespace aiverse::graph_telemetry
