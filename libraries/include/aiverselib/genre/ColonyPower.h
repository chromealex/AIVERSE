#pragma once
// settlement-production: power / fluid graphs. Nodes (producers, consumers, storage, conduits) of one medium
// are joined by undirected links; every connected component (breadth-first over a lib::Graph from Graphs.h, nodes in id
// order) balances on its own each tick. Shortage is covered by storage (rate-limited, ascending id), then
// consumers are shed deterministically: kept in (priority desc, id asc) order while they fit the available
// supply, later ones that still fit are kept too (skip-fit); a shed consumer stays off for restoreDelayTicks
// (hysteresis against flapping). Surplus charges storage in ascending id order.
// Integer units per tick (watts, litres, ...); producer output is scaled by owner factors (weather, fuel).
#include "../Graphs.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kPowerAmountLimit=1000000000000;
inline constexpr std::size_t kPowerMaxNodes=65536,kPowerMaxLinks=262144;
enum class PowerNodeKind:uint8_t {Producer,Consumer,Storage,Conduit};
struct PowerNode {
    int64_t id=0,medium=1;PowerNodeKind kind=PowerNodeKind::Conduit;
    int64_t output=0;                 // producer: nominal units per tick (scaled by PowerInput::outputMilli)
    int64_t demand=0,priority=0;      // consumer: units per tick, higher priority is shed later
    int64_t capacity=0,rate=0,charge=0; // storage: capacity, max charge/discharge per tick, current charge
    bool enabled=true,powered=false;  // powered: consumer served last tick (state, saved)
    int64_t reconnectAt=0;            // consumer: shed consumers stay off until this tick (hysteresis)
    bool operator==(const PowerNode&)const=default;
};
struct PowerGridState {std::map<int64_t,PowerNode> nodes;std::vector<std::pair<int64_t,int64_t>> links;int64_t restoreDelayTicks=0,lastTick=0;bool initialized=false;}; // links sorted, first < second; restoreDelayTicks: hysteresis after shedding
struct PowerInput {std::map<int64_t,int64_t> outputMilli;std::map<int64_t,bool> enabled;}; // producer factor 0..100000 (default 1000); node switches
struct PowerComponent {int64_t id=0,medium=0,supply=0,demand=0,served=0,shed=0,charged=0,discharged=0,stored=0,capacity=0;}; // id = lowest node id
enum class PowerEventKind:uint8_t {Shed,Restored,StorageEmpty,StorageFull};
struct PowerEvent {PowerEventKind kind=PowerEventKind::Shed;int64_t node=0,component=0,value=0;};
struct PowerReport {std::vector<PowerComponent> components;std::vector<int64_t> powered,unpowered;std::vector<PowerEvent> events;}; // consumer ids, ascending

