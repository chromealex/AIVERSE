// Bounded CPU-only pending request storage. No GPU work or ownership.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::render::texture_graph::gpu {
class TextureRequestQueue {
public:
    static constexpr size_t maxPending=8,maxPayloadBytes=512u*1024u,maxPendingBytes=2u*1024u*1024u;
    struct Entry {uint64_t ticket=0;int priority=0;std::string payload;};
    uint64_t push(std::string payload,int priority){
        if(priority<0||priority>3||payload.empty()||payload.size()>maxPayloadBytes||
           pending_.size()>=maxPending||payload.size()>maxPendingBytes-bytes_)
            throw std::invalid_argument("texture request queue limit or priority invalid");
        if(next_==std::numeric_limits<uint64_t>::max())throw std::overflow_error("texture queue identity exhausted");
        const auto id=next_+1;
        pending_.push_back({id,priority,std::move(payload)});
        bytes_+=pending_.back().payload.size();next_=id;return id;
    }
    std::optional<Entry> pop(){
        if(pending_.empty())return {};
        // max_element selects the first equal priority: stable FIFO.
        auto it=std::max_element(pending_.begin(),pending_.end(),[](const auto& a,const auto& b){return a.priority<b.priority;});
        Entry e=std::move(*it);bytes_-=e.payload.size();pending_.erase(it);return e;
    }
    bool cancel(uint64_t ticket){
        auto it=std::find_if(pending_.begin(),pending_.end(),[&](const auto& e){return e.ticket==ticket;});
        if(it==pending_.end())return false;
        bytes_-=it->payload.size();pending_.erase(it);return true;
    }
    void clear(){pending_.clear();bytes_=0;} // tickets are never recycled
    size_t bytes()const{return bytes_;}
    const std::vector<Entry>& pending()const{return pending_;}
private:
    std::vector<Entry> pending_;
    size_t bytes_=0;
    uint64_t next_=0;
};
} // namespace aiverse::render::texture_graph::gpu
