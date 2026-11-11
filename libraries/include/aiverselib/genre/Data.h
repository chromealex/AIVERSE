#pragma once
#include "Narrative.h"
#include "engine/data/Json.h"
#include <charconv>
namespace aiverse::lib::genre {
inline int64_t storyInteger(const Json& j,const char* label){
    if(!j.isNumber())throw JsonError(std::string("story.")+label+": expected integer");auto text=j.numberLiteral();int64_t value=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(std::string("story.")+label+": invalid integer");return value;
}
inline void storyKeys(const Json& j,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError("story: expected object");for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto name:names)if(name==key)found=true;if(!found)throw JsonError("story: unknown key '"+key+"'");}
}
inline std::string storyText(const Json& j){if(!j.isString()||j.str().empty())throw JsonError("story: expected nonempty localization key");return j.str();}
inline Story readStory(const Json& j){
    storyKeys(j,{"nodes"});if(!j["nodes"].isArray()||j["nodes"].size()>10000)throw JsonError("story.nodes: expected at most 10000 nodes");Story graph;
    for(const auto& item:j["nodes"].items()){
        storyKeys(item,{"id","textKey","choices"});StoryNode node;node.id=storyInteger(item["id"],"id");node.textKey=storyText(item["textKey"]);
        if(!item["choices"].isArray()||item["choices"].size()>128)throw JsonError("story.choices: expected at most 128 choices");
        for(const auto& raw:item["choices"].items()){
            storyKeys(raw,{"id","next","textKey","conditions","changes"});StoryChoice choice;choice.id=storyInteger(raw["id"],"choice.id");choice.next=storyInteger(raw["next"],"next");choice.textKey=storyText(raw["textKey"]);
            for(auto field:{"conditions","changes"})if(raw.has(field)&&(!raw[field].isArray()||raw[field].size()>256))throw JsonError(std::string("story.")+field+": expected at most 256 records");
            for(const auto& cond:raw["conditions"].items()){storyKeys(cond,{"key","minimum","maximum"});choice.conditions.push_back({storyInteger(cond["key"],"key"),storyInteger(cond["minimum"],"minimum"),storyInteger(cond["maximum"],"maximum")});}
            for(const auto& effect:raw["changes"].items()){
                storyKeys(effect,{"key","value","add"});if(effect.has("add")&&effect["add"].type()!=Json::Type::Bool)throw JsonError("story.add: expected bool");
                choice.changes.push_back({storyInteger(effect["key"],"key"),storyInteger(effect["value"],"value"),effect.getBool("add",false)});
            }node.choices.push_back(std::move(choice));
        }if(!graph.emplace(node.id,std::move(node)).second)throw JsonError("story: duplicate node id");
    }if(!valid(graph))throw JsonError("story: invalid graph, choice or condition");return graph;
}
}
