#pragma once
// Optional read-only world labels backed by script variables. No game-specific
// grouping or simulation mutation: games publish values, positions and visibility.
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace aiverse::game {
struct WorldCounter { int64_t value=0; render::float3 position{}; };
inline std::vector<WorldCounter> worldCounters(const std::map<std::string,Fixed>& vars,const Json& definitions) {
    std::vector<WorldCounter> out;
    auto read=[&](const std::string& key)->const Fixed*{auto it=vars.find(key);return it==vars.end()?nullptr:&it->second;};
    for(const auto& def:definitions.items()) {
        const auto enabled=def.getStr("enabledVar");
        if(!enabled.empty()){const auto* v=read(enabled);if(!v||v->raw<=0)continue;}
        const auto prefix=def.getStr("valuePrefix"),xp=def.getStr("xPrefix"),zp=def.getStr("zPrefix"),visible=def.getStr("visiblePrefix");
        if(prefix.empty()||xp.empty()||zp.empty())continue;
        for(auto it=vars.lower_bound(prefix);it!=vars.end()&&it->first.starts_with(prefix);++it) {
            const auto suffix=it->first.substr(prefix.size());if(suffix.empty())continue;
            if(!visible.empty()){const auto* v=read(visible+suffix);if(!v||v->raw<=0)continue;}
            const auto* x=read(xp+suffix);const auto* z=read(zp+suffix);if(!x||!z)continue;
            out.push_back({std::max<int64_t>(0,it->second.toInt()),{float(x->toDouble()),float(def.getFx("height",Fixed{}).toDouble()),float(z->toDouble())}});
        }
    }
    return out;
}
}
