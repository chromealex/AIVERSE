#pragma once
#include "Content.h"
#include <set>
namespace aiverse::lib::app {
struct ChangeRecipe { std::string id;std::vector<std::string> dependencies,files,checks; };
struct ChangePlan { std::vector<std::string> recipes,files,checks;std::string error;explicit operator bool()const{return error.empty();} };
inline ChangePlan planChanges(const std::vector<ChangeRecipe>& catalog,const std::vector<std::string>& requested) {
    std::map<std::string,const ChangeRecipe*> byId;
    for(const auto& r:catalog)if(r.id.empty()||!byId.emplace(r.id,&r).second)return {{},{},{},"duplicate or empty recipe"};
    std::set<std::string> selected;
    std::vector<std::string> queue=requested;
    for(std::size_t i=0;i<queue.size();++i){
        auto it=byId.find(queue[i]);if(it==byId.end())return {{},{},{},"unknown recipe: "+queue[i]};
        if(!selected.insert(queue[i]).second)continue;
        queue.insert(queue.end(),it->second->dependencies.begin(),it->second->dependencies.end());
    }
    std::vector<ContentPackage> packages;
    for(const auto& id:selected){ContentPackage p{id,"recipe",1,{}, {}};for(const auto& dep:byId.at(id)->dependencies)p.dependencies.push_back({dep,1,0});packages.push_back(std::move(p));}
    auto order=resolveContent(packages);if(!order)return {{},{},{},order.error};
    ChangePlan out;out.recipes=order.ids;std::set<std::string> files,checks;
    for(const auto& id:out.recipes){const auto& r=*byId.at(id);files.insert(r.files.begin(),r.files.end());checks.insert(r.checks.begin(),r.checks.end());}
    out.files.assign(files.begin(),files.end());out.checks.assign(checks.begin(),checks.end());return out;
}
}
