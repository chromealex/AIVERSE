#pragma once
#include <cstddef>
#include "../Graphs.h"
#include "../Progression.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace aiverse::lib::strategy {
struct RunEncounter {int64_t id=0,weight=0;uint32_t cost=0;};
struct RunConfig {
    std::vector<uint32_t> widths,budgets;
    std::vector<RunEncounter> encounters;
    std::vector<Weighted> rewards;
    uint32_t offerCount=3;
};
struct RunRoom {
    uint32_t layer=0,cost=0;
    int64_t encounter=0;
    std::vector<int64_t> offers;
    bool operator==(const RunRoom&)const=default;
};
enum class RunPhase:uint8_t {Encounter,Reward,Travel,Completed,Failed};
struct RunState {
    std::vector<RunRoom> rooms;
    // Portable IDs rather than size_t so saved layouts have the same wire representation.
    std::vector<std::vector<uint32_t>> edges;
    std::vector<uint32_t> path;
    std::map<uint32_t,int64_t> claims;
    RunPhase phase=RunPhase::Encounter;
    uint64_t lastCommand=0;
    bool operator==(const RunState&)const=default;
};
inline bool valid(const RunConfig& c) {
    if(c.widths.size()<2||c.widths.size()>64||c.widths.size()!=c.budgets.size()||c.widths.front()!=1||c.widths.back()!=1||c.encounters.empty()||c.encounters.size()>256||c.rewards.empty()||c.rewards.size()>256||!c.offerCount||c.offerCount>16||c.offerCount>c.rewards.size())return false;
    std::set<int64_t> ids;uint32_t minimum=UINT32_MAX;int64_t total=0;
    for(auto e:c.encounters){if(e.id<=0||e.weight<=0||e.weight>1000000||!e.cost||e.cost>1000000||!ids.insert(e.id).second)return false;minimum=std::min(minimum,e.cost);}
    for(size_t i=0;i<c.widths.size();++i)if(!c.widths[i]||c.widths[i]>16||c.budgets[i]>16000000||uint64_t(c.widths[i])*minimum>c.budgets[i])return false;
    ids.clear();for(auto r:c.rewards){if(r.id<=0||r.weight<=0||r.weight>1000000||!ids.insert(r.id).second)return false;total+=r.weight;}
    return total>0;
}
inline bool valid(const RunState& s) {
    const auto n=s.rooms.size();if(n<2||n>1024||s.edges.size()!=n||s.path.empty()||s.path.size()>64||s.path.front()!=0||s.phase>RunPhase::Failed||s.claims.size()>s.path.size())return false;
    Graph forward(n),reverse(n);
    for(size_t i=0;i<n;++i){
        const auto& room=s.rooms[i];if(room.layer>63||!room.cost||room.cost>1000000||room.encounter<=0||room.offers.empty()||room.offers.size()>16||s.edges[i].size()>16)return false;
        std::set<int64_t> offers;for(auto id:room.offers)if(id<=0||!offers.insert(id).second)return false;
        if(!std::is_sorted(s.edges[i].begin(),s.edges[i].end())||std::adjacent_find(s.edges[i].begin(),s.edges[i].end())!=s.edges[i].end())return false;
        for(auto next:s.edges[i]){if(next>=n||s.rooms[next].layer!=room.layer+1)return false;forward[i].push_back(next);reverse[next].push_back(i);}
    }
    if(s.rooms.front().layer!=0||s.rooms.back().layer<1)return false;
    auto fromStart=bfs(forward,0),toEnd=bfs(reverse,n-1);
    for(size_t i=0;i<n;++i)if(fromStart->distance[i]==kUnreachable||toEnd->distance[i]==kUnreachable)return false;
    for(size_t i=0;i<s.path.size();++i){auto id=s.path[i];if(id>=n||(i&&!std::binary_search(s.edges[s.path[i-1]].begin(),s.edges[s.path[i-1]].end(),id)))return false;}
    for(auto [id,reward]:s.claims){if(std::find(s.path.begin(),s.path.end(),id)==s.path.end()||std::find(s.rooms[id].offers.begin(),s.rooms[id].offers.end(),reward)==s.rooms[id].offers.end())return false;}
    for(size_t i=0;i+1<s.path.size();++i)if(!s.claims.contains(s.path[i]))return false;
    const auto current=s.path.back();const bool claimed=s.claims.contains(current),last=current==n-1;
    if(s.phase==RunPhase::Completed)return last&&claimed;
    if(s.phase==RunPhase::Travel)return !last&&claimed;
    return !claimed;
}
// draw(total) is the engine RNG adapter, in [1,total]. Failed callback does not roll
// caller RNG back; validate first and use a guaranteed-range simulation RNG adapter.
template<class Draw> std::optional<RunState> generateRun(const RunConfig& c,Draw draw) {
    if(!valid(c))return {};
    RunState s;s.path={0};uint32_t minimum=UINT32_MAX;
    for(auto e:c.encounters)minimum=std::min(minimum,e.cost);
    std::vector<uint32_t> starts;
    for(size_t layer=0;layer<c.widths.size();++layer){
        starts.push_back(uint32_t(s.rooms.size()));uint32_t available=c.budgets[layer];
        for(uint32_t i=0;i<c.widths[layer];++i){
            uint32_t allowance=available-(c.widths[layer]-i-1)*minimum;std::vector<Weighted> candidates;
            for(auto e:c.encounters)if(e.cost<=allowance)candidates.push_back({e.id,e.weight});
            auto selected=weighted(candidates,draw);if(!selected)return {};
            auto encounter=std::find_if(c.encounters.begin(),c.encounters.end(),[&](auto e){return e.id==*selected;});
            RunRoom room{uint32_t(layer),encounter->cost,encounter->id,{}};available-=room.cost;
            auto rewards=c.rewards;
            for(uint32_t j=0;j<c.offerCount;++j){auto reward=weighted(rewards,draw);if(!reward)return {};room.offers.push_back(*reward);for(auto& r:rewards)if(r.id==*reward)r.weight=0;}
            s.rooms.push_back(std::move(room));s.edges.emplace_back();
        }
    }
    for(size_t layer=0;layer+1<c.widths.size();++layer){
        auto left=c.widths[layer],right=c.widths[layer+1];
        // A cyclic shift connects every next-layer node and gives every source an exit.
        auto shift=draw(right);if(shift<1||uint64_t(shift)>right)return {};
        for(uint32_t i=0;i<std::max(left,right);++i)s.edges[starts[layer]+i%left].push_back(starts[layer+1]+(i+uint32_t(shift)-1)%right);
        for(uint32_t i=0;i<left;++i){auto& row=s.edges[starts[layer]+i];std::sort(row.begin(),row.end());row.erase(std::unique(row.begin(),row.end()),row.end());}
    }
    if(!valid(s))return {};
    return s;
}
inline bool finishRunRoom(RunState& s,bool won,uint64_t sequence) {
    if(!valid(s)||s.phase!=RunPhase::Encounter||sequence<=s.lastCommand)return false;
    s.phase=won?RunPhase::Reward:RunPhase::Failed;s.lastCommand=sequence;return true;
}
// grant must be an atomic transaction against the existing inventory owner. False
// must make no external mutation. Do not use this callback for asynchronous backend IO.
template<class Grant> bool claimRunReward(RunState& s,int64_t reward,uint64_t sequence,Grant grant) {
    if(!valid(s)||s.phase!=RunPhase::Reward||sequence<=s.lastCommand)return false;
    auto room=s.path.back();const auto& offers=s.rooms[room].offers;if(std::find(offers.begin(),offers.end(),reward)==offers.end())return false;
    auto next=s;next.claims.emplace(room,reward);next.lastCommand=sequence;next.phase=room+1==s.rooms.size()?RunPhase::Completed:RunPhase::Travel;
    if(!grant(reward))return false;
    s=std::move(next);return true;
}
inline bool travelRun(RunState& s,uint32_t room,uint64_t sequence) {
    if(!valid(s)||s.phase!=RunPhase::Travel||sequence<=s.lastCommand)return false;
    const auto& choices=s.edges[s.path.back()];if(!std::binary_search(choices.begin(),choices.end(),room))return false;
    s.path.push_back(room);s.phase=RunPhase::Encounter;s.lastCommand=sequence;return true;
}
}
