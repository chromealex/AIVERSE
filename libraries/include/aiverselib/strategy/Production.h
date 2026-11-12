#pragma once
#include "../Logistics.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace aiverse::lib::strategy {
struct FactoryNode {int64_t id=0;std::vector<int64_t> stock,capacity;bool operator==(const FactoryNode&)const=default;};
struct FactoryRecipe {int64_t node=0;std::vector<Ingredient> inputs,outputs;};
struct FactoryLink {int64_t id=0,from=0,to=0;size_t resource=0;int64_t rate=0;};
struct FactoryState {uint64_t tick=0;std::map<int64_t,FactoryNode> nodes;bool operator==(const FactoryState&)const=default;};
struct FactoryRules {std::vector<FactoryRecipe> recipes;std::vector<FactoryLink> links;};
inline bool valid(const FactoryState& s) {
    if(s.nodes.empty()||s.nodes.size()>4096)return false;
    const auto count=s.nodes.begin()->second.stock.size();if(!count||count>64)return false;
    for(const auto& [id,n]:s.nodes){if(id<=0||n.id!=id||n.stock.size()!=count||n.capacity.size()!=count)return false;for(size_t i=0;i<count;++i)if(n.capacity[i]<0||n.stock[i]<0||n.stock[i]>n.capacity[i])return false;}
    return true;
}
inline bool compatible(const FactoryRules& rules,const FactoryState& s) {
    if(!valid(s)||rules.recipes.size()>4096||rules.links.size()>16384)return false;
    const auto count=s.nodes.begin()->second.stock.size();int64_t previous=0;
    for(const auto& link:rules.links){if(link.id<=previous||link.from==link.to||!s.nodes.contains(link.from)||!s.nodes.contains(link.to)||link.resource>=count||link.rate<=0)return false;previous=link.id;}
    previous=0;
    for(const auto& r:rules.recipes){
        if(r.node<=previous||!s.nodes.contains(r.node)||r.inputs.size()>64||r.outputs.empty()||r.outputs.size()>64)return false;
        previous=r.node;
        for(const auto& items:{r.inputs,r.outputs}){std::vector<int64_t> sum(count);for(const auto& i:items){if(i.resource>=count||i.amount<=0||sum[i.resource]>INT64_MAX-i.amount)return false;sum[i.resource]+=i.amount;}}
    }
    return true;
}
// Recipe phase, then one-hop transport. Competition follows stable link IDs. Incoming
// material cannot travel twice in one tick; departure space becomes available next tick.
inline bool stepFactory(const FactoryRules& rules,FactoryState& state,uint64_t tick) {
    if(!compatible(rules,state)||state.tick==UINT64_MAX||tick!=state.tick+1)return false;
    auto out=state;
    for(const auto& r:rules.recipes){
        auto& node=out.nodes.at(r.node);auto stock=node.stock;if(!consume(stock,r.inputs))continue;
        bool fits=true;
        for(const auto& i:r.outputs){if(i.amount>node.capacity[i.resource]-stock[i.resource]){fits=false;break;}stock[i.resource]+=i.amount;}
        if(fits)node.stock=std::move(stock);
    }
    auto available=out.nodes;auto free=out.nodes;
    for(auto& [id,n]:free){(void)id;for(size_t i=0;i<n.stock.size();++i)n.stock[i]=n.capacity[i]-n.stock[i];}
    for(const auto& link:rules.links){
        const auto r=link.resource;auto amount=std::min({link.rate,available.at(link.from).stock[r],free.at(link.to).stock[r]});
        available.at(link.from).stock[r]-=amount;free.at(link.to).stock[r]-=amount;
        out.nodes.at(link.from).stock[r]-=amount;out.nodes.at(link.to).stock[r]+=amount;
    }
    out.tick=tick;state=std::move(out);return true;
}
}
