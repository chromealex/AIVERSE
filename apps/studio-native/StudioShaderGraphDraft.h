// Studio's writable Shader Graph subset: bounded graph-asset coordinates only.
// Compile and filesystem writes are always delegated to ShaderGraphOps.
#pragma once
#include "apps/studio-native/StudioShaderGraphValidation.h"
#include "engine/data/Json.h"
#include <cmath>
#include <set>
#include <string>

namespace aiverse::studio {
inline bool validStudioShaderGraphDraft(const Json& graph) {
    if(!graph.isObject())return false;
    const std::string domain=graph.getStr("domain");
    if(domain!="material"&&domain!="postfx"&&domain!="particle")return false;
    const Json& nodes=graph["nodes"];
    if(!nodes.isArray()||nodes.size()>128)return false;
    std::set<std::string> ids;
    for(const Json& node:nodes.items()) {
        if(!node.isObject())return false;
        const std::string id=node.getStr("id"),op=node.getStr("op");
        if(!validStudioShaderGraphName(id)||!validStudioShaderGraphName(op)||
           !ids.insert(id).second)return false;
        const Json& p=node["position"];
        if(!p.isNull()) {
            if(!p.isArray()||p.size()!=2)return false;
            for(size_t k=0;k<2;k++) {
                if(!p[k].isNumber()||!std::isfinite(p[k].toDouble())||
                   std::abs(p[k].toDouble())>1000000.0)return false;
            }
        }
        const Json& inputs=node["inputs"];
        if(!inputs.isNull()&&(!inputs.isArray()||inputs.size()>16))return false;
    }
    const std::string bytes=graph.dump(-1);
    return bytes.size()<=256u*1024u;
}
} // namespace aiverse::studio
