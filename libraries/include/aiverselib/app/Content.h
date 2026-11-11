#pragma once
#include "../Graphs.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
namespace aiverse::lib::app {
struct Dependency { std::string id; std::uint32_t minimum=1, maximum=0; };
struct ContentPackage { std::string id, digest; std::uint32_t version=1; std::vector<Dependency> dependencies; std::vector<std::string> conflicts; };
struct ContentOrder { std::vector<std::string> ids; std::string error; explicit operator bool() const { return error.empty(); } };
// Stable ordering independent of discovery order. Does not execute mods or read arbitrary paths.
inline ContentOrder resolveContent(std::vector<ContentPackage> packages) {
    std::sort(packages.begin(),packages.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    std::map<std::string,std::size_t> index;
    for(std::size_t i=0;i<packages.size();++i) {
        const auto& p=packages[i];
        if(p.id.empty()||p.digest.empty()||p.version==0||!index.emplace(p.id,i).second)return {{},"invalid or duplicate package: "+p.id};
    }
    Graph edges(packages.size());
    for(std::size_t i=0;i<packages.size();++i) {
        for(const auto& conflict:packages[i].conflicts) if(index.count(conflict))return {{},"conflict: "+packages[i].id+" / "+conflict};
        for(const auto& d:packages[i].dependencies) {
            auto it=index.find(d.id);
            if(it==index.end())return {{},"missing dependency: "+d.id};
            const auto v=packages[it->second].version;
            if(d.minimum==0||(d.maximum&&d.maximum<d.minimum)||v<d.minimum||(d.maximum&&v>d.maximum))return {{},"version mismatch: "+d.id};
            edges[it->second].push_back(i);
        }
    }
    auto order=topological(edges);if(!order)return {{},"dependency cycle"};
    ContentOrder out;for(auto i:*order)out.ids.push_back(packages[i].id);return out;
}
}
