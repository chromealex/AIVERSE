// CPU-only metadata and admission policy for actual dispatched GPU regions.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aiverse::graph_gpu_profile {
struct TextureGroupScope {
    size_t ordinal=0,slot=0,slotBytes=0;
    std::string root;
    std::vector<std::string> members;
    bool valid()const noexcept{
        if(ordinal>=32||slot>=32||!slotBytes||members.empty()||members.size()>8||
           root.empty()||root!=members.back())return false;
        for(size_t i=0;i<members.size();++i){
            if(members[i].empty()||members[i].size()>128)return false;
            for(size_t j=0;j<i;++j)if(members[j]==members[i])return false;
        }
        return true;
    }
};
// Bound attempted detailed queries even if the RHI refuses them. This is
// separate from the capture's pending-query and sample-history caps.
class TextureGroupScopeBudget {
    uint64_t frame_=0;
    size_t used_=0;
public:
    static constexpr size_t maxPerFrame=12;
    void clear()noexcept{frame_=0;used_=0;}
    bool admit(uint64_t frame)noexcept{
        if(frame<frame_)return false;
        if(frame!=frame_){frame_=frame;used_=0;}
        if(used_>=maxPerFrame)return false;
        ++used_;return true;
    }
};
} // namespace aiverse::graph_gpu_profile
