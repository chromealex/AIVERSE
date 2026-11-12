#pragma once
// survival-ecology: renewable resource nodes (berry bushes, trees, fish, grass). Harvests are applied first,
// sorted by (node, requester) and clamped to the amount left; a node that reaches 0 is depleted and waits
// regrowDelayTicks before it regrows. Every node regrows on ticks divisible by its periodTicks by
// regrowPerPeriod scaled by the weather growth factor (milli accumulators keep fractions). Integer only.
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kEcologyResourceLimit=1000000000000;
struct EcologyResourceNode {
    int64_t id=0,resource=0,region=0,amount=0,capacity=0,regrowPerPeriod=0,periodTicks=60,regrowDelayTicks=0;
    int64_t carry=0,depletedAt=-1;               // milli remainder; tick of depletion (-1 = not depleted)
    bool operator==(const EcologyResourceNode&)const=default;
};
struct EcologyResourceState {std::map<int64_t,EcologyResourceNode> nodes;int64_t lastTick=0;bool initialized=false;}; // record kind 109
struct EcologyResourceHarvest {int64_t node=0,requester=0,amount=0;};
struct EcologyResourceInput {std::vector<EcologyResourceHarvest> harvests;int64_t growthMilli=1000;std::map<int64_t,int64_t> regionGrowthMilli;}; // region factor multiplies the global one
enum class EcologyResourceEventKind:uint8_t {Harvested,Depleted,Regrowing,Full};
struct EcologyResourceEvent {EcologyResourceEventKind kind=EcologyResourceEventKind::Harvested;int64_t node=0,requester=0,value=0;};
struct EcologyResourceReport {std::vector<EcologyResourceEvent> events;std::map<int64_t,int64_t> taken;}; // node -> amount taken this tick

inline bool valid(const EcologyResourceNode& n){
    auto a=[](int64_t v){return v>=0&&v<=kEcologyResourceLimit;};
    return n.id>0&&n.resource>0&&n.region>=0&&a(n.capacity)&&n.amount>=0&&n.amount<=n.capacity&&a(n.regrowPerPeriod)&&n.periodTicks>=1&&n.periodTicks<=100000000
        &&n.regrowDelayTicks>=0&&n.regrowDelayTicks<=100000000&&n.carry>=0&&n.carry<1000&&n.depletedAt>= -1;
}
inline bool valid(const EcologyResourceState& s){
    if(s.nodes.size()>1000000||s.lastTick<0)return false;
    for(const auto& [id,n]:s.nodes)if(id!=n.id||!valid(n))return false;
    return true;
}
inline bool ecologyAddResource(EcologyResourceState& s,EcologyResourceNode n){if(!valid(n)||s.nodes.contains(n.id))return false;s.nodes.emplace(n.id,n);return true;}
inline int64_t ecologyResourceAmount(const EcologyResourceState& s,int64_t node){auto it=s.nodes.find(node);return it==s.nodes.end()?0:it->second.amount;}
inline std::optional<EcologyResourceReport> stepEcologyResources(EcologyResourceState& state,int64_t tick,const EcologyResourceInput& in){
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(state)||in.growthMilli<0||in.growthMilli>10000||in.harvests.size()>1000000)return std::nullopt;
    for(const auto& h:in.harvests)if(!state.nodes.contains(h.node)||h.amount<0||h.amount>kEcologyResourceLimit||h.requester<0)return std::nullopt;
    for(auto [r,f]:in.regionGrowthMilli)if(r<0||f<0||f>10000)return std::nullopt;
    auto s=state;EcologyResourceReport rep;auto harvests=in.harvests;
    std::stable_sort(harvests.begin(),harvests.end(),[](const auto& a,const auto& b){return a.node!=b.node?a.node<b.node:a.requester<b.requester;});
    for(const auto& h:harvests){
        auto& n=s.nodes.at(h.node);auto take=std::min(h.amount,n.amount);
        if(!take)continue;
        n.amount-=take;rep.taken[h.node]+=take;rep.events.push_back({EcologyResourceEventKind::Harvested,h.node,h.requester,take});
        if(n.amount==0){n.depletedAt=tick;n.carry=0;rep.events.push_back({EcologyResourceEventKind::Depleted,h.node,h.requester,0});}
    }
    for(auto& [id,n]:s.nodes){
        if(tick%n.periodTicks!=0||n.amount>=n.capacity||!n.regrowPerPeriod)continue;
        if(n.depletedAt>=0&&tick<n.depletedAt+n.regrowDelayTicks)continue;
        auto f=in.growthMilli;if(auto r=in.regionGrowthMilli.find(n.region);r!=in.regionGrowthMilli.end())f=f*r->second/1000;
        const auto gain=n.regrowPerPeriod*f+n.carry;const auto whole=gain/1000;n.carry=gain%1000;
        if(!whole)continue;
        if(n.depletedAt>=0){n.depletedAt=-1;rep.events.push_back({EcologyResourceEventKind::Regrowing,id,0,whole});}
        n.amount=std::min(n.capacity,n.amount+whole);
        if(n.amount==n.capacity){n.carry=0;rep.events.push_back({EcologyResourceEventKind::Full,id,0,n.amount});}
    }
    s.lastTick=tick;s.initialized=true;state=std::move(s);return rep;
}
}
