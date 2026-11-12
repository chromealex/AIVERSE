#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace aiverse::lib::strategy {
struct TurnActor {int64_t id=0;int32_t owner=0,initiative=0,maxAp=0,ap=0;bool alive=true;bool operator==(const TurnActor&)const=default;};
struct TurnAction {uint64_t sequence=0;int64_t actor=0,target=0;int32_t kind=0,cost=0;bool reaction=false;bool operator==(const TurnAction&)const=default;};
struct TurnState {
    std::map<int64_t,TurnActor> actors;
    std::vector<int64_t> order;
    std::vector<TurnAction> pending;
    uint64_t round=1,lastSequence=0;
    size_t cursor=0;
    bool operator==(const TurnState&)const=default;
};
inline bool valid(const TurnState& s) {
    if(s.actors.empty()||s.actors.size()>1024||!s.round||s.order.size()!=s.actors.size()||s.cursor>=s.order.size()||s.pending.size()>64)return false;
    auto ids=s.order;std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
    for(auto id:ids)if(!s.actors.contains(id))return false;
    for(const auto& [id,a]:s.actors)if(id<=0||id!=a.id||a.owner<=0||a.maxAp<0||a.ap<0||a.ap>a.maxAp)return false;
    uint64_t prior=0;
    for(const auto& a:s.pending){if(a.sequence<=prior||a.sequence>s.lastSequence||!s.actors.contains(a.actor)||a.kind<=0||a.cost<0||(a.target&&!s.actors.contains(a.target)))return false;prior=a.sequence;}
    return true;
}
inline std::optional<TurnState> beginTurns(const std::vector<TurnActor>& actors) {
    TurnState s;
    for(auto a:actors){a.ap=a.maxAp;if(!s.actors.emplace(a.id,a).second)return {};s.order.push_back(a.id);}
    std::sort(s.order.begin(),s.order.end(),[&](auto x,auto y){const auto& a=s.actors.at(x);const auto& b=s.actors.at(y);return a.initiative!=b.initiative?a.initiative>b.initiative:x<y;});
    if(!valid(s))return {};
    while(s.cursor<s.order.size()&&!s.actors.at(s.order[s.cursor]).alive)++s.cursor;
    if(s.cursor==s.order.size())return {};
    return s;
}
inline int64_t activeActor(const TurnState& s){return valid(s)&&s.actors.at(s.order[s.cursor]).alive?s.order[s.cursor]:0;}
// The host must validate kind/target/range and whether a reaction window is available.
// AP is charged at acceptance; cancellation/fizzling deliberately does not refund it.
template<class Authorize>
bool queueAction(TurnState& s,int32_t player,TurnAction action,Authorize authorize) {
    if(!valid(s)||action.sequence<=s.lastSequence||action.kind<=0||action.cost<0||s.pending.size()==64)return false;
    auto actor=s.actors.find(action.actor);
    if(actor==s.actors.end()||!actor->second.alive||actor->second.owner!=player||actor->second.ap<action.cost||(action.target&&!s.actors.contains(action.target)))return false;
    if(action.reaction?s.pending.empty():(!s.pending.empty()||action.actor!=activeActor(s)))return false;
    if(!authorize(static_cast<const TurnState&>(s),action))return false;
    actor->second.ap-=action.cost;s.lastSequence=action.sequence;s.pending.push_back(action);return true;
}
// LIFO permits nested reactions. Resolution returns data; only the authoritative host applies effects.
inline std::optional<TurnAction> resolveAction(TurnState& s) {
    if(!valid(s)||s.pending.empty())return {};
    auto a=s.pending.back();s.pending.pop_back();return a;
}
inline bool endTurn(TurnState& s,int32_t player) {
    if(!valid(s)||!s.pending.empty()||s.actors.at(s.order[s.cursor]).owner!=player)return false;
    auto out=s;
    for(size_t count=0;count<out.order.size();++count){
        if(++out.cursor==out.order.size()){
            if(out.round==std::numeric_limits<uint64_t>::max())return false;
            out.cursor=0;++out.round;for(auto& [id,a]:out.actors){(void)id;a.ap=a.maxAp;}
        }
        if(out.actors.at(out.order[out.cursor]).alive){s=std::move(out);return true;}
    }
    return false;
}
}
