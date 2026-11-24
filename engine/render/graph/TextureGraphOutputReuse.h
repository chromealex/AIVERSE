// Exact per-output dependency keys; no GPU resource ownership.
#pragma once
#include "TextureGraphLiveness.h"
#include <map>
#include <string>
#include <string_view>

namespace aiverse::render::texture_graph::gpu {
using OutputKeys=std::map<std::string,std::string>;
inline constexpr size_t outputKeyBudget=1024u*1024u;
inline bool appendOutputField(std::string& dst,std::string_view value,size_t limit){
    const auto length=std::to_string(value.size());
    if(dst.size()>limit||length.size()+1>limit-dst.size()||
       value.size()>limit-dst.size()-length.size()-1)return false;
    dst+=length;dst+=':';dst+=value;return true;
}
// Exact resolved node identity plus ordered input edges. Empty map means
// conservative full regeneration, not a cache hit.
inline OutputKeys outputDependencyKeys(const std::vector<std::vector<size_t>>& inputs,
        const std::vector<size_t>& order,const std::vector<std::string>& nodeKeys,
        const std::map<std::string,size_t>& outputs,std::string_view configuration){
    if(nodeKeys.size()!=inputs.size()||outputs.empty()||outputs.size()>6)return {};
    OutputKeys result;size_t used=0;
    for(const auto& [name,target]:outputs){
        std::string key;const auto limit=outputKeyBudget-used;
        if(!appendOutputField(key,configuration,limit)||
           !appendOutputField(key,std::to_string(target),limit))return {};
        for(const auto& step:planDagLifetime(inputs,order,{target}).steps){
            const auto n=step.node;
            if(!appendOutputField(key,std::to_string(n),limit)||
               !appendOutputField(key,nodeKeys[n],limit)||
               !appendOutputField(key,std::to_string(inputs[n].size()),limit))return {};
            for(auto dep:inputs[n])if(!appendOutputField(key,std::to_string(dep),limit))return {};
        }
        used+=key.size();result.emplace(name,std::move(key));
    }
    return result;
}
inline bool unchangedOutput(const OutputKeys& previous,const OutputKeys& next,const std::string& name){
    const auto a=previous.find(name),b=next.find(name);
    return a!=previous.end()&&b!=next.end()&&a->second==b->second;
}
} // namespace aiverse::render::texture_graph::gpu