inline bool valid(const PowerNode& n){
    auto amount=[](int64_t v){return v>=0&&v<=kPowerAmountLimit;};
    return n.id>0&&n.medium>0&&uint8_t(n.kind)<=uint8_t(PowerNodeKind::Conduit)&&amount(n.output)&&amount(n.demand)&&n.priority>= -1000000&&n.priority<=1000000
        &&amount(n.capacity)&&amount(n.rate)&&n.charge>=0&&n.charge<=n.capacity&&n.reconnectAt>=0;
}
inline bool valid(const PowerGridState& s){
    if(s.nodes.size()>kPowerMaxNodes||s.links.size()>kPowerMaxLinks||s.lastTick<0||s.restoreDelayTicks<0||s.restoreDelayTicks>100000000||!std::is_sorted(s.links.begin(),s.links.end())||std::adjacent_find(s.links.begin(),s.links.end())!=s.links.end())return false;
    for(const auto& [id,n]:s.nodes)if(id!=n.id||!valid(n))return false;
    for(auto [a,b]:s.links){auto x=s.nodes.find(a),y=s.nodes.find(b);if(a>=b||x==s.nodes.end()||y==s.nodes.end()||x->second.medium!=y->second.medium)return false;}
    return true;
}
inline bool powerAddNode(PowerGridState& s,PowerNode n){if(!valid(n)||s.nodes.contains(n.id)||s.nodes.size()>=kPowerMaxNodes)return false;n.powered=false;s.nodes.emplace(n.id,n);return true;}
inline bool powerLink(PowerGridState& s,int64_t a,int64_t b){
    if(a>b)std::swap(a,b);
    auto x=s.nodes.find(a),y=s.nodes.find(b);
    if(a==b||x==s.nodes.end()||y==s.nodes.end()||x->second.medium!=y->second.medium||s.links.size()>=kPowerMaxLinks)return false;
    auto it=std::lower_bound(s.links.begin(),s.links.end(),std::pair{a,b});if(it!=s.links.end()&&*it==std::pair{a,b})return false;
    s.links.insert(it,{a,b});return true;
}
inline bool powerUnlink(PowerGridState& s,int64_t a,int64_t b){
    if(a>b)std::swap(a,b);
    auto it=std::lower_bound(s.links.begin(),s.links.end(),std::pair{a,b});
    if(it==s.links.end()||*it!=std::pair{a,b})return false;
    s.links.erase(it);return true;
}
inline bool powerRemoveNode(PowerGridState& s,int64_t id){
    if(!s.nodes.erase(id))return false;
    s.links.erase(std::remove_if(s.links.begin(),s.links.end(),[&](auto l){return l.first==id||l.second==id;}),s.links.end());return true;
}
// Connected components as lists of node ids (ascending), ordered by their lowest id.
inline std::vector<std::vector<int64_t>> powerComponents(const PowerGridState& s){
    std::vector<int64_t> ids;std::map<int64_t,std::size_t> index;
    for(const auto& [id,n]:s.nodes){(void)n;index[id]=ids.size();ids.push_back(id);}
    Graph g(ids.size());for(auto [a,b]:s.links){g[index[a]].push_back(index[b]);g[index[b]].push_back(index[a]);}
    std::vector<bool> seen(ids.size());std::vector<std::vector<int64_t>> out;
    for(std::size_t i=0;i<ids.size();++i){
        if(seen[i])continue;
        std::vector<std::size_t> queue{i};seen[i]=true; // breadth-first over the lib::Graph, O(V+E) overall
        for(std::size_t head=0;head<queue.size();++head)for(auto v:g[queue[head]])if(!seen[v]){seen[v]=true;queue.push_back(v);}
        std::sort(queue.begin(),queue.end());std::vector<int64_t> comp;for(auto q:queue)comp.push_back(ids[q]);
        out.push_back(std::move(comp));
    }
    return out;
}
inline std::optional<PowerReport> stepPower(PowerGridState& state,int64_t tick,const PowerInput& in){
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(state))return std::nullopt;
    for(auto [id,f]:in.outputMilli)if(!state.nodes.contains(id)||f<0||f>100000)return std::nullopt;
    for(auto [id,e]:in.enabled){(void)e;if(!state.nodes.contains(id))return std::nullopt;}
    auto s=state;PowerReport r;
    for(auto [id,e]:in.enabled)s.nodes.at(id).enabled=e;
    for(const auto& comp:powerComponents(s)){
        PowerComponent c;c.id=comp.front();c.medium=s.nodes.at(c.id).medium;int64_t storable=0;
        std::vector<PowerNode*> consumers,storages;
        for(auto id:comp){
            auto& n=s.nodes.at(id);
            if(n.kind==PowerNodeKind::Producer&&n.enabled){auto f=in.outputMilli.contains(id)?in.outputMilli.at(id):1000;c.supply+=n.output/1000*f+n.output%1000*f/1000;}
            if(n.kind==PowerNodeKind::Consumer)consumers.push_back(&n);
            if(n.kind==PowerNodeKind::Storage&&n.enabled){storages.push_back(&n);storable+=std::min(n.charge,n.rate);}
        }
        std::stable_sort(consumers.begin(),consumers.end(),[](auto* a,auto* b){return a->priority!=b->priority?a->priority>b->priority:a->id<b->id;});
        const auto available=c.supply+storable;
        for(auto* n:consumers){
            bool served=false;
            if(n->enabled){c.demand+=n->demand;if(c.served+n->demand<=available&&(n->powered||tick>=n->reconnectAt)){c.served+=n->demand;served=true;}else ++c.shed;}
            if(state.initialized&&n->enabled&&n->powered&&!served)r.events.push_back({PowerEventKind::Shed,n->id,c.id,n->demand});
            if(state.initialized&&!n->powered&&served)r.events.push_back({PowerEventKind::Restored,n->id,c.id,n->demand});
            if(n->enabled&&n->powered&&!served)n->reconnectAt=tick+s.restoreDelayTicks;
            n->powered=served;
        }
        auto need=c.served-c.supply;
        for(auto* n:storages){
            const auto before=n->charge;
            if(need>0){auto take=std::min({need,n->charge,n->rate});n->charge-=take;need-=take;c.discharged+=take;}
            else if(need<0){auto put=std::min({-need,n->capacity-n->charge,n->rate});n->charge+=put;need+=put;c.charged+=put;}
            if(before>0&&n->charge==0)r.events.push_back({PowerEventKind::StorageEmpty,n->id,c.id,0});
            if(before<n->capacity&&n->charge==n->capacity)r.events.push_back({PowerEventKind::StorageFull,n->id,c.id,n->capacity});
            c.stored+=n->charge;c.capacity+=n->capacity;
        }
        r.components.push_back(c);
    }
    for(const auto& [id,n]:s.nodes)if(n.kind==PowerNodeKind::Consumer)(n.powered?r.powered:r.unpowered).push_back(id);
    s.lastTick=tick;s.initialized=true;state=std::move(s);return r;
}
}
