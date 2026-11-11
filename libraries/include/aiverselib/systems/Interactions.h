#pragma once
#include "Tags.h"
#include "../gameplay/Diagnostics.h"
#include <algorithm>
#include <span>
#include <vector>
namespace aiverse::lib::systems {
struct Interaction {std::int64_t id=0,priority=0;TagQuery targetTags;};
struct InteractionOption {std::int64_t id=0;gameplay::Decision decision;};
// Deterministic menu order: descending priority then ascending action ID. Pure previews are advisory.
template<class Check>
std::vector<InteractionOption> interactions(std::span<const Interaction> definitions,std::uint64_t tags,Check check){
    std::vector<Interaction> sorted(definitions.begin(),definitions.end());
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(std::size_t i=0;i<sorted.size();++i)if(sorted[i].id<=0||(i&&sorted[i-1].id==sorted[i].id))return {};
    std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.priority!=b.priority?a.priority>b.priority:a.id<b.id;});
    std::vector<InteractionOption> result;for(const auto& def:sorted)if(matches(tags,def.targetTags))result.push_back({def.id,check(def.id)});return result;
}
}
