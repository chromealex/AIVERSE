#pragma once
#include "../systems/Tags.h"
#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
struct FactChange {int64_t key=0,value=0;bool add=false;};
struct StoryChoice {int64_t id=0,next=0;std::string textKey;std::vector<systems::Condition> conditions;std::vector<FactChange> changes;};
struct StoryNode {int64_t id=0;std::string textKey;std::vector<StoryChoice> choices;};
using Story=std::map<int64_t,StoryNode>;
struct StoryState {int64_t node=0;std::map<int64_t,int64_t> facts,visits;uint64_t lastCommand=0;bool complete=false;};
inline bool valid(const Story& graph){
    if(graph.empty()||graph.size()>10000)return false;
    for(const auto& [id,node]:graph){
        if(id<=0||node.id!=id||node.textKey.empty()||node.choices.size()>128)return false;
        std::vector<int64_t> ids;
        for(const auto& choice:node.choices){
            if(choice.id<=0||choice.textKey.empty()||(choice.next&&!graph.contains(choice.next))||std::find(ids.begin(),ids.end(),choice.id)!=ids.end())return false;
            ids.push_back(choice.id);
            if(choice.changes.size()>256||choice.conditions.size()>256)return false;
            for(auto condition:choice.conditions)if(condition.key<=0||condition.minimum>condition.maximum)return false;
            for(auto change:choice.changes)if(change.key<=0)return false;
        }
    }return true;
}
inline std::optional<StoryState> beginStory(const Story& graph,int64_t node,std::map<int64_t,int64_t> facts={}){
    if(!valid(graph)||!graph.contains(node)||facts.size()>10000)return {};
    for(auto [key,value]:facts){
        (void)value;
        if(key<=0)return {};
    }
    StoryState s;s.node=node;s.facts=std::move(facts);s.visits[node]=1;return s;
}
inline bool choiceAllowed(const StoryChoice& choice,const StoryState& s){
    return systems::conditions(std::span<const systems::Condition>(choice.conditions),[&](int64_t key)->std::optional<int64_t>{auto it=s.facts.find(key);if(it==s.facts.end())return {};return it->second;});
}
inline std::vector<int64_t> storyChoices(const Story& graph,const StoryState& s){
    std::vector<int64_t> choices;auto node=graph.find(s.node);if(node==graph.end()||s.complete)return choices;
    for(const auto& choice:node->second.choices)if(choiceAllowed(choice,s))choices.push_back(choice.id);
    std::sort(choices.begin(),choices.end());
    return choices;
}
inline bool chooseStory(const Story& graph,StoryState& state,int64_t id,uint64_t command){
    if(!valid(graph)||state.complete||command==0||command<=state.lastCommand)return false;
    auto node=graph.find(state.node);
    if(node==graph.end())return false;
    auto found=std::find_if(node->second.choices.begin(),node->second.choices.end(),[&](const auto& c){return c.id==id;});if(found==node->second.choices.end()||!choiceAllowed(*found,state))return false;
    auto next=state;
    for(auto change:found->changes){auto& value=next.facts[change.key];if(change.add){if((change.value>0&&value>std::numeric_limits<int64_t>::max()-change.value)||(change.value<0&&value<std::numeric_limits<int64_t>::min()-change.value))return false;value+=change.value;}else value=change.value;}
    if(next.facts.size()>10000)return false;
    next.lastCommand=command;
    next.node=found->next;
    next.complete=found->next==0;
    if(!next.complete){auto& count=next.visits[next.node];if(count==std::numeric_limits<int64_t>::max())return false;++count;}
    state=std::move(next);return true;
}
// textKey/choice.textKey are localization keys. Camera/animation/dialogue UI consume the committed node;
// this graph does not perform scene transitions or issue economy rewards by itself.
}
