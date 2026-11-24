// Exact-source invalidation for dedicated, fence-retired GPU node slots.
// No GPU handles, hash collisions, allocations of GPU data or owner of its own.
#pragma once
#include "TextureGraphPassSchedule.h"
#include <string>
#include <string_view>

namespace aiverse::render::texture_graph::gpu {
inline constexpr size_t incrementalKeyBudget=1024u*1024u;
struct IncrementalNodeKey {
    std::string source;
    std::vector<size_t> inputs;
    bool operator==(const IncrementalNodeKey&)const=default;
};
// The retained layout is topological: each pass has one distinct slot,
// every input refers to a preceding slot. Aliased schedules cannot cache data.
template<class SourceFor>
std::vector<IncrementalNodeKey> incrementalKeys(const PassSchedule& schedule,SourceFor sourceFor){
    if(schedule.passes.size()!=schedule.slotBytes.size()||schedule.passes.empty()||
       schedule.passes.size()>32||!schedule.externalReady.empty())return {};
    std::vector<IncrementalNodeKey> keys;keys.reserve(schedule.passes.size());
    size_t used=0;
    for(const auto& pass:schedule.passes){
        if(pass.output!=keys.size()||pass.inputs.size()>3)return {};
        for(auto input:pass.inputs)if(input>=pass.output)return {};
        // Keep an owning temporary alive if the callback returns a string.
        auto&& raw=sourceFor(pass.pipeline);
        const std::string_view source=raw;
        if(source.empty()||source.size()>incrementalKeyBudget-used)return {};
        used+=source.size();keys.push_back({std::string(source),pass.inputs});
    }
    return keys;
}
// Only compare keys after owner proves identical buffer layout/backend/device
// and completed prior contents. Any changed input dirties all descendants.
inline std::vector<bool> incrementalDirty(const std::vector<IncrementalNodeKey>& previous,
        const std::vector<IncrementalNodeKey>& current,size_t count){
    std::vector<bool> dirty(count,true);
    if(previous.size()!=count||current.size()!=count||count>32)return dirty;
    for(size_t i=0;i<count;++i){
        bool changed=!(previous[i]==current[i]);
        for(auto input:current[i].inputs){
            if(input>=i)return std::vector<bool>(count,true);
            changed=changed||dirty[input];
        }
        dirty[i]=changed;
    }
    return dirty;
}
} // namespace aiverse::render::texture_graph::gpu
