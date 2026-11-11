#pragma once
#include "engine/data/Json.h"
#include <map>
#include <optional>
#include <string>
namespace aiverse::lib::runtime {
// Host diagnostic adapter. State is NativeState-compatible; names come from the project's module registry.
// Never call while another thread mutates the referenced states.
template<class State>
std::optional<aiverse::Json> nativeTrace(std::int64_t tick,const std::map<std::string,const State*>& states){
    if(tick<0)return {};
    auto modules=aiverse::Json::object();
    for(const auto& [name,state]:states){
        if(name.empty()||!state)return {};
        auto values=aiverse::Json::object();for(const auto& [key,value]:state->values)values.set(key,aiverse::Json::number(value));
        auto arrays=aiverse::Json::object();for(const auto& [key,data]:state->arrays){auto array=aiverse::Json::array();for(auto value:data)array.push(aiverse::Json::number(value));arrays.set(key,std::move(array));}
        auto module=aiverse::Json::object();module.set("values",std::move(values));module.set("arrays",std::move(arrays));modules.set(name,std::move(module));
    }
    auto row=aiverse::Json::object();row.set("tick",aiverse::Json::number(tick));row.set("modules",std::move(modules));return row;
}
}
